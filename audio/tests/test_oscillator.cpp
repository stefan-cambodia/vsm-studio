#include "TestFramework.h"
#include "vsm/audio/dsp/Oscillator.h"
#include <cmath>
#include <vector>

using namespace vsm::audio::dsp;

namespace {

/// Compte les fronts montants (négatif -> positif) sur un buffer : pour une
/// onde périodique simple (sinus/dent de scie/triangle/carré), ce compte
/// correspond exactement au nombre de cycles complets écoulés, ce qui donne
/// une mesure de fréquence indépendante de toute analyse spectrale.
int countRisingZeroCrossings(const std::vector<float>& samples) {
    int count = 0;
    for (size_t i = 1; i < samples.size(); ++i)
        if (samples[i - 1] < 0.0f && samples[i] >= 0.0f)
            ++count;
    return count;
}

std::vector<float> renderOneSecond(Waveform wf, float freqHz, double sampleRate = 48000.0) {
    BandLimitedOscillator osc;
    osc.setSampleRate(sampleRate);
    osc.setWaveform(wf);
    osc.setFrequency(freqHz);

    std::vector<float> out(static_cast<size_t>(sampleRate));
    for (auto& s : out) s = osc.nextSample();
    return out;
}

} // namespace

VSM_TEST(oscillator_sine_frequency_matches_zero_crossings) {
    auto samples = renderOneSecond(Waveform::Sine, 220.0f);
    int crossings = countRisingZeroCrossings(samples);
    VSM_ASSERT(std::abs(crossings - 220) <= 1);
}

VSM_TEST(oscillator_saw_frequency_matches_zero_crossings) {
    auto samples = renderOneSecond(Waveform::Saw, 110.0f);
    int crossings = countRisingZeroCrossings(samples);
    VSM_ASSERT(std::abs(crossings - 110) <= 1);
}

VSM_TEST(oscillator_square_frequency_matches_zero_crossings) {
    auto samples = renderOneSecond(Waveform::Square, 330.0f);
    int crossings = countRisingZeroCrossings(samples);
    VSM_ASSERT(std::abs(crossings - 330) <= 1);
}

VSM_TEST(oscillator_triangle_frequency_matches_zero_crossings) {
    auto samples = renderOneSecond(Waveform::Triangle, 165.0f);
    int crossings = countRisingZeroCrossings(samples);
    VSM_ASSERT(std::abs(crossings - 165) <= 2); // l'intégrateur "leaky" tolère un peu plus de marge
}

VSM_TEST(oscillator_never_produces_nan_or_inf) {
    for (Waveform wf : {Waveform::Sine, Waveform::Saw, Waveform::Square, Waveform::Triangle}) {
        auto samples = renderOneSecond(wf, 440.0f);
        for (float s : samples) {
            VSM_ASSERT(std::isfinite(s));
        }
    }
}

VSM_TEST(oscillator_amplitude_stays_within_reasonable_bounds) {
    // PolyBLEP peut légèrement dépasser +-1 près des discontinuités (c'est
    // le comportement anti-aliasing attendu) -- on vérifie une marge large
    // plutôt qu'une borne stricte à 1.0.
    for (Waveform wf : {Waveform::Sine, Waveform::Saw, Waveform::Square, Waveform::Triangle}) {
        auto samples = renderOneSecond(wf, 440.0f);
        for (float s : samples) {
            VSM_ASSERT(s > -1.5f && s < 1.5f);
        }
    }
}

VSM_TEST(oscillator_reset_restarts_phase) {
    BandLimitedOscillator osc;
    osc.setSampleRate(48000.0);
    osc.setWaveform(Waveform::Sine);
    osc.setFrequency(100.0f);

    std::vector<float> first(10);
    for (auto& s : first) s = osc.nextSample();

    osc.reset(0.0);
    std::vector<float> second(10);
    for (auto& s : second) s = osc.nextSample();

    for (size_t i = 0; i < first.size(); ++i)
        VSM_ASSERT_NEAR(first[i], second[i], 1e-6);
}
