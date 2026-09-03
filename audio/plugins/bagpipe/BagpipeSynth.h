#pragma once
#include "../cone/ConeSynth.h"
#include "../wind/WindSynth.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::bagpipe {

/// LA CORNEMUSE — la RÉSERVE D'AIR qui interdit le silence.
///
/// POURQUOI CETTE MACHINE, ET POURQUOI ELLE N'EST PAS `vsm.cone` AVEC DES
/// BOURDONS. Tous les vents du parc obéissent au souffle : une note commence
/// quand on souffle et finit quand on s'arrête, avec son attaque et sa chute.
/// La cornemuse met un SAC entre le souffle et les anches, et ce sac change
/// tout ce qu'un clavier peut lui faire :
///
///  1. **IL N'Y A PAS DE SILENCE ENTRE DEUX NOTES.** Le chalumeau sonne tant
///     que le sac a de l'air : lâcher une touche ne tait rien, la note tenue
///     reste jusqu'à la suivante. Un vent ordinaire, sur le même geste,
///     retombe en un dixième de seconde. Mesuré : le niveau dans un trou de
///     150 ms entre deux notes.
///  2. **UNE NOTE RÉPÉTÉE EXIGE UNE NOTE DE GRÂCE.** Puisque le son ne
///     s'arrête jamais, deux notes identiques à la suite ne peuvent être
///     séparées que par une autre note, très brève — c'est toute la technique
///     du sonneur, et la machine la fait d'elle-même : rejouer la note qui
///     sonne insère le la aigu du chalumeau pendant quelques dizaines de
///     millisecondes. Mesuré : l'énergie au la aigu dans la fenêtre qui suit
///     la seconde frappe.
///  3. **LE SAC SE VIDE, ET TOUT BAISSE ENSEMBLE.** Toutes les anches boivent
///     au même sac : quand le bras cesse de presser, la pression tombe, les
///     tuyaux se détendent (la hauteur baisse — les cornemuses « s'affaissent »
///     à la coupure) et se taisent ensemble, le chalumeau d'abord (son anche
///     demande plus), les bourdons après. Mesuré : la tenue après le
///     relâchement, puis l'extinction, puis la hauteur pendant la coupure.
///  4. **PAS DE NUANCE.** Une cornemuse ne joue ni fort ni doux : la
///     vélocité est ignorée au bit près, comme sur le clavecin. Pas de
///     molette non plus — les doigts bouchent des trous.
///
/// ```
///   touches ──> CHALUMEAU (perce CONIQUE de vsm.cone, anche battante) ─┐
///                       ^ note de grâce automatique                    │
///   SAC : pression ─────┼─────────────────────────────────────────────>├──> Σ
///     monte à la frappe │                                              │
///     tient (réserve)   └──> 3 BOURDONS (perces CYLINDRIQUES de vsm.wind) ┘
///     tombe (coupure)          la, la (ténors), la grave (basse)
/// ```
///
/// Le chalumeau est la perce conique de `vsm.cone` (tous les rangs), les
/// bourdons les perces cylindriques de `vsm.wind` (rangs impairs, le son de
/// bourdon) — réemployés tels quels ; ce que cette machine ajoute, c'est le
/// sac, et tout ce qu'il impose.
///
/// APPROXIMATIONS ASSUMÉES (§ 8), statut « dérivé » : la note de grâce ne
/// s'insère qu'entre deux notes IDENTIQUES (un sonneur en met partout, mais
/// c'est là qu'elle est obligatoire) ; le sac est un simple réservoir à
/// trois temps (montée, réserve, chute), sans la pression du bras modulée ;
/// l'affaissement de hauteur à la coupure est posé (4 % à pression nulle),
/// non mesuré sur un instrument ; les bourdons ne se désaccordent pas entre
/// eux.
class Pipe {
public:
    enum class Shape { Conical, Cylindrical };

