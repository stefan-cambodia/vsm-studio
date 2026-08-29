#pragma once
#include "vsm/audio/dsp/AnalogDrift.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::phasedist {

/// DISTORSION DE PHASE — déformer le TEMPS de lecture, pas l'amplitude.
///
/// POURQUOI CETTE MACHINE, alors que le parc sait déjà enrichir un sinus.
/// `vsm.westcoast` le fait par PLIAGE : une fonction non linéaire de
/// l'AMPLITUDE, un waveshaper sans mémoire. `vsm.dx7` le fait par MODULATION DE
/// FRÉQUENCE : un oscillateur en module un autre. Celle-ci fait une troisième
/// chose, qui n'est ni l'une ni l'autre : elle lit une table de sinus à une
/// vitesse VARIABLE dans le cycle -- vite au début, lentement ensuite. Le signal
/// reste une sinusoïde parcourue ; c'est le TEMPS qui est déformé.
///
/// LE TRAIT DISTINCTIF, ET IL SÉPARE CETTE MACHINE DU PLIEUR. Déformer la phase
/// ne touche pas à l'amplitude : la lecture parcourt toujours toute la table,
/// donc la crête est la même quelle que soit la déformation. **Le timbre s'ouvre
/// À NIVEAU CONSTANT.** Un plieur, lui, gagne des harmoniques en poussant plus
/// fort dans les replis : le niveau bouge nécessairement. Le test mesure les
/// deux ensemble -- le centroïde spectral doit monter d'un facteur trois pendant
/// que la valeur efficace reste dans une fenêtre de quinze pour cent -- et c'est
/// une chose qu'aucune autre machine du parc ne peut faire.
///
/// LE SECOND TRAIT : UNE RÉSONANCE VERROUILLÉE SUR LA FONDAMENTALE. Les Casio CZ
/// avaient des formes dites « résonantes » : un sinus rapide, à un multiple
/// ENTIER de la note, fenêtré par une dent de scie à la fondamentale. Le pic
/// spectral obtenu ressemble à une résonance de filtre, mais il ne peut se
/// placer QU'À `k x f0` -- il saute d'un rang à l'autre au lieu de glisser. Le
/// test vérifie qu'à k = 3 et k = 7 le maximum tombe bien sur ces rangs-là.
///
/// C'est l'exact complément de `vsm.vocal`, et les deux ensemble couvrent les
/// deux façons dont un pic spectral peut se comporter : les formants d'une voix
/// restent à des fréquences ABSOLUES quand la note change ; cette résonance-ci
/// suit la note en RAPPORT ENTIER. Aucune autre machine ne fait ni l'un ni
/// l'autre.
///
/// APPROXIMATIONS ASSUMÉES (§ 8 de `CDC-nouvelle-machine.md`), statut
/// « dérivé » -- aucune mesure sur un instrument réel :
///
///  - **Une seule courbe de déformation**, à un point de brisure, là où les
///    machines d'origine en offraient huit. Le point de brisure est justement
///    ce qui se règle, et huit courbes figées seraient huit fois moins
///    cherchables qu'un réglage continu (§ 6 de `CDC-machines-manquantes.md`).
///  - **Pas de « ring modulation » ni de bruit**, deux modes annexes des CZ qui
///    relèvent d'autres familles -- la première est déjà atteignable par la FM,
///    le second par n'importe quel oscillateur de bruit du parc.
///  - **Une seule ligne de distorsion**, pas deux empilées.

class PhaseDistVoice {
public:
    struct Params {
        float amount = 0.5f;        // profondeur de la déformation (le « DCW »)
        float envToAmount = 0.5f;   // l'enveloppe ouvre le timbre
        float resonance = 0.0f;     // dosage de la forme résonante
        float resonanceHarmonic = 3.0f;  // le rang k où le pic se place
        float velocityToAmount = 0.4f;
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate;
        ampEnv_.setSampleRate(sampleRate);
        modEnv_.setSampleRate(sampleRate);
        drift_.setSampleRate(sampleRate);
        drift_.setSeed(seed);
        drift_.setRateHz(0.09f);
        drift_.setAmount(0.0f);
        phase_ = 0.0f;
    }

