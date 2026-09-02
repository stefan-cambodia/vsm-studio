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

namespace vsm::plugins::plate {

/// LA PLAQUE — gong, tam-tam, cymbale à archet : le seul objet du parc dont la
/// brillance MONTE après la frappe.
///
/// POURQUOI CETTE MACHINE, ALORS QUE LE PARC A DÉJÀ DES CYMBALES. `vsm.drums`
/// rend les siennes « par un cluster de partiels aux rapports irrationnels
/// plus du bruit filtré » — c'est écrit dans son en-tête, et c'est bon pour un
/// kit, où la cymbale dure une seconde et ponctue. Mais ce cluster est
/// STATIQUE : chaque partiel décroît pour son compte, donc le son ne peut que
/// s'assombrir. C'est vrai de TOUT ce que contient le parc — `vsm.modal`,
/// `vsm.membrane`, `vsm.perc` ont des modes indépendants, et aucun chemin ne
/// mène de l'énergie d'un mode grave vers un mode aigu.
///
/// **Un tam-tam fait exactement le contraire.** Frappé fort, il est d'abord
/// sourd, puis sa brillance MONTE pendant plusieurs secondes avant de
/// retomber. Le mécanisme est un couplage NON LINÉAIRE entre modes : aux
/// grandes amplitudes, la flexion de la plaque convertit l'énergie des modes
/// bas vers les modes hauts (Rossing et Fletcher). C'est une différence de
/// STRUCTURE, pas de paramétrage — et c'est pour cela qu'une machine.
///
/// ```
///   frappe ──> modes bas ──┐
///                          │ transfert QUADRATIQUE (donc dépendant de la force)
///                          v
///                      modes hauts ──> le son s'ÉCLAIRCIT en durant
/// ```
///
/// LE TRAIT A DEUX MOITIÉS, et la seconde compte autant que la première :
/// **la montée dépend de la FORCE**. À frappe faible, le transfert est
/// négligeable et la plaque s'assombrit comme n'importe quel objet. Sans cette
/// moitié-là, un simple filtre qui s'ouvrirait avec le temps ferait illusion —
/// mais il ferait la même chose à toutes les nuances, ce qui n'est pas un
/// instrument, c'est un effet.
///
/// APPROXIMATIONS ASSUMÉES (§ 8), statut « inspiré » : le transfert est
/// modélisé comme une fuite quadratique d'un mode vers le suivant, et non par
/// les équations de von Kármán d'une plaque en grand déplacement. Les rapports
/// de modes sont ceux d'une plaque circulaire libre (Rayleigh), tronqués à
/// seize ; la densité modale réelle d'un tam-tam est bien plus grande, et
/// c'est ce qui lui donne son côté « bruit accordé » que cette machine n'a
/// qu'en partie.
class PlateVoice {
public:
    static constexpr int kModes = 16;

    /// Rapports d'une plaque circulaire libre, rapportés au premier mode.
    /// Ils ne sont ni harmoniques (corde) ni ceux d'une membrane (Bessel) :
    /// une plaque a de la RAIDEUR, et sa raideur écarte les modes hauts.
    static constexpr std::array<float, kModes> kRatios{
        1.000f, 1.730f, 2.328f, 3.910f, 4.107f, 5.343f, 6.301f, 7.032f,
        8.117f, 9.045f, 10.31f, 11.42f, 12.09f, 13.71f, 14.88f, 16.05f,
    };

    struct Params {
        float coupling = 0.6f;        // force du transfert non linéaire
        float decay = 6.0f;           // t60 du premier mode
        float decayTilt = 0.6f;       // combien les modes hauts meurent plus vite
        float strikeHardness = 0.4f;  // quels modes la frappe éveille
        float cutoff = 14000.0f;
        float velocitySensitivity = 0.6f;
        float bendSemitones = 0.0f;
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        filtre_.setSampleRate(sampleRate_);
        filtre_.reset();
        rng_ = vsm::util::DeterministicRng(seed);
        for (auto& m : modes_) m = Mode{};
        active_ = false;
    }

    bool isActive() const { return active_; }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel;
        note_ = note;
        velocity_ = velocity;
        active_ = true;
        frappeEnAttente_ = true;
    }
    /// Une plaque frappée sonne jusqu'au bout : le relâchement ne veut rien
    /// dire pour elle (l'étouffer de la main est un autre geste).
    void noteOff(uint8_t) {}