    void prepare(double sampleRate, Shape shape, uint64_t seed) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        shape_ = shape;
        cone_.prepare(sampleRate_, 25.0f);
        cyl_.prepare(sampleRate_, 25.0f);
        rng_ = vsm::util::DeterministicRng(seed);
        reset();
    }
    void reset() {
        cone_.reset();
        cyl_.reset();
        driveRamp_ = noiseLp_ = dcX1_ = dcY1_ = 0.0f;
        souffleLisse_ = 0.0f;
    }
    void setTuning(float hz, float bellDamping) {
        if (shape_ == Shape::Conical) cone_.setTuning(hz, bellDamping);
        else cyl_.setTuning(hz, bellDamping);
    }

    /// Un échantillon, à la pression que le SAC lui donne (0..1).
    float render(float bagPressure, float stiffness, float brassiness, float noise) {
        // L'anche voit la pression du sac, lissée : un sac n'a pas de front
        // raide, mais la lecture par bloc en aurait un.
        souffleLisse_ += 0.002f * (0.70f * std::clamp(bagPressure, 0.0f, 1.0f) - souffleLisse_);
        noiseLp_ += 0.35f * (rng_.nextBipolar() - noiseLp_);
        const float breath = souffleLisse_ * (1.0f + noise * noiseLp_ * 0.6f);

        // L'ANCHE NE PARLE PAS SANS SOUFFLE : sous un dixième de souffle
        // plein, la régénération s'éteint et la boucle repasse sous 1 —
        // c'est ce qui permet au sac vide de faire taire les tuyaux (le
        // même seuil est posé dans `vsm.cone`, dont la cornemuse a révélé
        // que la note relâchée ne s'éteignait jamais).
        const float parole = 0.45f + 0.55f * std::clamp(souffleLisse_ / 0.10f, 0.0f, 1.0f);
        float pressure;
        if (shape_ == Shape::Conical) {
            const float returning = cone_.returning() * parole;
            const float difference = returning - breath;
            const float slope = -(0.25f + 0.40f * std::clamp(stiffness, 0.0f, 1.0f));
            const float reed = std::clamp(0.7f + slope * difference, -1.0f, 1.0f);
            pressure = breath + difference * reed;
            // Le limiteur asymétrique de `vsm.cone` : c'est lui qui donne
            // le rang pair du chalumeau (mêmes constantes).
            driveRamp_ += kDriveRampCoeff * (souffleLisse_ - driveRamp_);
            const float drive = 1.0f + 0.3f + std::min(1.5f, 6.0f * brassiness * driveRamp_);
            pressure = std::tanh((pressure - 1.2f * pressure * pressure) * drive) / std::sqrt(drive);
            cone_.inject(pressure);
        } else {
            const float returning = cyl_.returning() * parole;
            const float difference = returning - breath;
            const float slope = -(0.10f + 0.55f * std::clamp(stiffness, 0.0f, 1.0f));
            const float reed = std::clamp(0.7f + slope * difference, -1.0f, 1.0f);
            pressure = breath + difference * reed;
            if (brassiness > 0.0f) {
                const float drive = 1.0f + 6.0f * brassiness * souffleLisse_;
                pressure = std::tanh(pressure * drive) / std::sqrt(drive);
            }
            cyl_.inject(pressure);
        }
        const float out = pressure - dcX1_ + 0.9995f * dcY1_;
        dcX1_ = pressure;
        dcY1_ = out;
        return out;
    }

private:
    static constexpr float kDriveRampCoeff = 1.0f / (0.15f * 48000.0f);
    double sampleRate_ = 48000.0;
    Shape shape_ = Shape::Conical;
    vsm::plugins::cone::ConicalBore cone_{};
    vsm::plugins::wind::Bore cyl_{};
    vsm::util::DeterministicRng rng_{0x424147ULL};
    float driveRamp_ = 0.0f, noiseLp_ = 0.0f, dcX1_ = 0.0f, dcY1_ = 0.0f, souffleLisse_ = 0.0f;
};

class BagpipeSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    enum ParamIds : vsm::audio::plugin::ParamId {
        kDroneNote = 1, kDrones, kBagReserve, kStrikeIn, kCutOff, kGraceLength,
        kReedStiffness, kBrassiness, kBreathNoise, kBellDamping, kOutputLevel,
    };
    static constexpr int kHeldMax = 16;

    BagpipeSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Bagpipe (la réserve d'air)"; }
    int activeVoiceCount() const override { return sounding_ ? 1 : 0; }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);
    void retune(float bagPressure);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};

    Pipe chanter_, tenor1_, tenor2_, bass_;
    // LE SAC.
    float bag_ = 0.0f;
    int sinceRelease_ = 0;          // échantillons depuis la dernière touche lâchée
    bool anyHeld_ = false, sounding_ = false;
    // LES DOIGTS : pile des touches tenues, la dernière commande.
    std::array<uint8_t, kHeldMax> held_{};
    int heldCount_ = 0;
    uint8_t chanterNote_ = 64;
    // LA NOTE DE GRÂCE.
    int graceRemaining_ = 0;
    float tunedBag_ = -1.0f;
    uint8_t tunedNote_ = 0, tunedDrone_ = 0;
    float tunedBell_ = -1.0f;
    bool tunedGrace_ = false;
};

} // namespace vsm::plugins::bagpipe
