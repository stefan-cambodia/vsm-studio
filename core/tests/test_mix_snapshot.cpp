#include "TestFramework.h"
#include "vsm/sequencer/MixSnapshot.h"
#include "vsm/sequencer/Project.h"
#include <string>
#include <vector>

using namespace vsm::sequencer;

// D535.2 de docs/ROADMAP-daw.md — LES INSTANTANÉS DE LA CONSOLE, le modèle : ce qu'un instantané
// garde, ce qu'il laisse, et que l'état suit SA piste quand les autres bougent.

namespace {
Project deuxPistes() {
    Project p;
    p.tracks.resize(2);
    p.tracks[0].name = "Basse";
    p.tracks[0].volume = 0.8f;
    p.tracks[0].sendLevels = {0.25f, 0.5f};
    p.tracks[0].effects = {{"compressor", {}, {}, true}, {"reverb", {}, {}, true}};
    p.tracks[1].name = "Nappe";
    p.tracks[1].volume = 0.6f;
    p.tracks[1].pan = -0.5f;
    return p;
}

void touteLaConsoleBouge(Track& t) {
    t.volume = 0.1f;
    t.pan = 0.9f;
    t.muted = true;
    t.solo = true;
    t.inputTrimDb = 6.0f;
    t.invertPhase = true;
    t.sendLevels = {0.75f};
    for (auto& e : t.effects) e.enabled = false;
}
} // namespace

// RAPPELER rend chaque réglage de la tranche, exactement — les huit, chacun bougé.
VSM_TEST(rappeler_rend_toute_la_tranche_exactement) {
    Project p = deuxPistes();
    takeMixSnapshot(p, "A");
    const TrackMixState basse = captureTrackMix(p.tracks[0]);
    const TrackMixState nappe = captureTrackMix(p.tracks[1]);
    touteLaConsoleBouge(p.tracks[0]);
    touteLaConsoleBouge(p.tracks[1]);
    const MixRecallReport apercu = previewMixRecall(p, "A");
    VSM_ASSERT_EQ(apercu.changed, static_cast<size_t>(2));
    VSM_ASSERT(p.tracks[0].muted);   // l'aperçu ne touche à rien
    const MixRecallReport bilan = recallMixSnapshot(p, "A");
    VSM_ASSERT(bilan.found);
    VSM_ASSERT_EQ(bilan.recalled, static_cast<size_t>(2));
    VSM_ASSERT_EQ(bilan.withoutState, static_cast<size_t>(0));
    VSM_ASSERT_EQ(bilan.insertsLeft, static_cast<size_t>(0));
    for (size_t i = 0; i < 2; ++i) {
        const TrackMixState& voulu = i == 0 ? basse : nappe;
        const Track& t = p.tracks[i];
        VSM_ASSERT_EQ(t.volume, voulu.volume);
        VSM_ASSERT_EQ(t.pan, voulu.pan);
        VSM_ASSERT(!t.muted && !t.solo && !t.invertPhase);
        VSM_ASSERT_EQ(t.inputTrimDb, 0.0f);
        VSM_ASSERT(t.sendLevels == voulu.sendLevels);
    }
    VSM_ASSERT(p.tracks[0].effects[0].enabled && p.tracks[0].effects[1].enabled);
    // RAPPELER CE QUI EST DÉJÀ LÀ ne change rien, et le dit : l'application n'ouvre pas de pas.
    VSM_ASSERT_EQ(previewMixRecall(p, "A").changed, static_cast<size_t>(0));
    // Un nom inconnu : rien de trouvé, rien de rappelé.
    VSM_ASSERT(!previewMixRecall(p, "Z").found);
}

// Une piste AJOUTÉE après la prise n'a pas d'état : elle reste telle quelle, et c'est compté.
// Un insert REMPLACÉ par un autre type, ou ajouté, est laissé — et compté.
VSM_TEST(ce_que_l_instantane_ne_connait_pas_est_laisse_et_compte) {
    Project p = deuxPistes();
    takeMixSnapshot(p, "A");
    Track neuve;
    neuve.name = "Voix";
    neuve.volume = 0.3f;
    neuve.muted = true;
    p.tracks.push_back(neuve);
    p.tracks[0].effects[0] = {"delay", {}, {}, false};      // remplacé : un autre type au rang 0
    p.tracks[0].effects[1].enabled = false;                  // même type, contourné depuis
    p.tracks[0].effects.push_back({"chorus", {}, {}, false}); // ajouté depuis
    const MixRecallReport bilan = recallMixSnapshot(p, "A");
    VSM_ASSERT_EQ(bilan.recalled, static_cast<size_t>(2));
    VSM_ASSERT_EQ(bilan.withoutState, static_cast<size_t>(1));
    VSM_ASSERT_EQ(bilan.insertsLeft, static_cast<size_t>(2));
    VSM_ASSERT_EQ(p.tracks[2].volume, 0.3f);
    VSM_ASSERT(p.tracks[2].muted);
    VSM_ASSERT(!p.tracks[0].effects[0].enabled);   // le delay laissé tel quel
    VSM_ASSERT(p.tracks[0].effects[1].enabled);    // la reverb reprend son état
    VSM_ASSERT(!p.tracks[0].effects[2].enabled);   // le chorus laissé tel quel
}

