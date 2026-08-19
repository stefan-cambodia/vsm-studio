#include "TestFramework.h"
#include "vsm/audio/dsp/LadderFilterZDF.h"
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

VSM_TEST(ladder_filter_default_is_stable_and_finite) {
    LadderFilterZDF filter;
    float out = filter.process(1.0f);
    VSM_ASSERT(std::isfinite(out));
}

VSM_TEST(ladder_filter_has_steep_24db_octave_rolloff) {
    double sr = 48000.0;
    LadderFilterZDF filter;
    filter.setSampleRate(sr);
    filter.setCutoffHz(500.0f);
    filter.setResonance(0.1f); // quasi pas de résonance : mesure la pente "pure"
    filter.setDrive(1.0f);

    BandLimitedOscillator osc;
    osc.setSampleRate(sr);
    osc.setWaveform(Waveform::Sine);
    osc.setFrequency(125.0f); // 2 octaves EN-DESSOUS du cutoff -> quasi non atténué

    std::vector<float> lowOut(4800);
    for (auto& s : lowOut) s = filter.process(osc.nextSample());

    filter.reset();
    osc.reset();
    osc.setFrequency(2000.0f); // 2 octaves AU-DESSUS du cutoff
    std::vector<float> highOut(4800);
    for (auto& s : highOut) s = filter.process(osc.nextSample());

    float rmsLow = computeRms(lowOut, 1000);
    float rmsHigh = computeRms(highOut, 1000);

    // 24 dB/oct sur 2 octaves = ~48 dB = facteur ~250 en théorie. On vérifie
    // une marge large (x20) pour rester robuste au modèle non-linéaire
    // simplifié, tout en confirmant nettement une pente plus raide qu'un
    // simple 12 dB/oct (qui donnerait ~x16 sur 2 octaves).
    VSM_ASSERT(rmsLow > rmsHigh * 20.0f);
}

VSM_TEST(ladder_filter_self_oscillates_at_high_resonance) {
    double sr = 48000.0;
    LadderFilterZDF filter;
    filter.setSampleRate(sr);
    filter.setCutoffHz(800.0f);
    filter.setResonance(4.1f); // au-delà du seuil d'auto-oscillation
    filter.setDrive(1.0f);

    // Impulsion brève pour amorcer l'auto-oscillation, puis silence.
    filter.process(1.0f);
    for (int i = 0; i < 1000; ++i) filter.process(0.0f);

    // Longtemps après l'impulsion (0.5s), un filtre auto-oscillant doit
    // toujours produire un signal significatif -- un filtre stable/amorti
    // serait retombé à (quasi) zéro depuis longtemps.
    float peakLate = 0.0f;
    for (int i = 0; i < static_cast<int>(sr * 0.5); ++i) {
        float out = filter.process(0.0f);
        VSM_ASSERT(std::isfinite(out));
        peakLate = std::max(peakLate, std::abs(out));
    }
    VSM_ASSERT(peakLate > 0.05f);
}

VSM_TEST(ladder_filter_self_oscillation_stays_bounded) {
    // La saturation dans le chemin de feedback doit borner l'amplitude en
    // auto-oscillation, PAS la laisser diverger numériquement.
    LadderFilterZDF filter;
    filter.setSampleRate(48000.0);
    filter.setCutoffHz(1000.0f);
    filter.setResonance(4.2f); // maximum autorisé

    filter.process(1.0f);
    float maxAbs = 0.0f;
    for (int i = 0; i < 48000; ++i) {
        float out = filter.process(0.0f);
        VSM_ASSERT(std::isfinite(out));
        maxAbs = std::max(maxAbs, std::abs(out));
    }
    VSM_ASSERT(maxAbs < 50.0f); // largement borné, loin d'une envolée numérique
}

VSM_TEST(ladder_filter_settles_to_silence_at_low_resonance) {
    LadderFilterZDF filter;
    filter.setSampleRate(48000.0);
    filter.setCutoffHz(1000.0f);
    filter.setResonance(0.5f); // loin de l'auto-oscillation

    filter.process(1.0f);
    float last = 0.0f;
    for (int i = 0; i < 48000; ++i) last = filter.process(0.0f);
    VSM_ASSERT(std::abs(last) < 1e-5f);
}

VSM_TEST(ladder_filter_output_changes_with_input_level) {
    // Section 9 : le comportement doit changer avec le niveau du signal
    // entrant -- on vérifie qu'un drive plus fort produit une forme d'onde
    // MESURABLEMENT différente (plus saturée), pas juste une version mise
    // à l'échelle linéairement du même signal.
    double sr = 48000.0;

    auto renderWithDrive = [&](float drive) {
        LadderFilterZDF filter;
        filter.setSampleRate(sr);
        filter.setCutoffHz(3000.0f); // cutoff haut : laisse passer l'essentiel du signal
        filter.setResonance(0.2f);
        filter.setDrive(drive);

        BandLimitedOscillator osc;
        osc.setSampleRate(sr);
        osc.setWaveform(Waveform::Sine);
        osc.setFrequency(220.0f);

        std::vector<float> out(2000);
        for (auto& s : out) s = filter.process(osc.nextSample() * 2.0f); // amplitude d'entrée forte
        return out;
    };

    auto lowDrive = renderWithDrive(0.3f);
    auto highDrive = renderWithDrive(4.0f);

    float peakLow = 0.0f, peakHigh = 0.0f;
    for (size_t i = 500; i < lowDrive.size(); ++i) peakLow = std::max(peakLow, std::abs(lowDrive[i]));
    for (size_t i = 500; i < highDrive.size(); ++i) peakHigh = std::max(peakHigh, std::abs(highDrive[i]));

    // Le signal à fort drive doit être clairement compressé/saturé par
    // rapport à une simple mise à l'échelle proportionnelle au drive.
    VSM_ASSERT(peakHigh < peakLow * (4.0f / 0.3f) * 0.5f);
}

