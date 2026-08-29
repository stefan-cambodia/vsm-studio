#include "TestFramework.h"
#include "vsm/audio/effect/BitCrusher.h"
#include "vsm/audio/effect/ChorusEffect.h"
#include "vsm/audio/effect/Delay.h"
#include "vsm/audio/effect/Distortion.h"
#include <cmath>
#include <set>
#include <vector>

using namespace vsm::audio::effect;

namespace {
constexpr double kTwoPiD = 6.28318530717958647692;

float peakOf(const std::vector<float>& b) {
    float p = 0.0f;
    for (float s : b) p = std::max(p, std::abs(s));
    return p;
}
double rmsOf(const std::vector<float>& b) {
    double acc = 0.0;
    for (float s : b) acc += static_cast<double>(s) * s;
    return std::sqrt(acc / static_cast<double>(b.size()));
}
} // namespace

// ---------------------------------------------------------------- Delay ---

VSM_TEST(delay_dry_when_mix_zero) {
    Delay fx;
    fx.prepare(48000.0, 512);
    fx.setParameter(Delay::kMix, 0.0f);
    std::vector<float> l(512), r(512);
    for (int i = 0; i < 512; ++i) { l[static_cast<size_t>(i)] = 0.3f * std::sin(static_cast<float>(0.1 * i)); r[static_cast<size_t>(i)] = l[static_cast<size_t>(i)]; }
    std::vector<float> ref = l;
    fx.process(l.data(), r.data(), 512);
    for (int i = 0; i < 512; ++i) VSM_ASSERT_NEAR(l[static_cast<size_t>(i)], ref[static_cast<size_t>(i)], 1e-6);
}

VSM_TEST(delay_produces_delayed_echo) {
    Delay fx;
    const double sr = 48000.0;
    fx.prepare(sr, 8192);
    fx.setParameter(Delay::kMix, 1.0f);       // full wet
    fx.setParameter(Delay::kFeedback, 0.0f);  // un seul écho
    fx.setParameter(Delay::kTimeMs, 100.0f);  // 4800 échantillons
    fx.setParameter(Delay::kToneHz, 18000.0f);

    std::vector<float> l(8192, 0.0f), r(8192, 0.0f);
    l[0] = 1.0f; r[0] = 1.0f; // impulsion
    fx.process(l.data(), r.data(), 8192);

    const int expected = static_cast<int>(0.1 * sr);
    // Silence juste avant, énergie au voisinage de l'écho attendu.
    VSM_ASSERT(std::abs(l[static_cast<size_t>(expected - 50)]) < 1e-4f);
    float nearEcho = 0.0f;
    for (int i = expected - 5; i <= expected + 5; ++i)
        nearEcho = std::max(nearEcho, std::abs(l[static_cast<size_t>(i)]));
    VSM_ASSERT(nearEcho > 0.5f);
}

VSM_TEST(delay_pingpong_moves_energy_across_channels) {
    Delay fx;
    const double sr = 48000.0;
    fx.prepare(sr, 16384);
    fx.setParameter(Delay::kMix, 1.0f);
    fx.setParameter(Delay::kFeedback, 0.7f);
    fx.setParameter(Delay::kTimeMs, 50.0f);
    fx.setParameter(Delay::kPingPong, 1.0f);

    std::vector<float> l(16384, 0.0f), r(16384, 0.0f);
    l[0] = 1.0f; // impulsion sur L uniquement
    fx.process(l.data(), r.data(), 16384);

    // En ping-pong, l'impulsion sur L doit finir par exciter le canal R.
    double energyR = 0.0;
    for (float s : r) energyR += static_cast<double>(s) * s;
    VSM_ASSERT(energyR > 0.01);
}

// ----------------------------------------------------------- Distortion ---

