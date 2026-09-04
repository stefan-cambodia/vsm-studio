#include "TestFramework.h"
#include "vsm/audio/dsp/SincResampler.h"
#include <cmath>
#include <cstdio>
#include <vector>

// LE BANC 8 DE D12.1 (`docs/CDC-etirement-temporel.md`, § 5) : le noyau
// fenêtré contre l'interpolation linéaire de D2.3, fréquence par fréquence,
// sur le rapport courant 44,1 → 48 kHz — et le sous-échantillonnage 48 → 44,1
// qui, lui, doit couper ce qui replierait.

namespace {

using vsm::audio::dsp::SincResampler;

std::vector<float> sinus(double hz, double sr, double secondes) {
    const auto n = static_cast<size_t>(sr * secondes);
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i) x[i] = static_cast<float>(std::sin(2.0 * M_PI * hz * static_cast<double>(i) / sr));
    return x;
}
std::vector<float> lineaire(const std::vector<float>& source, double ratio, size_t cibles) {
    std::vector<float> sortie(cibles, 0.0f);
    const double dernier = static_cast<double>(source.size() - 1);
    for (size_t i = 0; i < cibles; ++i) {
        const double position = std::min(static_cast<double>(i) * ratio, dernier);
        const size_t bas = static_cast<size_t>(position);
        const size_t haut = std::min(bas + 1, source.size() - 1);
        const float fraction = static_cast<float>(position - static_cast<double>(bas));
        sortie[i] = source[bas] * (1.0f - fraction) + source[haut] * fraction;
    }
    return sortie;
}
/// Erreur rms contre la référence analytique, en évitant les bords (le noyau
/// y voit du silence).
double erreur(const std::vector<float>& y, double hz, double sr, size_t marge) {
    double s = 0.0; size_t n = 0;
    for (size_t i = marge; i + marge < y.size(); ++i) {
        const double ref = std::sin(2.0 * M_PI * hz * static_cast<double>(i) / sr);
        s += (y[i] - ref) * (y[i] - ref); ++n;
    }
    return n ? std::sqrt(s / static_cast<double>(n)) : 0.0;
}

} // namespace

/// BANC 8 : le noyau contre l'interpolation linéaire, fréquence par fréquence,
/// et pour trois longueurs de noyau — c'est le banc qui choisit la longueur.
VSM_TEST(sinc_resampler_beats_linear_interpolation_at_every_frequency) {
    const double sr0 = 44100.0, sr1 = 48000.0, ratio = sr0 / sr1;
    std::printf("    [banc rééchantillonnage] 44,1 -> 48 kHz, erreur rms : linéaire | sinc 32 | sinc 64 | sinc 96\n");
    SincResampler r32, r64, r96;
    r32.prepare(ratio, 32); r64.prepare(ratio, 64); r96.prepare(ratio, 96);
    double pireDefaut = 0.0;
    for (double hz : {100.0, 1000.0, 5000.0, 10000.0, 15000.0, 18000.0, 19000.0, 20000.0}) {
        auto x = sinus(hz, sr0, 1.0);
        const auto cibles = static_cast<size_t>(std::llround(static_cast<double>(x.size()) / ratio));
        const double ea = erreur(lineaire(x, ratio, cibles), hz, sr1, 128);
        const double e32 = erreur(r32.resample(x, cibles), hz, sr1, 128);
        const double e64 = erreur(r64.resample(x, cibles), hz, sr1, 128);
        const double e96 = erreur(r96.resample(x, cibles), hz, sr1, 128);
        std::printf("        %6.0f Hz : %.2e | %.2e | %.2e | %.2e\n", hz, ea, e32, e64, e96);
        VSM_ASSERT(e64 < ea);
        if (hz <= 18000.0) pireDefaut = std::max(pireDefaut, e64);
    }
    std::printf("    [banc rééchantillonnage] pire erreur du noyau par défaut (%d points) jusqu'à 18 kHz : %.2e\n",
                SincResampler::kDefaultTaps, pireDefaut);
    VSM_ASSERT_EQ(SincResampler::kDefaultTaps, 64);
    VSM_ASSERT(pireDefaut < 1e-4);
}

