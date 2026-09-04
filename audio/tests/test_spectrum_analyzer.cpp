#include "TestFramework.h"
#include "vsm/audio/dsp/SpectrumAnalyzer.h"
#include "vsm/audio/dsp/SpectrumTap.h"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace vsm::audio::dsp;

// D15.3 -- L'ANALYSEUR DE SPECTRE. Attendu, écrit avant la mesure : un sinus
// à 1 kHz plein-échelle sort dans la bonne case à ± un demi-ton (le tableau
// de la phase), et l'on vise mieux -- à ± une case (11,7 Hz) sans
// interpolation, à ± 1 Hz avec ; son niveau lit 0 dB à ± 0,1 dB ; un sinus à
// -60 dB lit -60 dB ; le silence lit le plancher.

VSM_TEST(a_one_kilohertz_sine_lands_in_the_right_bin_at_zero_decibels) {
    SpectrumAnalyzer<4096> analyseur;
    std::vector<float> s(4096);
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = static_cast<float>(std::sin(2.0 * M_PI * 1000.0 * static_cast<double>(i) / 48000.0));
    analyseur.analyze(s.data());
    const double caseHz = SpectrumAnalyzer<4096>::binFrequency(analyseur.peakBin(), 48000.0);
    const double fine = analyseur.peakFrequency(48000.0);
    const float brut = analyseur.magnitudeDb(analyseur.peakBin());
    const float niveau = analyseur.peakDb();
    std::printf("    [banc spectre] 1 kHz : case %.1f Hz, affinee %.2f Hz, niveau de la case %.2f dB, sommet %.2f dB\n",
                caseHz, fine, static_cast<double>(brut), static_cast<double>(niveau));
    const double demiTon = 1000.0 * (std::pow(2.0, 1.0 / 24.0) - 1.0);   // 29,3 Hz
    VSM_ASSERT(std::abs(caseHz - 1000.0) < demiTon);
    VSM_ASSERT(std::abs(caseHz - 1000.0) <= 48000.0 / 4096.0);   // une case
    VSM_ASSERT(std::abs(fine - 1000.0) < 1.0);
    VSM_ASSERT(std::abs(niveau) < 0.1f);
}

VSM_TEST(the_decibel_scale_is_the_signal_level_and_silence_is_the_floor) {
    SpectrumAnalyzer<4096> analyseur;
    std::vector<float> s(4096);
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = 0.001f * static_cast<float>(std::sin(2.0 * M_PI * 440.0 * static_cast<double>(i) / 48000.0));
    analyseur.analyze(s.data());
    const float niveau = analyseur.peakDb();
    std::printf("    [banc spectre] 440 Hz a -60 dB : case %.2f dB, sommet %.2f dB, crete %.1f Hz\n",
                static_cast<double>(analyseur.magnitudeDb(analyseur.peakBin())), static_cast<double>(niveau),
                analyseur.peakFrequency(48000.0));
    VSM_ASSERT(std::abs(niveau + 60.0f) < 0.1f);
    VSM_ASSERT(std::abs(analyseur.peakFrequency(48000.0) - 440.0) < 1.0);

    std::fill(s.begin(), s.end(), 0.0f);
    analyseur.analyze(s.data());
    for (size_t k = 0; k < SpectrumAnalyzer<4096>::kBins; ++k)
        VSM_ASSERT_EQ(analyseur.magnitudeDb(k), SpectrumAnalyzer<4096>::kFloorDb);
}

VSM_TEST(the_tap_hands_back_the_latest_samples_in_order_and_zeros_before_the_start) {
    SpectrumTap prise;
    std::vector<float> l(512), r(512);
    // Éteinte : rien n'entre.
    for (size_t i = 0; i < 512; ++i) { l[i] = static_cast<float>(i); r[i] = static_cast<float>(i); }
    prise.write(l.data(), r.data(), 512);
    VSM_ASSERT_EQ(prise.totalWritten(), size_t(0));

    prise.setEnabled(true);
    for (int bloc = 0; bloc < 40; ++bloc) {   // 20 480 échantillons : l'anneau (16 384) fait plus d'un tour
        for (size_t i = 0; i < 512; ++i) {
            l[i] = static_cast<float>(bloc * 512 + static_cast<int>(i));
            r[i] = static_cast<float>(bloc * 512 + static_cast<int>(i));
        }
        prise.write(l.data(), r.data(), 512);
    }
    std::vector<float> lu(4096);
    VSM_ASSERT_EQ(prise.readLatest(lu.data(), 4096), size_t(4096));
    for (size_t i = 0; i < 4096; ++i) VSM_ASSERT_EQ(lu[i], static_cast<float>(20480 - 4096 + i));

    SpectrumTap jeune;
    jeune.setEnabled(true);
    jeune.write(l.data(), r.data(), 100);
    VSM_ASSERT_EQ(jeune.readLatest(lu.data(), 4096), size_t(100));
    for (size_t i = 0; i < 4096 - 100; ++i) VSM_ASSERT_EQ(lu[i], 0.0f);
    VSM_ASSERT_EQ(lu[4095], l[99]);
}
