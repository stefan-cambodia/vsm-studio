#include "TestFramework.h"
#include "vsm/audio/dsp/Chorus.h"
#include <cmath>
#include <vector>

using vsm::audio::dsp::Chorus;

namespace {
constexpr double kTwoPi = 6.28318530717958647692;
} // namespace

VSM_TEST(chorus_dry_when_mix_zero) {
    Chorus chorus;
    chorus.setSampleRate(48000.0);
    chorus.setMix(0.0f);

    for (int i = 0; i < 2000; ++i) {
        const float in = std::sin(static_cast<float>(kTwoPi * 220.0 * i / 48000.0));
        float l = 0.0f, r = 0.0f;
        chorus.process(in, l, r);
        // mix=0 -> les deux canaux reproduisent exactement l'entrée sèche.
        VSM_ASSERT_NEAR(l, in, 1e-6);
        VSM_ASSERT_NEAR(r, in, 1e-6);
    }
}

VSM_TEST(chorus_produces_stereo_width) {
    Chorus chorus;
    chorus.setSampleRate(48000.0);
    chorus.setRateHz(0.7f);
    chorus.setDepthMs(3.0f);
    chorus.setBaseDelayMs(8.0f);
    chorus.setMix(0.6f);

    bool anyStereoDifference = false;
    for (int i = 0; i < 8000; ++i) {
        const float in = std::sin(static_cast<float>(kTwoPi * 330.0 * i / 48000.0));
        float l = 0.0f, r = 0.0f;
        chorus.process(in, l, r);
        VSM_ASSERT(std::isfinite(l) && std::isfinite(r));
        if (std::abs(l - r) > 0.01f) anyStereoDifference = true;
    }
    // Les deux LFO en quadrature doivent créer une différence L/R mesurable.
    VSM_ASSERT(anyStereoDifference);
}

VSM_TEST(chorus_output_stays_bounded) {
    Chorus chorus;
    chorus.setSampleRate(44100.0);
    chorus.setMix(1.0f);
    chorus.setDepthMs(5.0f);

    float peak = 0.0f;
    for (int i = 0; i < 20000; ++i) {
        float l = 0.0f, r = 0.0f;
        chorus.process(1.0f, l, r); // entrée DC pleine échelle, cas défavorable
        peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
    }
    // Pas de feedback dans un chorus : la sortie ne peut pas diverger.
    VSM_ASSERT(peak <= 1.5f);
}

VSM_TEST(chorus_is_deterministic) {
    auto run = [] {
        Chorus chorus;
        chorus.setSampleRate(48000.0);
        chorus.setRateHz(0.6f);
        chorus.setDepthMs(3.0f);
        chorus.setMix(0.5f);
        std::vector<float> out;
        out.reserve(4000);
        for (int i = 0; i < 4000; ++i) {
            const float in = std::sin(static_cast<float>(kTwoPi * 200.0 * i / 48000.0));
            float l = 0.0f, r = 0.0f;
            chorus.process(in, l, r);
            out.push_back(l);
        }
        return out;
    };

    auto a = run();
    auto b = run();
    for (size_t i = 0; i < a.size(); ++i)
        VSM_ASSERT_NEAR(a[i], b[i], 1e-9);
}
