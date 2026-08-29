#include "TestFramework.h"
#include "vsm/interchange/ProjectBundle.h"
#include "vsm/interchange/ProjectDocument.h"
#include "vsm/sequencer/Track.h"
#include <filesystem>
#include <fstream>
#include <string>

using namespace vsm::interchange;
using namespace vsm::sequencer;
namespace fs = std::filesystem;

// D3.5 de docs/ROADMAP-daw.md — LES PRISES SE CONSERVENT.
//
// « Se conservent » veut dire : elles survivent à un enregistrement du projet et
// à sa réouverture, avec leurs notes. Les notes des prises vont dans un fichier
// SÉPARÉ d'`arrangement.mid`, parce que l'arrangement doit rester ce qu'on
// entend et non l'archive de tout ce qu'on a essayé.

namespace {

fs::path dossierNeuf(const std::string& nom) {
    const fs::path dossier = fs::temp_directory_path() / ("vsm-prises-" + nom);
    fs::remove_all(dossier);
    fs::create_directories(dossier);
    return dossier;
}

Take priseDeNotes(const std::string& nom, int hauteur, int combien) {
    Take prise;
    prise.name = nom;
    prise.startTick = 1920;
    prise.endTick = 1920 + 480 * combien;
    for (int i = 0; i < combien; ++i) {
        Note note;
        note.startTick = 1920 + 480 * i;
        note.endTick = note.startTick + 240;
        note.number = static_cast<uint8_t>(hauteur + i);
        note.velocity = 100;
        prise.notes.push_back(note);
    }
    return prise;
}

Project projetAvecPrises() {
    Project projet;
    projet.tracks.emplace_back();
    projet.tracks[0].name = "Clavier";
    projet.tracks[0].channel = 0;
    uint64_t compteur = 1;
    projet.tracks[0].addNote(0, 240, 60, 100, 0, compteur);   // le matériau d'origine

    pushTake(projet.tracks[0], priseDeNotes("Prise 1", 70, 2));
    pushTake(projet.tracks[0], priseDeNotes("Prise 2", 80, 3));
    return projet;
}

} // namespace

VSM_TEST(takes_survive_a_save_and_a_reload_with_their_notes) {
    const fs::path dossier = dossierNeuf("aller-retour");
    Project projet = projetAvecPrises();
    VSM_ASSERT_EQ(projet.tracks[0].takes.size(), size_t(3));  // Origine + deux prises
    VSM_ASSERT_EQ(projet.tracks[0].activeTake, 2);

    VSM_ASSERT(saveProjectBundle(projet, dossier.string(), {}).success);

    const BundleLoadResult relu = loadProjectBundle(dossier.string());
    VSM_ASSERT(relu.success);
    const Track& piste = relu.bundle.project.tracks[0];
    VSM_ASSERT_EQ(piste.takes.size(), size_t(3));
    VSM_ASSERT_EQ(piste.activeTake, 2);
    VSM_ASSERT_EQ(piste.takes[0].name, std::string("Origine"));
    VSM_ASSERT_EQ(piste.takes[1].name, std::string("Prise 1"));
    VSM_ASSERT_EQ(piste.takes[2].name, std::string("Prise 2"));

    // Les notes, c'est-à-dire tout l'intérêt de la chose.
    VSM_ASSERT_EQ(piste.takes[0].notes.size(), size_t(1));
    VSM_ASSERT_EQ(piste.takes[1].notes.size(), size_t(2));
    VSM_ASSERT_EQ(int(piste.takes[1].notes[0].number), 70);
    VSM_ASSERT_EQ(piste.takes[2].notes.size(), size_t(3));
    VSM_ASSERT_EQ(int(piste.takes[2].notes[2].number), 82);
    VSM_ASSERT_EQ(piste.takes[1].startTick, vsm::midi::Tick(1920));
}

VSM_TEST(the_arrangement_never_carries_the_takes_that_were_set_aside) {
    // C'est la raison d'être du second fichier : `arrangement.mid` est ce qu'on
    // entend, et c'est aussi ce qu'on ouvre ailleurs. S'il portait les passes
    // écartées, il montrerait des pistes qui ne jouent pas.
    const fs::path dossier = dossierNeuf("arrangement-propre");
    Project projet = projetAvecPrises();
    VSM_ASSERT(saveProjectBundle(projet, dossier.string(), {}).success);

    VSM_ASSERT(fs::exists(dossier / kDefaultMidiPath));
    VSM_ASSERT(fs::exists(dossier / kTakesMidiPath));

    const BundleLoadResult relu = loadProjectBundle(dossier.string());
    VSM_ASSERT(relu.success);
    // Une seule piste dans l'arrangement, celle du morceau -- pas quatre.
    VSM_ASSERT_EQ(relu.bundle.project.tracks.size(), size_t(1));
    // Et elle joue la prise active, pas le matériau d'origine.
    VSM_ASSERT_EQ(relu.bundle.project.tracks[0].notes.size(), size_t(3));
}

