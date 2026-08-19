#include "TestFramework.h"
#include "vsm/audio/dsp/AnalogDrift.h"
#include <cmath>

using namespace vsm::audio::dsp;

VSM_TEST(analog_drift_zero_amount_is_always_zero) {
    AnalogDrift drift;
    drift.setSampleRate(48000.0);
    drift.setSeed(42);
    drift.setAmount(0.0f); // 0% ANALOG CHARACTER -> parfaitement stable

    for (int i = 0; i < 48000; ++i)
        VSM_ASSERT_NEAR(drift.nextValue(), 0.0, 1e-6);
}

VSM_TEST(analog_drift_is_deterministic_for_same_seed) {
    AnalogDrift driftA, driftB;
    for (auto* d : {&driftA, &driftB}) {
        d->setSampleRate(48000.0);
        d->setSeed(1234);
        d->setAmount(0.5f);
    }

    for (int i = 0; i < 10000; ++i)
        VSM_ASSERT_NEAR(driftA.nextValue(), driftB.nextValue(), 1e-9);
}

VSM_TEST(analog_drift_differs_for_different_seeds) {
    AnalogDrift driftA, driftB;
    driftA.setSampleRate(48000.0);
    driftA.setSeed(1);
    driftA.setAmount(0.5f);
    driftB.setSampleRate(48000.0);
    driftB.setSeed(2);
    driftB.setAmount(0.5f);

    bool anyDifference = false;
    for (int i = 0; i < 10000; ++i)
        if (std::abs(driftA.nextValue() - driftB.nextValue()) > 1e-6) anyDifference = true;
    VSM_ASSERT(anyDifference);
}

VSM_TEST(analog_drift_stays_within_reasonable_bounds) {
    AnalogDrift drift;
    drift.setSampleRate(48000.0);
    drift.setSeed(7);
    drift.setAmount(1.0f); // 100% : le plus instable

    for (int i = 0; i < 480000; ++i) { // 10s
        float v = drift.nextValue();
        VSM_ASSERT(std::isfinite(v));
        VSM_ASSERT(v > -1.5f && v < 1.5f); // filtré, ne devrait jamais s'envoler
    }
}

VSM_TEST(analog_drift_amount_scales_typical_magnitude) {
    AnalogDrift low, high;
    low.setSampleRate(48000.0); low.setSeed(99); low.setAmount(0.1f);
    high.setSampleRate(48000.0); high.setSeed(99); high.setAmount(1.0f);

    float peakLow = 0.0f, peakHigh = 0.0f;
    for (int i = 0; i < 96000; ++i) {
        peakLow = std::max(peakLow, std::abs(low.nextValue()));
        peakHigh = std::max(peakHigh, std::abs(high.nextValue()));
    }
    VSM_ASSERT(peakHigh > peakLow); // un ANALOG CHARACTER plus élevé dérive davantage
}

VSM_TEST(analog_drift_changes_slowly_not_sample_to_sample_noise) {
    AnalogDrift drift;
    drift.setSampleRate(48000.0);
    drift.setSeed(5);
    drift.setAmount(1.0f);
    drift.setRateHz(0.3f);

    float prev = drift.nextValue();
    float maxStep = 0.0f;
    for (int i = 0; i < 48000; ++i) {
        float v = drift.nextValue();
        maxStep = std::max(maxStep, std::abs(v - prev));
        prev = v;
    }
    // Une dérive LENTE ne doit jamais sauter significativement d'un
    // échantillon à l'autre (contrairement à du bruit blanc non filtré).
    VSM_ASSERT(maxStep < 0.01f);
}
