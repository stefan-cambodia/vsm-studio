#include "TestFramework.h"
#include "vsm/audio/dsp/Oversampler.h"
#include <cmath>
#include <vector>

using vsm::audio::dsp::Oversampler;

namespace {
constexpr double kTwoPiD = 6.28318530717958647692;

// Magnitude d'un signal à la fréquence normalisée fNorm (fs=1), via un
// unique bin DFT (corrélation sin/cos). Sert à mesurer une raie d'aliasing.
double binMagnitude(const std::vector<float>& x, double fNorm) {
    double re = 0.0, im = 0.0;
    for (size_t n = 0; n < x.size(); ++n) {
        const double ph = kTwoPiD * fNorm * static_cast<double>(n);
        re += x[n] * std::cos(ph);
        im += x[n] * std::sin(ph);
    }
    return std::sqrt(re * re + im * im) / static_cast<double>(x.size());
}
} // namespace

VSM_TEST(oversampler_factor1_applies_function_directly) {
    Oversampler os;
    os.prepare(1, 64);
    std::vector<float> buf = {0.2f, -0.5f, 0.8f, -1.0f};
    os.processBlock(buf.data(), 4, [](float x) { return x * 0.5f; });
    VSM_ASSERT_NEAR(buf[0], 0.1f, 1e-6);
    VSM_ASSERT_NEAR(buf[3], -0.5f, 1e-6);
}

VSM_TEST(oversampler_identity_preserves_signal_level) {
    Oversampler os;
    os.prepare(4, 2048);
    std::vector<float> buf(2048);
    for (int i = 0; i < 2048; ++i)
        buf[static_cast<size_t>(i)] = std::sin(static_cast<float>(kTwoPiD * 0.05 * i)); // basse fréquence, bien en bande

    double inSq = 0.0;
    for (float s : buf) inSq += static_cast<double>(s) * s;

    os.processBlock(buf.data(), 2048, [](float x) { return x; }); // identité

    double outSq = 0.0;
    for (float s : buf) outSq += static_cast<double>(s) * s;

    // Monter puis redescendre avec une fonction identité doit préserver le
    // niveau du signal en bande (à la réponse du FIR près).
    const double ratio = std::sqrt(outSq / inSq);
    VSM_ASSERT_NEAR(ratio, 1.0, 0.1);
}

VSM_TEST(oversampler_reduces_aliasing_on_nonlinearity) {
    // f0 = 0.3*fs ; fn = x^2 crée une raie à 2*f0 = 0.6*fs qui, sans
    // suréchantillonnage, se replie à |1 - 0.6| = 0.4*fs. Avec oversampling,
    // cette composante est filtrée avant décimation -> raie d'alias bien plus
    // faible.
    const double f0 = 0.3;
    const double aliasFreq = 0.4;

    auto runSquare = [&](int factor) {
        Oversampler os;
        os.prepare(factor, 4096);
        std::vector<float> buf(4096);
        for (int i = 0; i < 4096; ++i)
            buf[static_cast<size_t>(i)] = 0.9f * std::sin(static_cast<float>(kTwoPiD * f0 * i));
        os.processBlock(buf.data(), 4096, [](float x) { return x * x; });
        return binMagnitude(buf, aliasFreq);
    };

    const double aliasNaive = runSquare(1);
    const double aliasOversampled = runSquare(4);

    VSM_ASSERT(aliasNaive > 0.01);                        // l'alias existe bien sans OS
    VSM_ASSERT(aliasOversampled < aliasNaive * 0.15);     // fortement réduit avec OS 4x
}

// ---------------------------------------------------------------------------
// Équivalence de l'optimisation polyphase (Phase 6).
//
// L'étage de montée saute les zéros insérés et l'étage de descente ne calcule
// plus les échantillons qu'il jette (voir le commentaire de classe
// d'Oversampler). Ces deux économies se PRÉTENDENT exactes : ce test le
// vérifie contre une implémentation naïve de référence, écrite ici exprès --
// zéro-stuffing explicite, convolution complète partout, aucune astuce. Si un
// jour l'optimisation dérive, c'est ce test qui le dira, pas l'oreille.
// ---------------------------------------------------------------------------
namespace {

/// Réplique exacte de l'ancienne implémentation naïve, coefficients compris.
class NaiveOversamplerReference {
public:
    void prepare(int factor, int maxBlockSize) {
        factor_ = factor;
        maxBlock_ = maxBlockSize;
        const int taps = 16 * factor_ + 1;
        const double cutoff = 0.5 / static_cast<double>(factor_);
        up_.design(taps, cutoff);
        down_.design(taps, cutoff);
        over_.assign(static_cast<size_t>(maxBlock_) * static_cast<size_t>(factor_), 0.0f);
    }

    template <typename Fn>
    void processBlock(float* data, int numSamples, Fn fn) {
        const int count = std::min(numSamples, maxBlock_);
        const int f = factor_;
        for (int i = 0; i < count; ++i)
            for (int k = 0; k < f; ++k) {
                const float in = (k == 0) ? data[i] * static_cast<float>(f) : 0.0f;
                over_[static_cast<size_t>(i * f + k)] = up_.process(in);
            }
        for (int j = 0; j < count * f; ++j)
            over_[static_cast<size_t>(j)] = fn(over_[static_cast<size_t>(j)]);
        for (int i = 0; i < count; ++i) {
            float out = 0.0f;
            for (int k = 0; k < f; ++k)
                out = down_.process(over_[static_cast<size_t>(i * f + k)]);
            data[i] = out;
        }
    }

private:
    int factor_ = 1, maxBlock_ = 1;
    vsm::audio::dsp::FirLowpass up_, down_;
    std::vector<float> over_;
};

} // namespace

VSM_TEST(oversampler_polyphase_matches_naive_reference) {
    for (int factor : {2, 4, 8}) {
        // Signal riche (plusieurs partiels + une composante proche de Nyquist)
        // traversant PLUSIEURS blocs : c'est aux frontières de blocs que l'état
        // du filtre doit rester exact, pas seulement à l'intérieur d'un bloc.
        constexpr int kBlock = 128, kBlocks = 5;
        std::vector<float> input(static_cast<size_t>(kBlock * kBlocks));
        for (size_t n = 0; n < input.size(); ++n) {
            const double t = static_cast<double>(n);
            input[n] = static_cast<float>(0.4 * std::sin(kTwoPiD * 0.031 * t)
                                        + 0.3 * std::sin(kTwoPiD * 0.17 * t)
                                        + 0.2 * std::sin(kTwoPiD * 0.44 * t));
        }
        auto shaper = [](float x) { return std::tanh(2.0f * x); }; // non-linéarité réaliste

        std::vector<float> fast = input, reference = input;
        Oversampler os;
        os.prepare(factor, kBlock);
        NaiveOversamplerReference naive;
        naive.prepare(factor, kBlock);
        for (int b = 0; b < kBlocks; ++b) {
            os.processBlock(fast.data() + b * kBlock, kBlock, shaper);
            naive.processBlock(reference.data() + b * kBlock, kBlock, shaper);
        }

        for (size_t n = 0; n < input.size(); ++n)
            VSM_ASSERT_NEAR(fast[n], reference[n], 1e-6);
    }
}
