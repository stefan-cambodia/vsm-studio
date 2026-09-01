#pragma once
#include "vsm/audio/dsp/AnalogDrift.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/dsp/Oscillator.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/io/WavFileReader.h"
#include "vsm/audio/plugin/ISampleLoader.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include <array>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

namespace vsm::plugins::pcmhybrid {

/// Banque de TRANSITOIRES D'ATTAQUE engendrés par calcul.
///
/// POURQUOI ELLE EXISTE : les machines hybrides de la fin des années 80
/// tirent leur caractère d'une attaque échantillonnée -- un coup de maillet,
/// un pincement, un souffle -- collée devant un corps de son synthétique.
/// C'est l'attaque qui dit à l'oreille de quel instrument il s'agit ; le
/// reste du son, elle le reconnaît beaucoup moins bien.
///
/// Les transitoires sont ENGENDRÉS, pas chargés : le projet se construit
/// hors-ligne, sans fichier annexe. Ce sont des familles de transitoires
/// choisies pour couvrir ce que l'attaque peut dire (percussif, pincé,
/// soufflé, frotté, métallique), et non des copies d'échantillons d'origine.
///
/// L'ATTAQUE PEUT ÊTRE REMPLACÉE par un vrai fichier via `ISampleLoader`.
/// C'est le point qui rend cette machine utile à la reconstruction : la
/// chaîne d'analyse découpe déjà l'attaque du son à reproduire, et peut la
/// déposer ici -- attaque réelle, corps synthétique, une combinaison qu'aucune
/// autre machine du parc ne propose.
class AttackBank {
public:
    static constexpr size_t kAttackCount = 5;
    /// Les transitoires sont engendrés à cette fréquence de référence puis
    /// relus au pas voulu. Les stocker une fois pour toutes évite de les
    /// recalculer à chaque changement de fréquence d'échantillonnage.
    static constexpr double kReferenceRate = 48000.0;

    static const AttackBank& shared();

    const char* attackName(size_t index) const;
    const std::vector<float>& attack(size_t index) const;

private:
    AttackBank();
    std::array<std::vector<float>, kAttackCount> attacks_{};
    std::array<const char*, kAttackCount> names_{};
};

/// Machine HYBRIDE : attaque échantillonnée + corps synthétique.
///
/// CE QU'ELLE APPORTE AU PARC : toutes les autres machines mélodiques
/// démarrent leur note sur une forme d'onde entretenue, dont l'attaque n'est
/// qu'une montée d'enveloppe. Aucune ne peut produire le CLAQUEMENT d'un
/// maillet ou le grattement d'un archet, parce que ces bruits ne sont pas
/// périodiques. Ici, les deux couches coexistent : les premières
/// millisecondes viennent d'un enregistrement (ou d'un transitoire engendré),
/// la suite d'un oscillateur.
///
/// LA STRUCTURE, réglage central : les deux couches se combinent soit en
/// PARALLÈLE (la plus courante : attaque puis corps), soit par MODULATION EN
/// ANNEAU (le corps est multiplié par l'attaque, ce qui donne des timbres
/// métalliques inharmoniques). Ce choix change complètement la machine, d'où
/// sa place sur la façade.
///
/// Approximations assumées (§ 27) : ni les transitoires ni la structure ne
/// sont relevés sur un instrument réel. Le nombre de structures est de deux,
/// là où les machines d'origine en proposent sept.
class PcmHybridVoice {
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
    void setDriftAmount(float amount) { drift_.setAmount(amount); }

    struct Params {
        int attackIndex = 0;
        float attackLevel = 0.9f, attackDecay = 0.12f, attackTune = 0.0f, attackTone = 0.7f;
        float velocityToAttack = 0.6f;
        int toneShape = 0;
        float toneLevel = 0.8f, toneDetune = 0.0f;
        bool ringModulation = false;
        float cutoff = 4000.0f, resonance = 0.2f, envAmount = 0.4f, keyTrack = 0.4f;
        float lfoToPitch = 0.0f, lfoToFilter = 0.0f;
        float velocityToFilter = 0.3f;
        // Molette de hauteur, en demi-tons. Elle porte sur le CORPS
        // synthétique ; l'attaque PCM, un transitoire de quelques dizaines de
        // millisecondes, garde sa lecture -- la plier ferait glisser le
        // claquement, pas la note. À zéro l'addition est exacte.
        float bendSemitones = 0.0f;
        // Molette de MODULATION (CC 1) mise à l'échelle : demi-tons de
        // vibrato ajoutés au LFO, une demi-note à fond. Additif, exact à 0.
        float wheelVibratoSemis = 0.0f;
    };

