#pragma once
#include "vsm/audio/dsp/Biquad.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DecayEnvelope.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace vsm::plugins::drums {

using DecayEnv = vsm::audio::dsp::DecayEnvelope;

/// Batterie ACOUSTIQUE — peaux et métal modélisés.
///
/// POURQUOI CETTE MACHINE, ALORS QUE LE PARC A DÉJÀ DEUX BOÎTES À RYTHMES.
/// La TR-808 et la TR-909 synthétisent des percussions ÉLECTRONIQUES : leur
/// grosse caisse est une sinusoïde balayée, et c'est exactement ce qu'on leur
/// demande. Une batterie acoustique n'est pas cela. Jusqu'ici elle passait par
/// `vsm.sampler`, qui rejouait les coups découpés du morceau — une réponse
/// honnête, mais qui n'est plus disponible : le sampler est désormais réservé
/// à la voix. Le stem `drums` d'un enregistrement réel n'avait donc plus
/// aucune machine cible, et c'est le trou que celle-ci comble.
///
/// CE QUI SÉPARE UNE PEAU FRAPPÉE D'UNE SINUSOÏDE
///
///  1. **Les modes d'une membrane sont INHARMONIQUES.** Une corde vibre sur
///     des multiples entiers ; une peau circulaire vibre sur les zéros de la
///     fonction de Bessel — 1,000 / 1,593 / 2,135 / 2,295 / 2,917... Ces
///     rapports irrationnels sont ce qu'on entend comme « peau » plutôt que
///     comme « note ». C'est LE trait de cette machine, et un banc modal le
///     produit directement au lieu de l'imiter par un filtre.
///  2. **Les modes hauts meurent les premiers.** L'air et l'amortissement de
///     la peau les emportent bien avant le fondamental : un coup de tom est
///     riche pendant 30 ms puis devient un bourdonnement. Chaque mode a donc
///     sa propre décroissance, et non une enveloppe commune.
///  3. **Le timbre bouge avec la force.** Frapper fort excite davantage les
///     modes hauts — comme le marteau du piano, mais par un autre mécanisme
///     (la peau se tend sous le choc). La vélocité agit donc sur l'ÉQUILIBRE
///     DES MODES, pas seulement sur le niveau.
///  4. **Le métal n'est pas une peau.** Une cymbale est un continuum de modes
///     si dense qu'on ne les distingue plus : on le rend par un cluster de
///     partiels aux rapports irrationnels plus du bruit filtré, pas par une
///     sinusoïde.
///  5. **Une batterie est DANS UNE PIÈCE.** C'est ce qui la trahit le plus
///     vite : sans ambiance, le kit le mieux modélisé sonne « électronique ».
///     Une petite pièce fait donc partie de l'instrument, comme le chorus fait
///     partie du Juno et le rotatif de l'orgue.
///
/// APPROXIMATIONS ASSUMÉES (§ 8 de CDC-nouvelle-machine.md, § 27
/// d'ARCHITECTURE.md) — aucune mesure sur un instrument réel, statut
/// « dérivé » :
///
///  - **Modes en nombre fixe** (six au plus) plutôt que calculés depuis une
///    géométrie. Au-delà, les modes d'une vraie peau sont trop serrés pour
///    être distingués et coûteraient sans s'entendre.
///  - **Une seule peau par fût.** Un tom réel en a deux, couplées par l'air,
///    ce qui produit le glissement de hauteur caractéristique. On en garde
///    l'effet audible par une enveloppe de hauteur, sans le mécanisme.
///  - **Pièce à deux peignes et un passe-tout** : assez pour poser un lieu,
///    pas assez pour prétendre être une réverbération. La vraie réverbération
///    du projet reste un effet d'insert.
///  - **Kit mono, pièce stéréo.** Les pièces ne sont pas étalées sur la scène
///    (ce qui demanderait un panoramique par pièce et autant de paramètres) ;
///    la largeur vient de l'ambiance.

/// Banc modal : quelques sinusoïdes inharmoniques, chacune avec sa propre
/// décroissance. C'est la brique commune aux peaux et aux métaux — ce qui les
/// distingue est le CHOIX des rapports et des durées, pas le mécanisme.
class ModalBank {
public:
    static constexpr int kMaxModes = 6;

    void setSampleRate(double sr) { sampleRate_ = sr; }

    void configure(int count, const float* ratios, const float* gains, const float* decays) {
        count_ = count < kMaxModes ? count : kMaxModes;
        for (int i = 0; i < count_; ++i) {
            ratios_[static_cast<size_t>(i)] = ratios[i];
            gains_[static_cast<size_t>(i)] = gains[i];
            decays_[static_cast<size_t>(i)] = decays[i];
        }
    }

    void trigger() {
        for (int i = 0; i < count_; ++i) {
            phase_[static_cast<size_t>(i)] = 0.0f;
            env_[static_cast<size_t>(i)].setSampleRate(sampleRate_);
            env_[static_cast<size_t>(i)].setDecaySeconds(decays_[static_cast<size_t>(i)]);
            env_[static_cast<size_t>(i)].trigger();
        }
    }