VSM_TEST(a_project_without_takes_writes_no_takes_file) {
    // « Vide veut dire aucune prise » jusque dans le dossier : un projet qui n'a
    // jamais servi à un enregistrement empilé garde exactement les fichiers
    // qu'il avait.
    const fs::path dossier = dossierNeuf("sans-prise");
    Project projet;
    projet.tracks.emplace_back();
    uint64_t compteur = 1;
    projet.tracks[0].addNote(0, 240, 60, 100, 0, compteur);

    VSM_ASSERT(saveProjectBundle(projet, dossier.string(), {}).success);
    VSM_ASSERT(fs::exists(dossier / kDefaultMidiPath));
    VSM_ASSERT(!fs::exists(dossier / kTakesMidiPath));

    const BundleLoadResult relu = loadProjectBundle(dossier.string());
    VSM_ASSERT(relu.success);
    VSM_ASSERT(relu.bundle.project.tracks[0].takes.empty());
    VSM_ASSERT_EQ(relu.bundle.project.tracks[0].activeTake, -1);
}

VSM_TEST(a_missing_takes_file_opens_the_project_and_says_so) {
    // Perdre des prises en silence serait pire que ne pas les avoir gardées ;
    // refuser d'ouvrir le morceau pour autant le serait tout autant.
    const fs::path dossier = dossierNeuf("tiroir-manquant");
    Project projet = projetAvecPrises();
    VSM_ASSERT(saveProjectBundle(projet, dossier.string(), {}).success);
    fs::remove(dossier / kTakesMidiPath);

    const BundleLoadResult relu = loadProjectBundle(dossier.string());
    VSM_ASSERT(relu.success);                                   // le morceau s'ouvre
    VSM_ASSERT(!relu.warnings.empty());                          // et il le dit
    VSM_ASSERT_EQ(relu.bundle.project.tracks[0].takes.size(), size_t(3));
    VSM_ASSERT(relu.bundle.project.tracks[0].takes[1].notes.empty());
}

VSM_TEST(the_punch_region_is_a_song_property_and_survives_a_save) {
    // On refait le même passage vingt fois : redéfinir la région à chaque
    // ouverture reviendrait à perdre l'endroit qu'on a mis dix minutes à cerner.
    const fs::path dossier = dossierNeuf("punch");
    Project projet;
    projet.tracks.emplace_back();
    uint64_t compteur = 1;
    projet.tracks[0].addNote(0, 240, 60, 100, 0, compteur);
    projet.punchEnabled = true;
    projet.punchStartTick = 3840;
    projet.punchEndTick = 7680;

    VSM_ASSERT(saveProjectBundle(projet, dossier.string(), {}).success);
    const BundleLoadResult relu = loadProjectBundle(dossier.string());
    VSM_ASSERT(relu.success);
    VSM_ASSERT(relu.bundle.project.punchEnabled);
    VSM_ASSERT_EQ(relu.bundle.project.punchStartTick, vsm::midi::Tick(3840));
    VSM_ASSERT_EQ(relu.bundle.project.punchEndTick, vsm::midi::Tick(7680));
}

VSM_TEST(a_project_without_punch_keeps_the_file_it_always_had) {
    // Champ facultatif : un projet qui n'en déclare pas n'écrit pas la clé.
    Project projet;
    projet.tracks.emplace_back();
    const auto json = projectDocumentToJson(documentFromProject(projet)).toString();
    VSM_ASSERT(json.find("punch") == std::string::npos);

    projet.punchEnabled = true;
    projet.punchEndTick = 480;
    const auto avecPunch = projectDocumentToJson(documentFromProject(projet)).toString();
    VSM_ASSERT(avecPunch.find("punch") != std::string::npos);
}

// --- D6.4 : un dossier de projet qui s'ouvre sur une autre machine ----------