// L'ÉTAT SUIT SA PISTE : la piste 0 supprimée, le rappel rend son état à l'ancienne piste 1 et
// à elle seule — rangé par index, il aurait donné à la nappe le volume de la basse.
VSM_TEST(l_etat_suit_sa_piste_quand_une_autre_est_supprimee) {
    Project p = deuxPistes();
    takeMixSnapshot(p, "A");
    removeTrack(p, 0);
    p.tracks[0].volume = 0.2f;
    recallMixSnapshot(p, "A");
    VSM_ASSERT_EQ(p.tracks.size(), static_cast<size_t>(1));
    VSM_ASSERT_EQ(p.tracks[0].name, std::string("Nappe"));
    VSM_ASSERT_EQ(p.tracks[0].volume, 0.6f);
    VSM_ASSERT_EQ(p.tracks[0].pan, -0.5f);
}

// Un vecteur de départs COURT vaut « pas d'envoi » : prolongé de zéros, ce n'est pas un changement.
VSM_TEST(un_depart_prolonge_de_zeros_n_est_pas_un_changement) {
    Project p = deuxPistes();
    takeMixSnapshot(p, "A");
    p.tracks[1].sendLevels = {0.0f, 0.0f};
    VSM_ASSERT_EQ(previewMixRecall(p, "A").changed, static_cast<size_t>(0));
    p.tracks[1].sendLevels = {0.0f, 0.1f};
    VSM_ASSERT_EQ(previewMixRecall(p, "A").changed, static_cast<size_t>(1));
}

// PRENDRE sous un nom pris REMPLACE, à sa place ; SUPPRIMER retire le nom et l'état de chaque piste.
VSM_TEST(prendre_remplace_a_sa_place_et_supprimer_retire_partout) {
    Project p = deuxPistes();
    VSM_ASSERT_EQ(nextMixSnapshotName(p, "Instantané"), std::string("Instantané 1"));
    VSM_ASSERT(!takeMixSnapshot(p, "A"));
    VSM_ASSERT(!takeMixSnapshot(p, "B"));
    VSM_ASSERT_EQ(nextMixSnapshotName(p, "Snapshot"), std::string("Snapshot 3"));
    p.tracks[0].volume = 0.4f;
    VSM_ASSERT(takeMixSnapshot(p, "A"));
    VSM_ASSERT(p.mixSnapshotNames == (std::vector<std::string>{"A", "B"}));
    VSM_ASSERT_EQ(p.tracks[0].mixSnapshots.at("A").volume, 0.4f);
    VSM_ASSERT_EQ(p.tracks[0].mixSnapshots.at("B").volume, 0.8f);
    VSM_ASSERT(removeMixSnapshot(p, "A"));
    VSM_ASSERT(!removeMixSnapshot(p, "A"));
    VSM_ASSERT(p.mixSnapshotNames == (std::vector<std::string>{"B"}));
    for (const auto& t : p.tracks) {
        VSM_ASSERT_EQ(t.mixSnapshots.count("A"), static_cast<size_t>(0));
        VSM_ASSERT_EQ(t.mixSnapshots.count("B"), static_cast<size_t>(1));
    }
}

// UN BUS DE DÉPART RETIRÉ L'EST AUSSI DES INSTANTANÉS : le départ gardé pour le bus 1 revient
// sur ce bus-là (devenu le 0), et non dans l'effet qui a pris son rang.
VSM_TEST(un_bus_retire_l_est_aussi_des_instantanes) {
    Project p = deuxPistes();
    takeMixSnapshot(p, "A");                 // la basse garde {0,25 ; 0,5}
    eraseSendLevelEverywhere(p, 0);
    VSM_ASSERT(p.tracks[0].sendLevels == (std::vector<float>{0.5f}));
    p.tracks[0].sendLevels = {0.0f};
    recallMixSnapshot(p, "A");
    VSM_ASSERT(p.tracks[0].sendLevels == (std::vector<float>{0.5f}));
    VSM_ASSERT_EQ(p.tracks[1].sendLevels.size(), static_cast<size_t>(0));   // rien à effacer : rien d'inventé
}