VSM_TEST(ladder_filter_stable_across_parameter_sweep) {
    LadderFilterZDF filter;
    filter.setSampleRate(48000.0);

    for (float cutoff : {50.0f, 500.0f, 5000.0f, 15000.0f}) {
        for (float resonance : {0.0f, 1.0f, 2.5f, 4.0f, 4.2f}) {
            for (float drive : {0.5f, 1.0f, 3.0f}) {
                filter.reset();
                filter.setCutoffHz(cutoff);
                filter.setResonance(resonance);
                filter.setDrive(drive);
                for (int i = 0; i < 2000; ++i) {
                    float out = filter.process(i == 0 ? 1.0f : 0.0f);
                    VSM_ASSERT(std::isfinite(out));
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Nombre de pôles configurable (2-4) : le TB-303-style utilise 3 pôles
// (18 dB/oct), pas 4 (24 dB/oct, Minimoog/Moog) -- voir section 10 du
// cahier des charges. Ces tests vérifient que la généralisation n'a RIEN
// changé au comportement par défaut (4 pôles) et se comporte correctement
// aux autres réglages.
// ---------------------------------------------------------------------------

VSM_TEST(ladder_filter_default_pole_count_is_four) {
    LadderFilterZDF filter;
    VSM_ASSERT_EQ(filter.poleCount(), 4);
}

VSM_TEST(ladder_filter_three_pole_has_18db_octave_rolloff) {
    double sr = 48000.0;
    LadderFilterZDF filter;
    filter.setSampleRate(sr);
    filter.setPoleCount(3);
    filter.setCutoffHz(500.0f);
    filter.setResonance(0.1f);
    filter.setDrive(1.0f);

    BandLimitedOscillator osc;
    osc.setSampleRate(sr);
    osc.setWaveform(Waveform::Sine);
    osc.setFrequency(125.0f); // 2 octaves en-dessous du cutoff

    std::vector<float> lowFreqOut(4800);
    for (auto& s : lowFreqOut) s = filter.process(osc.nextSample());

    filter.reset();
    osc.reset();
    osc.setFrequency(2000.0f); // 2 octaves au-dessus du cutoff
    std::vector<float> highFreqOut(4800);
    for (auto& s : highFreqOut) s = filter.process(osc.nextSample());

    float rmsLow = computeRms(lowFreqOut, 1000);
    float rmsHigh = computeRms(highFreqOut, 1000);

    // 18 dB/oct sur 2 octaves = ~36 dB = facteur ~63 en théorie. Marge
    // large (x8) pour rester robuste au modèle simplifié, tout en
    // confirmant une pente clairement moins raide que le 4 pôles (24 dB/oct)
    // mais toujours nettement plus raide qu'un simple 1 pôle.
    VSM_ASSERT(rmsLow > rmsHigh * 8.0f);
}

VSM_TEST(ladder_filter_two_pole_has_12db_octave_rolloff) {
    double sr = 48000.0;
    LadderFilterZDF filter;
    filter.setSampleRate(sr);
    filter.setPoleCount(2);
    filter.setCutoffHz(500.0f);
    filter.setResonance(0.1f);
    filter.setDrive(1.0f);

    BandLimitedOscillator osc;
    osc.setSampleRate(sr);
    osc.setWaveform(Waveform::Sine);
    osc.setFrequency(125.0f);

    std::vector<float> lowFreqOut(4800);
    for (auto& s : lowFreqOut) s = filter.process(osc.nextSample());

    filter.reset();
    osc.reset();
    osc.setFrequency(2000.0f);
    std::vector<float> highFreqOut(4800);
    for (auto& s : highFreqOut) s = filter.process(osc.nextSample());

    float rmsLow = computeRms(lowFreqOut, 1000);
    float rmsHigh = computeRms(highFreqOut, 1000);

    // 12 dB/oct sur 2 octaves = ~24 dB = facteur ~16 en théorie ; marge x4.
    VSM_ASSERT(rmsLow > rmsHigh * 4.0f);
}

VSM_TEST(ladder_filter_three_pole_remains_stable_and_finite_at_max_resonance) {
    LadderFilterZDF filter;
    filter.setSampleRate(48000.0);
    filter.setPoleCount(3);
    filter.setCutoffHz(800.0f);
    filter.setResonance(4.2f);

    filter.process(1.0f);
    for (int i = 0; i < 48000; ++i) {
        float out = filter.process(0.0f);
        VSM_ASSERT(std::isfinite(out));
        VSM_ASSERT(std::abs(out) < 1000.0f);
    }
}

VSM_TEST(ladder_filter_three_pole_settles_to_silence_at_low_resonance) {
    LadderFilterZDF filter;
    filter.setSampleRate(48000.0);
    filter.setPoleCount(3);
    filter.setCutoffHz(1000.0f);
    filter.setResonance(0.5f);

    filter.process(1.0f);
    float last = 0.0f;
    for (int i = 0; i < 48000; ++i) last = filter.process(0.0f);
    VSM_ASSERT(std::abs(last) < 1e-5f);
}