    bool isActive() const { return ampEnv_.isActive(); }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void setSettings(const vsm::audio::dsp::AdsrSettings& amp,
                     const vsm::audio::dsp::AdsrSettings& mod) {
        ampEnv_.setSettings(amp);
        modEnv_.setSettings(mod);
    }
    void setDriftAmount(float amount) { drift_.setAmount(amount); }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel;
        note_ = note;
        velocity_ = velocity;
        baseHz_ = 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
        ampEnv_.noteOn();
        modEnv_.noteOn();
        phase_ = 0.0f;
    }

    void noteOff(uint8_t) { ampEnv_.noteOff(); modEnv_.noteOff(); }

    /// LA DÉFORMATION. La phase 0..1 est relue par une fonction linéaire par
    /// morceaux, à un point de brisure : la première moitié du cycle est
    /// parcourue en `p` du temps, la seconde en `1 - p`. À `p = 0,5` la
    /// fonction est l'identité et il ne se passe RIEN -- la sortie est un sinus
    /// pur, ce que le test vérifie. Plus `p` est petit, plus le début du cycle
    /// est traversé vite, et plus le spectre est riche.
    static float warp(float phase, float p) {
        const float pivot = std::clamp(p, 0.02f, 0.98f);
        return phase < pivot ? 0.5f * phase / pivot
                             : 0.5f + 0.5f * (phase - pivot) / (1.0f - pivot);
    }

    float render(const Params& p) {
        if (!ampEnv_.isActive()) return 0.0f;

        const float vel = static_cast<float>(velocity_) / 127.0f;
        const float f0 = baseHz_ * std::pow(2.0f, drift_.nextValue() * 0.05f / 12.0f);

        phase_ += f0 / static_cast<float>(sampleRate_);
        if (phase_ >= 1.0f) phase_ -= 1.0f;

        // L'ENVELOPPE OUVRE LE TIMBRE, comme la coupure d'un soustractif : c'est
        // le geste que ces machines imitaient, et le seul rôle de cette seconde
        // enveloppe.
        const float ouverture = std::clamp(
            p.amount + p.envToAmount * modEnv_.nextSample() + p.velocityToAmount * vel,
            0.0f, 1.0f);
        // `ouverture` = 0 -> pivot 0,5 -> identité -> sinus pur.
        const float pivot = 0.5f - 0.48f * ouverture;
        const float sinus = std::sin(static_cast<float>(vsm::audio::dsp::kTwoPi)
                                     * warp(phase_, pivot));

        // LA FORME RÉSONANTE : un sinus à `k x f0` FENÊTRÉ par une dent de scie
        // descendante à la fondamentale. Le pic ne peut se placer qu'à un rang
        // entier -- c'est ce qui la distingue d'un filtre, qui glisse.
        float sortie = sinus;
        if (p.resonance > 0.001f) {
            const float k = std::max(1.0f, std::round(p.resonanceHarmonic));
            const float fenetre = 1.0f - phase_;                 // dent de scie
            const float rapide = std::sin(static_cast<float>(vsm::audio::dsp::kTwoPi)
                                          * phase_ * k);
            sortie = sinus * (1.0f - p.resonance) + rapide * fenetre * p.resonance;
        }

        return sortie * ampEnv_.nextSample() * (0.35f + 0.65f * vel);
    }

private:
    double sampleRate_ = 48000.0;
    vsm::audio::dsp::AdsrEnvelope ampEnv_, modEnv_;
    vsm::audio::dsp::AnalogDrift drift_;
    float phase_ = 0.0f, baseHz_ = 261.6f;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class PhaseDistSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kAmount = 1, kEnvToAmount, kResonance, kResonanceHarmonic,
        kModAttack, kModDecay, kModSustain, kModRelease,
        kAmpAttack, kAmpDecay, kAmpSustain, kAmpRelease,
        kVelocityToAmount, kAnalogCharacter, kOutputLevel,
    };

    PhaseDistSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Phase Distortion (le temps déformé)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<PhaseDistVoice, kMaxVoices> voiceManager_;
};

} // namespace vsm::plugins::phasedist
