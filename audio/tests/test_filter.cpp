#include "TestFramework.h"
#include "vsm/util/DeterministicRng.h"
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

VSM_TEST(state_variable_filter_multi_output_matches_the_single_outputs) {
    // Les trois sorties rendues d'un coup doivent être EXACTEMENT celles que
    // rendrait le filtre réglé sur chaque mode -- sinon une machine qui fond
    // d'un type à l'autre n'entendrait pas ce que les autres entendent.
    //
    // La comparaison se fait sur TROIS FILTRES SÉPARÉS, un par mode : les
    // interroger tour à tour sur un seul filtre ferait avancer son état trois
    // fois par échantillon, ce qui reviendrait à le faire tourner à triple
    // fréquence. C'est précisément le piège que `processMulti` supprime.
    vsm::audio::dsp::StateVariableFilter multi, low, band, high;
    for (auto* f : {&multi, &low, &band, &high}) {
        f->setSampleRate(48000.0);
        f->setCutoffHz(1200.0f);
        f->setResonance(2.5f);
    }
    low.setMode(vsm::audio::dsp::StateVariableFilter::Mode::LowPass);
    band.setMode(vsm::audio::dsp::StateVariableFilter::Mode::BandPass);
    high.setMode(vsm::audio::dsp::StateVariableFilter::Mode::HighPass);

    vsm::util::DeterministicRng rng{0x5A5A5A5AULL};
    for (int i = 0; i < 4096; ++i) {
        const float entree = rng.nextBipolar();
        const auto trois = multi.processMulti(entree);
        VSM_ASSERT_NEAR(trois.lowPass, low.process(entree), 1e-6);
        VSM_ASSERT_NEAR(trois.bandPass, band.process(entree), 1e-6);
        VSM_ASSERT_NEAR(trois.highPass, high.process(entree), 1e-6);
    }
}
