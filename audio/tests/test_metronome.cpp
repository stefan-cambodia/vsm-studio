#include "TestFramework.h"
#include "vsm/audio/engine/Metronome.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/sequencer/Project.h"
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace vsm::audio::engine;

// D590 : la carte des signatures d'origine (4/4 au tick 0), celle que les tests de D3.2 supposaient.
static const vsm::sequencer::TimeSignatureMap kQuatreQuatre;

// D3.2 de docs/ROADMAP-daw.md — LE MÉTRONOME.
//
// Sans lui on ne peut pas commencer un morceau à partir de rien : on n'a que le
// choix d'importer un fichier déjà joué. C'est la différence entre un lecteur
// et un studio.

VSM_TEST(the_beats_of_a_range_are_found_and_the_downbeat_is_marked) {
    std::vector<std::pair<int64_t, bool>> temps;
    forEachClickInRange(kQuatreQuatre, 480, 0, 480 * 8,
                        [&](int64_t tick, bool accent) { temps.emplace_back(tick, accent); });
    VSM_ASSERT_EQ(temps.size(), size_t(8));
    VSM_ASSERT_EQ(temps[0].first, int64_t(0));
    VSM_ASSERT(temps[0].second);        // premier temps de la mesure
    VSM_ASSERT(!temps[1].second);
    VSM_ASSERT(!temps[3].second);
    VSM_ASSERT(temps[4].second);        // mesure suivante
}

VSM_TEST(a_range_that_starts_between_two_beats_does_not_invent_one) {
    // LE PIÈGE : arrondir vers le bas donnerait un temps à chaque bloc, et le
    // métronome battrait la mesure du bloc audio plutôt que celle du morceau.
    std::vector<int64_t> temps;
    forEachClickInRange(kQuatreQuatre, 480, 100, 470, [&](int64_t tick, bool) { temps.push_back(tick); });
    VSM_ASSERT(temps.empty());

    forEachClickInRange(kQuatreQuatre, 480, 100, 500, [&](int64_t tick, bool) { temps.push_back(tick); });
    VSM_ASSERT_EQ(temps.size(), size_t(1));
    VSM_ASSERT_EQ(temps[0], int64_t(480));
}

VSM_TEST(a_beat_is_never_counted_twice_across_adjoining_ranges) {
    // Deux blocs qui se suivent doivent produire chaque temps UNE fois : un
    // intervalle fermé des deux côtés doublerait le temps de la frontière.
    std::vector<int64_t> temps;
    forEachClickInRange(kQuatreQuatre, 480, 0, 480, [&](int64_t t, bool) { temps.push_back(t); });
    forEachClickInRange(kQuatreQuatre, 480, 480, 960, [&](int64_t t, bool) { temps.push_back(t); });
    VSM_ASSERT_EQ(temps.size(), size_t(2));
    VSM_ASSERT_EQ(temps[0], int64_t(0));
    VSM_ASSERT_EQ(temps[1], int64_t(480));
}

VSM_TEST(a_click_sounds_then_stops_by_itself) {
    Metronome metronome;
    metronome.prepare(48000.0);
    VSM_ASSERT(!metronome.active());
    metronome.trigger(true);
    VSM_ASSERT(metronome.active());

    float crete = 0.0f;
    int echantillons = 0;
    while (metronome.active() && echantillons < 48000) {
        crete = std::max(crete, std::abs(metronome.nextSample()));
        ++echantillons;
    }
    VSM_ASSERT(crete > 0.01f);                  // il s'entend
    VSM_ASSERT(echantillons < 48000 / 10);      // et il ne dure pas
    VSM_ASSERT_NEAR(metronome.nextSample(), 0.0f, 1e-9);
}

VSM_TEST(the_downbeat_is_higher_than_the_other_beats) {
    // Reconnaître le premier temps sans y penser est tout l'intérêt : deux
    // clics identiques obligeraient à compter.
    auto passagesParZero = [](bool accent) {
        Metronome m;
        m.prepare(48000.0);
        m.trigger(accent);
        int passages = 0;
        float precedent = 0.0f;
        while (m.active()) {
            const float valeur = m.nextSample();
            if ((precedent < 0.0f) != (valeur < 0.0f)) ++passages;
            precedent = valeur;
        }
        return passages;
    };
    VSM_ASSERT(passagesParZero(true) > passagesParZero(false));
}

// D590 de docs/ROADMAP-daw.md — LE CLIC, À SON ÉCHANTILLON, SUR L'UNITÉ ET SUR LA MESURE QUI JOUENT. Mesuré dans le
// GRAPHE, par ce qui sort : un morceau sans rien qui sonne avant 40 s (une note au loin, sans quoi la lecture d'un
// projet sans note s'arrête à 0), le clic allumé. Ces tests ne se servent que de l'API qui existait avant D590 : le
// même code a mesuré le témoin (le moteur d'avant).

