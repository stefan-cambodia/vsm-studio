#pragma once
#include "vsm/audio/dsp/AnalogDrift.h"
#include "vsm/audio/dsp/Chorus.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/dsp/LadderFilterZDFx4.h"
#include "vsm/audio/dsp/Oscillator.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::juno106 {

enum ParamIds : vsm::audio::plugin::ParamId {
    kDcoSawLevel = 0,
    kDcoPulseLevel,
    kDcoSubLevel,
    kDcoNoiseLevel,
    kDcoPulseWidth,
    kPwmLfoAmount,
    kLfoRate,
    kLfoDelay,
    kLfoPitchAmount,
    kHpfCutoff,
    kVcfCutoff,
    kVcfResonance,
    kVcfEnvAmount,
    kVcfLfoAmount,
    kVcfKeyTrack,
    kEnvAttack,
    kEnvDecay,
    kEnvSustain,
    kEnvRelease,
    kChorusMode,
    kAnalogCharacter,
    kNumParams
};

/// Paramètres partagés lus une fois par bloc et passés à chaque voix.
/// (Le LFO global et la largeur d'impulsion modulée, eux, varient par
/// échantillon et sont passés séparément à Juno106Voice::render.)
struct Juno106Params {
    float sawLevel = 1.0f;
    float pulseLevel = 0.0f;
    float subLevel = 0.0f;
    float noiseLevel = 0.0f;
    float hpfCutoff = 20.0f;
    float vcfCutoffBase = 1200.0f;
    float vcfResonance = 0.3f;
    float vcfEnvAmount = 0.6f;
    float vcfLfoAmount = 0.0f;
    float vcfKeyTrack = 0.3f;
    float lfoPitchAmount = 0.0f;
    float analogCharacter = 0.3f;
    // Molette de hauteur, en demi-tons (D0.5 : le MIDI non-note atteint les
    // machines). À zéro, l'addition flottante est exacte : l'empreinte audio
    // de non-régression ne bouge pas d'un bit.
    float bendSemitones = 0.0f;
    // Molette de MODULATION (CC 1) déjà mise à l'échelle : demi-tons de
    // vibrato ajoutés au LFO, une demi-note à fond. Terme ADDITIF, exact à
    // zéro, pour la même raison que la molette de hauteur.
    float wheelVibratoSemis = 0.0f;
};

/// Une voix polyphonique complète : DCO (saw + pulse + sub) + bruit ->
/// HPF non résonant -> VCF 24 dB/oct -> VCA (enveloppe). Satisfait le
/// contrat attendu par vsm::audio::engine::VoiceManager (isActive/note/
/// channel/noteOn/noteOff) tout en portant sa propre chaîne DSP.
///
/// Choix d'architecture fidèles au Juno-106 (section 7) :
///  - DCO (oscillateur à commande NUMÉRIQUE) : accord très stable,
///    contrairement au VCO du Minimoog -> la dérive analogique appliquée
///    ici est volontairement bien plus faible (kMaxDriftSemitones petit).
///    La phase des oscillateurs est réinitialisée à chaque note (trait DCO :
///    attaque cohérente, horloge numérique qui remet la rampe à zéro).
///  - Sub-oscillateur carré une octave sous le DCO (le "sub" iconique).
///  - PAS de sensibilité à la vélocité : le clavier du Juno-106 n'est pas
///    vélocité-sensible -> le VCA ne dépend QUE de l'enveloppe, jamais de
///    velocity_ (trait authentique, pas un oubli).
///
/// Approximations assumées (section 27) : le VCF réel du Juno est un filtre
/// OTA (IR3109), modélisé ici par le LadderFilterZDF à 4 pôles déjà présent
/// -- même pente (24 dB/oct) et même comportement de résonance/
/// auto-oscillation, mais topologie ladder plutôt qu'OTA. Le HPF utilise le
/// StateVariableFilter générique en mode passe-haut. Aucune mesure
/// comparative avec un Juno matériel n'a été faite.
class Juno106Voice {
public:
    void prepare(double sampleRate, uint64_t seed) {
        using namespace vsm::audio::dsp;
        sampleRate_ = sampleRate;
        saw_.setSampleRate(sampleRate);   saw_.setWaveform(Waveform::Saw);
        pulse_.setSampleRate(sampleRate); pulse_.setWaveform(Waveform::Square);
        sub_.setSampleRate(sampleRate);   sub_.setWaveform(Waveform::Square);
        hpf_.setSampleRate(sampleRate);
        hpf_.setMode(StateVariableFilter::Mode::HighPass);
        hpf_.setResonance(0.707f);
        // Le VCF (4 pôles, pas de knob Drive sur le Juno) n'appartient plus à
        // la voix : il est partagé par groupe de quatre voix et configuré par
        // le synthé (voir Juno106Synth::voiceFilters_).
        env_.setSampleRate(sampleRate);
        drift_.setSampleRate(sampleRate);
        drift_.setSeed(seed);
        drift_.setRateHz(0.12f);
        noiseRng_ = vsm::util::DeterministicRng(seed ^ 0x0123456789ABCDEFULL);
    }

