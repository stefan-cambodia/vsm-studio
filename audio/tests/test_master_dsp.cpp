#include "TestFramework.h"
#include "vsm/audio/dsp/Biquad.h"
#include "vsm/audio/dsp/Dynamics.h"
#include "vsm/audio/dsp/LufsMeter.h"
#include <cmath>
#include <vector>

using namespace vsm::audio::dsp;

namespace {
constexpr double kTwoPiD = 6.28318530717958647692;

float rmsOfSine(Biquad& f, float freq, double sr, int n) {
    double acc = 0.0;
    // Jette le transitoire (premier tiers).
    const int settle = n / 3;
    int counted = 0;
    for (int i = 0; i < n; ++i) {
        const float x = std::sin(static_cast<float>(kTwoPiD * freq * i / sr));
        const float y = f.process(x);
        if (i >= settle) { acc += static_cast<double>(y) * y; ++counted; }
    }
    return static_cast<float>(std::sqrt(acc / std::max(1, counted)));
}
} // namespace

VSM_TEST(biquad_flat_peaking_is_transparent) {
    Biquad f;
    f.setSampleRate(48000.0);
    f.set(Biquad::Type::Peaking, 1000.0f, 1.0f, 0.0f); // 0 dB -> identité
    for (int i = 0; i < 500; ++i) {
        const float x = std::sin(static_cast<float>(kTwoPiD * 300.0 * i / 48000.0));
        VSM_ASSERT_NEAR(f.process(x), x, 1e-5);
    }
}

VSM_TEST(biquad_peaking_boosts_center_frequency) {
    Biquad f;
    f.setSampleRate(48000.0);
    f.set(Biquad::Type::Peaking, 1000.0f, 2.0f, 6.0f); // +6 dB -> ~x1.995
    const float gain = rmsOfSine(f, 1000.0f, 48000.0, 9000);
    // RMS d'une sinusoïde d'amplitude 1 = 0.707 ; gain ~2 -> ~1.41.
    VSM_ASSERT_NEAR(gain, 0.7071f * 1.995f, 0.15f);
}

VSM_TEST(biquad_lowshelf_boosts_dc) {
    Biquad f;
    f.setSampleRate(48000.0);
    f.set(Biquad::Type::LowShelf, 200.0f, 0.707f, 6.0f); // +6 dB en bas -> DC x~2
    float y = 0.0f;
    for (int i = 0; i < 5000; ++i) y = f.process(1.0f);
    VSM_ASSERT_NEAR(y, 1.995f, 0.05f);
}

VSM_TEST(biquad_highpass_blocks_dc) {
    Biquad f;
    f.setSampleRate(48000.0);
    f.set(Biquad::Type::HighPass, 100.0f, 0.707f, 0.0f);
    float y = 0.0f;
    for (int i = 0; i < 8000; ++i) y = f.process(1.0f);
    VSM_ASSERT_NEAR(y, 0.0f, 1e-3); // DC entièrement bloqué à l'équilibre
}

VSM_TEST(compressor_below_threshold_is_transparent) {
    Compressor c;
    c.setSampleRate(48000.0);
    c.setThresholdDb(0.0f);   // rien ne dépasse 0 dBFS ici
    c.setRatio(4.0f);
    c.setMakeupDb(0.0f);
    for (int i = 0; i < 2000; ++i) {
        float l = 0.03f * std::sin(static_cast<float>(kTwoPiD * 440.0 * i / 48000.0));
        float r = l;
        const float ref = l;
        c.processStereo(l, r);
        VSM_ASSERT_NEAR(l, ref, 1e-4);
    }
}

VSM_TEST(compressor_reduces_level_above_threshold) {
    Compressor c;
    c.setSampleRate(48000.0);
    c.setThresholdDb(-20.0f);
    c.setRatio(4.0f);
    c.setAttackMs(1.0f);
    c.setReleaseMs(50.0f);
    c.setMakeupDb(0.0f);

    double inSq = 0.0, outSq = 0.0;
    const int n = 8000;
    for (int i = 0; i < n; ++i) {
        float l = 0.9f * std::sin(static_cast<float>(kTwoPiD * 220.0 * i / 48000.0));
        float r = l;
        const float in = l;
        c.processStereo(l, r);
        if (i > n / 3) { inSq += static_cast<double>(in) * in; outSq += static_cast<double>(l) * l; }
    }
    // Un signal ~-1 dBFS bien au-dessus du seuil doit ressortir plus bas.
    VSM_ASSERT(outSq < inSq * 0.7);
}

VSM_TEST(limiter_guarantees_ceiling) {
    Limiter lim;
    lim.setSampleRate(48000.0);
    lim.setCeiling(0.5f);
    lim.setReleaseMs(50.0f);

    float maxOut = 0.0f;
    for (int i = 0; i < 20000; ++i) {
        // Amplitude 2.0 : très au-dessus du plafond, plus des transitoires.
        float l = 2.0f * std::sin(static_cast<float>(kTwoPiD * 130.0 * i / 48000.0));
        float r = 1.5f * std::sin(static_cast<float>(kTwoPiD * 190.0 * i / 48000.0));
        lim.processStereo(l, r);
        maxOut = std::max(maxOut, std::max(std::abs(l), std::abs(r)));
    }
    VSM_ASSERT(maxOut <= 0.5f + 1e-5f); // plafond STRICTEMENT respecté
}

VSM_TEST(limiter_transparent_below_ceiling) {
    Limiter lim;
    lim.setSampleRate(48000.0);
    lim.setCeiling(0.9f);
    for (int i = 0; i < 2000; ++i) {
        float l = 0.3f * std::sin(static_cast<float>(kTwoPiD * 440.0 * i / 48000.0));
        float r = l;
        const float ref = l;
        lim.processStereo(l, r);
        VSM_ASSERT_NEAR(l, ref, 1e-6);
    }
}

VSM_TEST(lufs_relative_level_matches_amplitude_ratio) {
    auto measure = [](float amp) {
        LufsMeter m;
        m.prepare(48000.0);
        for (int i = 0; i < 48000; ++i) {
            const float s = amp * std::sin(static_cast<float>(kTwoPiD * 1000.0 * i / 48000.0));
            m.processStereo(s, s);
        }
        return m.integratedLufs();
    };
    const double loud = measure(1.0f);
    const double quiet = measure(0.1f); // -20 dB
    VSM_ASSERT_NEAR(loud - quiet, 20.0, 0.5);
}

VSM_TEST(lufs_silence_returns_floor) {
    LufsMeter m;
    m.prepare(48000.0);
    for (int i = 0; i < 4800; ++i) m.processStereo(0.0f, 0.0f);
    VSM_ASSERT(m.integratedLufs() <= LufsMeter::kSilence + 1e-6);
}