VSM_TEST(sinc_resampler_downsampling_rejects_what_would_alias) {
    // 48 -> 44,1 kHz : un 23 kHz (au-dessus du Nyquist de 44,1) doit
    // disparaître, un 10 kHz doit passer intact.
    const double sr0 = 48000.0, sr1 = 44100.0, ratio = sr0 / sr1;
    std::printf("    [banc rééchantillonnage] 48 -> 44,1 kHz, ce qui replierait (rms restant, 0,707 à l'entrée) : sinc 32 | 64 | 96\n");
    double reste64a24k = 1.0, erreur10k64 = 1.0;
    for (int taps : {32, 64, 96}) {
        SincResampler r;
        r.prepare(ratio, taps);
        std::printf("        %2d points :", taps);
        // Entre le Nyquist de sortie (22,05 kHz) et celui d'entrée (24 kHz) :
        // 24 kHz même est un signal nul (sin(πn)), 26 kHz replie déjà à l'entrée.
        for (double hz : {22500.0, 23000.0, 23500.0}) {
            auto x = sinus(hz, sr0, 1.0);
            const auto cibles = static_cast<size_t>(std::llround(static_cast<double>(x.size()) / ratio));
            auto y = r.resample(x, cibles);
            double e = 0.0;
            for (size_t i = 128; i + 128 < cibles; ++i) e += y[i] * y[i];
            e = std::sqrt(e / static_cast<double>(cibles - 256));
            std::printf(" %5.0f Hz %.2e", hz, e);
            if (taps == 64 && hz == 23000.0) reste64a24k = e;
        }
        auto bas = sinus(10000.0, sr0, 1.0);
        const auto cibles = static_cast<size_t>(std::llround(static_cast<double>(bas.size()) / ratio));
        const double e10 = erreur(r.resample(bas, cibles), 10000.0, sr1, 128);
        std::printf(" ; 10 kHz erreur %.2e\n", e10);
        if (taps == 64) erreur10k64 = e10;
    }
    // Le noyau par défaut : un 23 kHz (qui replierait à 21,1 kHz) atténué
    // d'au moins 20 dB, un 10 kHz intact au millième.
    VSM_ASSERT(reste64a24k < 0.0707);
    VSM_ASSERT(erreur10k64 < 1e-3);
}

VSM_TEST(sinc_resampler_is_identity_at_integer_positions_when_ratio_is_one) {
    SincResampler r;
    r.prepare(1.0);
    auto x = sinus(1234.5, 48000.0, 0.2);
    auto y = r.resample(x, x.size());
    double pire = 0.0;
    for (size_t i = 64; i + 64 < x.size(); ++i) pire = std::max(pire, static_cast<double>(std::abs(y[i] - x[i])));
    std::printf("    [banc rééchantillonnage] rapport 1 : écart max %.2e\n", pire);
    VSM_ASSERT(pire < 1e-6);
}

VSM_TEST(sinc_resampler_is_deterministic_and_silent_outside_the_signal) {
    SincResampler a, b;
    a.prepare(44100.0 / 48000.0); b.prepare(44100.0 / 48000.0);
    auto x = sinus(440.0, 44100.0, 0.5);
    VSM_ASSERT(a.resample(x, 24000) == b.resample(x, 24000));
    VSM_ASSERT_NEAR(a.at(x.data(), static_cast<int64_t>(x.size()), -100.0), 0.0f, 1e-9f);
    VSM_ASSERT_NEAR(a.at(x.data(), static_cast<int64_t>(x.size()), static_cast<double>(x.size()) + 100.0), 0.0f, 1e-9f);
}
