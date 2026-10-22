#include "TestFramework.h"
#include "vsm/sequencer/MixSnapshot.h"
#include "vsm/sequencer/Project.h"
#include <cmath>
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

// D559.1 — LEVER TOUS LES SOLOS, TOUS LES MUETS (*Deactivate All Solo / Mute*) : chaque piste perd le sien, le compte dit
// combien ; le solo PROTÉGÉ (D30.1) n'est pas un solo et reste ; rejoué, zéro.
VSM_TEST(clearing_all_solos_and_mutes_counts_them_and_keeps_solo_safe) {
    Project p = deuxPistes();
    p.tracks.resize(4);
    p.tracks[0].solo = true;
    p.tracks[1].solo = true;
    p.tracks[2].muted = true;
    p.tracks[3].soloSafe = true;
    p.tracks[3].muted = true;
    VSM_ASSERT_EQ(countSoloedTracks(p), size_t(2));
    VSM_ASSERT_EQ(countMutedTracks(p), size_t(2));
    VSM_ASSERT_EQ(clearAllSolos(p), size_t(2));
    VSM_ASSERT(!p.tracks[0].solo && !p.tracks[1].solo);
    VSM_ASSERT(p.tracks[2].muted);   // lever les solos ne touche pas aux muets
    VSM_ASSERT(p.tracks[3].soloSafe);
    VSM_ASSERT_EQ(clearAllSolos(p), size_t(0));
    VSM_ASSERT_EQ(clearAllMutes(p), size_t(2));
    VSM_ASSERT(!p.tracks[2].muted && !p.tracks[3].muted);
    VSM_ASSERT_EQ(countMutedTracks(p), size_t(0));
}

// D559.2 — REMETTRE LA CONSOLE À ZÉRO (*Reset Mixer*) : chaque tranche revient à l'état d'une piste neuve — volume 0 dB,
// panoramique au centre, ni muet ni solo, trim 0, polarité normale, départs à zéro ; les INSERTS restent, leur état
// compris ; le compte dit les tranches changées ; rejoué, zéro.
VSM_TEST(resetting_the_console_brings_every_strip_back_to_a_new_track_but_keeps_inserts) {
    Project p = deuxPistes();
    touteLaConsoleBouge(p.tracks[1]);
    p.tracks[0].effects[1].enabled = false;   // un insert contourné
    VSM_ASSERT_EQ(countTracksToReset(p), size_t(2));
    VSM_ASSERT_EQ(resetConsole(p), size_t(2));
    for (const auto& t : p.tracks) {
        VSM_ASSERT_EQ(t.volume, 1.0f);
        VSM_ASSERT_EQ(t.pan, 0.0f);
        VSM_ASSERT(!t.muted && !t.solo && !t.invertPhase);
        VSM_ASSERT_EQ(t.inputTrimDb, 0.0f);
        for (const float d : t.sendLevels) VSM_ASSERT_EQ(d, 0.0f);
    }
    VSM_ASSERT_EQ(p.tracks[0].effects.size(), size_t(2));   // les inserts restent
    VSM_ASSERT(!p.tracks[0].effects[1].enabled);              // et leur état
    VSM_ASSERT_EQ(countTracksToReset(p), size_t(0));
    VSM_ASSERT_EQ(resetConsole(p), size_t(0));
}

