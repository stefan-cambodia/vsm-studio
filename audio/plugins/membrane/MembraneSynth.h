#pragma once
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::membrane {

/// LA PEAU TENDUE — un objet à DEUX dimensions, et le parc n'en avait aucun.
///
/// POURQUOI CETTE MACHINE, ET POURQUOI PAS UN RÉGLAGE DE PLUS SUR `vsm.modal`.
/// La question mérite d'être posée, parce que `vsm.modal` est déjà une banque
/// de résonateurs à rapports libres : ajouter « membrane » à son réglage
/// `Material` semblerait plus économique que d'écrire une machine. **Le calcul
/// dit que c'est impossible**, et il se fait sur le code de cette machine-là,
/// pas sur une intuition.
///
/// `ModalSynth::ratioOf` interpole entre la corde (rapport `n`) et la barre
/// libre-libre (`((2n+1)/3)²`), puis multiplie par `spread^((n-1)/10)` avec
/// `spread` dans [0,5 ; 2]. Pour le SECOND partiel, cela couvre exactement
/// l'intervalle **[1,866 ; 2,978]**. Or une membrane circulaire a son second
/// mode à **1,593** — le rapport des deux premiers zéros de Bessel. Il est
/// hors de portée, quel que soit le réglage, et il en va de même du troisième
/// (2,136 contre un intervalle qui commence à 2,61).
///
/// La raison est structurelle : une corde et une barre sont des objets à UNE
/// dimension, dont les modes s'indexent par un seul entier. Une membrane est à
/// DEUX dimensions, et ses modes s'indexent par un couple `(m, n)` — nombre de
/// diamètres nodaux, nombre de cercles nodaux. Ce n'est pas un point de plus
/// sur le segment corde↔barre, c'est un autre espace.
///
/// TROIS TRAITS, ET LE DEUXIÈME EST UN FAIT ACOUSTIQUE REMARQUABLE.
///
/// **1. Les rapports de Bessel.** Second mode à 1,593·f0 : c'est ce qui fait
/// qu'une timbale sonne « sans note » franche, ses partiels ne formant aucune
/// série harmonique.
///
/// **2. LA CHARGE REND LA MEMBRANE ACCORDABLE — le miracle du tabla.** Le
/// disque de pâte noire collé au centre d'un tabla (le *syahi*) alourdit la
/// peau en son milieu, et déplace les rapports de Bessel vers des ENTIERS :
/// C. V. Raman l'a montré en 1934, et c'est pour cela qu'un tabla joue des
/// notes là où une timbale joue des bruits accordés. Le réglage `Loading` fait
/// ce trajet, et le test le mesure aux deux bouts : 1,59 à charge nulle,
/// 2,00 à charge pleine. Aucune autre machine du parc ne transforme un objet
/// inharmonique en objet harmonique par un seul bouton.
///
/// **3. Frapper au CENTRE ou au BORD ne donne pas le même instrument.** Un
/// mode à `m` diamètres nodaux a un nœud au centre dès que `m ≥ 1` : le frapper
/// au milieu ne l'excite PAS. Le son du centre est donc fait des seuls modes
/// symétriques — c'est le *na* sourd du tabla — et celui du bord contient tout
/// le reste, brillant et chantant. Le test mesure cela sur le second partiel,
/// qui est un mode à un diamètre : présent au bord, absent au centre.
///
/// APPROXIMATIONS ASSUMÉES (§ 8), statut « dérivé » : les douze premiers modes
/// seulement ; l'amplitude d'excitation d'un mode `(m, n)` au rayon `r` devrait
/// suivre `J_m(j_{m,n}·r)` et suit ici une forme simplifiée qui en garde le
/// FAIT essentiel (nœud au centre pour `m ≥ 1`, décroissance vers le bord
/// encastré) sans en avoir la forme exacte ; et la charge est modélisée par le
/// DÉPLACEMENT des rapports, non par la mécanique de la masse ajoutée.
class MembraneVoice {
public:
    static constexpr int kMaxModes = 12;

    /// Les douze premiers modes d'une membrane circulaire encastrée : rapport
    /// au fondamental (zéros de Bessel rapportés à `j_{0,1}`), et nombre `m`
    /// de DIAMÈTRES nodaux — c'est `m` qui décide si le centre est un nœud.
    struct ModeDeBessel { float ratio; int diametres; };
    static constexpr std::array<ModeDeBessel, kMaxModes> kBessel{{
        {1.000f, 0}, {1.593f, 1}, {2.136f, 2}, {2.296f, 0},
        {2.653f, 3}, {2.918f, 1}, {3.156f, 4}, {3.501f, 2},
        {3.600f, 0}, {3.652f, 5}, {4.060f, 3}, {4.154f, 6},
    }};

    struct Params {
        float loading = 0.55f;        // 0 = timbale, 1 = tabla accordé
        float strikeRadius = 0.6f;    // 0 = centre, 1 = bord
        float hardness = 0.5f;
        float decay = 1.2f;
        float decayTilt = 1.0f;
        float modes = 10.0f;
        float velocityToHardness = 0.3f;
        float bendSemitones = 0.0f;
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
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
    /// Relâcher une peau frappée ne veut rien dire : elle sonne jusqu'au bout.
    /// (Étouffer la peau de la main est un autre geste, que le MIDI de note ne
    /// porte pas.)
    void noteOff(uint8_t) {}

    /// Le rapport du mode `i`, entre Bessel et l'entier que la charge vise.
    /// C'est la ligne qui contient le miracle du tabla, et elle tient en une
    /// interpolation : la pâte au centre tire chaque mode vers son rang.
    static float ratioOf(int i, float loading) {
        const float bessel = kBessel[static_cast<size_t>(i)].ratio;
        const float harmonique = static_cast<float>(i + 1);
        return bessel + (harmonique - bessel) * std::clamp(loading, 0.0f, 1.0f);
    }

    /// Le poids d'excitation d'un mode frappé au rayon `r`. FAIT ESSENTIEL :
    /// un mode à `m ≥ 1` diamètres nodaux a un nœud au CENTRE et ne s'excite
    /// pas là ; un mode symétrique (`m = 0`) y est au contraire maximal.
    static float poidsDeFrappe(int diametres, float rayon) {
        const float r = std::clamp(rayon, 0.0f, 0.95f);
        if (diametres == 0)
            return std::cos(r * 1.5708f);                       // max au centre
        const float x = std::min(1.0f, r * static_cast<float>(diametres));
        return std::sin(x * 1.5708f) * (1.0f - 0.35f * r);      // nul au centre
    }

    float render(const Params& p) {
        if (!active_) return 0.0f;
        if (frappeEnAttente_) {
            frapper(p);
            frappeEnAttente_ = false;
        }

        const int compte = std::clamp(static_cast<int>(p.modes + 0.5f), 1, kMaxModes);
        float somme = 0.0f, reste = 0.0f;
        for (int i = 0; i < compte; ++i) {
            auto& mode = modes_[static_cast<size_t>(i)];
            if (mode.amplitude < 1e-5f) continue;
            somme += static_cast<float>(std::sin(mode.phase)) * mode.amplitude;
            mode.phase += mode.increment;
            if (mode.phase > vsm::audio::dsp::kTwoPi) mode.phase -= vsm::audio::dsp::kTwoPi;
            mode.amplitude *= mode.damping;
            reste += mode.amplitude;
        }
        if (reste < 1e-4f) active_ = false;
        return somme;
    }

private:
    struct Mode {
        double phase = 0.0;
        double increment = 0.0;
        float amplitude = 0.0f;
        float damping = 0.999f;
    };

    void frapper(const Params& p) {
        const float velocity = static_cast<float>(velocity_) / 127.0f;
        const float durete = std::clamp(
            p.hardness + p.velocityToHardness * (velocity - 0.5f), 0.0f, 1.0f);
        const float f0 = 440.0f * std::exp2f(
            (static_cast<float>(note_) + p.bendSemitones - 69.0f) / 12.0f);
        const int compte = std::clamp(static_cast<int>(p.modes + 0.5f), 1, kMaxModes);

        for (int i = 0; i < kMaxModes; ++i) {
            auto& mode = modes_[static_cast<size_t>(i)];
            const float ratio = ratioOf(i, p.loading);
            const float hz = f0 * ratio;
            if (i >= compte || hz > static_cast<float>(sampleRate_) * 0.45f) {
                mode.amplitude = 0.0f;
                continue;
            }
            // Un maillet mou n'injecte pas dans les modes hauts, comme partout
            // ailleurs dans le parc.
            const float pente = 0.5f + 3.0f * (1.0f - durete);
            const float injection = std::pow(ratio, -pente);
            mode.amplitude = poidsDeFrappe(kBessel[static_cast<size_t>(i)].diametres, p.strikeRadius)
                           * injection * (0.4f + 0.6f * velocity);
            mode.phase = 0.0;
            mode.increment = static_cast<double>(hz) * vsm::audio::dsp::kTwoPi / sampleRate_;
            // Une peau perd ses modes hauts vite : c'est ce qui la distingue
            // d'un métal, dont le tilt est proche de zéro.
            const float tau = std::max(0.02f, p.decay)
                            / std::pow(ratio, std::max(0.0f, p.decayTilt));
            mode.damping = std::exp(-1.0f / (tau * static_cast<float>(sampleRate_)));
        }
    }

    double sampleRate_ = 48000.0;
    std::array<Mode, kMaxModes> modes_{};
    vsm::util::DeterministicRng rng_{0x4D454D42ULL};   // "MEMB"
    bool active_ = false, frappeEnAttente_ = false;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class MembraneSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kLoading = 1, kStrikeRadius, kHardness,
        kDecay, kDecayTilt, kModeCount,
        kVelocityToHardness, kOutputLevel,
    };

    MembraneSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override {
        // Comme `vsm.modal` : la molette agit sur les frappes À VENIR. Une peau
        // déjà frappée garde ses modes.
        if (event.kind == vsm::audio::plugin::MidiControlEvent::Kind::PitchBend) {
            bendSemitones_.store(event.value, std::memory_order_relaxed);
            return true;
        }
        return false;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Membrane (la peau tendue)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<MembraneVoice, kMaxVoices> voiceManager_;
    std::atomic<float> bendSemitones_{0.0f};
};

} // namespace vsm::plugins::membrane