    bool isActive() const {
        for (int i = 0; i < count_; ++i)
            if (env_[static_cast<size_t>(i)].isActive()) return true;
        return false;
    }

    /// Coupe le banc en quelques millisecondes. Indispensable au groupe de
    /// coupure : étouffer une charleston ouverte en ne coupant que son bruit
    /// laisserait son CLUSTER MÉTALLIQUE sonner par-dessus la fermée -- ce que
    /// deux positions d'un même instrument ne peuvent pas faire. Le défaut a
    /// été trouvé par la mesure (0,43 d'énergie restante au lieu des 0,05
    /// attendus), pas par lecture du code.
    void choke() {
        for (int i = 0; i < count_; ++i) env_[static_cast<size_t>(i)].choke();
    }

    /// `pitchScale` permet la légère chute de hauteur d'une peau frappée.
    float render(float baseHz, float pitchScale, float brightness) {
        float sum = 0.0f;
        for (int i = 0; i < count_; ++i) {
            const size_t k = static_cast<size_t>(i);
            if (!env_[k].isActive()) continue;
            const float hz = baseHz * ratios_[k] * pitchScale;
            phase_[k] += static_cast<float>(vsm::audio::dsp::kTwoPi) * hz / static_cast<float>(sampleRate_);
            if (phase_[k] > static_cast<float>(vsm::audio::dsp::kTwoPi))
                phase_[k] -= static_cast<float>(vsm::audio::dsp::kTwoPi);
            // Les modes hauts ne sortent vraiment qu'en frappe forte : c'est
            // ainsi que la vélocité change le TIMBRE et pas seulement le
            // niveau.
            const float weight = (i == 0) ? 1.0f : (0.25f + 0.75f * brightness);
            sum += std::sin(phase_[k]) * gains_[k] * weight * env_[k].next();
        }
        return sum;
    }

private:
    double sampleRate_ = 48000.0;
    int count_ = 0;
    std::array<float, kMaxModes> ratios_{}, gains_{}, decays_{}, phase_{};
    std::array<DecayEnv, kMaxModes> env_{};
};

class DrumsSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    enum ParamIds : vsm::audio::plugin::ParamId {
        kKickLevel = 0, kKickTune, kKickDecay, kKickBeater,
        kSnareLevel, kSnareTune, kSnareDecay, kSnareWires,
        kTomLevel, kTomTune, kTomDecay,
        kClosedHatLevel, kClosedHatDecay,
        kOpenHatLevel, kOpenHatDecay,
        kRideLevel, kRideDecay,
        kCrashLevel, kCrashDecay,
        kRoomLevel, kRoomSize,
        kOutputLevel,
        kNumParams
    };

    /// Notes de déclenchement : la convention General MIDI de la batterie,
    /// celle que produit toute transcription et que lit tout séquenceur.
    enum Note : uint8_t {
        kNoteKick = 36, kNoteSnare = 38,
        kNoteLowTom = 41, kNoteMidTom = 45, kNoteHighTom = 48,
        kNoteClosedHat = 42, kNotePedalHat = 44, kNoteOpenHat = 46,
        kNoteCrash = 49, kNoteRide = 51
    };

    DrumsSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Drums (batterie acoustique)"; }
    int activeVoiceCount() const override;

private:
    struct Membrane {
        ModalBank modes;
        DecayEnv pitchDrop, transient;
        vsm::audio::dsp::StateVariableFilter transientFilter;
        vsm::util::DeterministicRng rng{0x4452554DULL};
        float velocity = 1.0f;
        bool triggered = false;
    };

    struct Metal {
        ModalBank modes;
        DecayEnv noiseEnv;
        vsm::audio::dsp::StateVariableFilter noiseFilter;
        vsm::util::DeterministicRng rng{0x4D4554414CULL};
        float velocity = 1.0f;
    };

    void trigger(uint8_t note, uint8_t velocity);
    float renderMembrane(Membrane& piece, float baseHz, float pitchDepth,
                         float transientLevel, float transientHz, float unused);
    float renderMetal(Metal& piece, float baseHz, float noiseHz);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kNumParams> params_{};

    Membrane kick_{}, snare_{}, tom_{};
    Metal closedHat_{}, openHat_{}, ride_{}, crash_{};
    DecayEnv snareWires_{};
    vsm::audio::dsp::StateVariableFilter wireFilter_{};
    vsm::util::DeterministicRng wireRng_{0x574952455AULL};
    float tomHz_ = 120.0f;

    // Pièce : deux peignes et un passe-tout, un par oreille pour le premier
    // peigne afin d'obtenir de la largeur sans décorréler la frappe elle-même.
    std::vector<float> combL_, combR_, allpass_;
    size_t combIndexL_ = 0, combIndexR_ = 0, allpassIndex_ = 0;
    size_t combLenL_ = 1, combLenR_ = 1, allpassLen_ = 1;
};

} // namespace vsm::plugins::drums
