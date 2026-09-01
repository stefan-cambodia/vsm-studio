#pragma once
#include "vsm/audio/dsp/AnalogDrift.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/dsp/WaveTable.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::wavetable {

/// Synthétiseur à TABLE D'ONDES (esprit PPG / Waldorf).
///
/// CE QU'IL APPORTE AU PARC, ET QUE RIEN D'AUTRE N'APPORTE : une forme d'onde
/// qui CHANGE pendant la note. Toutes les autres machines du projet partent
/// d'une forme fixe (scie, carré, sinus) qu'un filtre vient ensuite tailler ;
/// le timbre y évolue par soustraction. Ici, c'est la SOURCE elle-même qui se
/// déforme -- on passe d'un spectre à un autre, y compris vers des spectres
/// qu'aucun oscillateur analogique ne peut produire (harmoniques inversées,
/// raies inharmoniques). Filtre fermé, cette machine bouge encore ; un Juno,
/// non.
///
/// TROIS CHOIX DE CONCEPTION, et leurs raisons :
///
///  1. Une ENVELOPPE DÉDIÉE à la position dans la table, séparée de celle du
///     filtre. C'est la commande principale de l'instrument : lui faire
///     partager l'enveloppe du filtre aurait lié deux mouvements qui, sur ces
///     machines, se règlent indépendamment.
///  2. DEUX oscillateurs lisant la MÊME table à des positions décalées. Le
///     décalage entre les deux crée un battement de timbre, pas seulement de
///     hauteur -- c'est la signature de la famille.
///  3. Le repliement est traité dans la brique `WaveTableBank`, pas ici. Voir
///     son en-tête : sans niveaux de repliement, une note aiguë sur une forme
///     riche siffle.
///
/// Approximations assumées (§ 27) : les tables sont ENGENDRÉES par calcul à
/// partir de spectres décrits dans le code, et non relevées sur un
/// instrument. Aucune n'est une copie d'une table d'origine ; ce sont des
/// familles de spectres choisies pour couvrir ce que la synthèse par table
/// sait faire. L'interpolation est linéaire, là où certaines machines
/// utilisent une interpolation d'ordre supérieur.
class WavetableVoice {
public:
    void prepare(double sampleRate, uint64_t seed);

    bool isActive() const { return ampEnv_.isActive(); }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    void noteOff(uint8_t) { ampEnv_.noteOff(); filterEnv_.noteOff(); waveEnv_.noteOff(); }

    void setSettings(const vsm::audio::dsp::AdsrSettings& amp,
                      const vsm::audio::dsp::AdsrSettings& filter,
                      const vsm::audio::dsp::AdsrSettings& wave) {
        ampEnv_.setSettings(amp); filterEnv_.setSettings(filter); waveEnv_.setSettings(wave);
    }
    void setDriftAmount(float amount) { driftA_.setAmount(amount); driftB_.setAmount(amount); }

    struct Params {
        int table = 0;
        float position = 0.0f;
        float waveEnvAmount = 0.5f;
        float lfoToPosition = 0.0f;
        float oscBLevel = 0.0f, oscBDetune = 0.05f, oscBPosition = 0.15f;
        float noiseLevel = 0.0f;
        float cutoff = 4000.0f, resonance = 0.2f, envAmount = 0.4f, keyTrack = 0.3f;
        float lfoToFilter = 0.0f, lfoToPitch = 0.0f;
        float velocityToFilter = 0.3f;
        // Molette de hauteur, en demi-tons (la somme drift+vibrato l'est
        // déjà). À zéro l'addition est exacte : empreinte inchangée.
        float bendSemitones = 0.0f;
        // Molette de MODULATION (CC 1) mise à l'échelle : demi-tons de
        // vibrato ajoutés au LFO, une demi-note à fond. Additif, exact à 0.
        float wheelVibratoSemis = 0.0f;
    };

    float render(const vsm::audio::dsp::WaveTableBank& bank, const Params& p, float lfo);

private:
    double sampleRate_ = 48000.0;
    vsm::audio::dsp::WaveTableOscillator oscA_, oscB_;
    vsm::audio::dsp::StateVariableFilter filter_;
    vsm::audio::dsp::AdsrEnvelope ampEnv_, filterEnv_, waveEnv_;
    vsm::audio::dsp::AnalogDrift driftA_, driftB_;
    vsm::util::DeterministicRng rng_{0x57415654ULL};
    float baseHz_ = 261.6f;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class WavetableSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kTable = 1, kPosition, kWaveEnvAmount, kLfoToPosition,
        kOscBLevel, kOscBDetune, kOscBPosition, kNoiseLevel,
        kFilterCutoff, kFilterResonance, kFilterEnvAmount, kFilterKeyTrack,
        kFilterAttack, kFilterDecay, kFilterSustain, kFilterRelease,
        kAmpAttack, kAmpDecay, kAmpSustain, kAmpRelease,
        kWaveAttack, kWaveDecay, kWaveSustain, kWaveRelease,
        kLfoRate, kLfoToFilter, kLfoToPitch, kVelocityToFilter, kAnalogCharacter,
    };

    WavetableSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Wavetable Synth"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kAnalogCharacter + 1> params_{};
    vsm::audio::engine::VoiceManager<WavetableVoice, kMaxVoices> voiceManager_;
    /// Pointeur vers la banque PARTAGÉE, résolu dans `initialize()`. Le fil
    /// audio ne doit jamais déclencher sa construction.
    const vsm::audio::dsp::WaveTableBank* bank_ = nullptr;
    double lfoPhase_ = 0.0;
    // Molettes de hauteur (demi-tons) et de modulation (CC 1, 0..1), même
    // contrat que params_.
    std::atomic<float> bendSemitones_{0.0f};
    std::atomic<float> modWheel_{0.0f};
    // Vibrato de la molette de modulation à fond : une demi-note.
    static constexpr float kWheelVibratoSemitones = 0.5f;
};

} // namespace vsm::plugins::wavetable
