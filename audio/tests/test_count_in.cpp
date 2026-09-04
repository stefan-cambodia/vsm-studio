#include "TestFramework.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/sequencer/Project.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace vsm::sequencer;
using namespace vsm::audio::engine;

// D3.2/D3.3 de docs/ROADMAP-daw.md — LE DÉCOMPTE.
//
// Il est modélisé comme un MORCEAU DE LIGNE DE TEMPS SITUÉ AVANT ZÉRO, et non
// comme un second ordonnanceur : la position du transport devient simplement
// négative pendant qu'on compte. Tout le reste du moteur suit sans rien changer,
// et le dernier clic tombe exactement sur le premier temps du morceau -- ce qui
// est toute la raison d'être d'un décompte.

namespace {

Project projetDUneNote(uint16_t ppq = 480) {
    Project projet;
    projet.ticksPerQuarterNote = ppq;   // tempo par défaut : 120 BPM
    Track piste;
    piste.name = "Test";
    uint64_t compteur = 1;
    piste.addNote(0, ppq, 69, 100, 0, compteur);
    projet.tracks.push_back(piste);
    return projet;
}

float crete(const std::vector<float>& buffer) {
    float valeur = 0.0f;
    for (float e : buffer) valeur = std::max(valeur, std::abs(e));
    return valeur;
}

/// Rend `secondes` de son d'un seul tenant, par blocs, à partir de la position
/// courante du graphe.
std::vector<float> rendre(ProcessGraph& graphe, double frequence, int bloc, double secondes) {
    const int total = static_cast<int>(frequence * secondes);
    std::vector<float> gauche(static_cast<size_t>(total), 0.0f);
    std::vector<float> droite(static_cast<size_t>(total), 0.0f);
    for (int i = 0; i < total; i += bloc) {
        const int n = std::min(bloc, total - i);
        graphe.processBlock(gauche.data() + i, droite.data() + i, n);
    }
    return gauche;
}

} // namespace

VSM_TEST(the_transport_position_may_be_negative) {
    // Elle était rabotée à zéro, ce qui rendait le décompte inexprimable.
    ProcessGraph graphe;
    graphe.prepare(8000.0, 256);
    graphe.seekSeconds(-2.0);
    VSM_ASSERT_NEAR(graphe.currentSeconds(), -2.0, 1e-12);
}

VSM_TEST(the_count_in_clicks_even_when_the_metronome_is_off) {
    // Un décompte qu'on n'entend pas ne compte rien : c'est sa seule raison
    // d'être. Le bouton « Clic » reste donc sans effet sur lui.
    ProcessGraph graphe;
    graphe.prepare(8000.0, 256);
    graphe.setProject(projetDUneNote());
    VSM_ASSERT(!graphe.metronomeEnabled());
    graphe.seekSeconds(-1.0);          // deux temps à 120 BPM
    graphe.setPlaying(true);

    auto son = rendre(graphe, 8000.0, 256, 0.9);   // s'arrête avant zéro
    VSM_ASSERT(crete(son) > 0.01f);
}

VSM_TEST(before_zero_only_the_click_sounds_and_never_the_song) {
    // La piste ne doit pas commencer pendant le décompte : le planning n'a rien
    // avant zéro, et c'est ce qui rend le modèle sûr.
    ProcessGraph avecClic;
    avecClic.prepare(8000.0, 256);
    avecClic.setProject(projetDUneNote());
    avecClic.setTrackInstrument(0, "vsm.testtone");
    avecClic.seekSeconds(-1.0);
    avecClic.setPlaying(true);
    // Un seul bloc, pris JUSTE APRÈS le clic du temps -1,0 s pour qu'il soit
    // retombé : à -0,4 s il ne reste que le silence, ou la note si le planning
    // avait fui.
    rendre(avecClic, 8000.0, 256, 0.6);
    auto silence = rendre(avecClic, 8000.0, 256, 0.15);
    VSM_ASSERT(crete(silence) < 1.0e-4f);
}

VSM_TEST(the_last_click_of_the_count_in_falls_on_the_downbeat) {
    // Le clic qui compte vraiment : celui sur lequel on entre. Le bloc qui
    // franchit zéro doit le porter, même métronome éteint.
    ProcessGraph graphe;
    graphe.prepare(8000.0, 64);
    graphe.setProject(projetDUneNote());
    graphe.seekSeconds(-0.004);        // moins d'un bloc avant le zéro
    graphe.setPlaying(true);

    std::vector<float> gauche(64, 0.0f), droite(64, 0.0f);
    graphe.processBlock(gauche.data(), droite.data(), 64);
    VSM_ASSERT(crete(gauche) > 0.01f);
}

