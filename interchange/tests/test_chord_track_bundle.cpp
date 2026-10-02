// D532.3 : LA LIGNE D'ACCORDS TRAVERSE LE DISQUE, PAR SES SYMBOLES.
//
// Le modèle est tenu par `core/tests/test_chord_track.cpp` ; ici, le chemin réel :
// `saveProjectBundle` puis `loadProjectBundle`, et la lecture d'un fichier écrit
// ailleurs, où un symbole peut être faux.

#include "TestFramework.h"
#include "vsm/interchange/ProjectBundle.h"
#include "vsm/interchange/ProjectDocument.h"
#include "vsm/sequencer/ChordTrack.h"
#include "vsm/sequencer/Project.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace vsm::interchange;
using namespace vsm::sequencer;
namespace fs = std::filesystem;

namespace {

fs::path dossierNeuf(const std::string& nom) {
    const fs::path dossier = fs::temp_directory_path() / ("vsm-accords-" + nom);
    fs::remove_all(dossier);
    fs::create_directories(dossier);
    return dossier;
}

Project projetAccorde() {
    Project p;
    p.title = "essai";   // pas « chords » : la clé cherchée serait dans le titre (D532.2)
    p.ticksPerQuarterNote = 480;
    p.tempoMap.addTempoChange(0, 500000);
    Track t;
    t.name = "Piano";
    t.instrumentId = "vsm.minimoog";
    uint64_t ids = 1;
    t.addNote(0, 480, 60, 100, 0, ids);
    p.tracks.push_back(t);
    for (const auto& [tick, symbole] : {std::pair<Tick, const char*>{0, "C"}, {1920, "Am7/G"}, {3840, "F#m7b5"}}) {
        ChordEvent a;
        a.tick = tick;
        VSM_ASSERT(parseChordSymbol(symbole, a));
        setChordAt(p.chords, a);
    }
    return p;
}

} // namespace

VSM_TEST(la_ligne_d_accords_traverse_le_disque) {
    const fs::path dossier = dossierNeuf("aller-retour");
    VSM_ASSERT(saveProjectBundle(projetAccorde(), dossier.string()).success);
    const auto relu = loadProjectBundle(dossier.string());
    VSM_ASSERT(relu.success);
    const auto& accords = relu.bundle.project.chords;
    VSM_ASSERT_EQ(accords.size(), static_cast<size_t>(3));
    VSM_ASSERT(accords == projetAccorde().chords);
    VSM_ASSERT_EQ(chordSymbol(accords[1]), std::string("Am7/G"));
}

// UN SYMBOLE ILLISIBLE, ÉCRIT AILLEURS, EST ÉCARTÉ ET DIT ; les autres restent.
VSM_TEST(un_symbole_illisible_est_ecarte_et_dit) {
    ProjectDocument document = documentFromProject(projetAccorde());
    document.chords[1].symbol = "Cmaj13";
    Project projet = projetAccorde();
    const auto rapport = applyDocumentToProject(document, projet);
    VSM_ASSERT_EQ(projet.chords.size(), static_cast<size_t>(2));
    bool dit = false;
    for (const auto& phrase : rapport.warnings)
        if (phrase.find("Cmaj13") != std::string::npos && phrase.find("1920") != std::string::npos) dit = true;
    VSM_ASSERT(dit);
}

VSM_TEST(un_projet_sans_accord_n_en_ecrit_rien) {
    Project p = projetAccorde();
    p.chords.clear();
    const std::string texte = projectDocumentToJson(documentFromProject(p)).toString();
    VSM_ASSERT(texte.find("\"chords\"") == std::string::npos);
}