namespace {

struct ClicRendu {
    int64_t echantillon;   ///< celui où le clic part : le premier échantillon d'un clic vaut sin 0 = 0
    bool accent;           ///< 1 600 Hz (plus de 26 passages par zéro en 10 ms) contre 1 000 Hz
};

vsm::sequencer::Project projetDuClic(int numerateur, int puissanceDuDenominateur) {
    vsm::sequencer::Project projet;
    projet.ticksPerQuarterNote = 480;   // tempo par défaut : 120 BPM
    projet.timeSignatureMap.clear();
    projet.timeSignatureMap.addChange(0, static_cast<uint8_t>(numerateur), static_cast<uint8_t>(puissanceDuDenominateur));
    vsm::sequencer::Track piste;
    piste.name = "Loin";
    uint64_t compteur = 1;
    piste.addNote(480 * 80, 480, 69, 100, 0, compteur);
    projet.tracks.push_back(piste);
    return projet;
}

/// Rend `secondes` depuis `depuis`, par blocs de `bloc` à 48 kHz, et relève les clics : un échantillon nul suivi d'un
/// non nul, après au moins 64 échantillons de silence.
std::vector<ClicRendu> clicsRendus(const vsm::sequencer::Project& projet, int bloc, double depuis, double secondes) {
    const double frequence = 48000.0;
    auto graphe = std::make_unique<ProcessGraph>();   // 3,4 Mo : sur le tas (D589)
    graphe->prepare(frequence, bloc);
    graphe->setProject(projet);
    graphe->setMetronomeEnabled(true);
    graphe->seekSeconds(depuis);
    graphe->setPlaying(true);
    const int total = static_cast<int>(frequence * secondes);
    std::vector<float> gauche(static_cast<size_t>(total), 0.0f), droite(static_cast<size_t>(total), 0.0f);
    for (int i = 0; i < total; i += bloc) graphe->processBlock(gauche.data() + i, droite.data() + i, std::min(bloc, total - i));
    std::vector<ClicRendu> clics;
    int silence = 64;
    for (int i = 0; i + 1 < total; ++i) {
        if (gauche[static_cast<size_t>(i)] == 0.0f && gauche[static_cast<size_t>(i) + 1] != 0.0f && silence >= 64) {
            int passages = 0;
            const int fin = std::min(total, i + static_cast<int>(frequence * 0.010));
            for (int j = i + 1; j + 1 < fin; ++j)
                if ((gauche[static_cast<size_t>(j)] < 0.0f) != (gauche[static_cast<size_t>(j) + 1] < 0.0f)) ++passages;
            clics.push_back({i, passages > 26});
        }
        silence = gauche[static_cast<size_t>(i)] == 0.0f ? silence + 1 : 0;
    }
    return clics;
}

std::string decrire(const std::vector<ClicRendu>& clics) {
    std::string s;
    for (const auto& c : clics) s += std::to_string(c.echantillon) + (c.accent ? "* " : " ");
    return s;
}

} // namespace

VSM_TEST(d590_each_click_starts_at_the_sample_of_its_beat_whatever_the_block) {
    // D590.1 (1) : 4/4, huit temps, le temps k à k·24 000. Blocs de 512 (24 000 ≡ 448 mod 512 : le témoin part en avance
    // de 0, 448, 384 … 64) et blocs de 100 (24 000 est un multiple de 100 : le témoin du témoin, aucune avance).
    // Les deux tailles se relèvent AVANT la première assertion : une assertion ratée rend la main, et le témoin du
    // témoin ne s'imprimait pas (D590, mesuré).
    std::vector<std::vector<ClicRendu>> parBloc;
    for (int bloc : {512, 100}) {
        parBloc.push_back(clicsRendus(projetDuClic(4, 2), bloc, 0.0, 4.0 - 0.001));
        std::string avances;
        for (const auto& c : parBloc.back())
            avances += std::to_string(((c.echantillon + 12000) / 24000) * 24000 - c.echantillon) + " ";
        std::printf("    [D590.1] blocs de %d : %zu clics, avances %s\n", bloc, parBloc.back().size(), avances.c_str());
    }
    for (const auto& clics : parBloc) {
        VSM_ASSERT_EQ(clics.size(), size_t(8));
        for (size_t k = 0; k < clics.size(); ++k) VSM_ASSERT_EQ(clics[k].echantillon, static_cast<int64_t>(k) * 24000);
    }
}

