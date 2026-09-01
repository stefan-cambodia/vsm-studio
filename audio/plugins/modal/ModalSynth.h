#pragma once
#include "vsm/audio/dsp/Biquad.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::modal {

/// SYNTHÈSE MODALE — les modes d'un OBJET frappé, à rapports libres.
///
/// POURQUOI CETTE MACHINE, ET POURQUOI ELLE N'EST PAS `vsm.additive` AVEC
/// D'AUTRES RAPPORTS. La question mérite d'être posée d'emblée, parce que les
/// deux machines somment des sinusoïdes amorties et que le § 0 du CDC
/// nouvelle-machine interdit de dupliquer. Trois choses les séparent, et la
/// première est mesurable :
///
///  1. **LES RAPPORTS.** `vsm.additive` étire ses rangs par la loi de la
///     CORDE RAIDE — `n·f0·sqrt(1 + B·n²)` — qui ne peut pas s'éloigner de
///     l'harmonique : à son maximum, son second rang est à 2,003·f0. Une
///     barre de vibraphone libre-libre a le sien à **2,76·f0**, une plaque
///     ailleurs encore. Aucune machine du parc ne sait placer un partiel là ;
///     c'est le trait distinctif, et le test le mesure.
///  2. **LE POINT DE FRAPPE.** Frapper une barre à sa moitié annule les modes
///     pairs, comme le marteau du piano annule le huitième harmonique
///     (`vsm.piano`). C'est une commande physique, pas un réglage de spectre :
///     elle agit sur TOUS les modes à la fois par une seule position.
///  3. **LE VOCABULAIRE.** `vsm.additive` se règle en termes de SPECTRE
///     (pente en dB/octave, pairs contre impairs, étalement d'attaque) ;
///     celle-ci se règle en termes d'OBJET (matériau, taille, dureté du
///     maillet, point de frappe). Deux façades pour deux façons de penser le
///     même son : c'est le précédent de `vsm.piano` et `vsm.string`, qui
///     partagent un guide d'ondes et restent deux machines parce que « le
///     marteau porte la loi expressive ».
///
/// LE MODÈLE. Vingt-quatre modes au plus, chacun une sinusoïde amortie
/// réexcitée à la frappe :
///
/// ```
///   maillet (dureté -> filtre) ──> excitation ──> Σ modes (ratio_n, τ_n, A_n)
///                                                    ^
///                                      point de frappe : A_n = sin(n·π·pos)
/// ```
///
///  - **Le matériau est un CONTINUUM**, de la corde (rapports entiers) à la
///    barre libre-libre (`((2n+1)/3)²` : 1 — 2,78 — 5,44 — 9,00…, la formule
///    exacte à un pour cent près des tables). Un réglage continu, parce que
///    le § 3 du CDC `vsm.generic` a montré qu'un sélecteur discret creuse une
///    falaise dans toute recherche, et parce que le fondu entre les deux
///    familles est musicalement le plus intéressant.
///  - **L'amortissement suit le rang** : `τ_n = τ / n^tilt`. Sur du bois les
///    modes hauts meurent bien plus vite que le fondamental (tilt élevé), sur
///    du métal ils tiennent (tilt bas). C'est ce qui sépare un marimba d'un
///    vibraphone, et c'est un seul réglage.
///  - **La dureté du maillet** filtre l'impulsion d'excitation : un maillet
///    mou n'a pas assez d'aigus pour réveiller les modes hauts. Même physique
///    que la durée de contact du marteau de `vsm.piano`, et même conséquence
///    — frapper fort ouvre le timbre, pas seulement le volume.
///
/// APPROXIMATIONS ASSUMÉES (§ 8), statut « dérivé », aucune mesure sur un
/// instrument réel : pas de couplage entre modes (chaque mode s'éteint dans
/// son coin, là où un objet réel échange de l'énergie entre ses modes) ; pas
/// de corps résonant séparé (le vibraphone a des tubes, on ne les modélise
/// pas) ; l'excitation est une impulsion filtrée, pas un contact de maillet
/// avec sa durée.
class ModalVoice {
public:
    static constexpr int kMaxModes = 24;

    struct Params {
        float material = 0.6f;     // 0 = corde (harmonique), 1 = barre libre
        float modes = 12.0f;       // combien de modes sonnent
        float decay = 2.0f;        // T60 du fondamental, en secondes
        float decayTilt = 1.0f;    // les modes hauts meurent en tau/n^tilt
        float strikePosition = 0.28f;
        float hardness = 0.5f;     // dureté du maillet : 0 mou, 1 dur
        float spread = 1.0f;       // étirement global des rapports
        float velocityToHardness = 0.5f;
        float bendSemitones = 0.0f;
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        rng_ = vsm::util::DeterministicRng(seed);
        excitationFilter_.setSampleRate(sampleRate_);
        for (auto& mode : modes_) { mode.amplitude = 0.0f; mode.phase = 0.0; }
        active_ = false;
    }

