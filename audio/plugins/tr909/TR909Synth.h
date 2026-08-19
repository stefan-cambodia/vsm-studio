#pragma once
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DecayEnvelope.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/dsp/Oscillator.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::tr909 {

using DecayEnv = vsm::audio::dsp::DecayEnvelope;

/// Grosse caisse 909 : plus "tight" et punchy que la 808. Cœur sinus + forte
/// enveloppe de pitch + un CLICK d'attaque marqué (bruit passe-haut très
/// bref), puis saturation plus franche. C'est ce click + le balayage de pitch
/// plus profond qui donnent le caractère 909 face à la 808.
class Kick909 {
public:
    void setSampleRate(double sr) {
        sampleRate_ = sr; amp_.setSampleRate(sr); pitch_.setSampleRate(sr); click_.setSampleRate(sr);
        clickHp_.setSampleRate(sr); clickHp_.setMode(vsm::audio::dsp::StateVariableFilter::Mode::HighPass);
        clickHp_.setResonance(0.7f); clickHp_.setCutoffHz(1200.0f);
    }
    void configure(float tuneHz, float decay, float attack, float level) {
        tuneHz_ = tuneHz; decay_ = decay; attack_ = attack; level_ = level;
    }
    bool isActive() const { return amp_.isActive() || click_.isActive(); }

    void trigger(float velGain) {
        velGain_ = velGain; phase_ = 0.0f;
        amp_.setDecaySeconds(decay_);
        pitch_.setDecaySeconds(0.03f);   // balayage plus rapide que la 808
        click_.setDecaySeconds(0.006f);  // transitoire d'attaque très court
        amp_.trigger(); pitch_.trigger(); click_.trigger();
    }

    float render() {
        if (!isActive()) return 0.0f;
        const float pe = pitch_.next();
        const float freq = tuneHz_ * (1.0f + pe * 6.5f);
        phase_ += static_cast<float>(vsm::audio::dsp::kTwoPi) * freq / static_cast<float>(sampleRate_);
        if (phase_ > static_cast<float>(vsm::audio::dsp::kTwoPi)) phase_ -= static_cast<float>(vsm::audio::dsp::kTwoPi);
        const float body = std::tanh(std::sin(phase_) * 2.4f) * amp_.next();
        const float click = clickHp_.process(rng_.nextBipolar()) * click_.next() * attack_ * 1.5f;
        return (body + click) * level_ * velGain_;
    }

private:
    double sampleRate_ = 48000.0;
    DecayEnv amp_, pitch_, click_;
    vsm::audio::dsp::StateVariableFilter clickHp_;
    vsm::util::DeterministicRng rng_{0x909C1C0000000001ULL};
    float tuneHz_ = 55.0f, decay_ = 0.32f, attack_ = 0.5f, level_ = 1.0f, velGain_ = 1.0f, phase_ = 0.0f;
};

/// Caisse claire 909 : dominée par le BRUIT (contrairement à la 808 plus
/// tonale). Deux oscillateurs tonaux discrets + un fort composant de bruit
/// passe-bande large, enveloppes séparées, "snappy" par défaut élevé.
class Snare909 {
public:
    void setSampleRate(double sr) {
        sampleRate_ = sr;
        o1_.setSampleRate(sr); o1_.setWaveform(vsm::audio::dsp::Waveform::Triangle);
        o2_.setSampleRate(sr); o2_.setWaveform(vsm::audio::dsp::Waveform::Triangle);
        bp_.setSampleRate(sr); bp_.setMode(vsm::audio::dsp::StateVariableFilter::Mode::BandPass);
        bp_.setResonance(0.5f);
        tonal_.setSampleRate(sr); noise_.setSampleRate(sr);
    }
    void configure(float tuneHz, float decay, float level, float snappy) {
        tuneHz_ = tuneHz; decay_ = decay; level_ = level; snappy_ = snappy;
    }
    bool isActive() const { return tonal_.isActive() || noise_.isActive(); }

    void trigger(float velGain) {
        velGain_ = velGain;
        o1_.setFrequency(tuneHz_); o2_.setFrequency(tuneHz_ * 1.6f);
        o1_.reset(0.0); o2_.reset(0.0);
        tonal_.setDecaySeconds(decay_ * 0.35f);
        noise_.setDecaySeconds(decay_);
        bp_.setCutoffHz(2800.0f);
        tonal_.trigger(); noise_.trigger();
    }

    float render() {
        if (!isActive()) return 0.0f;
        const float tone = (o1_.nextSample() + o2_.nextSample()) * 0.5f * tonal_.next();
        const float n = bp_.process(rng_.nextBipolar()) * noise_.next();
        return (tone * (1.0f - snappy_) + n * snappy_) * 2.1f * level_ * velGain_;
    }

private:
    double sampleRate_ = 48000.0;
    vsm::audio::dsp::BandLimitedOscillator o1_, o2_;
    vsm::audio::dsp::StateVariableFilter bp_;
    DecayEnv tonal_, noise_;
    vsm::util::DeterministicRng rng_{0x909503A5E0000001ULL};
    float tuneHz_ = 190.0f, decay_ = 0.18f, level_ = 1.0f, snappy_ = 0.78f, velGain_ = 1.0f;
};

/// Charleston 909. La 909 d'origine utilisait des ÉCHANTILLONS PCM ; ici on
/// MODÉLISE (section 12/28) : réseau de 6 carrés inharmoniques BRILLANTS +
/// une couche de bruit pour le côté "sizzle" numérique, passe-haut élevé.
/// Approximation documentée : ce n'est pas une reproduction des samples 909.
class Hat909 {
public:
    void setSampleRate(double sr) {
        sampleRate_ = sr;
        static constexpr float ratios[6] = {1.0f, 1.41f, 1.68f, 1.99f, 2.34f, 2.82f};
        for (size_t i = 0; i < 6; ++i) {
            oscs_[i].setSampleRate(sr);
            oscs_[i].setWaveform(vsm::audio::dsp::Waveform::Square);
            oscs_[i].setFrequency(kBaseHz * ratios[i]);
        }
        hp_.setSampleRate(sr); hp_.setMode(vsm::audio::dsp::StateVariableFilter::Mode::HighPass);
        hp_.setResonance(0.7f);
        noiseHp_.setSampleRate(sr); noiseHp_.setMode(vsm::audio::dsp::StateVariableFilter::Mode::HighPass);
        noiseHp_.setResonance(0.7f);
        env_.setSampleRate(sr);
    }
    void configure(float decay, float level, float hpHz) { decay_ = decay; level_ = level; hpHz_ = hpHz; }
    bool isActive() const { return env_.isActive(); }
    void choke() { env_.choke(); }

    void trigger(float velGain) {
        velGain_ = velGain;
        hp_.setCutoffHz(hpHz_); noiseHp_.setCutoffHz(hpHz_ * 1.1f);
        env_.setDecaySeconds(decay_);
        env_.trigger();
    }

    float render() {
        if (!env_.isActive()) return 0.0f;
        float metal = 0.0f;
        for (auto& o : oscs_) metal += o.nextSample();
        metal *= (1.0f / 6.0f);
        const float sizzle = noiseHp_.process(rng_.nextBipolar());
        const float e = env_.next();
        return (hp_.process(metal) * 0.7f + sizzle * 0.3f) * e * 2.0f * level_ * velGain_;
    }

private:
    static constexpr float kBaseHz = 370.0f; // plus brillant que la 808
    double sampleRate_ = 48000.0;
    std::array<vsm::audio::dsp::BandLimitedOscillator, 6> oscs_;
    vsm::audio::dsp::StateVariableFilter hp_, noiseHp_;
    DecayEnv env_;
    vsm::util::DeterministicRng rng_{0x909A700000000001ULL};
    float decay_ = 0.06f, level_ = 1.0f, hpHz_ = 8500.0f, velGain_ = 1.0f;
};

/// Crash 909 : cymbale brillante à longue traîne. Modélisée (pas de sample) :
/// réseau métallique dense + bruit passe-haut, longue décroissance.
class Crash909 {
public:
    void setSampleRate(double sr) {
        sampleRate_ = sr;
        static constexpr float ratios[8] = {1.0f, 1.35f, 1.66f, 2.02f, 2.49f, 2.98f, 3.62f, 4.21f};
        for (size_t i = 0; i < 8; ++i) {
            oscs_[i].setSampleRate(sr);
            oscs_[i].setWaveform(vsm::audio::dsp::Waveform::Square);
            oscs_[i].setFrequency(520.0f * ratios[i]);
        }
        hp_.setSampleRate(sr); hp_.setMode(vsm::audio::dsp::StateVariableFilter::Mode::HighPass);
        hp_.setResonance(0.6f); hp_.setCutoffHz(5000.0f);
        env_.setSampleRate(sr);
    }
    void configure(float decay, float level) { decay_ = decay; level_ = level; }
    bool isActive() const { return env_.isActive(); }

    void trigger(float velGain) { velGain_ = velGain; env_.setDecaySeconds(decay_); env_.trigger(); }

    float render() {
        if (!env_.isActive()) return 0.0f;
        float metal = 0.0f;
        for (auto& o : oscs_) metal += o.nextSample();
        metal *= (1.0f / 8.0f);
        const float mix = metal * 0.5f + rng_.nextBipolar() * 0.5f;
        return hp_.process(mix) * env_.next() * 1.8f * level_ * velGain_;
    }

private:
    double sampleRate_ = 48000.0;
    std::array<vsm::audio::dsp::BandLimitedOscillator, 8> oscs_;
    vsm::audio::dsp::StateVariableFilter hp_;
    DecayEnv env_;
    vsm::util::DeterministicRng rng_{0xC7A50000C7A50001ULL};
    float decay_ = 1.4f, level_ = 0.7f, velGain_ = 1.0f;
};

/// Clap 909 : bruit passe-bande, enveloppe multi-burst (mêmes principes que
/// la 808, réglages 909).
class Clap909 {
public:
    void setSampleRate(double sr) {
        sampleRate_ = sr;
        bp_.setSampleRate(sr); bp_.setMode(vsm::audio::dsp::StateVariableFilter::Mode::BandPass);
        bp_.setResonance(0.8f); bp_.setCutoffHz(1100.0f);
        tail_.setSampleRate(sr);
    }
    void configure(float decay, float level) { decay_ = decay; level_ = level; }
    bool isActive() const { return burstsLeft_ > 0 || tail_.isActive(); }

    void trigger(float velGain) {
        velGain_ = velGain; burstsLeft_ = 3; burstTimer_ = 0;
        burstGap_ = static_cast<int>(0.008f * static_cast<float>(sampleRate_));
        tail_.setDecaySeconds(decay_); tail_.trigger();
    }

    float render() {
        if (!isActive()) return 0.0f;
        float burst = 0.0f;
        if (burstsLeft_ > 0) { burst = 1.0f; if (++burstTimer_ >= burstGap_) { burstTimer_ = 0; --burstsLeft_; } }
        const float env = std::max(burst, tail_.next());
        return bp_.process(rng_.nextBipolar()) * env * 2.2f * level_ * velGain_;
    }

private:
    double sampleRate_ = 48000.0;
    vsm::audio::dsp::StateVariableFilter bp_;
    DecayEnv tail_;
    vsm::util::DeterministicRng rng_{0x909C1A5000000001ULL};
    int burstsLeft_ = 0, burstTimer_ = 0, burstGap_ = 384;
    float decay_ = 0.2f, level_ = 1.0f, velGain_ = 1.0f;
};

/// Tom 909 : sinus + enveloppe de pitch modérée + enveloppe d'amplitude.
/// Une même classe instanciée en trois accords (grave/médium/aigu).
class Tom909 {
public:
    void setSampleRate(double sr) { sampleRate_ = sr; amp_.setSampleRate(sr); pitch_.setSampleRate(sr); }
    void configure(float tuneHz, float decay, float level) { tuneHz_ = tuneHz; decay_ = decay; level_ = level; }
    bool isActive() const { return amp_.isActive(); }

    void trigger(float velGain) {
        velGain_ = velGain; phase_ = 0.0f;
        amp_.setDecaySeconds(decay_); pitch_.setDecaySeconds(0.08f);
        amp_.trigger(); pitch_.trigger();
    }

    float render() {
        if (!amp_.isActive()) return 0.0f;
        const float freq = tuneHz_ * (1.0f + pitch_.next() * 1.2f);
        phase_ += static_cast<float>(vsm::audio::dsp::kTwoPi) * freq / static_cast<float>(sampleRate_);
        if (phase_ > static_cast<float>(vsm::audio::dsp::kTwoPi)) phase_ -= static_cast<float>(vsm::audio::dsp::kTwoPi);
        return std::sin(phase_) * amp_.next() * 1.4f * level_ * velGain_;
    }

private:
    double sampleRate_ = 48000.0;
    DecayEnv amp_, pitch_;
    float tuneHz_ = 100.0f, decay_ = 0.4f, level_ = 1.0f, velGain_ = 1.0f, phase_ = 0.0f;
};

/// Boîte à rythmes TR-909-style. Percussion entièrement SYNTHÉTISÉE (section
/// 12) : aucun échantillon, y compris pour les cymbales/charlestons que la
/// 909 d'origine restituait par PCM (approximation assumée, section 27/28).
class TR909Synth : public vsm::audio::plugin::ISynthPlugin {
public:
    enum ParamIds : vsm::audio::plugin::ParamId {
        kKickLevel = 0, kKickTune, kKickDecay, kKickAttack,
        kSnareLevel, kSnareTune, kSnareDecay, kSnareSnappy,
        kClosedHatLevel, kClosedHatDecay,
        kOpenHatLevel, kOpenHatDecay,
        kClapLevel, kClapDecay,
        kCrashLevel, kCrashDecay,
        kTomLevel, kTomTune, kTomDecay,
        kAccent,
        kNumParams
    };

    enum Note : uint8_t {
        kNoteKick = 36, kNoteSnare = 38, kNoteClap = 39,
        kNoteClosedHat = 42, kNoteOpenHat = 46, kNoteCrash = 49,
        kNoteLowTom = 45, kNoteMidTom = 47, kNoteHiTom = 50
    };

    TR909Synth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;

    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override;

    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;

    const char* machineName() const override { return "TR-909-style Drum Machine"; }
    int activeVoiceCount() const override;

private:
    void triggerNote(uint8_t note, uint8_t velocity);
    void applyConfig();

    Kick909 kick_;
    Snare909 snare_;
    Hat909 closedHat_, openHat_;
    Clap909 clap_;
    Crash909 crash_;
    Tom909 lowTom_, midTom_, hiTom_;

    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
    double sampleRate_ = 48000.0;
};

} // namespace vsm::plugins::tr909
