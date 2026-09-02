#pragma once
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::glass {

/// LE VERRE FROTTÉ — l'harmonica de verre : un son qui met des SECONDES à
/// naître, et qui ne s'arrête pas quand on le lâche.
///
/// POURQUOI CETTE MACHINE, ALORS QUE LE PARC SAIT DÉJÀ FROTTER. `vsm.string`
/// applique la friction de Helmholtz à un GUIDE D'ONDES, et son archet établit
/// le son en quelques dizaines de millisecondes. Un verre frotté du doigt met
/// **une à trois secondes** à parler — c'est ce que tout le monde reconnaît de
/// cet instrument : le son semble venir de nulle part, sans attaque.
///
/// **Ce n'est pas un réglage plus lent, c'est un autre objet.** Un bol de
/// verre est un résonateur à Q très élevé : quelques modes, très peu amortis.
/// L'énergie qu'un décrochement du doigt lui apporte est minuscule devant
/// celle qu'il faut accumuler, et **le temps d'établissement EST la
/// conséquence du Q**. Il n'y a pas de ligne à retard, donc pas de boucle
/// longue à borner : la même raison qui a fait réussir `vsm.reed` là où cinq
/// tentatives de vent avaient échoué (§ 33 et § 44 d'ARCHITECTURE).
///
/// ```
///   doigt (pression, vitesse)
///        │  friction : adhérence puis décrochement
///        v
///   quelques modes de verre, Q très élevé ──> le son MONTE pendant des
///        ^                                     secondes, puis se tient
///        └── la vitesse du mode décide de l'adhérence
/// ```
///
/// TROIS TRAITS, ET LE DEUXIÈME EST CE QUI SÉPARE UN INSTRUMENT D'UNE
/// ENVELOPPE.
///
/// **1. L'établissement est lent** : l'énergie croît encore franchement entre
/// 0,3 s et 1,5 s. Partout ailleurs dans le parc, une machine entretenue a
/// atteint son régime bien avant.
///
/// **2. Il dépend de la PRESSION du doigt** : plus on appuie, plus le verre
/// parle vite. Une enveloppe d'attaque lente mettrait le même temps à toutes
/// les nuances ; ici la dose-réponse est mesurée, et c'est elle qui fait qu'on
/// joue de cet instrument au lieu de le déclencher.
///
/// **3. Lâcher ne coupe pas** : le Q est tel que le verre continue plusieurs
/// secondes. C'est le contraire exact de `vsm.clavichord`, dont le feutre
/// coupe en cinquante millisecondes — le parc mesure désormais les deux
/// extrêmes au même protocole.
///
/// APPROXIMATIONS ASSUMÉES (§ 8), statut « inspiré » : la friction est une
/// courbe empirique adhérence/décrochement appliquée à la vitesse modale, et
/// non le modèle de Coulomb à deux régimes ; les modes du bol sont donnés par
/// des rapports fixes plutôt que calculés d'une géométrie ; et l'eau au bord
/// du verre — qui change réellement la hauteur — n'est pas modélisée.
class GlassVoice {
public:
    static constexpr int kModes = 5;
    /// Les rapports des premiers modes d'un bol de verre : inharmoniques, et
    /// bien plus écartés que ceux d'une corde. C'est ce qui donne au verre son
    /// timbre « pur mais pas droit ».
    static constexpr std::array<float, kModes> kRatios{1.000f, 2.322f, 3.911f, 5.782f, 7.912f};

    struct Params {
        float pressure = 0.5f;       // le doigt : décide de la VITESSE d'établissement
        float speed = 0.5f;          // vitesse de frottement
        float brightness = 0.35f;    // combien les modes hauts reçoivent
        float ring = 6.0f;           // t60 du premier mode, doigt levé
        float velocitySensitivity = 0.3f;
        float bendSemitones = 0.0f;
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        rng_ = vsm::util::DeterministicRng(seed);
        for (auto& m : modes_) m = Mode{};
        active_ = false;
        frotte_ = false;
    }

    bool isActive() const { return active_; }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel;
        note_ = note;
        velocity_ = velocity;
        active_ = true;
        frotte_ = true;
        aAccorder_ = true;
        for (auto& m : modes_) { m.x = 0.0f; m.v = 0.0f; }
        // UNE CHIQUENAUDE MINUSCULE, et elle est nécessaire : un résonateur
        // parfaitement au repos ne démarre pas, la friction n'ayant aucune
        // vitesse à laquelle s'accrocher. Sur l'instrument c'est le grain du
        // doigt ; ici c'est déterministe, donc deux rendus sont identiques au
        // bit près.
        modes_[0].v = 1.0e-4f * (1.0f + rng_.nextUnipolar());
    }
    /// LE DOIGT SE LÈVE, ET LE VERRE CONTINUE : il n'y a rien pour l'arrêter.
    void noteOff(uint8_t) { frotte_ = false; }

    float render(const Params& p) {
        if (!active_) return 0.0f;
        if (aAccorder_) {
            accorder(p);
            aAccorder_ = false;
        }

        const float velocity = static_cast<float>(velocity_) / 127.0f;
        const float appui = std::clamp(
            p.pressure * (1.0f - p.velocitySensitivity * (1.0f - velocity)), 0.0f, 1.0f);
        const float vitesseDoigt = 0.02f + 0.30f * std::clamp(p.speed, 0.0f, 1.0f);

        float somme = 0.0f, reste = 0.0f;
        for (int i = 0; i < kModes; ++i) {
            auto& mode = modes_[static_cast<size_t>(i)];
            if (mode.omega <= 0.0f) continue;

            float force = -mode.omega * mode.omega * mode.x - 2.0f * mode.amortissement * mode.v;

            if (frotte_) {
                // FRICTION : le doigt entraîne le mode tant que leur vitesse
                // relative est faible (adhérence), puis décroche. C'est ce
                // décrochement, répété à chaque période, qui verse l'énergie —
                // et il en verse PEU, d'où les secondes d'établissement.
                const float relative = vitesseDoigt - mode.v;
                const float adherence = 1.0f / (1.0f + 90.0f * relative * relative);
                force += appui * mode.partage * adherence * relative * kCouplageDoigt;
            }

            mode.v += force;
            mode.v = std::clamp(mode.v, -4.0f, 4.0f);
            mode.x = std::clamp(mode.x + mode.v, -4.0f, 4.0f);
            somme += mode.x * mode.partage;
            reste += std::abs(mode.x);
        }

        if (!frotte_ && reste < 1e-5f) active_ = false;
        return somme * kGain;
    }

private:
    static constexpr float kCouplageDoigt = 0.006f;
    /// LE CYCLE LIMITE S'ÉTABLIT VERS ±2 — c'est la friction qui le fixe, pas
    /// un réglage — et il faut donc redescendre la sortie au niveau du reste
    /// du parc. Mesuré : pic 2,04 avant, 0,38 après, ce qui met cette machine
    /// dans la même plage que les autres et évite qu'un projet sature dès
    /// qu'on l'y ajoute.
    static constexpr float kGain = 0.15f;

    struct Mode {
        float x = 0.0f, v = 0.0f;
        float omega = 0.0f;
        float amortissement = 0.0f;
        float partage = 1.0f;
    };

    void accorder(const Params& p) {
        const float f0 = 440.0f * std::exp2f(
            (static_cast<float>(note_) + p.bendSemitones - 69.0f) / 12.0f);
        for (int i = 0; i < kModes; ++i) {
            auto& mode = modes_[static_cast<size_t>(i)];
            const float ratio = kRatios[static_cast<size_t>(i)];
            const float hz = f0 * ratio;
            if (hz > static_cast<float>(sampleRate_) * 0.45f) {
                mode.omega = 0.0f;
                continue;
            }
            mode.omega = 2.0f * static_cast<float>(M_PI) * hz / static_cast<float>(sampleRate_);
            // LE Q TRÈS ÉLEVÉ EST TOUT LE SUJET : c'est lui qui fait à la fois
            // l'établissement lent et la traîne interminable. Un amortissement
            // dix fois plus grand donnerait un bol qui parle vite et se tait
            // vite -- c'est-à-dire une cloche, pas un verre.
            const float tau = std::max(0.2f, p.ring) / ratio;
            mode.amortissement = 1.0f / (tau * static_cast<float>(sampleRate_));
            mode.partage = std::pow(ratio, -(2.2f - 1.6f * std::clamp(p.brightness, 0.0f, 1.0f)));
        }
    }

    double sampleRate_ = 48000.0;
    std::array<Mode, kModes> modes_{};
    vsm::util::DeterministicRng rng_{0x474C4153ULL};   // "GLAS"
    bool active_ = false, frotte_ = false, aAccorder_ = false;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class GlassSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kPressure = 1, kSpeed, kBrightness, kRing,
        kCutoff, kResonance, kVelocitySensitivity, kOutputLevel,
    };

    GlassSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override {
        using Kind = vsm::audio::plugin::MidiControlEvent::Kind;
        if (event.kind == Kind::PitchBend) {
            bendSemitones_.store(event.value, std::memory_order_relaxed);
            return true;
        }
        // LA PRESSION EST LE DOIGT, et sur cet instrument c'est le seul geste :
        // on ne frappe pas un verre, on appuie plus ou moins fort en tournant.
        if (event.kind == Kind::ChannelPressure) {
            pressionDeCanal_.store(std::clamp(event.value, 0.0f, 1.0f), std::memory_order_relaxed);
            return true;
        }
        return false;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Glass (le verre frotté)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<GlassVoice, kMaxVoices> voiceManager_;
    vsm::audio::dsp::StateVariableFilter filtre_;
    std::atomic<float> bendSemitones_{0.0f};
    std::atomic<float> pressionDeCanal_{-1.0f};
};

} // namespace vsm::plugins::glass
