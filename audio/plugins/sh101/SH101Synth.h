#pragma once
#include "vsm/audio/dsp/AnalogDrift.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/LadderFilterZDF.h"
#include "vsm/audio/dsp/Oscillator.h"
#include "vsm/audio/dsp/ParameterSmoother.h"
#include "vsm/audio/engine/MonoVoiceAllocator.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::sh101 {

enum ParamIds : vsm::audio::plugin::ParamId {
    kSawLevel = 0,
    kPulseLevel,
    kSubLevel,
    kNoiseLevel,
    kPulseWidth,
    kPwmLfoAmount,
    kSubType,          // 0 = -1 octave, 1 = -2 octaves
    kLfoRate,
    kLfoWaveform,      // 0 tri, 1 square, 2 random (sample & hold)
    kLfoPitchAmount,
    kLfoFilterAmount,
    kFilterCutoff,
    kFilterResonance,
    kFilterEnvAmount,
    kFilterKeyTrack,
    kEnvAttack,
    kEnvDecay,
    kEnvSustain,
    kEnvRelease,
    kVcaMode,          // 0 = enveloppe, 1 = gate
    kGlideTime,
    kAnalogCharacter,
    kNumParams
};

/// Monosynthé SH-101-style. Architecture fidèle (section 7) :
///  - UN SEUL VCO fournissant SIMULTANÉMENT saw + pulse (avec PWM) + un
///    sous-oscillateur carré (-1 ou -2 octaves) + bruit, chacun avec son
///    niveau -- c'est le "mixer" du SH-101 (différent du Minimoog à 3 VCO).
///  - UN LFO (triangle / carré / aléatoire S&H) routable vers le pitch
///    (vibrato), la largeur d'impulsion (PWM) et la coupure du filtre.
///  - UNE enveloppe ADSR partagée : elle pilote toujours le VCF, et le VCA
///    au choix (commutateur ENV / GATE, comme le hardware).
///  - VCF passe-bas 24 dB/oct résonant (LadderFilterZDF 4 pôles).
///  - Monophonique à priorité dernière note + glide (MonoVoiceAllocator),
///    comme le Minimoog/TB-303.
///
/// Traits authentiques : le clavier du SH-101 n'est PAS sensible à la
/// vélocité -> le VCA ne dépend jamais de la vélocité (documenté, testé).
///
/// Approximations (section 27) : VCF réel = OTA IR3109, modélisé par le
/// ladder ZDF déjà présent (même pente/résonance, topologie différente).
/// Le séquenceur/arpégiateur interne du SH-101 n'est pas modélisé ici : le
/// séquencement vient de l'hôte (MIDI), conformément à l'architecture DAW.
/// Aucune mesure comparative avec un SH-101 matériel n'a été faite.
class SH101Synth : public vsm::audio::plugin::ISynthPlugin {
public:
    SH101Synth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;

    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override;

    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;

    const char* machineName() const override { return "SH-101-style Monosynth"; }
    int activeVoiceCount() const override {
        return (env_.isActive() || voiceAllocator_.hasHeldNotes()) ? 1 : 0;
    }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& ev);
    float renderLfo(int waveform);

    static constexpr float kMaxPitchDriftSemitones = 0.06f;
    static constexpr float kMaxCutoffDriftOctaves = 0.15f;
    static constexpr float kFilterEnvRangeOctaves = 6.0f;
    static constexpr float kLfoPitchRangeSemitones = 7.0f;
    static constexpr float kLfoFilterRangeOctaves = 4.0f;

    vsm::audio::dsp::BandLimitedOscillator saw_, pulse_, sub_;
    vsm::audio::dsp::LadderFilterZDF filter_;
    vsm::audio::dsp::AdsrEnvelope env_;
    vsm::audio::dsp::ParameterSmoother pitchGlide_;
    vsm::audio::dsp::AnalogDrift pitchDrift_, cutoffDrift_;
    vsm::util::DeterministicRng noiseRng_{0x5348313031ULL};       // "SH101"
    vsm::util::DeterministicRng lfoRng_{0x5348313031AAULL};
    vsm::audio::engine::MonoVoiceAllocator voiceAllocator_;

    double lfoPhase_ = 0.0;
    double lfoIncrement_ = 0.0;
    float lfoRandom_ = 0.0f;
    float gateGain_ = 0.0f;
    bool gateHeld_ = false;

    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
    double sampleRate_ = 48000.0;
};

} // namespace vsm::plugins::sh101
