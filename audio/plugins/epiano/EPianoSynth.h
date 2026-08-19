#pragma once
#include "vsm/audio/dsp/AnalogDrift.h"
#include "vsm/audio/dsp/Biquad.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::epiano {

/// Piano électrique à lames — modélisation, pas échantillonnage.
///
/// POURQUOI CETTE MACHINE EN PREMIER (docs/CDC-machines-manquantes.md § 9) :
/// le classificateur du projet d'analyse sort `piano_or_keys` sur quantité de
/// morceaux, et jusqu'ici AUCUNE machine ne pouvait répondre. Un piano
/// acoustique demanderait une bibliothèque d'échantillons ; un piano
/// électrique, non — son timbre naît de trois choses simples et modélisables.
///
/// LE MODÈLE, ET CE QU'IL SIMPLIFIE (§ 27) :
///
///  1. **La lame** vibre sur un mode fondamental et quelques partiels
///     INHARMONIQUES (une lame de métal n'est pas une corde : ses partiels ne
///     tombent pas sur des multiples entiers). C'est cette inharmonicité qui
///     fait qu'on entend « métal » et pas « orgue ».
///  2. **Le marteau** produit un choc large bande très bref, plus audible fort
///     qu'à faible vélocité : c'est le « knock » caractéristique.
///  3. **Le micro** capte la lame de façon asymétrique et sature doucement
///     quand elle vibre fort, d'où la cloche brillante des notes jouées fort
///     qui s'assagit en tenue.
///
/// Simplifications assumées : partiels fixes en nombre (trois) plutôt que
/// modes calculés ; couplage micro/lame réduit à une non-linéarité statique ;
/// aucune mesure faite sur un instrument réel — le statut honnête est
/// « dérivé », jamais « mesuré ».
///
/// `Character` fait glisser d'un timbre de lame frappée (clair, cloche
/// marquée) vers un timbre d'anche (plus creux, plus court) : les deux familles
/// d'instruments partagent le même principe, elles diffèrent surtout par
/// l'inharmonicité et l'amortissement.
class EPianoVoice {
public:
    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate;
        amp_.setSampleRate(sampleRate);
        knockFilter_.setSampleRate(sampleRate);
        rng_ = vsm::util::DeterministicRng(seed);
        drift_.setSampleRate(sampleRate);
        drift_.setSeed(seed ^ 0x9E37ULL);
        drift_.setRateHz(0.07f);
    }

    bool isActive() const { return amp_.isActive(); }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel;
        note_ = note;
        velocity_ = velocity;
        for (auto& phase : partialPhase_) phase = 0.0;
        knockLevel_ = 1.0f;
        amp_.noteOn();
    }

    void noteOff(uint8_t) { amp_.noteOff(); }

    void setSettings(const vsm::audio::dsp::AdsrSettings& settings) { amp_.setSettings(settings); }
    void setDriftAmount(float amount) { drift_.setAmount(amount); }

    struct Params {
        float bellLevel = 0.5f;
        float hammerHardness = 0.5f;
        float hammerNoise = 0.35f;
        float pickupDrive = 0.3f;
        float character = 0.0f;      ///< 0 = lame frappée, 1 = anche
        float velocitySensitivity = 0.8f;
        float toneBass = 0.0f;
        float toneTreble = 0.0f;
    };

    float render(const Params& p);

private:
    static constexpr int kPartialCount = 3;

    double sampleRate_ = 48000.0;
    std::array<double, kPartialCount> partialPhase_{};
    vsm::audio::dsp::AdsrEnvelope amp_;
    vsm::audio::dsp::Biquad knockFilter_;
    vsm::audio::dsp::AnalogDrift drift_;
    vsm::util::DeterministicRng rng_{0x455049414E4FULL}; // "EPIANO"
    float knockLevel_ = 0.0f;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class EPianoSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kBellLevel = 1, kTineDecay, kRelease, kHammerHardness, kHammerNoise,
        kPickupDrive, kCharacter, kVelocitySensitivity,
        kToneBass, kToneTreble,
        kTremoloRate, kTremoloDepth, kTremoloStereo,
        kAnalogCharacter, kOutputLevel,
    };

    EPianoSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Electric Piano (lames)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<EPianoVoice, kMaxVoices> voiceManager_;
    double tremoloPhase_ = 0.0;
    vsm::audio::dsp::Biquad bassShelf_, trebleShelf_;
};

} // namespace vsm::plugins::epiano
