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

namespace vsm::plugins::tr808 {

/// Alias vers la brique partagée : le TR-808 conserve le nom `DecayEnv`
/// utilisé dans tout ce fichier, mais l'implémentation est désormais
/// mutualisée (dsp/DecayEnvelope.h).
using DecayEnv = vsm::audio::dsp::DecayEnvelope;

/// Grosse caisse (section 12) : oscillateur sinus + enveloppe de PITCH
/// (balayage descendant rapide qui donne le "boom") + enveloppe d'amplitude,
/// suivi d'un étage non linéaire (tanh) pour le punch.
class KickVoice {
public:
    void setSampleRate(double sr) { sampleRate_ = sr; amp_.setSampleRate(sr); pitch_.setSampleRate(sr); }
    void configure(float tuneHz, float decay, float level) { tuneHz_ = tuneHz; decay_ = decay; level_ = level; }
    bool isActive() const { return amp_.isActive(); }

    void trigger(float velGain) {
        velGain_ = velGain;
        phase_ = 0.0f;
        amp_.setDecaySeconds(decay_);
        pitch_.setDecaySeconds(0.045f); // balayage de pitch bref
        amp_.trigger();
        pitch_.trigger();
    }

    float render() {
        if (!amp_.isActive()) return 0.0f;
        const float pitchEnv = pitch_.next();
        const float freq = tuneHz_ * (1.0f + pitchEnv * 5.0f); // départ ~6x, chute vers tuneHz_
        phase_ += static_cast<float>(vsm::audio::dsp::kTwoPi) * freq / static_cast<float>(sampleRate_);
        if (phase_ > static_cast<float>(vsm::audio::dsp::kTwoPi)) phase_ -= static_cast<float>(vsm::audio::dsp::kTwoPi);
        const float sine = std::sin(phase_);
        const float a = amp_.next();
        return std::tanh(sine * 1.8f) * a * level_ * velGain_;
    }

private:
    double sampleRate_ = 48000.0;
    DecayEnv amp_, pitch_;
    float tuneHz_ = 52.0f, decay_ = 0.45f, level_ = 1.0f, velGain_ = 1.0f, phase_ = 0.0f;
};

/// Caisse claire (section 12) : partie tonale (deux oscillateurs) + générateur
/// de bruit filtré passe-bande, chacun avec sa propre enveloppe.
class SnareVoice {
public:
    void setSampleRate(double sr) {
        sampleRate_ = sr;
        osc1_.setSampleRate(sr); osc1_.setWaveform(vsm::audio::dsp::Waveform::Triangle);
        osc2_.setSampleRate(sr); osc2_.setWaveform(vsm::audio::dsp::Waveform::Triangle);
        noiseBp_.setSampleRate(sr); noiseBp_.setMode(vsm::audio::dsp::StateVariableFilter::Mode::BandPass);
        noiseBp_.setResonance(0.7f);
        tonal_.setSampleRate(sr); noise_.setSampleRate(sr);
    }
    void configure(float tuneHz, float decay, float level, float snappy) {
        tuneHz_ = tuneHz; decay_ = decay; level_ = level; snappy_ = snappy;
    }
    bool isActive() const { return tonal_.isActive() || noise_.isActive(); }

    void trigger(float velGain) {
        velGain_ = velGain;
        osc1_.setFrequency(tuneHz_); osc2_.setFrequency(tuneHz_ * 1.83f);
        osc1_.reset(0.0); osc2_.reset(0.0);
        tonal_.setDecaySeconds(decay_ * 0.5f);
        noise_.setDecaySeconds(decay_);
        noiseBp_.setCutoffHz(3200.0f);
        tonal_.trigger(); noise_.trigger();
    }

