#pragma once
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/util/DeterministicRng.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace vsm::plugins::tonewheel {

/// Orgue à ROUES PHONIQUES, avec haut-parleur rotatif.
///
/// CE QU'IL A DE RADICALEMENT DIFFÉRENT du reste du parc : il n'y a ni
/// enveloppe, ni filtre, ni oscillateur au sens des autres machines. Un orgue
/// à roues phoniques est un instrument ADDITIF : quatre-vingt-onze roues
/// dentées tournent en permanence devant des bobines, et une touche ne fait
/// que RACCORDER neuf d'entre elles à la sortie, dosées par les tirettes. Il
/// n'y a rien à démarrer et rien à éteindre -- d'où le son qui apparaît et
/// disparaît instantanément, et le célèbre claquement du contact.
///
/// QUATRE TRAITS reproduits délibérément, et pourquoi :
///
///  1. ROUES PARTAGÉES. Les roues tournent pour tout l'instrument, pas par
///     note. Jouer un do et un sol qui partagent une roue ne donne donc PAS
///     deux fois cette roue : le niveau ne s'additionne pas. C'est ce qui
///     rend le tutti d'un orgue moins fort qu'attendu, et c'est audible.
///     Une implémentation « une voix par note » manque ce comportement.
///  2. ROUES À FRÉQUENCES LÉGÈREMENT FAUSSES. Le rapport d'engrenage réel
///     n'est pas exactement celui du tempérament égal ; l'écart est infime
///     mais crée des battements lents entre roues voisines. Sans lui, l'orgue
///     sonne comme un banc d'additif, pas comme une machine.
///  3. CLAQUEMENT DE CONTACT (« key click »). Un défaut d'origine, que les
///     organistes ont adopté comme partie du son : les contacts se ferment
///     l'un après l'autre et produisent un bruit bref. Le supprimer donnerait
///     un orgue propre et mort.
///  4. HAUT-PARLEUR ROTATIF. Ce n'est PAS un effet ajouté : sans lui, cet
///     instrument ne ressemble à rien de ce qu'on connaît. Il module en
///     amplitude, en fréquence (effet Doppler) et en timbre, avec deux
///     rotors -- pavillon aigu et tambour grave -- qui tournent à des vitesses
///     DIFFÉRENTES et accélèrent chacun à son rythme.
///
/// Approximations assumées (§ 27) : les roues sont des sinusoïdes pures, là
/// où les roues réelles ont une forme légèrement irrégulière ; la diaphonie
/// entre roues voisines n'est pas modélisée ; le rotatif est un modèle
/// d'amplitude, de retard et de filtrage, sans acoustique de cabine.
class TonewheelGenerator {
public:
    /// 91 roues, comme l'instrument d'origine.
    static constexpr int kWheelCount = 91;
    /// 9 tirettes : 16', 5⅓', 8', 4', 2⅔', 2', 1⅗', 1⅓', 1'.
    static constexpr int kDrawbarCount = 9;

    void prepare(double sampleRate);
    void reset();

    /// Avance toutes les roues d'un échantillon. À appeler UNE FOIS par
    /// échantillon, avant de lire les roues : elles sont partagées.
    void advance();

    /// Valeur courante d'une roue.
    float wheel(int index) const {
        return (index >= 0 && index < kWheelCount) ? wheelValue_[static_cast<size_t>(index)] : 0.0f;
    }

    /// Numéro de roue pour une note et un rang de tirette. Renvoie -1 si le
    /// rang sort du générateur -- auquel cas l'instrument REPLIE d'une octave,
    /// exactement comme le mécanisme réel (« foldback »), ce qui explique que
    /// les notes extrêmes changent de couleur.
    static int wheelFor(int note, int drawbar);

private:
    double sampleRate_ = 48000.0;
    std::array<double, kWheelCount> phase_{};
    std::array<double, kWheelCount> increment_{};
    std::array<float, kWheelCount> wheelValue_{};
};

/// Haut-parleur rotatif à deux rotors.
class RotarySpeaker {
public:
    void prepare(double sampleRate);
    void reset();

    struct Params {
        bool fast = false;
        float depth = 1.0f;      ///< dosage global de l'effet
        float hornMix = 0.6f;    ///< équilibre pavillon / tambour
    };

    void process(float input, const Params& p, float& outL, float& outR);

private:
    double sampleRate_ = 48000.0;
    double hornPhase_ = 0.0, drumPhase_ = 0.0;
    float hornRate_ = 0.8f, drumRate_ = 0.7f; ///< vitesses courantes, en Hz
    /// Lignes à retard courtes : c'est ce qui produit le décalage Doppler,
    /// et non une simple modulation d'amplitude. Une taille fixe suffit --
    /// le retard maximal est de quelques millisecondes.
    static constexpr size_t kDelayLength = 512;
    std::array<float, kDelayLength> hornDelay_{};
    std::array<float, kDelayLength> drumDelay_{};
    size_t writeIndex_ = 0;
    vsm::audio::dsp::StateVariableFilter crossoverLow_, crossoverHigh_;
};

class TonewheelOrgan : public vsm::audio::plugin::ISynthPlugin {
public:
    /// Polyphonie : l'orgue est un instrument à contacts, pas à voix. On
    /// suit donc les touches ENFONCÉES, sans limite de « voix » au sens des
    /// synthétiseurs -- 16 touches simultanées couvrent les deux mains.
    static constexpr size_t kMaxKeys = 16;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kDrawbar1 = 1, kDrawbar2, kDrawbar3, kDrawbar4, kDrawbar5,
        kDrawbar6, kDrawbar7, kDrawbar8, kDrawbar9,
        kPercussionLevel, kPercussionDecay, kPercussionHarmonic,
        kKeyClick, kVibratoDepth, kVibratoRate,
        kRotaryFast, kRotaryDepth, kRotaryBalance,
        kOverdrive, kOutputLevel,
    };

    TonewheelOrgan();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Tonewheel Organ"; }
    int activeVoiceCount() const override;

private:
    struct Key {
        bool held = false;
        uint8_t note = 0;
        uint8_t channel = 0;
        /// Enveloppe de contact : montée et descente très rapides, quelques
        /// millisecondes. Ce n'est pas une enveloppe musicale, c'est le temps
        /// de fermeture d'un contact -- d'où l'absence de réglage.
        float contact = 0.0f;
        /// Percussion : une seule enveloppe pour tout l'instrument sur la
        /// machine réelle (elle ne se recharge qu'en relâchant TOUTES les
        /// touches), mais suivie par touche ici pour rester simple à lire.
        float percussion = 0.0f;
        float clickEnergy = 0.0f;
    };

    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    TonewheelGenerator generator_;
    RotarySpeaker rotary_;
    std::array<Key, kMaxKeys> keys_{};
    vsm::util::DeterministicRng clickRng_{0x544F4E45ULL};
    double vibratoPhase_ = 0.0;
    /// Vraie percussion d'orgue : elle ne se réarme que lorsque PLUS AUCUNE
    /// touche n'est enfoncée. Jouer legato ne la redéclenche donc pas -- c'est
    /// ce comportement, et non l'enveloppe elle-même, qui fait le phrasé
    /// caractéristique de cet instrument.
    bool percussionArmed_ = true;
};

} // namespace vsm::plugins::tonewheel
