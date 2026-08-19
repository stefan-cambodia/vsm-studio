#include "TestFramework.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/dsp/Oscillator.h"
#include <cmath>
#include <vector>

using namespace vsm::audio::dsp;

namespace {
float computeRms(const std::vector<float>& samples, size_t skipSamples) {
    double sumSq = 0.0;
    size_t count = 0;
    for (size_t i = skipSamples; i < samples.size(); ++i) {
        sumSq += static_cast<double>(samples[i]) * samples[i];
        ++count;
    }
    return count ? static_cast<float>(std::sqrt(sumSq / static_cast<double>(count))) : 0.0f;
}
} // namespace

VSM_TEST(filter_default_constructed_has_valid_coefficients) {
    StateVariableFilter filter; // pas de setSampleRate/setCutoff appelé avant
    float out = filter.process(1.0f);
    VSM_ASSERT(std::isfinite(out));
    VSM_ASSERT(out != 0.0f); // si les coefficients étaient nuls (bug), la sortie resterait nulle
}

VSM_TEST(filter_lowpass_attenuates_high_frequencies) {
    double sr = 48000.0;
    StateVariableFilter filter;
    filter.setSampleRate(sr);
    filter.setCutoffHz(500.0f);
    filter.setResonance(0.707f);
    filter.setMode(StateVariableFilter::Mode::LowPass);

    BandLimitedOscillator osc;
    osc.setSampleRate(sr);
    osc.setWaveform(Waveform::Sine);
    osc.setFrequency(200.0f); // bien en-dessous du cutoff

    std::vector<float> lowFreqOut(4800);
    for (auto& s : lowFreqOut) s = filter.process(osc.nextSample());

    filter.reset();
    osc.reset();
    osc.setFrequency(8000.0f); // bien au-dessus du cutoff
    std::vector<float> highFreqOut(4800);
    for (auto& s : highFreqOut) s = filter.process(osc.nextSample());

    float rmsLow = computeRms(lowFreqOut, 1000);   // laisse le filtre "s'installer"
    float rmsHigh = computeRms(highFreqOut, 1000);

    VSM_ASSERT(rmsLow > rmsHigh * 3.0f); // la fréquence basse doit clairement mieux passer
}

VSM_TEST(filter_remains_stable_near_self_oscillation) {
    StateVariableFilter filter;
    filter.setSampleRate(48000.0);
    filter.setCutoffHz(1000.0f);
    filter.setResonance(9.5f); // proche de l'auto-oscillation
    for (int i = 0; i < 48000; ++i) {
        float impulse = (i == 0) ? 1.0f : 0.0f;
        float out = filter.process(impulse);
        VSM_ASSERT(std::isfinite(out));
        VSM_ASSERT(std::abs(out) < 1000.0f); // pas d'envolée numérique
    }
}

VSM_TEST(filter_settles_to_silence_after_impulse) {
    StateVariableFilter filter;
    filter.setSampleRate(48000.0);
    filter.setCutoffHz(1000.0f);
    filter.setResonance(2.0f);
    filter.process(1.0f);

    float last = 0.0f;
    for (int i = 0; i < 48000; ++i) last = filter.process(0.0f);
    VSM_ASSERT(std::abs(last) < 1e-6f);
}