    float render() {
        if (!isActive()) return 0.0f;
        const float tone = (osc1_.nextSample() + osc2_.nextSample()) * 0.5f * tonal_.next();
        const float n = noiseBp_.process(rng_.nextBipolar()) * noise_.next();
        return (tone * (1.0f - snappy_) + n * snappy_) * 2.0f * level_ * velGain_;
    }

private:
    double sampleRate_ = 48000.0;
    vsm::audio::dsp::BandLimitedOscillator osc1_, osc2_;
    vsm::audio::dsp::StateVariableFilter noiseBp_;
    DecayEnv tonal_, noise_;
    vsm::util::DeterministicRng rng_{0x5EED000000534E52ULL};
    float tuneHz_ = 180.0f, decay_ = 0.2f, level_ = 1.0f, snappy_ = 0.6f, velGain_ = 1.0f;
};

/// Charleston (section 12) : réseau de 6 oscillateurs carrés inharmoniques
/// (le coeur métallique de la 808) -> passe-haut -> enveloppe. Utilisé pour
/// le charleston fermé (décroissance courte) ET ouvert (longue), avec choke.
class HatVoice {
public:
    void setSampleRate(double sr) {
        sampleRate_ = sr;
        // 6 fréquences inharmoniques fixes (ratios classiques du réseau 808).
        static constexpr float ratios[6] = {1.0f, 1.34f, 1.61f, 1.86f, 2.13f, 2.51f};
        for (size_t i = 0; i < 6; ++i) {
            oscs_[i].setSampleRate(sr);
            oscs_[i].setWaveform(vsm::audio::dsp::Waveform::Square);
            oscs_[i].setFrequency(kBaseHz * ratios[i]);
        }
        hp_.setSampleRate(sr);
        hp_.setMode(vsm::audio::dsp::StateVariableFilter::Mode::HighPass);
        hp_.setResonance(0.7f);
        env_.setSampleRate(sr);
    }
    void configure(float decay, float level, float hpHz) { decay_ = decay; level_ = level; hpHz_ = hpHz; }
    bool isActive() const { return env_.isActive(); }
    void choke() { env_.choke(); }

    void trigger(float velGain) {
        velGain_ = velGain;
        hp_.setCutoffHz(hpHz_);
        env_.setDecaySeconds(decay_);
        env_.trigger();
    }

    float render() {
        if (!env_.isActive()) return 0.0f;
        float sum = 0.0f;
        for (auto& o : oscs_) sum += o.nextSample();
        sum *= (1.0f / 6.0f);
        return hp_.process(sum) * env_.next() * 2.0f * level_ * velGain_;
    }

private:
    static constexpr float kBaseHz = 320.0f;
    double sampleRate_ = 48000.0;
    std::array<vsm::audio::dsp::BandLimitedOscillator, 6> oscs_;
    vsm::audio::dsp::StateVariableFilter hp_;
    DecayEnv env_;
    float decay_ = 0.05f, level_ = 1.0f, hpHz_ = 7000.0f, velGain_ = 1.0f;
};

/// Clap (section 12) : bruit passe-bande avec enveloppe multi-burst (trois
/// ré-attaques rapides qui imitent plusieurs mains, puis une traîne).
class ClapVoice {
public:
    void setSampleRate(double sr) {
        sampleRate_ = sr;
        bp_.setSampleRate(sr); bp_.setMode(vsm::audio::dsp::StateVariableFilter::Mode::BandPass);
        bp_.setResonance(0.8f); bp_.setCutoffHz(1050.0f);
        tail_.setSampleRate(sr);
    }
    void configure(float decay, float level) { decay_ = decay; level_ = level; }
    bool isActive() const { return burstsLeft_ > 0 || tail_.isActive(); }

    void trigger(float velGain) {
        velGain_ = velGain;
        burstsLeft_ = 3;
        burstTimer_ = 0;
        burstGap_ = static_cast<int>(0.009f * static_cast<float>(sampleRate_)); // ~9 ms
        tail_.setDecaySeconds(decay_);
        tail_.trigger();
    }

