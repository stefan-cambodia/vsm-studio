#pragma once
#include "vsm/audio/dsp/AnalogDrift.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/dsp/Oscillator.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::obx {

/// Polysynthé « brass » américain à filtre DEUX PÔLES.
///
/// CE QUI LE DISTINGUE DU RESTE DU PARC : six machines du projet partagent
/// déjà un filtre en échelle à quatre pôles (24 dB/oct). Celui-ci coupe à
/// 12 dB/oct, et cela s'entend immédiatement -- la pente douce laisse passer
/// les harmoniques hautes, d'où ce son de cuivres large et présent qui ne
/// « ferme » jamais complètement, là où un ladder étouffe. C'est la raison
/// d'être de cette machine : une famille de filtre, pas un nom de plus.
///
/// L'UNISSON n'est pas un détail décoratif : sur ces instruments, empiler
/// toutes les voix sur une note avec un léger désaccord EST le son de
/// référence. Il est donc traité comme un mode de jeu à part entière.
///
/// Approximations assumées (§ 27) : le filtre est un `StateVariableFilter`
/// (topologie TPT) et non le circuit SEM d'origine -- même pente, même
/// comportement de résonance, électronique différente ; aucune mesure sur
/// matériel réel n'a été faite.
class ObxVoice {
public:
    void prepare(double sampleRate, uint64_t seed);

    bool isActive() const { return ampEnv_.isActive(); }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel; note_ = note; velocity_ = velocity;
        ampEnv_.noteOn(); filterEnv_.noteOn();
        baseHz_ = 440.0f * std::exp2f((static_cast<float>(note_) - 69.0f) / 12.0f);
    }
    void noteOff(uint8_t) { ampEnv_.noteOff(); filterEnv_.noteOff(); }

    void setSettings(const vsm::audio::dsp::AdsrSettings& amp,
                      const vsm::audio::dsp::AdsrSettings& filter) {
        ampEnv_.setSettings(amp); filterEnv_.setSettings(filter);
    }
    void setDriftAmount(float amount) { drift1_.setAmount(amount); drift2_.setAmount(amount); }
    /// Décalage d'unisson propre à la voix, en demi-tons (0 hors unisson).
    void setUnisonOffset(float semitones) { unisonOffset_ = semitones; }

    struct Params {
        float osc1Level = 0.8f, osc2Level = 0.5f;
        int osc1Shape = 0, osc2Shape = 0;
        float osc1PulseWidth = 0.5f, osc2PulseWidth = 0.5f;
        float osc2Detune = 0.1f;
        bool sync = false;
        float cutoff = 2000.0f, resonance = 0.3f, envAmount = 0.5f, keyTrack = 0.3f;
        bool fourPole = false;
        float lfoToPitch = 0.0f, lfoToPulseWidth = 0.0f, lfoToFilter = 0.0f;
        float velocityToFilter = 0.3f;
        // Molette de hauteur, en demi-tons (les sommes des oscillateurs sont
        // en demi-tons). À zéro l'addition est exacte : empreinte inchangée.
        float bendSemitones = 0.0f;
        // Molette de MODULATION (CC 1) mise à l'échelle : demi-tons de
        // vibrato ajoutés au LFO, une demi-note à fond. Additif, exact à 0.
        float wheelVibratoSemis = 0.0f;
    };

    float render(const Params& p, float lfo);

private:
    double sampleRate_ = 48000.0;
    vsm::audio::dsp::BandLimitedOscillator osc1_, osc2_;
    /// DEUX filtres distincts, et c'est nécessaire : cascader 12 dB/oct en
    /// 24 dB/oct demande deux étages avec leur PROPRE état. Réutiliser la même
    /// instance deux fois par échantillon ferait avancer son état deux fois
    /// par échantillon -- ce qui revient à la faire tourner à double
    /// fréquence, et décale la coupure au lieu de raidir la pente.
    vsm::audio::dsp::StateVariableFilter filter_, filter2_;
    vsm::audio::dsp::AdsrEnvelope ampEnv_, filterEnv_;
    vsm::audio::dsp::AnalogDrift drift1_, drift2_;
    float syncPhase_ = 0.0f;
    float baseHz_ = 261.6f;
    float unisonOffset_ = 0.0f;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class ObxSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kOsc1Level = 1, kOsc1Shape, kOsc1PulseWidth,
        kOsc2Level, kOsc2Shape, kOsc2PulseWidth, kOsc2Detune, kSync,
        kFilterCutoff, kFilterResonance, kFilterEnvAmount, kFilterKeyTrack, kFilterSlope,
        kFilterAttack, kFilterDecay, kFilterSustain, kFilterRelease,
        kAmpAttack, kAmpDecay, kAmpSustain, kAmpRelease,
        kLfoRate, kLfoWaveform, kLfoToPitch, kLfoToPulseWidth, kLfoToFilter,
        kUnison, kUnisonDetune, kVelocityToFilter, kAnalogCharacter,
    };

    ObxSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "OB-style Polysynth"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);
    void updateUnisonOffsets(float detune, bool unison);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kAnalogCharacter + 1> params_{};
    vsm::audio::engine::VoiceManager<ObxVoice, kMaxVoices> voiceManager_;
    double lfoPhase_ = 0.0;
    vsm::util::DeterministicRng lfoRng_{0x4F425800ULL};
    float lfoRandom_ = 0.0f;
    // Molettes de hauteur (demi-tons) et de modulation (CC 1, 0..1), même
    // contrat que params_.
    std::atomic<float> bendSemitones_{0.0f};
    std::atomic<float> modWheel_{0.0f};
    // Vibrato de la molette de modulation à fond : une demi-note.
    static constexpr float kWheelVibratoSemitones = 0.5f;
};

} // namespace vsm::plugins::obx
