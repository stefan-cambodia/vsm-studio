#pragma once
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/dsp/Oscillator.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::testtone {

enum ParamIds : vsm::audio::plugin::ParamId {
    kWaveform = 0, // 0=Sine,1=Saw,2=Square,3=Triangle (discret, encodé en float)
    kFilterCutoff,
    kFilterResonance,
    kAttack,
    kDecay,
    kSustain,
    kRelease,
    kNumParams
};

/// Une voix : oscillateur -> filtre -> enveloppe d'amplitude. Utilisée par
/// VoiceManager (générique, réutilisable par toute future machine
/// polyphonique).
class Voice {
public:
    bool isActive() const { return envelope_.isActive(); }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void prepare(double sampleRate) {
        osc_.setSampleRate(sampleRate);
        filter_.setSampleRate(sampleRate);
        envelope_.setSampleRate(sampleRate);
    }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel;
        note_ = note;
        velocity_ = velocity;
        float hz = 440.0f * std::exp2f((static_cast<float>(note) - 69.0f) / 12.0f);
        osc_.setFrequency(hz);
        envelope_.noteOn();
    }
    void noteOff(uint8_t /*velocity*/) { envelope_.noteOff(); }

    void setAdsr(const vsm::audio::dsp::AdsrSettings& s) { envelope_.setSettings(s); }

    float nextSample(vsm::audio::dsp::Waveform waveform, float cutoffHz, float resonance) {
        osc_.setWaveform(waveform);
        // NB Phase 2 : recalcule les coefficients du filtre à CHAQUE
        // échantillon (setCutoffHz/setResonance le font en interne), même
        // quand la valeur n'a pas changé -- correct mais gourmand ; un
        // dirty-check est un raffinement CPU de la Phase 6 (section 23).
        filter_.setCutoffHz(cutoffHz);
        filter_.setResonance(resonance);

        float env = envelope_.nextSample();
        float raw = osc_.nextSample();
        float filtered = filter_.process(raw);
        return filtered * env * (static_cast<float>(velocity_) / 127.0f);
    }

private:
    vsm::audio::dsp::BandLimitedOscillator osc_;
    vsm::audio::dsp::StateVariableFilter filter_;
    vsm::audio::dsp::AdsrEnvelope envelope_;
    uint8_t channel_ = 0, note_ = 60, velocity_ = 100;
};

/// Synthé de référence minimal. PAS une émulation vintage (celles-ci
/// arrivent en Phase 3 avec le Minimoog/TB-303/Juno-106-style). Son unique
/// rôle : valider de bout en bout l'architecture ISynthPlugin /
/// PluginRegistry / VoiceManager / ProcessGraph avant d'investir dans la
/// modélisation fine d'une machine précise.
class TestToneSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    TestToneSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;

    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override;

    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;

    const char* machineName() const override { return "Test Tone (reference Phase 2)"; }
    int activeVoiceCount() const override;

private:
    static constexpr size_t kMaxVoices = 16;
    vsm::audio::engine::VoiceManager<Voice, kMaxVoices> voices_;

    // Paramètres thread-safe : std::atomic<float> par paramètre, jamais de
    // lock -- setParameter()/getParameter() peuvent être appelés depuis le
    // thread UI (automation, knobs) ET lus depuis le thread audio (section 13).
    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;

    double sampleRate_ = 48000.0;
    vsm::audio::dsp::Waveform waveformFromParam() const;
};

} // namespace vsm::plugins::testtone
