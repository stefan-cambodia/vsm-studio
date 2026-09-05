#include "TestFramework.h"
#include "vsm/audio/io/ZeroCrossing.h"
#include <cmath>
#include <vector>

using namespace vsm::audio::io;

// D21.3 de docs/ROADMAP-daw.md — COUPER AU PASSAGE PAR ZÉRO.

namespace {
auto lecteur(const std::vector<float>& s) {
    return [&s](int64_t i, float& g, float& d) {
        if (i < 0 || i >= static_cast<int64_t>(s.size())) return false;
        g = d = s[static_cast<size_t>(i)];
        return true;
    };
}
} // namespace

VSM_TEST(the_nearest_zero_crossing_of_a_sine_is_found_on_both_sides) {
    // Une sinusoïde à 100 Hz sur 48 kHz : un passage par zéro tous les 240
    // échantillons, aux multiples de 240.
    std::vector<float> s(48000);
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = std::sin(2.0f * 3.14159265f * 100.0f * static_cast<float>(i) / 48000.0f);
    // À 250 (10 après le zéro de 240) : le plus proche est 240, pas 480.
    VSM_ASSERT_EQ(nearestZeroCrossing(lecteur(s), 250, 96), int64_t(240));
    // À 470 (10 avant le zéro de 480) : 480.
    VSM_ASSERT_EQ(nearestZeroCrossing(lecteur(s), 470, 96), int64_t(480));
    // Déjà sur un zéro : ne bouge pas.
    VSM_ASSERT_EQ(nearestZeroCrossing(lecteur(s), 480, 96), int64_t(480));
    // Fenêtre trop courte pour atteindre un zéro (à 360, le plus proche est à
    // 120 échantillons) : l'instant est rendu tel quel.
    VSM_ASSERT_EQ(nearestZeroCrossing(lecteur(s), 360, 50), int64_t(360));
}

VSM_TEST(a_dc_offset_or_an_empty_window_leaves_the_cut_where_it_is) {
    std::vector<float> s(4800, 0.3f);   // du continu : aucun passage
    VSM_ASSERT_EQ(nearestZeroCrossing(lecteur(s), 1000, 96), int64_t(1000));
    VSM_ASSERT_EQ(nearestZeroCrossing(lecteur(s), 1000, 0), int64_t(1000));
    // Au bord du matériau, la fenêtre est tronquée sans lire hors des bornes.
    std::vector<float> t(100, 0.3f);
    t[95] = -0.3f;   // deux passages : 94/95 et 95/96
    // Depuis 98, le plus proche est 95/96 ; à égale distance de zéro, le
    // premier des deux échantillons, 95.
    VSM_ASSERT_EQ(nearestZeroCrossing(lecteur(t), 98, 96), int64_t(95));
    // Depuis 93, c'est 94/95 : 94.
    VSM_ASSERT_EQ(nearestZeroCrossing(lecteur(t), 93, 96), int64_t(94));
}
