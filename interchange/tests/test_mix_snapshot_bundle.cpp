// D535.2 : LES INSTANTANÉS DE CONSOLE TRAVERSENT LE DISQUE.
//
// Le modèle est tenu par `core/tests/test_mix_snapshot.cpp` ; ici, le chemin réel :
// `saveProjectBundle` puis `loadProjectBundle`, un fichier écrit ailleurs, et un projet sans
// instantané qui garde son fichier octet pour octet.

#include "TestFramework.h"
#include "vsm/interchange/ProjectBundle.h"
#include "vsm/interchange/ProjectDocument.h"
#include "vsm/sequencer/MixSnapshot.h"
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
    const fs::path dossier = fs::temp_directory_path() / ("vsm-instantanes-" + nom);
    fs::remove_all(dossier);
    fs::create_directories(dossier);
    return dossier;
}

std::string lire(const fs::path& chemin) {
    std::ifstream f(chemin, std::ios::binary);
    std::stringstream tout;
    tout << f.rdbuf();
    return tout.str();
}

Project projetMixe() {
    Project p;
    p.title = "essai";   // pas « mixSnapshots » : la clé cherchée serait dans le titre (D532.2)
    p.ticksPerQuarterNote = 480;
    p.tempoMap.addTempoChange(0, 500000);
    p.sends.push_back({});
    for (const char* nom : {"Basse", "Nappe"}) {
        Track t;
        t.name = nom;
        t.instrumentId = "vsm.minimoog";
        uint64_t ids = 1;
        t.addNote(0, 480, 60, 100, 0, ids);
        p.tracks.push_back(t);
    }
    p.tracks[0].volume = 0.5f;
    p.tracks[0].sendLevels = {0.25f};
    p.tracks[0].effects.push_back({"reverb", {}, {}, true});
    takeMixSnapshot(p, "Couplet");
    p.tracks[0].effects[0].enabled = false;
    p.tracks[1].muted = true;
    p.tracks[1].pan = -0.25f;
    takeMixSnapshot(p, "Refrain");
    return p;
}

} // namespace

VSM_TEST(les_instantanes_traversent_le_disque) {
    const fs::path dossier = dossierNeuf("aller-retour");
    const Project avant = projetMixe();
    VSM_ASSERT(saveProjectBundle(avant, dossier.string()).success);
    const auto relu = loadProjectBundle(dossier.string());
    VSM_ASSERT(relu.success);
    const Project& p = relu.bundle.project;
    VSM_ASSERT(p.mixSnapshotNames == (std::vector<std::string>{"Couplet", "Refrain"}));
    VSM_ASSERT_EQ(p.tracks.size(), static_cast<size_t>(2));
    for (size_t t = 0; t < 2; ++t)
        for (const char* nom : {"Couplet", "Refrain"}) {
            const TrackMixState& a = avant.tracks[t].mixSnapshots.at(nom);
            const TrackMixState& b = p.tracks[t].mixSnapshots.at(nom);
            VSM_ASSERT_EQ(a.volume, b.volume);
            VSM_ASSERT_EQ(a.pan, b.pan);
            VSM_ASSERT(a.muted == b.muted && a.solo == b.solo && a.invertPhase == b.invertPhase);
            VSM_ASSERT_EQ(a.inputTrimDb, b.inputTrimDb);
            VSM_ASSERT(a.sendLevels == b.sendLevels);
            VSM_ASSERT(a.inserts == b.inserts);
        }
    VSM_ASSERT(p.tracks[0].mixSnapshots.at("Couplet").inserts[0].enabled);
    VSM_ASSERT(!p.tracks[0].mixSnapshots.at("Refrain").inserts[0].enabled);
    // RELU, le rappel agit : la nappe redevient audible et centrée à -0,25 près.
    Project rappele = p;
    const MixRecallReport bilan = recallMixSnapshot(rappele, "Couplet");
    VSM_ASSERT_EQ(bilan.recalled, static_cast<size_t>(2));
    VSM_ASSERT(!rappele.tracks[1].muted);
    VSM_ASSERT_EQ(rappele.tracks[1].pan, 0.0f);
    VSM_ASSERT(rappele.tracks[0].effects[0].enabled);
}

// UN ÉTAT SOUS UN NOM QUE LE PROJET NE PORTE PAS (un fichier retouché à la main) est ÉCARTÉ, et DIT.
VSM_TEST(un_etat_sans_nom_au_projet_est_ecarte_et_dit) {
    ProjectDocument document = documentFromProject(projetMixe());
    document.mixSnapshotNames = {"Couplet"};
    Project projet = projetMixe();   // le document s'applique à des pistes existantes
    VSM_ASSERT_EQ(projet.tracks.size(), static_cast<size_t>(2));
    const auto rapport = applyDocumentToProject(document, projet);
    VSM_ASSERT_EQ(projet.tracks[0].mixSnapshots.count("Refrain"), static_cast<size_t>(0));
    VSM_ASSERT_EQ(projet.tracks[0].mixSnapshots.count("Couplet"), static_cast<size_t>(1));
    size_t dits = 0;
    for (const auto& phrase : rapport.warnings)
        if (phrase.find("Refrain") != std::string::npos) ++dits;
    VSM_ASSERT_EQ(dits, static_cast<size_t>(2));   // une phrase par piste
}

// UN PROJET SANS INSTANTANÉ N'EN ÉCRIT RIEN, et se réécrit octet pour octet.
VSM_TEST(un_projet_sans_instantane_n_en_ecrit_rien) {
    Project p = projetMixe();
    p.mixSnapshotNames.clear();
    for (auto& t : p.tracks) t.mixSnapshots.clear();
    const std::string texte = projectDocumentToJson(documentFromProject(p)).toString();
    VSM_ASSERT(texte.find("mixSnapshots") == std::string::npos);
    const fs::path un = dossierNeuf("sans-1"), deux = dossierNeuf("sans-2");
    VSM_ASSERT(saveProjectBundle(p, un.string()).success);
    const auto relu = loadProjectBundle(un.string());
    VSM_ASSERT(relu.success);
    VSM_ASSERT(saveProjectBundle(relu.bundle.project, deux.string()).success);
    const std::string a = lire(un / "project.json"), b = lire(deux / "project.json");
    VSM_ASSERT(!a.empty());
    VSM_ASSERT(a == b);
}