VSM_TEST(distortion_dry_when_mix_zero) {
    Distortion fx;
    fx.prepare(48000.0, 512);
    fx.setParameter(Distortion::kMix, 0.0f);
    fx.setParameter(Distortion::kOutputDb, 0.0f);
    std::vector<float> l(512), r(512);
    for (int i = 0; i < 512; ++i) { l[static_cast<size_t>(i)] = 0.5f * std::sin(static_cast<float>(0.07 * i)); r[static_cast<size_t>(i)] = l[static_cast<size_t>(i)]; }
    std::vector<float> ref = l;
    fx.process(l.data(), r.data(), 512);
    for (int i = 0; i < 512; ++i) VSM_ASSERT_NEAR(l[static_cast<size_t>(i)], ref[static_cast<size_t>(i)], 1e-5);
}

VSM_TEST(distortion_adds_harmonics_and_stays_bounded) {
    Distortion fx;
    fx.prepare(48000.0, 4096);
    fx.setParameter(Distortion::kDrive, 0.9f);
    fx.setParameter(Distortion::kMode, 1.0f); // hard clip
    fx.setParameter(Distortion::kMix, 1.0f);
    fx.setParameter(Distortion::kToneHz, 18000.0f);
    fx.setParameter(Distortion::kOutputDb, 0.0f);

    std::vector<float> l(4096), r(4096), input(4096);
    for (int i = 0; i < 4096; ++i) {
        input[static_cast<size_t>(i)] = 0.5f * std::sin(static_cast<float>(kTwoPiD * 220.0 * i / 48000.0));
        l[static_cast<size_t>(i)] = input[static_cast<size_t>(i)];
        r[static_cast<size_t>(i)] = input[static_cast<size_t>(i)];
    }
    fx.process(l.data(), r.data(), 4096);

    for (float s : l) VSM_ASSERT(std::isfinite(s) && std::abs(s) <= 1.05f); // borné
    // Un drive fort doit transformer le signal (RMS nettement différent).
    VSM_ASSERT(rmsOf(l) > rmsOf(input) * 1.2);
}

// ------------------------------------------------------------ BitCrusher ---

VSM_TEST(bitcrusher_reduces_to_few_levels) {
    BitCrusher fx;
    fx.prepare(48000.0, 2048);
    fx.setParameter(BitCrusher::kBits, 2.0f);       // 4 paliers
    fx.setParameter(BitCrusher::kDownsample, 1.0f);
    fx.setParameter(BitCrusher::kMix, 1.0f);

    std::vector<float> l(2048), r(2048);
    for (int i = 0; i < 2048; ++i) { l[static_cast<size_t>(i)] = std::sin(static_cast<float>(kTwoPiD * 100.0 * i / 48000.0)); r[static_cast<size_t>(i)] = l[static_cast<size_t>(i)]; }
    fx.process(l.data(), r.data(), 2048);

    std::set<int> distinct;
    for (float s : l) distinct.insert(static_cast<int>(std::lround(s * 1000.0f)));
    // 2 bits -> une poignée de niveaux distincts seulement.
    VSM_ASSERT(distinct.size() <= 8);
}

VSM_TEST(bitcrusher_downsample_holds_value) {
    BitCrusher fx;
    fx.prepare(48000.0, 512);
    fx.setParameter(BitCrusher::kBits, 16.0f);      // pas de réduction de bits
    fx.setParameter(BitCrusher::kDownsample, 4.0f); // sample & hold sur 4
    fx.setParameter(BitCrusher::kMix, 1.0f);

    std::vector<float> l(512), r(512);
    for (int i = 0; i < 512; ++i) { l[static_cast<size_t>(i)] = std::sin(static_cast<float>(0.05 * i)); r[static_cast<size_t>(i)] = l[static_cast<size_t>(i)]; }
    fx.process(l.data(), r.data(), 512);

    // Par groupes de 4, la valeur est gelée.
    for (int base = 0; base + 3 < 512; base += 4) {
        VSM_ASSERT_NEAR(l[static_cast<size_t>(base)], l[static_cast<size_t>(base + 1)], 1e-6);
        VSM_ASSERT_NEAR(l[static_cast<size_t>(base)], l[static_cast<size_t>(base + 3)], 1e-6);
    }
}

// --------------------------------------------------------------- Chorus ---