// D562.3 — LES RÉGLAGES D'UNE TRANCHE : trim, phase, départs et inserts passent ; volume, panoramique, muet, solo et
// sortie restent ; un dossier et un VCA ne reçoivent rien ; recollé, rien ne change.
VSM_TEST(channel_settings_carry_the_sound_of_a_strip_and_leave_its_balance) {
    Track source;
    source.inputTrimDb = 4.5f;
    source.invertPhase = true;
    source.sendLevels = {0.25f, 0.5f};
    TrackEffect comp;
    comp.type = "compressor";
    comp.parameters["threshold"] = -18.0f;
    TrackEffect rev;
    rev.type = "reverb";
    rev.enabled = false;
    source.effects = {comp, rev};
    source.volume = 0.3f;
    const auto reglages = captureChannelSettings(source);

    Track cible;
    cible.volume = 0.9f;
    cible.pan = -0.5f;
    cible.muted = true;
    cible.outputGroup = 3;
    VSM_ASSERT(channelSettingsWouldChange(cible, reglages));
    VSM_ASSERT(applyChannelSettings(cible, reglages));
    VSM_ASSERT_NEAR(cible.inputTrimDb, 4.5f, 1e-6);
    VSM_ASSERT(cible.invertPhase);
    VSM_ASSERT_NEAR(cible.sendLevel(1), 0.5f, 1e-6);
    VSM_ASSERT_EQ(cible.effects.size(), size_t(2));
    VSM_ASSERT_NEAR(cible.effects[0].parameters["threshold"], -18.0f, 1e-6);
    VSM_ASSERT(!cible.effects[1].enabled);                         // le contournement passe
    VSM_ASSERT_NEAR(cible.volume, 0.9f, 1e-6);                     // la balance reste
    VSM_ASSERT_NEAR(cible.pan, -0.5f, 1e-6);
    VSM_ASSERT(cible.muted);
    VSM_ASSERT_EQ(cible.outputGroup, 3);
    VSM_ASSERT(!applyChannelSettings(cible, reglages));            // recollé : rien

    Track dossier;
    dossier.kind = Track::Kind::Folder;
    VSM_ASSERT(!applyChannelSettings(dossier, reglages));
    Track vca;
    vca.kind = Track::Kind::Vca;
    VSM_ASSERT(!channelSettingsWouldChange(vca, reglages));

    // Un départ court vaut zéro au-delà : {0,25} et {0,25, 0} sont les mêmes départs.
    Track courte = cible;
    courte.sendLevels = {0.25f, 0.5f, 0.0f};
    VSM_ASSERT(!channelSettingsWouldChange(courte, reglages));
}

// D564.2 — REMETTRE À ZÉRO LES TRANCHES CHOISIES : la remise de D559.2, sur les seules pistes données.
VSM_TEST(resetting_chosen_strips_leaves_the_others_alone) {
    Project p;
    for (int i = 0; i < 3; ++i) {
        Track t;
        t.volume = 0.3f;
        t.inputTrimDb = 2.0f;
        t.sendLevels = {0.5f};
        p.tracks.push_back(t);
    }
    VSM_ASSERT_EQ(countTracksToReset(p, {0, 2, 7}), size_t(2));      // un rang hors du projet est ignoré
    VSM_ASSERT_EQ(resetConsole(p, {0, 2}), size_t(2));
    VSM_ASSERT_NEAR(p.tracks[0].volume, Track().volume, 1e-6);
    VSM_ASSERT_NEAR(p.tracks[2].sendLevel(0), 0.0f, 1e-6);
    VSM_ASSERT_NEAR(p.tracks[1].volume, 0.3f, 1e-6);                  // la piste non choisie garde tout
    VSM_ASSERT_NEAR(p.tracks[1].inputTrimDb, 2.0f, 1e-6);
    VSM_ASSERT_EQ(countTracksToReset(p, {0, 2}), size_t(0));
    VSM_ASSERT_EQ(resetConsole(p), size_t(1));                        // la console entière : la dernière
}

// D566.3 — LIER LES TRANCHES CHOISIES : le même écart, en dB pour le volume, compté depuis le début du geste.
namespace {
float gainDe(float db) { return std::pow(10.0f, db / 20.0f); }
float dbDe(float gain) { return 20.0f * std::log10(gain); }
} // namespace

VSM_TEST(linked_volume_follows_by_the_same_decibels_from_the_gesture_start) {
    Project p;
    p.tracks.resize(4);
    p.tracks[0].volume = 1.0f;            // 0 dB, la piste touchée
    p.tracks[1].volume = gainDe(-6.0f);
    p.tracks[2].volume = gainDe(-50.0f);
    p.tracks[3].volume = 0.5f;            // hors du choix
    const auto g = beginLinkedMix(p, LinkedMixParam::Volume, 0, {0, 1, 2});
    VSM_ASSERT(g.active());
    VSM_ASSERT_EQ(g.followers.size(), size_t(2));

    p.tracks[0].volume = gainDe(3.0f);
    VSM_ASSERT_EQ(applyLinkedMix(p, g), size_t(2));
    VSM_ASSERT_NEAR(dbDe(p.tracks[1].volume), -3.0f, 1e-3);
    VSM_ASSERT_NEAR(dbDe(p.tracks[2].volume), -47.0f, 1e-3);
    VSM_ASSERT_NEAR(p.tracks[3].volume, 0.5f, 1e-9);            // hors du choix : rien

    // Borné en route : −50 − 20 tombe sous −60, c'est le silence…
    p.tracks[0].volume = gainDe(-20.0f);
    applyLinkedMix(p, g);
    VSM_ASSERT_NEAR(dbDe(p.tracks[1].volume), -26.0f, 1e-3);
    VSM_ASSERT_EQ(p.tracks[2].volume, 0.0f);
    // … et le geste qui revient lui rend son écart, compté depuis le DÉBUT et non depuis la borne.
    p.tracks[0].volume = gainDe(-5.0f);
    applyLinkedMix(p, g);
    VSM_ASSERT_NEAR(dbDe(p.tracks[2].volume), -55.0f, 1e-3);
    // Revenu au départ : les valeurs exactes, sans aller-retour par les dB.
    p.tracks[0].volume = 1.0f;
    applyLinkedMix(p, g);
    VSM_ASSERT_EQ(p.tracks[1].volume, gainDe(-6.0f));
    VSM_ASSERT_EQ(p.tracks[2].volume, gainDe(-50.0f));
    VSM_ASSERT_EQ(applyLinkedMix(p, g), size_t(0));             // rien de plus à écrire
}

