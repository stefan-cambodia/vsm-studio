#pragma once
#include "vsm/audio/dsp/AnalogDrift.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/dsp/MorphOscillator.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::cs80 {

/// CS-80-style — DEUX COUCHES PAR VOIX, et une pression PAR NOTE.
///
/// POURQUOI CETTE MACHINE, ET POURQUOI SEULEMENT MAINTENANT. Le § 9 du CDC
/// machines la tenait en réserve depuis le début, avec sa raison :
/// « Célèbre, magnifique — et très coûteux à faire honnêtement (double couche
/// complète, sensibilité polyphonique à la pression, rubans). À garder pour
/// plus tard, en le faisant bien plutôt qu'à moitié. » Deux des trois
/// obstacles sont tombés le 02/09/2026 : le moteur livre désormais tout le
/// MIDI non-note aux machines (D0.5), pression polyphonique comprise, et le
/// § 10 du CDC nouvelle-machine a fixé la doctrine des contrôleurs. Le
/// troisième — les rubans — est un contrôleur physique, pas du son : il
/// n'entre pas dans une machine logicielle sans matériel pour le porter, et
/// c'est dit ici plutôt que simulé par un potentiomètre de plus.
///
/// CE QUE LE PARC N'AVAIT PAS, ET QUI FAIT CETTE MACHINE :
///
///  1. **DEUX COUCHES COMPLÈTES PAR VOIX.** Chaque touche allume DEUX
///     synthétiseurs — oscillateur, passe-haut, passe-bas, deux enveloppes —
///     dont on dose le mélange. Toutes les autres machines polyphoniques du
///     parc ont une seule chaîne par voix ; empiler deux pistes n'est pas la
///     même chose, car ici les deux couches partagent la MÊME touche, la même
///     enveloppe de pression et le même destin d'allocation.
///  2. **UNE PRESSION PAR NOTE.** Le CS-80 est le premier clavier
///     polyphonique à sentir chaque doigt séparément. Le parc refusait
///     jusqu'ici la pression polyphonique EN LE DISANT (§ 10 : « le parc n'a
///     pas de modulation par-voix, l'honorer à moitié mentirait ») — cette
///     machine est la première à pouvoir l'honorer vraiment : la pression sur
///     une touche ouvre le filtre et pousse le niveau de CETTE voix, et
///     d'elle seule. C'est le trait distinctif, et c'est ce que le test
///     mesure : deux notes tenues, la pression sur une seule, et le spectre
///     de l'autre ne bouge pas.
///
/// APPROXIMATIONS ASSUMÉES (§ 8 du CDC nouvelle-machine), statut « dérivé »,
/// aucune mesure sur un CS-80 réel :
///
///  - **Pas de ruban**, on vient de le dire.
///  - **Le filtre est le passe-haut + passe-bas à variable d'état du parc.**
///    Le CS-80 a un HPF et un LPF résonants par couche, ce que cette
///    structure donne ; leur topologie exacte (SM-style discret) n'est pas
///    reproduite.
///  - **Sub-oscillateur et modulation en anneau écartés** : la machine a déjà
///    deux couches, et le § 0 du CDC demande de ne pas empiler des fonctions
///    qu'aucune mesure ne réclame.
///  - **L'enveloppe de pression est un lissage exponentiel** (30 ms), pas la
///    mécanique du capteur.
class Cs80Voice {
public:
    /// Une COUCHE : oscillateur morphable, passe-haut, passe-bas résonant,
    /// deux enveloppes (filtre et amplitude). C'est un synthétiseur complet,
    /// et il y en a deux par touche.
    struct LayerParams {
        float shape = 2.0f;          // 0..3, continu (le CS-80 a scie/carré/PWM)
        float detune = 0.0f;         // demi-tons
        float pulseWidth = 0.5f;
        float highPass = 20.0f;      // Hz
        float cutoff = 4000.0f, resonance = 0.2f;
        float envAmount = 0.4f;
        float level = 1.0f;
    };

    struct Params {
        std::array<LayerParams, 2> layer{};
        float layerMix = 0.5f;       // 0 = couche I seule, 1 = couche II seule
        float pressureToCutoff = 1.0f;   // octaves à pleine pression
        float pressureToLevel = 0.3f;
        float velocityToCutoff = 0.5f;
        float velocityToLevel = 0.4f;
        // Molette de hauteur, en demi-tons. À zéro l'addition est exacte :
        // empreinte inchangée au bit.
        float bendSemitones = 0.0f;
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        for (int i = 0; i < 2; ++i) {
            osc_[static_cast<size_t>(i)].setSampleRate(sampleRate_);
            lowPass_[static_cast<size_t>(i)].setSampleRate(sampleRate_);
            highPass_[static_cast<size_t>(i)].setSampleRate(sampleRate_);
            highPass_[static_cast<size_t>(i)].setMode(
                vsm::audio::dsp::StateVariableFilter::Mode::HighPass);
            ampEnv_[static_cast<size_t>(i)].setSampleRate(sampleRate_);
            filterEnv_[static_cast<size_t>(i)].setSampleRate(sampleRate_);
        }
        drift_.setSampleRate(sampleRate_);
        drift_.setSeed(seed);
        drift_.setRateHz(0.1f);
        drift_.setAmount(0.0f);
        // Lissage de la pression : ~30 ms, pour qu'un saut de contrôleur ne
        // s'entende pas comme un clic de filtre.
        pressureCoeff_ = 1.0f - std::exp(-1.0f / (0.03f * static_cast<float>(sampleRate_)));
    }

