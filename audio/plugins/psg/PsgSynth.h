#pragma once
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace vsm::plugins::psg {

/// GÉNÉRATEUR DE SON PROGRAMMABLE — le son des puces 8 bits, et la seule
/// machine du parc dont la JUSTESSE soit limitée par construction.
///
/// POURQUOI CETTE MACHINE. Toutes les autres accordent une note en calculant sa
/// fréquence en nombre flottant : elles sont justes au millième de cent près, et
/// aucune ne peut être autre chose. Une puce sonore des années 1980 -- AY-3-8910,
/// SID, la puce du NES -- ne sait pas faire ça. Elle possède une horloge fixe et
/// un COMPTEUR ENTIER : la seule chose qu'on puisse lui écrire est une période,
/// en nombre entier de cycles d'horloge. La fréquence obtenue vaut donc
/// `horloge / (16 x periode)`, et il n'existe presque jamais d'entier qui tombe
/// juste sur la note demandée.
///
/// C'EST CE DÉFAUT QUI FAIT LE SON, et c'est le trait distinctif : dans l'aigu,
/// les périodes disponibles sont si courtes que deux notes voisines peuvent
/// tomber sur le même entier -- ou s'écarter d'un quart de ton. Ce n'est pas une
/// approximation de modélisation, c'est le comportement de l'objet, et un
/// musicien de cette époque composait AVEC.
///
/// LE TEST LE VÉRIFIE EN DEUX MOITIÉS, et il faut les deux. D'abord que l'erreur
/// de justesse EXISTE et suit exactement la formule : la fréquence rendue est
/// `horloge / (16 x round(horloge / (16 x f)))`, au hertz près, et pas la
/// fréquence tempérée. Ensuite qu'elle DIMINUE quand on accélère l'horloge --
/// sans quoi on aurait pu mesurer n'importe quel désaccord et l'appeler
/// quantification.
///
/// LE SECOND TRAIT : L'AMPLITUDE EST QUANTIFIÉE ELLE AUSSI. Ces puces avaient
/// quatre bits de volume, soit seize marches. Une note tenue ne peut donc
/// prendre qu'un nombre FINI de valeurs, et le test les compte : au plus
/// `2^bits` niveaux distincts. Aucune autre machine du parc n'a de sortie
/// dénombrable.
///
/// LE BRUIT EST PÉRIODIQUE, et c'est le troisième détail qui trahit ces puces :
/// il sort d'un registre à décalage bouclé, donc il se RÉPÈTE. À registre court,
/// on entend une hauteur dans le bruit -- c'est le « periodic noise » dont les
/// musiciens se servaient comme d'une percussion accordable.
///
/// APPROXIMATIONS ASSUMÉES (§ 8 de `CDC-nouvelle-machine.md`), statut
/// « dérivé », aucune mesure sur une puce réelle :
///
///  - **Trois voix carrées et une de bruit**, comme l'AY-3-8910. Le SID en a
///    trois avec un filtre analogique, la puce du NES a un canal triangle et un
///    canal PCM : les modéliser toutes demanderait trois machines, et ce qu'elles
///    ont en COMMUN -- l'horloge entière et le volume à quatre bits -- est ce qui
///    les fait reconnaître.
///  - **Pas de filtre**, donc pas de SID : son filtre analogique est justement
///    ce qui le distingue des autres puces, et il relève du soustractif, que le
///    parc couvre dix fois.
///  - **Le rendu n'est pas suréchantillonné.** Une onde carrée à fronts francs
///    replie ; ces puces repliaient aussi, et c'est une part de leur son. Le
///    choix est assumé plutôt que subi.

class PsgVoice {
public:
    static constexpr int kSquares = 3;

    struct Params {
        float clockHz = 1789773.0f;   // l'horloge : c'est elle qui quantifie
        float pulseWidth = 0.5f;      // rapport cyclique des voix carrées
        float detune = 0.0f;          // désaccord des voix 2 et 3, en cents
        float noiseLevel = 0.0f;
        float noisePeriod = 32.0f;    // longueur du cycle de bruit
        float bits = 4.0f;            // marches de volume
        int voices = 1;               // combien de voix carrées sonnent
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate;
        env_.setSampleRate(sampleRate);
        rng_ = vsm::util::DeterministicRng{seed};
        for (auto& p : phases_) p = 0.0f;
        lfsr_ = 0x7FFFu;
        compteurBruit_ = 0.0f;
        bruit_ = 0.0f;
    }

    bool isActive() const { return env_.isActive(); }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void setSettings(const vsm::audio::dsp::AdsrSettings& s) { env_.setSettings(s); }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel;
        note_ = note;
        velocity_ = velocity;
        demandeHz_ = 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
        env_.noteOn();
        for (auto& p : phases_) p = 0.0f;
    }

    void noteOff(uint8_t) { env_.noteOff(); }

    /// LA FRÉQUENCE QU'UNE PUCE PEUT RÉELLEMENT PRODUIRE, et rien d'autre.
    /// On écrit une période ENTIÈRE dans un compteur, la fréquence en découle.
    /// C'est une fonction publique parce que le test s'en sert pour prédire ce
    /// qu'il doit mesurer : une machine dont on peut calculer l'erreur d'avance
    /// est une machine dont le défaut est un MODÈLE, pas un accident.
    static float quantifier(float demandeHz, float clockHz) {
        if (demandeHz <= 0.0f) return 0.0f;
        const float exacte = clockHz / (16.0f * demandeHz);
        const float periode = std::max(1.0f, std::round(exacte));
        return clockHz / (16.0f * periode);
    }

    float render(const Params& p) {
        if (!env_.isActive()) return 0.0f;

        float somme = 0.0f;
        const int n = std::clamp(p.voices, 1, kSquares);
        for (int k = 0; k < n; ++k) {
            // Chaque voix a son propre compteur, donc sa propre quantification :
            // désaccorder de quelques cents peut ne RIEN changer si les deux
            // demandes tombent sur la même période entière. C'est fidèle, et
            // c'est exactement ce qui rendait le désaccord capricieux sur ces
            // puces.
            const float cents = p.detune * static_cast<float>(k);
            const float demande = demandeHz_ * std::pow(2.0f, cents / 1200.0f);
            const float f = quantifier(demande, p.clockHz);
            phases_[static_cast<size_t>(k)] += f / static_cast<float>(sampleRate_);
            if (phases_[static_cast<size_t>(k)] >= 1.0f) phases_[static_cast<size_t>(k)] -= 1.0f;
            somme += (phases_[static_cast<size_t>(k)] < p.pulseWidth) ? 1.0f : -1.0f;
        }
        somme /= static_cast<float>(n);

        if (p.noiseLevel > 0.001f) {
            // REGISTRE À DÉCALAGE BOUCLÉ : le bruit se répète, et sa période est
            // réglable. C'est ce qui fait qu'on entend une HAUTEUR dedans quand
            // elle est courte.
            compteurBruit_ += 1.0f;
            const float pas = std::max(1.0f, p.noisePeriod);
            if (compteurBruit_ >= pas) {
                compteurBruit_ -= pas;
                const uint32_t bit = ((lfsr_ ^ (lfsr_ >> 1)) & 1u);
                lfsr_ = (lfsr_ >> 1) | (bit << 14);
                bruit_ = (lfsr_ & 1u) ? 1.0f : -1.0f;
            }
            somme = somme * (1.0f - p.noiseLevel) + bruit_ * p.noiseLevel;
        }

        // AMPLITUDE QUANTIFIÉE : quatre bits, seize marches. La sortie d'une
        // note tenue ne peut prendre qu'un nombre FINI de valeurs, et c'est
        // dénombrable -- aucune autre machine du parc n'a cette propriété.
        const float niveau = env_.nextSample() * (0.4f + 0.6f * static_cast<float>(velocity_) / 127.0f);
        const float marches = std::pow(2.0f, std::round(std::clamp(p.bits, 1.0f, 16.0f))) - 1.0f;
        const float quantifie = std::round(niveau * marches) / marches;
        return somme * quantifie;
    }

private:
    double sampleRate_ = 48000.0;
    vsm::audio::dsp::AdsrEnvelope env_;
    vsm::util::DeterministicRng rng_{0x50534700ULL};
    std::array<float, kSquares> phases_{};
    uint32_t lfsr_ = 0x7FFFu;
    float compteurBruit_ = 0.0f, bruit_ = 0.0f, demandeHz_ = 440.0f;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class PsgSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 6;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kClock = 1, kPulseWidth, kVoices, kDetune,
        kNoiseLevel, kNoisePeriod, kBits,
        kAttack, kDecay, kSustain, kRelease,
        kOutputLevel,
    };

    PsgSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "PSG (puce 8 bits)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<PsgVoice, kMaxVoices> voiceManager_;
};

} // namespace vsm::plugins::psg
