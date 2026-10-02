// D532.2 : LES VERSIONS DE PISTE TRAVERSENT LE DISQUE.
//
// Le modèle est tenu par `core/tests/test_track_versions.cpp` ; ici, le chemin réel de
// l'application : `saveProjectBundle` puis `loadProjectBundle`. Les notes et événements
// de canal des versions rangées vont dans `midi/versions.mid`, le reste dans
// `project.json` ; un projet sans version garde son fichier octet pour octet.

#include "TestFramework.h"
#include "vsm/interchange/ProjectBundle.h"
#include "vsm/interchange/ProjectDocument.h"
#include "vsm/sequencer/Project.h"
#include "vsm/sequencer/Track.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace vsm::interchange;
using namespace vsm::sequencer;
namespace fs = std::filesystem;

namespace {

fs::path dossierNeuf(const std::string& nom) {
    const fs::path dossier = fs::temp_directory_path() / ("vsm-versions-" + nom);
    fs::remove_all(dossier);
    fs::create_directories(dossier);
    return dossier;
}

std::string lire(const fs::path& fichier) {
    std::ifstream flux(fichier);
    std::stringstream tout;
    tout << flux.rdbuf();
    return tout.str();
}

/// Une piste à deux versions : « Couplet » (rangée) et « Refrain » (active), chacune
/// avec ses notes, un contrôleur, un clip et une courbe — tous distincts.
Project projetADeuxVersions() {
    Project p;
    p.title = "essai";   // pas « versions » : la clé cherchée serait alors dans le titre
    p.ticksPerQuarterNote = 480;
    p.tempoMap.addTempoChange(0, 500000);
    Track t;
    t.name = "Basse";
    t.instrumentId = "vsm.minimoog";
    uint64_t ids = 1;
    t.addNote(0, 480, 40, 100, 0, ids);
    t.controlChanges = {{0, 0, 74, 20}};
    Clip clip;
    clip.startTick = 0;
    clip.length = 1920;
    clip.name = "couplet";
    t.clips = {clip};
    AutomationCurve courbe;
    courbe.parameter = "mix.volume";
    courbe.points.push_back({0, 0.25f, false, 0.0f});
    t.automation = {courbe};
    createVersion(t, "Refrain", false, "Couplet");
    t.addNote(0, 960, 52, 90, 0, ids);
    t.addNote(960, 1920, 55, 90, 0, ids);
    t.controlChanges = {{480, 0, 74, 100}};
    clip.name = "refrain";
    t.clips = {clip};
    courbe.points[0].value = 0.75f;
    t.automation = {courbe};
    p.tracks.push_back(t);
    return p;
}

} // namespace

VSM_TEST(deux_versions_traversent_le_disque_et_la_rangee_ressort_entiere) {
    const fs::path dossier = dossierNeuf("aller-retour");
    VSM_ASSERT(saveProjectBundle(projetADeuxVersions(), dossier.string()).success);
    VSM_ASSERT(fs::exists(dossier / kVersionsMidiPath));
    const auto relu = loadProjectBundle(dossier.string());
    VSM_ASSERT(relu.success);
    Track t = relu.bundle.project.tracks.at(0);
    VSM_ASSERT_EQ(t.versions.size(), static_cast<size_t>(2));
    VSM_ASSERT_EQ(t.versions[0].name, std::string("Couplet"));
    VSM_ASSERT_EQ(t.versions[1].name, std::string("Refrain"));
    VSM_ASSERT_EQ(t.activeVersion, 1);
    // La version active est dans la piste — c'est l'arrangement.
    VSM_ASSERT_EQ(t.notes.size(), static_cast<size_t>(2));
    VSM_ASSERT_EQ(t.clips.at(0).name, std::string("refrain"));
    VSM_ASSERT(t.automation.at(0).points.at(0).value == 0.75f);
    // La version rangée ressort entière en la choisissant.
    VSM_ASSERT(selectVersion(t, 0));
    VSM_ASSERT_EQ(t.notes.size(), static_cast<size_t>(1));
    VSM_ASSERT_EQ(static_cast<int>(t.notes[0].number), 40);
    VSM_ASSERT_EQ(t.controlChanges.size(), static_cast<size_t>(1));
    VSM_ASSERT_EQ(static_cast<int>(t.controlChanges[0].value), 20);
    VSM_ASSERT_EQ(t.clips.at(0).name, std::string("couplet"));
    VSM_ASSERT(t.automation.at(0).points.at(0).value == 0.25f);
    // Et le retour rend le refrain, rangé au passage.
    VSM_ASSERT(selectVersion(t, 1));
    VSM_ASSERT_EQ(t.notes.size(), static_cast<size_t>(2));
    VSM_ASSERT_EQ(static_cast<int>(t.controlChanges.at(0).value), 100);
}

// UN PROJET SANS VERSION GARDE SON FICHIER : ni « versions », ni `versions.mid`.
VSM_TEST(un_projet_sans_version_n_en_ecrit_rien) {
    Project p = projetADeuxVersions();
    p.tracks[0].versions.clear();
    p.tracks[0].activeVersion = -1;
    const fs::path dossier = dossierNeuf("sans");
    VSM_ASSERT(saveProjectBundle(p, dossier.string()).success);
    VSM_ASSERT(!fs::exists(dossier / kVersionsMidiPath));
    const std::string texte = lire(dossier / kProjectFileName);
    VSM_ASSERT(texte.find("\"versions\"") == std::string::npos);
    VSM_ASSERT(texte.find("\"activeVersion\"") == std::string::npos);
}

// UN TIROIR QUI MANQUE SE DIT, et le projet s'ouvre quand même.
VSM_TEST(un_fichier_de_versions_absent_se_dit) {
    const fs::path dossier = dossierNeuf("absent");
    VSM_ASSERT(saveProjectBundle(projetADeuxVersions(), dossier.string()).success);
    fs::remove(dossier / kVersionsMidiPath);
    const auto relu = loadProjectBundle(dossier.string());
    VSM_ASSERT(relu.success);
    bool dit = false;
    for (const auto& phrase : relu.warnings)
        if (phrase.find("versions introuvables") != std::string::npos) dit = true;
    VSM_ASSERT(dit);
}