    bool isActive() const { return ampEnv_[0].isActive() || ampEnv_[1].isActive(); }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void setEnvelopes(int layer, const vsm::audio::dsp::AdsrSettings& amp,
                      const vsm::audio::dsp::AdsrSettings& filter) {
        ampEnv_[static_cast<size_t>(layer)].setSettings(amp);
        filterEnv_[static_cast<size_t>(layer)].setSettings(filter);
    }
    void setDriftAmount(float amount) { drift_.setAmount(amount); }

    /// LA PRESSION DE CETTE VOIX, et d'elle seule. C'est ce qui distingue
    /// cette machine de tout le reste du parc : ailleurs, une pression est
    /// une valeur globale que toutes les voix partagent.
    void setPressure(float value) { pressureTarget_ = std::clamp(value, 0.0f, 1.0f); }
    float pressure() const { return pressureTarget_; }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel;
        note_ = note;
        velocity_ = velocity;
        for (int i = 0; i < 2; ++i) {
            ampEnv_[static_cast<size_t>(i)].noteOn();
            filterEnv_[static_cast<size_t>(i)].noteOn();
            osc_[static_cast<size_t>(i)].reset(0.0);
        }
        // La pression repart de zéro : une touche neuve n'hérite pas de la
        // pression de la note qui occupait la voix avant elle.
        pressureTarget_ = 0.0f;
        pressure_ = 0.0f;
    }
    void noteOff(uint8_t) {
        for (int i = 0; i < 2; ++i) {
            ampEnv_[static_cast<size_t>(i)].noteOff();
            filterEnv_[static_cast<size_t>(i)].noteOff();
        }
    }

    float render(const Params& p) {
        if (!isActive()) return 0.0f;

        pressure_ += pressureCoeff_ * (pressureTarget_ - pressure_);
        const float velocity = static_cast<float>(velocity_) / 127.0f;
        const float driftSemis = drift_.nextValue() * 0.05f;

        float somme = 0.0f;
        for (int i = 0; i < 2; ++i) {
            const size_t s = static_cast<size_t>(i);
            if (!ampEnv_[s].isActive()) continue;
            const auto& couche = p.layer[s];

            const float hz = 440.0f * std::exp2f(
                (static_cast<float>(note_) + couche.detune + driftSemis
                 + p.bendSemitones - 69.0f) / 12.0f);
            osc_[s].setFrequency(hz);
            const float brut = osc_[s].nextSample(couche.shape, couche.pulseWidth);

            highPass_[s].setCutoffHz(std::clamp(couche.highPass, 20.0f,
                                                static_cast<float>(sampleRate_) * 0.45f));
            highPass_[s].setResonance(0.0f);
            const float coupe = highPass_[s].process(brut);

            // LA PRESSION OUVRE LE FILTRE DE CETTE VOIX. C'est le geste du
            // CS-80 : appuyer dans la touche fait chanter la note, sans
            // toucher aux autres.
            const float octaves = couche.envAmount * filterEnv_[s].nextSample() * 4.0f
                                + p.velocityToCutoff * velocity
                                + p.pressureToCutoff * pressure_;
            const float coupure = std::clamp(couche.cutoff * std::exp2f(octaves),
                                             20.0f, static_cast<float>(sampleRate_) * 0.45f);
            lowPass_[s].setCutoffHz(coupure);
            lowPass_[s].setResonance(couche.resonance);
            const float filtre = lowPass_[s].process(coupe);

            const float poids = (i == 0) ? (1.0f - p.layerMix) : p.layerMix;
            const float gain = (1.0f - p.velocityToLevel * (1.0f - velocity))
                             * (1.0f + p.pressureToLevel * pressure_);
            somme += filtre * ampEnv_[s].nextSample() * couche.level * poids * gain;
        }
        return somme;
    }

private:
    double sampleRate_ = 48000.0;
    std::array<vsm::audio::dsp::MorphOscillator, 2> osc_{};
    std::array<vsm::audio::dsp::StateVariableFilter, 2> lowPass_{}, highPass_{};
    std::array<vsm::audio::dsp::AdsrEnvelope, 2> ampEnv_{}, filterEnv_{};
    vsm::audio::dsp::AnalogDrift drift_;
    float pressure_ = 0.0f, pressureTarget_ = 0.0f, pressureCoeff_ = 0.001f;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class Cs80Synth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kLayerMix = 1,
        kShapeI, kDetuneI, kPulseWidthI, kHighPassI, kCutoffI, kResonanceI, kEnvAmountI, kLevelI,
        kShapeII, kDetuneII, kPulseWidthII, kHighPassII, kCutoffII, kResonanceII, kEnvAmountII, kLevelII,
        kAmpAttackI, kAmpDecayI, kAmpSustainI, kAmpReleaseI,
        kAmpAttackII, kAmpDecayII, kAmpSustainII, kAmpReleaseII,
        kFilterAttack, kFilterDecay, kFilterSustain, kFilterRelease,
        kPressureToCutoff, kPressureToLevel,
        kVelocityToCutoff, kVelocityToLevel,
        kAnalogCharacter, kOutputLevel,
    };

    Cs80Synth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "CS-80-style (deux couches, pression par note)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<Cs80Voice, kMaxVoices> voiceManager_;
    // Molette de hauteur (demi-tons) et pression de CANAL (0..1). La pression
    // polyphonique, elle, ne passe pas par ici : elle va droit à la voix qui
    // porte la note, ce qui est tout l'objet de cette machine.
    std::atomic<float> bendSemitones_{0.0f};
    std::atomic<float> channelPressure_{0.0f};
};

} // namespace vsm::plugins::cs80