    float render() {
        if (!isActive()) return 0.0f;
        float burstGain = 0.0f;
        if (burstsLeft_ > 0) {
            burstGain = 1.0f;
            if (++burstTimer_ >= burstGap_) { burstTimer_ = 0; --burstsLeft_; }
        }
        const float env = std::max(burstGain, tail_.next());
        return bp_.process(rng_.nextBipolar()) * env * 2.2f * level_ * velGain_;
    }

private:
    double sampleRate_ = 48000.0;
    vsm::audio::dsp::StateVariableFilter bp_;
    DecayEnv tail_;
    vsm::util::DeterministicRng rng_{0xC1AB0000C1AB0000ULL};
    int burstsLeft_ = 0, burstTimer_ = 0, burstGap_ = 400;
    float decay_ = 0.2f, level_ = 1.0f, velGain_ = 1.0f;
};

/// Cowbell (section 12) : deux oscillateurs carrés -> passe-bande -> enveloppe.
class CowbellVoice {
public:
    void setSampleRate(double sr) {
        sampleRate_ = sr;
        osc1_.setSampleRate(sr); osc1_.setWaveform(vsm::audio::dsp::Waveform::Square);
        osc2_.setSampleRate(sr); osc2_.setWaveform(vsm::audio::dsp::Waveform::Square);
        bp_.setSampleRate(sr); bp_.setMode(vsm::audio::dsp::StateVariableFilter::Mode::BandPass);
        bp_.setResonance(0.6f); bp_.setCutoffHz(2640.0f);
        env_.setSampleRate(sr);
    }
    void configure(float tuneHz, float decay, float level) { tuneHz_ = tuneHz; decay_ = decay; level_ = level; }
    bool isActive() const { return env_.isActive(); }

    void trigger(float velGain) {
        velGain_ = velGain;
        osc1_.setFrequency(tuneHz_); osc2_.setFrequency(tuneHz_ * 1.48f);
        env_.setDecaySeconds(decay_);
        env_.trigger();
    }

    float render() {
        if (!env_.isActive()) return 0.0f;
        const float mix = (osc1_.nextSample() + osc2_.nextSample()) * 0.5f;
        return bp_.process(mix) * env_.next() * 2.0f * level_ * velGain_;
    }

private:
    double sampleRate_ = 48000.0;
    vsm::audio::dsp::BandLimitedOscillator osc1_, osc2_;
    vsm::audio::dsp::StateVariableFilter bp_;
    DecayEnv env_;
    float tuneHz_ = 540.0f, decay_ = 0.4f, level_ = 1.0f, velGain_ = 1.0f;
};

/// Boîte à rythmes TR-808-style. Percussion entièrement SYNTHÉTISÉE (aucun
/// sample, section 12). Chaque numéro de note MIDI déclenche une voix de
/// batterie ; les voix sont indépendantes et sommées ; le charleston fermé
/// coupe (choke) le charleston ouvert. Non polyphonique par note : chaque
/// pièce est mono et se redéclenche.
///
/// Approximations assumées (section 27) : fréquences du réseau de charleston
/// et rapports d'oscillateurs choisis d'après des valeurs d'émulation
/// publiques, pas d'après une mesure du circuit à pont en T d'origine. Aucune
/// mesure comparative avec une 808 matérielle n'a été faite.
class TR808Synth : public vsm::audio::plugin::ISynthPlugin {
public:
    enum ParamIds : vsm::audio::plugin::ParamId {
        kKickLevel = 0, kKickTune, kKickDecay,
        kSnareLevel, kSnareTune, kSnareDecay, kSnareSnappy,
        kClosedHatLevel, kClosedHatDecay,
        kOpenHatLevel, kOpenHatDecay,
        kClapLevel, kClapDecay,
        kCowbellLevel, kCowbellTune,
        kAccent,
        kNumParams
    };

    // Note map (classique 808 / GM batterie).
    enum Note : uint8_t {
        kNoteKick = 36, kNoteClap = 39, kNoteSnare = 38,
        kNoteClosedHat = 42, kNoteOpenHat = 46, kNoteCowbell = 56
    };

    TR808Synth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;

    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override;

    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;

    const char* machineName() const override { return "TR-808-style Drum Machine"; }
    int activeVoiceCount() const override;

private:
    void triggerNote(uint8_t note, uint8_t velocity);
    void applyConfig();

    KickVoice kick_;
    SnareVoice snare_;
    HatVoice closedHat_, openHat_;
    ClapVoice clap_;
    CowbellVoice cowbell_;

    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
    double sampleRate_ = 48000.0;
};

} // namespace vsm::plugins::tr808