VSM_TEST(linked_volume_counts_silence_as_minus_sixty_and_stops_at_plus_six) {
    Project p;
    p.tracks.resize(3);
    p.tracks[0].volume = gainDe(-10.0f);
    p.tracks[1].volume = 0.0f;            // le silence, le bas du fader
    p.tracks[2].volume = gainDe(4.0f);
    VSM_ASSERT_NEAR(linkedVolumeDb(0.0f), -60.0f, 1e-9);
    VSM_ASSERT_NEAR(linkedVolumeDb(gainDe(-80.0f)), -60.0f, 1e-6);
    const auto g = beginLinkedMix(p, LinkedMixParam::Volume, 0, {0, 1, 2});
    p.tracks[0].volume = gainDe(-4.0f);   // +6 dB
    applyLinkedMix(p, g);
    VSM_ASSERT_NEAR(dbDe(p.tracks[1].volume), -54.0f, 1e-3);
    VSM_ASSERT_NEAR(dbDe(p.tracks[2].volume), 6.0f, 1e-3);      // +10 borné à +6
}

VSM_TEST(linked_pan_follows_by_value_and_skips_what_has_no_pan) {
    Project p;
    p.tracks.resize(5);
    p.tracks[1].pan = -0.5f;
    p.tracks[2].pan = 0.8f;
    p.tracks[3].kind = Track::Kind::Vca;      // un VCA : pas de panoramique, mais un volume
    p.tracks[4].kind = Track::Kind::Folder;   // un dossier : ni l'un ni l'autre
    const auto g = beginLinkedMix(p, LinkedMixParam::Pan, 0, {0, 1, 2, 3, 4});
    VSM_ASSERT_EQ(g.followers.size(), size_t(2));
    p.tracks[0].pan = 0.3f;
    VSM_ASSERT_EQ(applyLinkedMix(p, g), size_t(2));
    VSM_ASSERT_NEAR(p.tracks[1].pan, -0.2f, 1e-6);
    VSM_ASSERT_NEAR(p.tracks[2].pan, 1.0f, 1e-9);               // borné à +1
    VSM_ASSERT_EQ(p.tracks[3].pan, 0.0f);
    p.tracks[0].pan = 0.0f;
    applyLinkedMix(p, g);
    VSM_ASSERT_EQ(p.tracks[2].pan, 0.8f);                        // le geste revenu : l'écart retrouvé
    const auto v = beginLinkedMix(p, LinkedMixParam::Volume, 0, {0, 1, 2, 3, 4});
    VSM_ASSERT_EQ(v.followers.size(), size_t(3));                // le VCA suit le volume, pas le dossier
}

VSM_TEST(a_track_touched_outside_the_selection_moves_alone) {
    Project p;
    p.tracks.resize(3);
    const auto g = beginLinkedMix(p, LinkedMixParam::Volume, 2, {0, 1});
    VSM_ASSERT(!g.active());
    p.tracks[2].volume = 0.25f;
    VSM_ASSERT_EQ(applyLinkedMix(p, g), size_t(0));
    VSM_ASSERT_EQ(p.tracks[0].volume, 1.0f);
    VSM_ASSERT(!beginLinkedMix(p, LinkedMixParam::Volume, 0, {0}).active());   // seule choisie : rien à lier
}