    // --- Contrat VoiceManager -------------------------------------------
    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel;
        note_ = note;
        velocity_ = velocity;
        env_.noteOn();
        saw_.reset(0.0);
        pulse_.reset(0.0);
        sub_.reset(0.0);
    }
    void noteOff(uint8_t /*velocity*/) { env_.noteOff(); }
    bool isActive() const { return env_.isActive(); }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    // --- Réglages poussés une fois par bloc -----------------------------
    void setEnvSettings(const vsm::audio::dsp::AdsrSettings& s) { env_.setSettings(s); }
    void setDriftAmount(float amount) { drift_.setAmount(amount); }

    /// Un échantillon de sortie MONO pour cette voix (0 si inactive).
    /// `lfoBipolar` : valeur courante du LFO global [-1,1] déjà fondue par
    /// le delay. `pulseWidthNow` : largeur d'impulsion déjà modulée.
    // Rendu en deux temps : les six voix partagent deux VCF vectorisés
    // (quatre lignes chacun). Voir Jupiter8Synth.h et SimdFloat4.h pour le
    // raisonnement complet -- résumé : un filtre récursif passe son temps à
    // attendre ses propres résultats, quatre filtres indépendants comblent ces
    // attentes.
    float renderPreFilter(const Juno106Params& p, float lfoBipolar, float pulseWidthNow) {
        active_ = env_.isActive();
        if (!active_) {
            pendingCutoff_ = p.vcfCutoffBase; // ligne SIMD calculée quand même : valeur saine
            pendingEnvLevel_ = 0.0f;
            return 0.0f;
        }

        const float driftSemis = drift_.nextValue() * kMaxDriftSemitones;
        const float vibratoSemis = lfoBipolar * p.lfoPitchAmount * kMaxVibratoSemitones
                                 + lfoBipolar * p.wheelVibratoSemis;
        const float freq = 440.0f * std::exp2f(
            (static_cast<float>(note_) + driftSemis + vibratoSemis
             + p.bendSemitones - 69.0f) / 12.0f);

        saw_.setFrequency(freq);
        pulse_.setFrequency(freq);
        pulse_.setPulseWidth(pulseWidthNow);
        sub_.setFrequency(freq * 0.5f);

        const float noise = noiseRng_.nextBipolar();
        float mix = saw_.nextSample() * p.sawLevel
                  + pulse_.nextSample() * p.pulseLevel
                  + sub_.nextSample() * p.subLevel
                  + noise * p.noiseLevel;
        mix *= 0.3f; // normalisation grossière avant filtrage

        hpf_.setCutoffHz(p.hpfCutoff);
        const float highPassed = hpf_.process(mix);

        const float envLevel = env_.nextSample();

        const float envOct = p.vcfEnvAmount * envLevel * kVcfEnvRangeOctaves;
        const float lfoOct = lfoBipolar * p.vcfLfoAmount * kVcfLfoRangeOctaves;
        const float trackOct = p.vcfKeyTrack * (static_cast<float>(note_) - 60.0f) / 12.0f;
        pendingCutoff_ = p.vcfCutoffBase * std::exp2f(envOct + lfoOct + trackOct);
        pendingEnvLevel_ = envLevel;
        return highPassed;
    }

    /// VCA = enveloppe seule (le clavier du Juno-106 n'est pas sensible à la
    /// vélocité -- trait authentique, couvert par un test).
    float applyFilterOutput(float filtered) const {
        return active_ ? filtered * pendingEnvLevel_ : 0.0f;
    }

    float pendingCutoffHz() const { return pendingCutoff_; }

