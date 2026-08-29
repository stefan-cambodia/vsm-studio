#include "TestFramework.h"
#include "vsm/audio/engine/Metronome.h"
#include <vector>

using namespace vsm::audio::engine;

// D3.2 de docs/ROADMAP-daw.md — LE MÉTRONOME.
//
// Sans lui on ne peut pas commencer un morceau à partir de rien : on n'a que le
// choix d'importer un fichier déjà joué. C'est la différence entre un lecteur
// et un studio.

VSM_TEST(the_beats_of_a_range_are_found_and_the_downbeat_is_marked) {
    std::vector<std::pair<int64_t, bool>> temps;
    forEachBeatInRange(0, 480 * 8, 480, 4,
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
    forEachBeatInRange(100, 470, 480, 4, [&](int64_t tick, bool) { temps.push_back(tick); });
    VSM_ASSERT(temps.empty());

    forEachBeatInRange(100, 500, 480, 4, [&](int64_t tick, bool) { temps.push_back(tick); });
    VSM_ASSERT_EQ(temps.size(), size_t(1));
    VSM_ASSERT_EQ(temps[0], int64_t(480));
}

VSM_TEST(a_beat_is_never_counted_twice_across_adjoining_ranges) {
    // Deux blocs qui se suivent doivent produire chaque temps UNE fois : un
    // intervalle fermé des deux côtés doublerait le temps de la frontière.
    std::vector<int64_t> temps;
    forEachBeatInRange(0, 480, 480, 4, [&](int64_t t, bool) { temps.push_back(t); });
    forEachBeatInRange(480, 960, 480, 4, [&](int64_t t, bool) { temps.push_back(t); });
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
