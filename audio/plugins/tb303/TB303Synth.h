#pragma once
#include "vsm/audio/dsp/AnalogDrift.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/LadderFilterZDF.h"
#include "vsm/audio/dsp/Oscillator.h"
#include "vsm/audio/dsp/ParameterSmoother.h"
#include "vsm/audio/engine/MonoVoiceAllocator.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::tb303 {

enum ParamIds : vsm::audio::plugin::ParamId {
    kWaveform = 0,             // 0=Saw, 1=Square -- SEULES les deux formes du hardware réel (section 10)
    kCutoff,
    kResonance,                // 0..1, mis à l'échelle en interne vers la plage du LadderFilterZDF
    kEnvMod,
    kDecay,                    // decay de l'enveloppe de FILTRE (le seul "Decay" du panneau réel)
    kAccent,                   // intensité globale de l'accent (knob "Accent" du panneau réel)
    kAccentVelocityThreshold,  // conversion vélocité MIDI -> accent (section 10, exigé explicitement)
    kGlideTime,                // temps de glissando en cas de SLIDE (notes MIDI qui se chevauchent)
    kAnalogCharacter,
    kNumParams
};

/// Synthé TB-303-style : oscillateur unique (saw/square) -> LadderFilterZDF
/// EN MODE 3 PÔLES (18 dB/oct, PAS 24 -- voir LadderFilterZDF::setPoleCount,
/// c'est le point de fidélité le plus important de cette machine, le vrai
/// TB-303 n'a pas un filtre à 4 pôles comme le Moog) -> enveloppe
/// d'amplitude. Monophonique, priorité à la dernière note
/// (MonoVoiceAllocator, réutilisé tel quel depuis le Minimoog-style).
///
/// SLIDE : legatoMode_ est TOUJOURS actif sur l'allocateur de voix -- deux
/// notes MIDI qui se chevauchent sont donc interprétées comme un slide
/// (glissando de pitch, PAS de retrigger d'enveloppe), exactement le
/// comportement attendu d'un pattern 303 avec le flag "slide" activé sur un
/// pas. Une note qui arrive APRÈS que la précédente soit relâchée (pas de
/// chevauchement) redéclenche normalement -- c'est la convention la plus
/// répandue dans les émulations logicielles pilotées par MIDI externe (le
/// hardware original tire ce flag de son séquenceur interne, pas d'un
/// signal MIDI -- voir section 27, pas de prétention d'identité absolue).
///
/// ACCENT : converti intelligemment depuis la vélocité MIDI (section 10) --
/// une note dont la vélocité dépasse "Accent Threshold" déclenche un accent
/// dont l'INTENSITÉ croît avec la vélocité (pas un simple booléen tout ou
/// rien), mise à l'échelle par le knob global "Accent". Un accent élevé :
/// ouvre davantage le filtre (Env Mod boosté), augmente le niveau, ET
/// raccourcit le decay -- le caractère "qui claque" typique d'une note
/// accentuée sur le hardware réel. Les facteurs de boost exacts
/// (kAccent*Boost/Shorten ci-dessous) sont des choix raisonnés, pas des
/// valeurs mesurées sur un hardware réel.
class TB303Synth : public vsm::audio::plugin::ISynthPlugin {
public:
    TB303Synth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;

    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override;

    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;

    const char* machineName() const override { return "TB-303-style Acid Synth"; }
    int activeVoiceCount() const override;

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& ev);
    vsm::audio::dsp::Waveform waveformFromParam() const;

    static constexpr float kFilterAttackSeconds = 0.001f;
    static constexpr float kAmpAttackSeconds = 0.001f;
    static constexpr float kAmpDecaySeconds = 0.005f;
    static constexpr float kAmpReleaseSeconds = 0.04f;
    static constexpr float kEnvModRangeOctaves = 5.0f;
    static constexpr float kAccentEnvModBoost = 0.6f;   // +60% d'ouverture de filtre à accent max
    static constexpr float kAccentAmpBoost = 0.5f;      // +50% de niveau à accent max
    static constexpr float kAccentDecayShorten = 0.5f;  // decay jusqu'à 50% plus court à accent max
    static constexpr float kMinDecaySeconds = 0.03f;
    static constexpr float kMaxCutoffDriftOctaves = 0.4f;
    static constexpr float kMaxPitchDriftSemitones = 0.12f;
    static constexpr float kFilterDrive = 1.6f; // caractère "growl" -- pas un paramètre exposé (pas de knob "drive" sur le hardware réel)

    vsm::audio::dsp::BandLimitedOscillator osc_;
    vsm::audio::dsp::LadderFilterZDF filter_;
    vsm::audio::dsp::AdsrEnvelope filterEnv_;
    vsm::audio::dsp::AdsrEnvelope ampEnv_;
    vsm::audio::dsp::ParameterSmoother pitchGlide_;
    vsm::audio::dsp::AnalogDrift pitchDrift_;
    vsm::audio::dsp::AnalogDrift cutoffDrift_;
    vsm::audio::engine::MonoVoiceAllocator voiceAllocator_;

    uint8_t currentVelocity_ = 100;
    float currentEffectiveAccent_ = 0.0f; // calculé au moment du noteOn, utilisé pendant toute la durée de la note

    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
    double sampleRate_ = 48000.0;
};

} // namespace vsm::plugins::tb303