    bool isActive() const { return active_; }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel;
        note_ = note;
        velocity_ = velocity;
        pendingStrike_ = true;
        active_ = true;
        released_ = false;
    }
    /// UNE BARRE FRAPPÉE NE S'ARRÊTE PAS QUAND ON LÂCHE LA TOUCHE : elle
    /// s'éteint d'elle-même. Le relâchement n'accélère donc rien -- sauf s'il
    /// y avait un étouffoir, que cette machine n'a pas et ne prétend pas
    /// avoir. La voix se libère quand ses modes sont éteints.
    void noteOff(uint8_t) { released_ = true; }

    float render(const Params& p) {
        if (!active_) return 0.0f;

        if (pendingStrike_) {
            frapper(p);
            pendingStrike_ = false;
        }

        const int compte = std::clamp(static_cast<int>(p.modes + 0.5f), 1, kMaxModes);
        float somme = 0.0f;
        float reste = 0.0f;
        for (int i = 0; i < compte; ++i) {
            auto& mode = modes_[static_cast<size_t>(i)];
            if (mode.amplitude < 1e-5f) continue;
            somme += static_cast<float>(std::sin(mode.phase)) * mode.amplitude;
            mode.phase += mode.increment;
            if (mode.phase > vsm::audio::dsp::kTwoPi) mode.phase -= vsm::audio::dsp::kTwoPi;
            mode.amplitude *= mode.damping;
            reste += mode.amplitude;
        }
        // La voix se libère quand tous les modes se sont tus -- pas au
        // relâchement, qui ne veut rien dire pour un objet frappé.
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

    /// Le rapport du mode `n` (1-indexé) : fondu continu entre la corde
    /// (rapports entiers) et la barre libre-libre (`((2n+1)/3)²`).
    static float ratioOf(int n, float material) {
        const float corde = static_cast<float>(n);
        const float k = (2.0f * static_cast<float>(n) + 1.0f) / 3.0f;
        const float barre = k * k;
        const float m = std::clamp(material, 0.0f, 1.0f);
        return corde + (barre - corde) * m;
    }

    void frapper(const Params& p) {
        const float velocity = static_cast<float>(velocity_) / 127.0f;
        const float durete = std::clamp(
            p.hardness + p.velocityToHardness * (velocity - 0.5f), 0.0f, 1.0f);
        const float f0 = 440.0f * std::exp2f(
            (static_cast<float>(note_) + p.bendSemitones - 69.0f) / 12.0f);
        const float compte = static_cast<float>(
            std::clamp(static_cast<int>(p.modes + 0.5f), 1, kMaxModes));

        for (int i = 0; i < kMaxModes; ++i) {
            auto& mode = modes_[static_cast<size_t>(i)];
            const int n = i + 1;
            const float ratio = ratioOf(n, p.material) * std::pow(
                std::max(0.5f, p.spread), static_cast<float>(n - 1) * 0.1f);
            const float hz = f0 * ratio;
            if (n > static_cast<int>(compte) || hz > static_cast<float>(sampleRate_) * 0.45f) {
                mode.amplitude = 0.0f;
                continue;
            }

            // LE POINT DE FRAPPE ANNULE LES MODES DONT IL EST UN NŒUD : c'est
            // le peigne de `sin(n·π·position)`, la même physique que le
            // marteau du piano au huitième de la corde.
            const float noeud = std::sin(static_cast<float>(n) * static_cast<float>(M_PI)
                                         * std::clamp(p.strikePosition, 0.02f, 0.98f));
            // UN MAILLET MOU N'A PAS D'AIGUS : l'énergie injectée dans un
            // mode décroît avec son rang, d'autant plus vite qu'il est mou.
            const float pente = 0.6f + 3.4f * (1.0f - durete);
            const float injection = std::pow(static_cast<float>(n), -pente);

            mode.amplitude = std::abs(noeud) * injection * (0.4f + 0.6f * velocity);
            mode.phase = 0.0;
            mode.increment = static_cast<double>(hz) * vsm::audio::dsp::kTwoPi / sampleRate_;
            // τ_n = τ / n^tilt, converti en facteur par échantillon.
            const float tau = std::max(0.02f, p.decay)
                            / std::pow(static_cast<float>(n), std::max(0.0f, p.decayTilt));
            mode.damping = std::exp(-1.0f / (tau * static_cast<float>(sampleRate_)));
        }
    }

    double sampleRate_ = 48000.0;
    std::array<Mode, kMaxModes> modes_{};
    vsm::audio::dsp::Biquad excitationFilter_;
    vsm::util::DeterministicRng rng_{0x4D4F44414CULL}; // "MODAL"
    bool active_ = false, released_ = false, pendingStrike_ = false;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class ModalSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kMaterial = 1, kModeCount, kDecay, kDecayTilt,
        kStrikePosition, kHardness, kSpread,
        kVelocityToHardness, kOutputLevel,
    };

    ModalSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override {
        // La molette de hauteur agit sur les frappes À VENIR : un objet déjà
        // frappé garde ses modes, comme une cloche ne change pas de note
        // parce qu'on tourne une molette. Le reste est refusé en le disant.
        if (event.kind == vsm::audio::plugin::MidiControlEvent::Kind::PitchBend) {
            bendSemitones_.store(event.value, std::memory_order_relaxed);
            return true;
        }
        return false;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Modal (l'objet frappé)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<ModalVoice, kMaxVoices> voiceManager_;
    std::atomic<float> bendSemitones_{0.0f};
};

} // namespace vsm::plugins::modal