VSM_TEST(chorus_effect_dry_when_mix_zero) {
    ChorusEffect fx;
    fx.prepare(48000.0, 512);
    fx.setParameter(ChorusEffect::kMix, 0.0f);
    std::vector<float> l(512), r(512);
    for (int i = 0; i < 512; ++i) { l[static_cast<size_t>(i)] = 0.4f * std::sin(static_cast<float>(0.09 * i)); r[static_cast<size_t>(i)] = l[static_cast<size_t>(i)]; }
    std::vector<float> ref = l;
    fx.process(l.data(), r.data(), 512);
    for (int i = 0; i < 512; ++i) VSM_ASSERT_NEAR(l[static_cast<size_t>(i)], ref[static_cast<size_t>(i)], 1e-6);
}

VSM_TEST(chorus_effect_creates_stereo_width) {
    ChorusEffect fx;
    fx.prepare(48000.0, 8192);
    fx.setParameter(ChorusEffect::kMix, 1.0f);
    std::vector<float> l(8192), r(8192);
    for (int i = 0; i < 8192; ++i) { l[static_cast<size_t>(i)] = 0.5f * std::sin(static_cast<float>(kTwoPiD * 330.0 * i / 48000.0)); r[static_cast<size_t>(i)] = l[static_cast<size_t>(i)]; }
    fx.process(l.data(), r.data(), 8192);

    bool anyDiff = false;
    for (int i = 0; i < 8192; ++i)
        if (std::abs(l[static_cast<size_t>(i)] - r[static_cast<size_t>(i)]) > 0.01f) anyDiff = true;
    VSM_ASSERT(anyDiff);
    VSM_ASSERT(peakOf(l) < 2.0f); // borné
}

// --- D0.4 : un temps en millisecondes est un temps, pas un nombre d'échantillons
//
// L'application préparait tous ses effets à 48 kHz ÉCRIT EN DUR, quelle que
// soit la fréquence réelle de la carte son. Sur une carte à 44,1 kHz, un delay
// réglé sur 500 ms durait 500 x 48000/44100 = 544 ms, et une réverbération
// changeait de taille de pièce -- silencieusement, puisque rien ne compare un
// réglage à ce qu'on entend. Ce test tient l'autre bout de la chaîne : à
// n'importe quelle fréquence, l'effet PRÉPARÉ À CETTE FRÉQUENCE rend le temps
// qu'on lui demande.
VSM_TEST(a_delay_lasts_the_time_it_is_given_at_every_sample_rate) {
    for (double sampleRate : {44100.0, 48000.0, 96000.0}) {
        vsm::audio::effect::Delay delay;
        delay.prepare(sampleRate, 512);
        delay.setParameter(vsm::audio::effect::Delay::kTimeMs, 100.0f);
        delay.setParameter(vsm::audio::effect::Delay::kFeedback, 0.0f);
        delay.setParameter(vsm::audio::effect::Delay::kMix, 1.0f);   // 100 % traité
        delay.setParameter(vsm::audio::effect::Delay::kPingPong, 0.0f);

        // Une impulsion, puis du silence : on cherche où elle ressort.
        const int total = static_cast<int>(sampleRate * 0.3);
        std::vector<float> left(static_cast<size_t>(total), 0.0f);
        std::vector<float> right(static_cast<size_t>(total), 0.0f);
        left[0] = 1.0f;
        right[0] = 1.0f;
        for (int i = 0; i < total; i += 512) {
            const int n = std::min(512, total - i);
            delay.process(left.data() + i, right.data() + i, n);
        }

        int crete = 0;
        float valeur = 0.0f;
        // On saute les premiers échantillons : le passage direct de l'entrée
        // n'est pas l'écho qu'on mesure.
        for (int i = 16; i < total; ++i)
            if (std::abs(left[static_cast<size_t>(i)]) > valeur) {
                valeur = std::abs(left[static_cast<size_t>(i)]);
                crete = i;
            }

        const double secondes = static_cast<double>(crete) / sampleRate;
        VSM_ASSERT(valeur > 0.05f);                       // l'écho existe
        VSM_ASSERT(std::abs(secondes - 0.100) < 0.005);   // et il tombe à 100 ms
    }
}