VSM_TEST(d590_the_click_beats_the_unit_of_the_signature_and_marks_every_bar) {
    // D590.2 (2) : 6/8, trois secondes — douze croches (toutes les 0,25 s = 12 000 échantillons), l'accent à 0 et 1,5 s.
    const auto clics = clicsRendus(projetDuClic(6, 3), 512, 0.0, 3.0 - 0.001);
    std::printf("    [D590.2] 6/8 : %zu clics : %s\n", clics.size(), decrire(clics).c_str());
    VSM_ASSERT_EQ(clics.size(), size_t(12));
    for (size_t k = 0; k < clics.size(); ++k) {
        VSM_ASSERT_EQ(clics[k].echantillon, static_cast<int64_t>(k) * 12000);
        VSM_ASSERT_EQ(clics[k].accent, k % 6 == 0);
    }
}

VSM_TEST(d590_the_accent_falls_where_the_ruler_puts_the_bar_after_a_signature_change) {
    // D590.3 (3) : 4/4, puis 3/4 au tick 1 920 (2 s) — les accents aux ticks 0, 1 920, 3 360, 4 800 (0, 2, 3,5 et 5 s).
    auto projet = projetDuClic(4, 2);
    projet.timeSignatureMap.addChange(1920, 3, 2);
    const auto clics = clicsRendus(projet, 512, 0.0, 6.0 - 0.001);
    std::vector<int64_t> accents;
    std::string s;
    for (const auto& c : clics)
        if (c.accent) { accents.push_back(c.echantillon * 480 / 24000); s += std::to_string(accents.back()) + " "; }
    std::printf("    [D590.3] 4/4 puis 3/4 : %zu clics, accents aux ticks %s\n", clics.size(), s.c_str());
    VSM_ASSERT_EQ(clics.size(), size_t(12));
    VSM_ASSERT(accents == (std::vector<int64_t>{0, 1920, 3360, 4800}));
}

VSM_TEST(d590_a_count_in_bar_in_six_eight_clicks_six_times_from_its_downbeat) {
    // D590.2 et D590.3 (4) : le décompte d'une mesure en 6/8, de −1,5 s à 0 — six croches, l'accent sur la première,
    // puis l'accent de zéro.
    const auto clics = clicsRendus(projetDuClic(6, 3), 512, -1.5, 1.5 + 0.1);
    std::printf("    [D590.3] décompte 6/8 : %zu clics : %s\n", clics.size(), decrire(clics).c_str());
    VSM_ASSERT_EQ(clics.size(), size_t(7));
    for (size_t k = 0; k < clics.size(); ++k) {
        VSM_ASSERT_EQ(clics[k].echantillon, static_cast<int64_t>(k) * 12000);
        VSM_ASSERT_EQ(clics[k].accent, k == 0 || k == 6);
    }
}

VSM_TEST(d590_two_two_clicks_the_half_note) {
    // D590.2 : en 2/2, la blanche (960 ticks), l'accent toutes les deux.
    vsm::sequencer::TimeSignatureMap carte;
    carte.addChange(0, 2, 1);
    std::vector<std::pair<int64_t, bool>> clics;
    forEachClickInRange(carte, 480, 0, 1920 * 2, [&](int64_t t, bool a) { clics.emplace_back(t, a); });
    VSM_ASSERT(clics == (std::vector<std::pair<int64_t, bool>>{{0, true}, {960, false}, {1920, true}, {2880, false}}));
}

VSM_TEST(d590_adjoining_ranges_across_a_signature_change_give_each_click_once) {
    // Le piège de D3.2, à cheval sur un changement (4/4, puis 6/8 au tick 1 920) et avant zéro : des blocs jointifs de
    // 37 ticks rendent EXACTEMENT les clics d'un seul intervalle — aucun doublé à la frontière, aucun manqué.
    vsm::sequencer::TimeSignatureMap carte;
    carte.addChange(1920, 6, 3);
    std::vector<std::pair<int64_t, bool>> dUnTenant, parBlocs;
    forEachClickInRange(carte, 480, -1000, 4000, [&](int64_t t, bool a) { dUnTenant.emplace_back(t, a); });
    for (int64_t debut = -1000; debut < 4000; debut += 37)
        forEachClickInRange(carte, 480, debut, std::min<int64_t>(debut + 37, 4000),
                            [&](int64_t t, bool a) { parBlocs.emplace_back(t, a); });
    VSM_ASSERT(dUnTenant == parBlocs);
    // −960, −480 (la mesure de 4/4 qui finit à 0 commence à −1 920) ; 0* 480 960 1440 ; 1920* 2160 … 3120 ; 3360* …
    VSM_ASSERT(dUnTenant.front() == (std::pair<int64_t, bool>{-960, false}));
    VSM_ASSERT(dUnTenant[2] == (std::pair<int64_t, bool>{0, true}));
    VSM_ASSERT(dUnTenant[6] == (std::pair<int64_t, bool>{1920, true}));
    VSM_ASSERT(dUnTenant[7] == (std::pair<int64_t, bool>{2160, false}));
    VSM_ASSERT(dUnTenant[12] == (std::pair<int64_t, bool>{3360, true}));
}
