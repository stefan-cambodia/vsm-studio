#pragma once
#include "vsm/audio/dsp/AnalogDrift.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/LadderFilterZDF.h"
#include "vsm/audio/dsp/Oscillator.h"
#include "vsm/audio/dsp/ParameterSmoother.h"
#include "vsm/audio/engine/MonoVoiceAllocator.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>

namespace vsm::plugins::minimoog {

enum ParamIds : vsm::audio::plugin::ParamId {
    kOsc1Waveform = 0,
    kOsc2Waveform,
    kOsc3Waveform,
    kOsc1Level,
    kOsc2Level,
    kOsc3Level,
    kNoiseLevel,
    kOsc2DetuneSemitones,
    kOsc3DetuneSemitones,
    kFilterCutoff,
    kFilterResonance,
    kFilterDrive,
    kFilterEnvAmount,
    kFilterKeyTrack,
    kFilterAttack,
    kFilterDecay,
    kFilterSustain,
    kAmpAttack,
    kAmpDecay,
    kAmpSustain,
    kGlideTimeSeconds,
    kAnalogCharacter,
    kNumParams
};

/// Monosynth Minimoog-style : 3 oscillateurs + bruit -> mixeur -> filtre
/// ladder 24 dB/oct (LadderFilterZDF, le vrai cœur du "son Moog") ->
/// enveloppe d'amplitude. Architecture fidèle à l'ORIGINALE dans ses
/// grandes lignes (section 7 du cahier des charges) :
///
///  - Monophonique, priorité à la dernière note, retombée sur la note
///    précédente encore tenue (MonoVoiceAllocator) -- le jeu "en trille"
///    caractéristique des synthés mono analogiques.
///  - Enveloppes à 3 étages (Attack/Decay/Sustain) SANS release dédié :
///    comme sur le Model D original, le release réutilise le temps de
///    decay (pas de 4e potentiomètre "Release" sur le panneau réel).
///  - Glide/portamento exponentiel entre notes tenues.
///  - ANALOG CHARACTER (section 8) : dérive lente et déterministe du pitch
///    ET du cutoff, seedée pour une session reproductible.
///
/// Le filtre reste un modèle SIMPLIFIÉ de la topologie ladder réelle (voir
/// LadderFilterZDF) : non-linéarité en 2 points (entrée + feedback) plutôt
/// qu'un modèle par étage complet. Aucune mesure comparative avec un
/// Minimoog matériel réel n'a été faite -- pas de prétention d'identité à
/// 100% (section 27).
class MinimoogSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    MinimoogSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;

    /// Le pitch bend, en demi-tons, ajouté au numéro de note juste avant la
    /// conversion en hertz -- donc AVANT le glide et la dérive analogique, qui
    /// travaillent tous deux dans le même domaine. Le Minimoog avait sa molette
    /// de hauteur à gauche du clavier, et c'est l'un des rares gestes sans
    /// lesquels un solo de cette machine ne ressemble à rien.
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override;

    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override;

    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;

    const char* machineName() const override { return "Minimoog-style Monosynth"; }
    int activeVoiceCount() const override;

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& ev);
    vsm::audio::dsp::Waveform waveformFromParam(vsm::audio::plugin::ParamId id) const;

    static constexpr float kMaxPitchDriftSemitones = 0.15f;
    static constexpr float kMaxCutoffDriftOctaves = 0.5f;
    static constexpr float kFilterEnvRangeOctaves = 6.0f;

    vsm::audio::dsp::BandLimitedOscillator osc1_, osc2_, osc3_;
    vsm::util::DeterministicRng noiseRng_{0x9E3779B97F4A7C15ULL};

    vsm::audio::dsp::LadderFilterZDF filter_;
    vsm::audio::dsp::AdsrEnvelope filterEnv_;
    vsm::audio::dsp::AdsrEnvelope ampEnv_;

    vsm::audio::dsp::ParameterSmoother pitchGlide_; // domaine "numéro de note" (demi-tons)
    vsm::audio::dsp::AnalogDrift pitchDrift_;
    vsm::audio::dsp::AnalogDrift cutoffDrift_;

    std::atomic<float> bendSemitones_{0.0f};

    vsm::audio::engine::MonoVoiceAllocator voiceAllocator_;
    uint8_t currentVelocity_ = 100;

    // Paramètres thread-safe : std::atomic<float>, jamais de lock (section 13).
    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
    double sampleRate_ = 48000.0;
};

} // namespace vsm::plugins::minimoog