VSM_TEST(the_count_in_clicks_land_on_the_beats_and_nowhere_else) {
    // Le métronome bat la mesure du MORCEAU, pas celle du bloc audio. On rend
    // deux secondes à 120 BPM en commençant une seconde avant zéro, et on
    // regarde OÙ se trouve l'énergie : sur les temps -1,0 / -0,5 / 0,0 / 0,5 /
    // 1,0 s, et sur rien d'autre. La taille de bloc est choisie pour qu'aucun
    // temps ne tombe sur une frontière -- c'est le cas qui casserait un
    // métronome qui compterait les blocs.
    const double frequence = 8000.0;
    ProcessGraph graphe;
    graphe.prepare(frequence, 333);
    graphe.setProject(projetDUneNote());
    graphe.setMetronomeEnabled(true);
    graphe.seekSeconds(-1.0);
    graphe.setPlaying(true);

    auto son = rendre(graphe, frequence, 333, 2.0);
    // Énergie d'une fenêtre de 20 ms commençant à `secondes` après le début du
    // rendu (lequel commence à -1,0 s).
    auto creteA = [&](double secondes) {
        const auto debut = static_cast<size_t>((secondes + 1.0) * frequence);
        float valeur = 0.0f;
        for (size_t i = debut; i < debut + 160 && i < son.size(); ++i)
            valeur = std::max(valeur, std::abs(son[i]));
        return valeur;
    };

    for (double temps : {-1.0, -0.5, 0.0, 0.5}) VSM_ASSERT(creteA(temps) > 0.01f);
    // Entre deux temps, le clic est retombé depuis longtemps (22 ms de durée) :
    // il ne doit rester que la note du morceau, et elle n'a pas d'instrument.
    for (double temps : {-0.25, 0.25, 0.75}) VSM_ASSERT(creteA(temps) < 1.0e-4f);
}

// --------------------------------------------------------------------------
// D16.6 — LE MÉTRONOME RÉGLABLE. `setMetronomeLevel` existait et n'était
// appelé nulle part ; l'interface n'avait qu'un interrupteur.
// --------------------------------------------------------------------------

namespace {

/// La crête du CLIC SEUL, à un niveau donné : un projet sans instrument ne
/// produit que le métronome, ce qui rend la mesure sans ambiguïté.
float creteDuClic(float niveau) {
    ProcessGraph graphe;
    graphe.prepare(48000.0, 256);
    graphe.setProject(projetDUneNote());   // aucune machine assignée : pas de son de piste
    graphe.setMetronomeEnabled(true);
    graphe.setMetronomeLevel(niveau);
    graphe.seekSeconds(0.0);
    graphe.setPlaying(true);
    return crete(rendre(graphe, 48000.0, 256, 0.5));
}

} // namespace

VSM_TEST(the_metronome_level_scales_the_click_exactly) {
    const float plein = creteDuClic(1.0f);
    VSM_ASSERT(plein > 0.01f);                       // sinon la mesure ne prouve rien
    const float quart = creteDuClic(0.25f);
    // Le niveau est un GAIN LINÉAIRE appliqué à l'échantillon : un quart du
    // niveau donne un quart de la crête, à l'arrondi du flottant près.
    VSM_ASSERT_NEAR(static_cast<double>(quart), static_cast<double>(plein) * 0.25, 1.0e-6);
    // À zéro, il ne reste rien du tout -- « éteint » et « à zéro » doivent se
    // valoir à l'oreille.
    VSM_ASSERT_NEAR(static_cast<double>(creteDuClic(0.0f)), 0.0, 1.0e-9);
}

VSM_TEST(count_in_only_silences_the_song_but_never_the_count_in) {
    // Un décompte qu'on n'entend pas ne compte rien : c'est sa seule raison
    // d'être, et AUCUN réglage ne doit pouvoir le taire.
    ProcessGraph graphe;
    graphe.prepare(48000.0, 256);
    graphe.setProject(projetDUneNote());
    graphe.setMetronomeEnabled(true);
    graphe.setMetronomeCountInOnly(true);
    graphe.seekSeconds(-1.0);                        // deux temps de décompte à 120 BPM
    graphe.setPlaying(true);
    VSM_ASSERT(crete(rendre(graphe, 48000.0, 256, 0.9)) > 0.01f);

    // Passé zéro, plus rien : c'est ce que « seulement au décompte » veut dire.
    ProcessGraph apres;
    apres.prepare(48000.0, 256);
    apres.setProject(projetDUneNote());
    apres.setMetronomeEnabled(true);
    apres.setMetronomeCountInOnly(true);
    apres.seekSeconds(0.0);
    apres.setPlaying(true);
    VSM_ASSERT_NEAR(static_cast<double>(crete(rendre(apres, 48000.0, 256, 0.5))), 0.0, 1.0e-9);
}

VSM_TEST(record_only_stays_quiet_until_the_application_says_it_records) {
    auto creteEnLecture = [](bool enregistre) {
        ProcessGraph graphe;
        graphe.prepare(48000.0, 256);
        graphe.setProject(projetDUneNote());
        graphe.setMetronomeEnabled(true);
        graphe.setMetronomeRecordOnly(true);
        graphe.setRecording(enregistre);
        graphe.seekSeconds(0.0);
        graphe.setPlaying(true);
        return crete(rendre(graphe, 48000.0, 256, 0.5));
    };
    VSM_ASSERT_NEAR(static_cast<double>(creteEnLecture(false)), 0.0, 1.0e-9);
    VSM_ASSERT(creteEnLecture(true) > 0.01f);

    // Et le décompte bat quand même, enregistrement ou non.
    ProcessGraph graphe;
    graphe.prepare(48000.0, 256);
    graphe.setProject(projetDUneNote());
    graphe.setMetronomeEnabled(true);
    graphe.setMetronomeRecordOnly(true);
    graphe.setRecording(false);
    graphe.seekSeconds(-1.0);
    graphe.setPlaying(true);
    VSM_ASSERT(crete(rendre(graphe, 48000.0, 256, 0.9)) > 0.01f);
}
