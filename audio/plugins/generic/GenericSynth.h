#pragma once
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::generic {

/// Oscillateur à forme MORPHABLE, sans palier.
///
/// La forme est un nombre continu de 0 à 3 : sinus, triangle, dent de scie,
/// carré. Entre deux entiers, on entend le fondu des deux formes voisines.
///
/// POURQUOI PAS QUATRE OSCILLATEURS QU'ON MÉLANGE : ils auraient des phases
/// indépendantes, et deux formes à la même fréquence mais déphasées se
/// peignent au lieu de se fondre. Une seule phase, donc, et les deux formes
/// voisines calculées à cette phase.
///
/// Le repliement est traité par la même correction polynomiale que le reste du
/// parc (« polyBLEP ») : sans elle, la dent de scie et le carré sifflent dans
/// les aigus, et la machine deviendrait d'autant plus fausse qu'on monte -- ce
/// qui est le contraire de ce qu'on attend d'un instrument de mesure.
class MorphOscillator {
public:
    void setSampleRate(double sampleRate) { sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0; }
    void setFrequency(float hz) { frequencyHz_ = hz > 0.0f ? hz : 1.0f; }
    void reset(double phase = 0.0) { phase_ = phase - std::floor(phase); }

    /// `shape` de 0 (sinus) à 3 (carré), continu.
    float nextSample(float shape, float pulseWidth);

private:
    static float polyBlep(double t, double dt);

    double sampleRate_ = 48000.0;
    double phase_ = 0.0;
    float frequencyHz_ = 440.0f;
};

/// Synthétiseur NEUTRE, conçu pour la recherche automatique de patch.
///
/// CE QU'IL EST, ET CE QU'IL N'EST PAS. Toutes les autres machines du parc
/// visent un caractère : un filtre qui chante, une dérive qui vit, une
/// saturation qui colore. Celle-ci vise l'inverse -- couvrir le plus large
/// espace de timbres possible SANS signature propre, pour qu'une recherche
/// automatique puisse s'en approcher au lieu de s'y heurter.
///
/// QUATRE EXIGENCES la distinguent, et chacune répond à un obstacle mesuré de
/// l'optimisation :
///
///  1. CONTINUITÉ. Tout ce qui peut être continu l'est, forme d'onde et type
///     de filtre compris. Un sélecteur discret crée une falaise dans la
///     fonction de coût, et une recherche par descente s'y bloque.
///  2. NEUTRALITÉ AU REPOS. À réglages neutres, la machine rend une forme
///     d'onde propre : aucune dérive, aucune saturation, aucun bruit ajouté.
///     Ce qui est réglé à zéro disparaît vraiment. Un test le vérifie sur le
///     spectre.
///  3. MONOTONIE. Ouvrir la coupure augmente le contenu aigu ; monter la
///     résonance accentue la bande ; monter le drive ajoute des harmoniques.
///     Toujours, sans exception ni retournement. Testé, parce qu'un paramètre
///     non monotone piège toute recherche par descente.
///  4. DÉCOUPLAGE. Un paramètre agit sur une dimension et une seule, autant
///     que la physique le permet. Là où le couplage est inévitable -- la
///     résonance qui fait monter le niveau -- il est COMPENSÉ en interne, et
///     la compensation est documentée là où elle est faite.
class GenericVoice {
public:
    void prepare(double sampleRate, uint64_t seed);

    bool isActive() const { return ampEnv_.isActive(); }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    void noteOff(uint8_t) { ampEnv_.noteOff(); filterEnv_.noteOff(); }

    void setSettings(const vsm::audio::dsp::AdsrSettings& amp,
                      const vsm::audio::dsp::AdsrSettings& filter) {
        ampEnv_.setSettings(amp); filterEnv_.setSettings(filter);
    }

    struct Params {
        float osc1Shape = 1.0f, osc1Level = 0.8f, osc1PulseWidth = 0.5f;
        float osc2Shape = 1.0f, osc2Level = 0.0f, osc2PulseWidth = 0.5f;
        float osc2Detune = 0.0f;
        int osc2Octave = 0;
        float subLevel = 0.0f, subShape = 0.0f;
        float noiseLevel = 0.0f, noiseColour = 0.0f;
        float filterType = 0.0f;  ///< 0 = passe-bas, 1 = passe-bande, 2 = passe-haut
        float cutoff = 8000.0f, resonance = 0.0f, envAmount = 0.0f, keyTrack = 0.0f;
        bool fourPole = false;
        float lfo1ToPitch = 0.0f, lfo1ToFilter = 0.0f, lfo1ToAmp = 0.0f, lfo1ToPulseWidth = 0.0f;
        float lfo2ToPitch = 0.0f, lfo2ToFilter = 0.0f;
        float drive = 0.0f;
        float velocityToFilter = 0.0f, velocityToAmp = 0.0f;
        // Molette de hauteur, en demi-tons (le vibrato l'est déjà). À zéro
        // l'addition est exacte : empreinte inchangée.
        float bendSemitones = 0.0f;
        // Molette de MODULATION (CC 1) mise à l'échelle : demi-tons de
        // vibrato ajoutés au LFO 1, une demi-note à fond. Additif, exact à 0.
        float wheelVibratoSemis = 0.0f;
    };

    float render(const Params& p, float lfo1, float lfo2);

private:
    double sampleRate_ = 48000.0;
    MorphOscillator osc1_, osc2_, sub_;
    vsm::audio::dsp::StateVariableFilter filter_, filter2_;
    vsm::audio::dsp::AdsrEnvelope ampEnv_, filterEnv_;
    vsm::util::DeterministicRng rng_{0x47454E45ULL};
    float pinkState_ = 0.0f;
    float baseHz_ = 261.6f;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class GenericSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kOsc1Shape = 1, kOsc1Level, kOsc1PulseWidth,
        kOsc2Shape, kOsc2Level, kOsc2PulseWidth, kOsc2Detune, kOsc2Octave,
        kSubLevel, kSubShape, kNoiseLevel, kNoiseColour,
        kFilterType, kFilterCutoff, kFilterResonance, kFilterSlope,
        kFilterEnvAmount, kFilterKeyTrack,
        kAmpAttack, kAmpDecay, kAmpSustain, kAmpRelease,
        kFilterAttack, kFilterDecay, kFilterSustain, kFilterRelease,
        kLfo1Rate, kLfo1Shape, kLfo1ToPitch, kLfo1ToFilter, kLfo1ToAmp, kLfo1ToPulseWidth,
        kLfo2Rate, kLfo2Shape, kLfo2ToPitch, kLfo2ToFilter,
        kVelocityToFilter, kVelocityToAmp,
        kDrive, kOutputLevel,
    };

    GenericSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Generic Synth"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);
    float renderLfo(double phase, float shape) const;

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<GenericVoice, kMaxVoices> voiceManager_;
    double lfo1Phase_ = 0.0, lfo2Phase_ = 0.0;
    // Molettes de hauteur (demi-tons) et de modulation (CC 1, 0..1), même
    // contrat que params_.
    std::atomic<float> bendSemitones_{0.0f};
    std::atomic<float> modWheel_{0.0f};
    // Vibrato de la molette de modulation à fond : une demi-note.
    static constexpr float kWheelVibratoSemitones = 0.5f;
};

} // namespace vsm::plugins::generic