private:
    static constexpr float kMaxDriftSemitones = 0.04f;   // DCO stable -> dérive faible
    static constexpr float kMaxVibratoSemitones = 1.0f;
    static constexpr float kVcfEnvRangeOctaves = 6.0f;
    static constexpr float kVcfLfoRangeOctaves = 2.0f;

    double sampleRate_ = 48000.0;
    vsm::audio::dsp::BandLimitedOscillator saw_, pulse_, sub_;
    vsm::audio::dsp::StateVariableFilter hpf_;
    float pendingCutoff_ = 2000.0f;   // coupure du VCF pour l'échantillon en cours
    float pendingEnvLevel_ = 0.0f;    // niveau du VCA pour l'échantillon en cours
    bool active_ = false;
    vsm::audio::dsp::AdsrEnvelope env_;
    vsm::audio::dsp::AnalogDrift drift_;
    vsm::util::DeterministicRng noiseRng_{0};
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

/// Polysynthé Juno-106-style, 6 voix (comme le hardware). Premier plugin
/// du projet à utiliser le VoiceManager polyphonique générique. Chaîne :
/// 6 x voix (DCO->HPF->VCF->VCA) -> somme mono -> chorus BBD stéréo global.
class Juno106Synth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 6;

    Juno106Synth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;

    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override;
    const vsm::audio::plugin::ParameterList& parameterList() const override;

    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;

    const char* machineName() const override { return "Juno-106-style Polysynth"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& ev);
    void applyChorusMode(int mode);

    vsm::audio::engine::VoiceManager<Juno106Voice, kMaxVoices> voiceManager_;

    // Six voix pour des lignes SIMD par quatre : deux filtres, dont deux
    // lignes inutilisées. Ce gâchis apparent reste très gagnant -- deux
    // filtres vectorisés coûtent ~124 ns contre ~430 ns pour six filtres
    // scalaires. Les lignes vides reçoivent un signal nul et une coupure
    // valide, jamais des valeurs aberrantes qui produiraient des dénormales.
    static constexpr size_t kLanes = vsm::audio::dsp::LadderFilterZDFx4::kLanes;
    static constexpr size_t kVoiceGroups = (kMaxVoices + kLanes - 1) / kLanes;
    std::array<vsm::audio::dsp::LadderFilterZDFx4, kVoiceGroups> voiceFilters_;
    vsm::audio::dsp::Chorus chorus_;

    // Molettes de hauteur et de modulation, écrites par le thread
    // d'événements, lues une fois par bloc -- même contrat que params_.
    std::atomic<float> bendSemitones_{0.0f};
    std::atomic<float> modWheel_{0.0f};
    // Vibrato de la molette de modulation à fond : une demi-note.
    static constexpr float kWheelVibratoSemitones = 0.5f;

    // État du LFO global (partagé par toutes les voix).
    double lfoPhase_ = 0.0;
    double lfoIncrement_ = 0.0;
    float lfoDelayGain_ = 1.0f;      // fondu d'entrée du LFO (0..1)
    long lfoDelayElapsed_ = 0;       // échantillons écoulés depuis le dernier "réveil"
    long lfoDelaySamples_ = 0;       // durée du fondu, recalculée par bloc

    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
    double sampleRate_ = 48000.0;
    int lastChorusMode_ = -1;        // évite de reconfigurer le chorus à chaque bloc
};

} // namespace vsm::plugins::juno106
