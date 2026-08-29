#include "TestFramework.h"
#include "vsm/audio/io/WaveformPeaks.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace vsm::audio::io;

// D5.7 de docs/ROADMAP-daw.md — LA FORME D'ONDE DANS LE CLIP.
//
// Le critère est « 9 minutes s'affichent sans bloquer l'interface ». Neuf
// minutes de stéréo à 48 kHz font cinquante-deux millions d'échantillons : les
// parcourir à chaque rafraîchissement gèlerait tout. On ne les parcourt qu'une
// fois, et le dessin ne lit ensuite que ce qui est à l'écran.

VSM_TEST(the_cache_keeps_both_extremes_and_not_just_the_amplitude) {
    // Une forme d'onde dessinée à partir de l'amplitude seule est symétrique,
    // donc fausse : elle cache les asymétries d'une caisse claire ou d'une
    // voix, qui sont précisément ce qu'on reconnaît d'un coup d'oeil.
    std::vector<float> gauche(512, 0.0f);
    gauche[10] = 0.8f;      // une pointe vers le haut
    gauche[300] = -0.3f;    // une plus petite vers le bas
    const auto cache = computePeaks(gauche.data(), gauche.data(), 512, 256);
    VSM_ASSERT_EQ(cache.size(), size_t(2));
    VSM_ASSERT_NEAR(cache[0].maximum, 0.8f, 1e-6f);
    VSM_ASSERT_NEAR(cache[0].minimum, 0.0f, 1e-6f);
    VSM_ASSERT_NEAR(cache[1].minimum, -0.3f, 1e-6f);
    VSM_ASSERT_NEAR(cache[1].maximum, 0.0f, 1e-6f);
}

VSM_TEST(the_two_channels_are_joined_into_one_shape) {
    // On dessine la forme du CLIP, pas celle d'un de ses côtés : deux tracés
    // superposés dans un rectangle de cinquante pixels ne se distinguent pas.
    std::vector<float> gauche(256, 0.0f), droite(256, 0.0f);
    gauche[5] = 0.5f;
    droite[9] = -0.9f;
    const auto cache = computePeaks(gauche.data(), droite.data(), 256, 256);
    VSM_ASSERT_EQ(cache.size(), size_t(1));
    VSM_ASSERT_NEAR(cache[0].maximum, 0.5f, 1e-6f);
    VSM_ASSERT_NEAR(cache[0].minimum, -0.9f, 1e-6f);
}

VSM_TEST(a_partial_last_bin_is_kept_rather_than_dropped) {
    // Sans elle, la fin du fichier manquerait au dessin -- un clip qui
    // s'arrêterait avant sa fin sans qu'on sache pourquoi.
    std::vector<float> signal(300, 0.25f);
    const auto cache = computePeaks(signal.data(), signal.data(), 300, 256);
    VSM_ASSERT_EQ(cache.size(), size_t(2));
    VSM_ASSERT_NEAR(cache[1].maximum, 0.25f, 1e-6f);
}

VSM_TEST(the_drawing_range_reduces_the_cache_to_the_columns_asked_for) {
    std::vector<float> signal(256 * 100, 0.0f);
    for (size_t i = 0; i < signal.size(); ++i)
        signal[i] = std::sin(static_cast<double>(i) * 0.01) * 0.7f;
    const auto cache = computePeaks(signal.data(), signal.data(),
                                     static_cast<int64_t>(signal.size()), 256);

    const auto colonnes = peaksForRange(cache, 0, static_cast<int64_t>(signal.size()), 40, 256);
    VSM_ASSERT_EQ(colonnes.size(), size_t(40));
    float plusHaut = 0.0f;
    for (const auto& c : colonnes) plusHaut = std::max(plusHaut, c.maximum);
    VSM_ASSERT_NEAR(plusHaut, 0.7f, 0.02f);
}

VSM_TEST(a_range_outside_the_file_draws_a_flat_line_rather_than_nothing) {
    // L'appelant dessine alors une ligne droite, ce qui est exactement ce qu'il
    // y a à voir -- plutôt qu'un tableau court dont il faudrait se méfier.
    std::vector<float> signal(1024, 0.5f);
    const auto cache = computePeaks(signal.data(), signal.data(), 1024, 256);
    const auto colonnes = peaksForRange(cache, 1'000'000, 1'001'000, 20, 256);
    VSM_ASSERT_EQ(colonnes.size(), size_t(20));
    for (const auto& c : colonnes) {
        VSM_ASSERT_NEAR(c.minimum, 0.0f, 1e-9f);
        VSM_ASSERT_NEAR(c.maximum, 0.0f, 1e-9f);
    }
}

VSM_TEST(zoomed_in_no_column_is_left_empty) {
    // Très zoomé, plusieurs colonnes tombent dans la même tranche : une colonne
    // vide ferait un trou dans le tracé là où le son est continu.
    std::vector<float> signal(2048, 0.6f);
    const auto cache = computePeaks(signal.data(), signal.data(), 2048, 256);
    const auto colonnes = peaksForRange(cache, 0, 300, 200, 256);
    VSM_ASSERT_EQ(colonnes.size(), size_t(200));
    for (const auto& c : colonnes) VSM_ASSERT_NEAR(c.maximum, 0.6f, 1e-6f);
}

VSM_TEST(nine_minutes_cost_one_pass_and_the_drawing_costs_none) {
    // LE CRITÈRE DE L'ÉTAPE, mesuré. Neuf minutes de stéréo à 48 kHz : on
    // chronomètre le calcul du cache (une fois, sur un thread de fond) et le
    // calcul d'un tracé (à chaque rafraîchissement, sur le thread d'interface).
    // Le second ne doit pas dépendre de la longueur du fichier.
    const int64_t trames = static_cast<int64_t>(48000.0 * 9.0 * 60.0);
    std::vector<float> gauche(static_cast<size_t>(trames));
    for (int64_t i = 0; i < trames; ++i)
        gauche[static_cast<size_t>(i)] = std::sin(static_cast<double>(i) * 0.001) * 0.5f;

    const auto avant = std::chrono::steady_clock::now();
    const auto cache = computePeaks(gauche.data(), gauche.data(), trames);
    const auto apres = std::chrono::steady_clock::now();
    const double msCache = std::chrono::duration<double, std::milli>(apres - avant).count();

    const auto avantTrace = std::chrono::steady_clock::now();
    for (int passe = 0; passe < 100; ++passe) {
        const auto colonnes = peaksForRange(cache, 0, trames, 1200);
        VSM_ASSERT_EQ(colonnes.size(), size_t(1200));
    }
    const auto apresTrace = std::chrono::steady_clock::now();
    const double msTrace =
        std::chrono::duration<double, std::milli>(apresTrace - avantTrace).count() / 100.0;

    std::printf("// 9 min : cache %.1f ms (une fois), trace %.3f ms (par rafraichissement)\n",
                msCache, msTrace);

    // Le cache tient dans un mégaoctet et demi environ, et se calcule en une
    // fraction de seconde sur un thread de fond.
    VSM_ASSERT(cache.size() > 100000);
    VSM_ASSERT(msCache < 2000.0);
    // LE DESSIN, LUI, DOIT ÊTRE INSTANTANÉ : c'est lui qui tourne sur le thread
    // d'interface, et c'est lui qui gèlerait tout s'il parcourait le fichier.
    VSM_ASSERT(msTrace < 5.0);
}