    float render(const Params& p) {
        if (!active_) return 0.0f;
        if (frappeEnAttente_) {
            frapper(p);
            frappeEnAttente_ = false;
        }

        float somme = 0.0f, reste = 0.0f;
        for (int i = 0; i < kModes; ++i) {
            auto& mode = modes_[static_cast<size_t>(i)];
            somme += static_cast<float>(std::sin(mode.phase)) * mode.amplitude;
            mode.phase += mode.increment;
            if (mode.phase > vsm::audio::dsp::kTwoPi) mode.phase -= vsm::audio::dsp::kTwoPi;
            mode.amplitude *= mode.damping;
            reste += mode.amplitude;
        }

        // --- LE TRANSFERT, ET C'EST TOUTE LA MACHINE -------------------------
        // Une fraction de l'énergie de chaque mode passe au suivant, et cette
        // fraction est PROPORTIONNELLE À SON AMPLITUDE : c'est ce qui la rend
        // non linéaire, et c'est ce qui fait que le phénomène n'existe qu'aux
        // fortes frappes. Un transfert linéaire donnerait le même éclaircis-
        // sement à toutes les nuances, ce qui serait un effet et non un
        // instrument.
        //
        // Décimé, parce qu'il n'a pas besoin d'être précis : le transfert se
        // compte en dixièmes de seconde, pas en échantillons, et le faire à
        // chaque trame coûterait seize multiplications pour rien.
        if (++compteur_ >= kDecimation) {
            compteur_ = 0;
            const float k = 0.06f * std::clamp(p.coupling, 0.0f, 1.0f);
            for (int i = kModes - 2; i >= 0; --i) {
                auto& bas = modes_[static_cast<size_t>(i)];
                auto& haut = modes_[static_cast<size_t>(i + 1)];
                const float fuite = k * bas.amplitude * bas.amplitude;
                bas.amplitude -= fuite;
                haut.amplitude += fuite;
            }
        }

        if (reste < 1e-4f) active_ = false;
        filtre_.setCutoffHz(p.cutoff);
        filtre_.setResonance(0.05f);
        return filtre_.process(somme);
    }

private:
    /// Un pas de transfert tous les 64 échantillons : 750 Hz à 48 kHz, très
    /// au-dessus des dixièmes de seconde du phénomène.
    static constexpr int kDecimation = 64;

    struct Mode {
        double phase = 0.0;
        double increment = 0.0;
        float amplitude = 0.0f;
        float damping = 0.999f;
    };

    void frapper(const Params& p) {
        const float velocity = static_cast<float>(velocity_) / 127.0f;
        const float f0 = 440.0f * std::exp2f(
            (static_cast<float>(note_) + p.bendSemitones - 69.0f) / 12.0f);
        const float force = 1.0f - p.velocitySensitivity * (1.0f - velocity);

        for (int i = 0; i < kModes; ++i) {
            auto& mode = modes_[static_cast<size_t>(i)];
            const float ratio = kRatios[static_cast<size_t>(i)];
            const float hz = f0 * ratio;
            if (hz > static_cast<float>(sampleRate_) * 0.45f) {
                mode.amplitude = 0.0f;
                mode.increment = 0.0;
                continue;
            }
            // UNE FRAPPE ÉVEILLE SURTOUT LES MODES BAS, et d'autant plus
            // exclusivement que le maillet est mou. C'est ce qui laisse au
            // transfert quelque chose à faire : si la frappe remplissait déjà
            // les modes hauts, il n'y aurait pas de montée à entendre.
            const float pente = 1.2f + 3.0f * (1.0f - std::clamp(p.strikeHardness, 0.0f, 1.0f));
            mode.amplitude = force * std::pow(ratio, -pente);
            mode.phase = 0.0;
            mode.increment = static_cast<double>(hz) * vsm::audio::dsp::kTwoPi / sampleRate_;
            const float tau = std::max(0.05f, p.decay)
                            / std::pow(ratio, std::max(0.0f, p.decayTilt));
            mode.damping = std::exp(-1.0f / (tau * static_cast<float>(sampleRate_)));
        }
        compteur_ = 0;
    }

    double sampleRate_ = 48000.0;
    std::array<Mode, kModes> modes_{};
    vsm::audio::dsp::StateVariableFilter filtre_;
    vsm::util::DeterministicRng rng_{0x504C4154ULL};   // "PLAT"
    int compteur_ = 0;
    bool active_ = false, frappeEnAttente_ = false;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class PlateSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 6;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kCoupling = 1, kDecay, kDecayTilt, kStrikeHardness,
        kCutoff, kVelocitySensitivity, kOutputLevel,
    };

    PlateSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override {
        // Comme `vsm.modal` et `vsm.membrane` : la molette agit sur les frappes
        // À VENIR. Une plaque déjà frappée garde ses modes.
        if (event.kind == vsm::audio::plugin::MidiControlEvent::Kind::PitchBend) {
            bendSemitones_.store(event.value, std::memory_order_relaxed);
            return true;
        }
        return false;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Plate (le gong qui s'éclaircit)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<PlateVoice, kMaxVoices> voiceManager_;
    std::atomic<float> bendSemitones_{0.0f};
};

} // namespace vsm::plugins::plate