    /// `override` remplace le transitoire engendré quand un fichier a été
    /// chargé. Passé en argument plutôt que stocké : la voix ne possède rien
    /// et le pointeur reste valide le temps du bloc.
    float render(const AttackBank& bank, const vsm::audio::io::SampleBuffer* overrideSample,
                 const Params& p, float lfo);

private:
    double sampleRate_ = 48000.0;
    vsm::audio::dsp::BandLimitedOscillator tone_;
    vsm::audio::dsp::StateVariableFilter filter_;
    vsm::audio::dsp::StateVariableFilter attackTone_;
    vsm::audio::dsp::AdsrEnvelope ampEnv_, filterEnv_;
    vsm::audio::dsp::AnalogDrift drift_;
    vsm::util::DeterministicRng rng_{0x50434D48ULL};
    double attackPosition_ = 0.0;
    float attackEnvelope_ = 0.0f;
    bool attackPlaying_ = false;
    float baseHz_ = 261.6f;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class PcmHybridSynth : public vsm::audio::plugin::ISynthPlugin,
                        public vsm::audio::plugin::ISampleLoader {
public:
    static constexpr size_t kMaxVoices = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kAttackSample = 1, kAttackLevel, kAttackDecay, kAttackTune, kAttackTone, kVelocityToAttack,
        kToneShape, kToneLevel, kToneDetune, kStructure,
        kFilterCutoff, kFilterResonance, kFilterEnvAmount, kFilterKeyTrack,
        kFilterAttack, kFilterDecay, kFilterSustain, kFilterRelease,
        kAmpAttack, kAmpDecay, kAmpSustain, kAmpRelease,
        kLfoRate, kLfoToPitch, kLfoToFilter, kVelocityToFilter, kAnalogCharacter,
    };

    PcmHybridSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "PCM + Synth Hybrid"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

    // --- ISampleLoader -----------------------------------------------------
    /// Un seul emplacement : l'attaque. Le corps du son est synthétique par
    /// définition -- lui permettre un échantillon ferait de cette machine un
    /// second sampler, ce que le parc a déjà.
    bool loadSample(int slot, const std::string& path, std::string& outError) override;
    void clearSample(int slot) override;
    std::string samplePath(int slot) const override;
    int slotCount() const override { return 1; }

    /// Publie une attaque déjà en mémoire, sans passer par un fichier. C'est
    /// le chemin qu'emprunte la chaîne d'analyse : elle a découpé l'attaque du
    /// son à reproduire et la dépose directement ici.
    void setAttackSample(vsm::audio::io::SampleBufferPtr sample);

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    // Molettes de hauteur (demi-tons) et de modulation (CC 1, 0..1), même
    // contrat que params_.
    std::atomic<float> bendSemitones_{0.0f};
    std::atomic<float> modWheel_{0.0f};
    // Vibrato de la molette de modulation à fond : une demi-note.
    static constexpr float kWheelVibratoSemitones = 0.5f;
    mutable std::array<std::atomic<float>, kAnalogCharacter + 1> params_{};
    vsm::audio::engine::VoiceManager<PcmHybridVoice, kMaxVoices> voiceManager_;
    const AttackBank* bank_ = nullptr;
    /// Publication atomique, même raison que dans le sampler : le chargement a
    /// lieu hors du fil audio, la lecture dedans.
    std::atomic<vsm::audio::io::SampleBufferPtr> attackSample_{};
    std::string attackPath_; // fil UI uniquement
    double lfoPhase_ = 0.0;
};

} // namespace vsm::plugins::pcmhybrid
