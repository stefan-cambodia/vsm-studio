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

namespace vsm::plugins::supersaw {

/// Lead « supersaw » (type JP-8000).
///
/// CE QUI LE DISTINGUE DU RESTE DU PARC : ce n'est pas un soustractif de plus
/// avec un réglage d'unisson. Le timbre vient d'une SEULE forme d'onde
/// composite -- sept dents de scie par voix, désaccordées selon une courbe
/// très particulière -- et c'est cette courbe, pas le filtre, qui fait le son.
/// Ouvrir le filtre en grand sur cette machine donne encore un son
/// reconnaissable ; faire la même chose sur un Juno donne une scie ordinaire.
///
/// TROIS TRAITS que l'on reproduit délibérément, parce que sans eux
/// l'empilement sonne comme sept scies quelconques :
///
///  1. Le désaccord est NON LINÉAIRE en fonction du réglage. Les sept voix ne
///     sont pas réparties régulièrement : trois sont serrées contre la
///     fondamentale, quatre s'en écartent bien plus. C'est ce déséquilibre qui
///     donne l'épaisseur sans brouiller la hauteur.
///  2. Les six voix latérales et la voix centrale ont des courbes de niveau
///     OPPOSÉES : plus on désaccorde, plus les latérales montent et plus la
///     centrale baisse. À désaccord nul, on entend une scie franche ; à fond,
///     un nuage.
///  3. Les phases sont TIRÉES AU SORT à chaque note. Sept scies démarrant en
///     phase produisent une bouffée de niveau à l'attaque et un timbre creux ;
///     c'est l'erreur classique, et elle s'entend.
///
/// SOURCE ET HONNÊTETÉ (§ 27) : les coefficients de désaccord et de mélange
/// viennent de l'analyse publiée d'Adam Szabo (« How to Emulate the Super
/// Saw », KTH, 2010), qui a mesuré l'instrument d'origine. Aucune mesure n'a
/// été faite ici sur du matériel réel : ce sont des courbes reprises, pas
/// vérifiées par nos soins. Le filtre est un `StateVariableFilter` (TPT), pas
/// le circuit d'origine.
class SupersawVoice {
public:
    /// Sept scies : le nombre n'est pas un réglage, c'est ce que fait la
    /// machine d'origine. En mettre plus ne rend pas le son « plus gros », ça
    /// le rend flou -- le désaccord est calibré pour sept.
    static constexpr int kSawCount = 7;

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
    void setDriftAmount(float amount) { drift_.setAmount(amount); }
    void setGlideSeconds(float seconds) { glideSeconds_ = seconds; }
    /// Note de départ du portamento : la hauteur d'où la voix glisse.
    void setGlideFrom(float hz) { currentHz_ = hz > 0.0f ? hz : targetHz_; }
    float currentHz() const { return currentHz_; }

    struct Params {
        float detune = 0.4f;      ///< réglage brut 0..1, passé dans la courbe
        float mix = 0.5f;         ///< dosage latérales / centrale, 0..1
        float spread = 0.6f;      ///< étalement stéréo des sept scies
        float subLevel = 0.0f;
        float noiseLevel = 0.0f;
        float pitchHpf = 1.0f;    ///< coupe-bas suivant la fondamentale
        float cutoff = 6000.0f, resonance = 0.3f, envAmount = 0.4f, keyTrack = 0.3f;
        float lfoToPitch = 0.0f, lfoToFilter = 0.0f;
        float velocityToFilter = 0.3f;
        // Molette de hauteur, en demi-tons (la somme drift+vibrato l'est
        // déjà). À zéro l'addition est exacte : empreinte inchangée.
        float bendSemitones = 0.0f;
        // Molette de MODULATION (CC 1) mise à l'échelle : demi-tons de
        // vibrato ajoutés au LFO, une demi-note à fond. Additif, exact à 0.
        float wheelVibratoSemis = 0.0f;
    };

    /// Rend un échantillon stéréo. Le supersaw est stéréo PAR NATURE : les
    /// sept scies sont réparties dans l'image, et les sommer en mono avant de
    /// dupliquer donnerait un lead plat au milieu du champ.
    void render(const Params& p, float lfo, float& outL, float& outR);

private:
    double sampleRate_ = 48000.0;
    std::array<vsm::audio::dsp::BandLimitedOscillator, kSawCount> saws_{};
    vsm::audio::dsp::BandLimitedOscillator sub_{};
    vsm::audio::dsp::StateVariableFilter filterL_, filterR_;
    vsm::audio::dsp::StateVariableFilter hpfL_, hpfR_;
    vsm::audio::dsp::AdsrEnvelope ampEnv_, filterEnv_;
    vsm::audio::dsp::AnalogDrift drift_;
    vsm::util::DeterministicRng rng_{0x5350530ULL};
    float baseHz_ = 261.6f, targetHz_ = 261.6f, currentHz_ = 261.6f;
    float glideSeconds_ = 0.0f;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class SupersawSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kOscDetune = 1, kOscMix, kOscSpread, kSubLevel, kNoiseLevel, kPitchHpf,
        kFilterCutoff, kFilterResonance, kFilterEnvAmount, kFilterKeyTrack,
        kFilterAttack, kFilterDecay, kFilterSustain, kFilterRelease,
        kAmpAttack, kAmpDecay, kAmpSustain, kAmpRelease,
        kLfoRate, kLfoToPitch, kLfoToFilter,
        kGlide, kVelocityToFilter, kAnalogCharacter,
    };

    SupersawSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Supersaw Lead"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kAnalogCharacter + 1> params_{};
    vsm::audio::engine::VoiceManager<SupersawVoice, kMaxVoices> voiceManager_;
    double lfoPhase_ = 0.0;
    // Molettes de hauteur (demi-tons) et de modulation (CC 1, 0..1), même
    // contrat que params_.
    std::atomic<float> bendSemitones_{0.0f};
    std::atomic<float> modWheel_{0.0f};
    // Aftertouch (pression de canal, 0..1) : s'ajoute à la molette, borné à 1.
    std::atomic<float> pressure_{0.0f};
    // Vibrato de la molette de modulation à fond : une demi-note.
    static constexpr float kWheelVibratoSemitones = 0.5f;
    /// Dernière hauteur jouée : point de départ du portamento de la note
    /// suivante. Un lead sans glissando n'est pas un lead.
    float lastHz_ = 0.0f;
};

} // namespace vsm::plugins::supersaw