VSM_TEST(saving_elsewhere_takes_the_media_along) {
    // LA PANNE : le format était portable, l'enregistrement ne l'était pas.
    // Enregistrer sous un autre dossier écrivait un project.json qui désignait
    // des fichiers restés dans l'ancien.
    const fs::path source = dossierNeuf("autonome-source");
    Project project;
    project.ticksPerQuarterNote = 480;
    Track piste;
    piste.name = "Voix";
    piste.kind = Track::Kind::Audio;
    piste.audio.path = "audio/voix.wav";
    piste.audio.sampleRate = 48000.0;
    piste.audio.frames = 4800;
    piste.audio.channels = 2;
    project.tracks.push_back(piste);
    VSM_ASSERT(saveProjectBundle(project, source.string()).success);

    // Le média, écrit à la main : ce que ferait l'enregistrement d'une prise.
    const auto fichier = source / "audio" / "voix.wav";
    fs::create_directories(fichier.parent_path());
    { std::ofstream flux(fichier, std::ios::binary); flux << "RIFF----WAVEfmt "; }

    const auto chargé = loadProjectBundle(source.string());
    VSM_ASSERT(chargé.success);
    VSM_ASSERT(missingMediaPaths(chargé.bundle).empty());

    const fs::path destination = dossierNeuf("autonome-destination");
    const auto exporté = exportStandaloneProject(chargé.bundle, destination.string());
    VSM_ASSERT(exporté.success);
    VSM_ASSERT(exporté.missing.empty());
    VSM_ASSERT_EQ(exporté.copiedFiles.size(), static_cast<size_t>(1));
    VSM_ASSERT(fs::exists(destination / "audio" / "voix.wav"));

    // ET LE VRAI CRITÈRE : le dossier écrit se recharge, complet, tout seul.
    const auto ailleurs = loadProjectBundle(destination.string());
    VSM_ASSERT(ailleurs.success);
    VSM_ASSERT(missingMediaPaths(ailleurs.bundle).empty());
}

VSM_TEST(a_missing_media_is_named_not_swallowed) {
    const fs::path source = dossierNeuf("autonome-manquant");
    Project project;
    project.ticksPerQuarterNote = 480;
    Track piste;
    piste.kind = Track::Kind::Audio;
    piste.audio.path = "audio/introuvable.wav";
    piste.audio.sampleRate = 48000.0;
    piste.audio.frames = 100;
    piste.audio.channels = 2;
    project.tracks.push_back(piste);
    VSM_ASSERT(saveProjectBundle(project, source.string()).success);

    const auto chargé = loadProjectBundle(source.string());
    VSM_ASSERT(chargé.success);
    VSM_ASSERT_EQ(missingMediaPaths(chargé.bundle).size(), static_cast<size_t>(1));

    const fs::path destination = dossierNeuf("autonome-manquant-dest");
    const auto exporté = exportStandaloneProject(chargé.bundle, destination.string());
    // ON ÉCRIT QUAND MÊME, et on nomme ce qui manque : refuser ferait perdre
    // les pistes qui, elles, sont là.
    VSM_ASSERT(exporté.success);
    VSM_ASSERT_EQ(exporté.missing.size(), static_cast<size_t>(1));
    VSM_ASSERT_EQ(exporté.missing[0], std::string("audio/introuvable.wav"));
}

VSM_TEST(exporting_a_project_onto_itself_does_not_empty_its_media) {
    // `copy_file` sur lui-même vide le fichier. Enregistrer par-dessus son
    // propre dossier est ce qu'on fait le plus souvent (Ctrl+S) : c'est le cas
    // où une erreur coûterait tout.
    const fs::path dossier = dossierNeuf("autonome-sur-place");
    Project project;
    project.ticksPerQuarterNote = 480;
    Track piste;
    piste.kind = Track::Kind::Audio;
    piste.audio.path = "audio/prise.wav";
    piste.audio.sampleRate = 48000.0;
    piste.audio.frames = 100;
    piste.audio.channels = 2;
    project.tracks.push_back(piste);
    VSM_ASSERT(saveProjectBundle(project, dossier.string()).success);
    const auto fichier = dossier / "audio" / "prise.wav";
    fs::create_directories(fichier.parent_path());
    { std::ofstream flux(fichier, std::ios::binary); flux << "des octets qui comptent"; }
    const auto tailleAvant = fs::file_size(fichier);

    const auto chargé = loadProjectBundle(dossier.string());
    VSM_ASSERT(chargé.success);
    VSM_ASSERT(exportStandaloneProject(chargé.bundle, dossier.string()).success);
    VSM_ASSERT_EQ(fs::file_size(fichier), tailleAvant);
}
