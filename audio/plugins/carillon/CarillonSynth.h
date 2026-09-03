#pragma once
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::carillon {

/// LE CARILLON — la cloche : une TIERCE MINEURE dans le spectre, des partiels
/// qui vont par DEUX et qui battent, un bourdon qui survit à tout.
///
/// POURQUOI CETTE MACHINE, ET POURQUOI ELLE N'EST PAS `vsm.modal`. `vsm.modal`
/// place ses partiels sur un continuum corde → barre libre ; `vsm.dx7` fait
/// des cloches par FM, avec des rapports que personne n'a choisis. Une cloche
/// fondue est autre chose, et depuis le XVIIe siècle les fondeurs l'ACCORDENT
/// partiel par partiel : le bourdon (hum) à l'octave grave, la prime, la
/// TIERCE à 1,2·f0 — une tierce MINEURE, que n'a aucun instrument harmonique,
/// et qui donne à la cloche sa mélancolie —, la quinte, la nominale à
/// l'octave. Trois traits qu'aucune machine du parc n'a, et que le banc mesure :
///
///  1. **LA TIERCE MINEURE** (6:5). Ou majeure (5:4) : les fondeurs
///     d'Eindhoven l'ont obtenue en 1985, et c'est un autre instrument. Un
///     réglage continu entre les deux.
///  2. **LES PARTIELS VONT PAR DEUX.** Une cloche n'est jamais parfaitement
///     ronde : chaque mode se dédouble en deux composantes à quelques
///     dixièmes de hertz, et leur battement est l'ondulation (le warble) qui
///     fait vivre la note. Ce n'est pas un LFO : c'est la géométrie.
///  3. **LE BOURDON SURVIT À TOUT.** Les partiels hauts meurent en secondes,
///     le bourdon en dizaines de secondes : au bout de la note, il ne reste
///     que lui, une octave sous ce qu'on a entendu.
///
/// ```
///   battant (dureté) ──> 8 partiels accordés (0,5 · 1 · 1,2 · 1,5 · 2 · 2,5 · 2,67 · 3)
///                          chacun DÉDOUBLÉ (± split/2) ──> Σ, τ par partiel
/// ```
///
/// APPROXIMATIONS ASSUMÉES (§ 8), statut « dérivé » : huit partiels là où
/// une cloche en a des dizaines ; les rapports sont ceux des tables de
/// fonderie (Lehr), pas d'une cloche mesurée ; le point de frappe est fixe
/// (le battant frappe la pince, toujours) ; pas de couplage entre modes.
class CarillonVoice {
public:
    static constexpr int kPartials = 8;

    struct Params {
        float tierce = 0.0f;            // 0 = mineure (1,2), 1 = majeure (1,25)
        float humDecay = 12.0f;         // T60 du bourdon, en secondes
        float decayTilt = 1.4f;         // les partiels hauts meurent en tau / ratio^tilt
        float doublet = 0.8f;           // écart des deux composantes d'un partiel, en Hz (à la prime)
        float hardness = 0.6f;          // dureté du battant
        float velocityToHardness = 0.5f;
    };

    void prepare(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        for (auto& c : composantes_) c = {};
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
    }
    /// Une cloche n'a pas d'étouffoir : lâcher la touche ne fait rien.
    void noteOff(uint8_t) {}

    float render(const Params& p) {
        if (!active_) return 0.0f;
        if (pendingStrike_) { frapper(p); pendingStrike_ = false; }
        float somme = 0.0f, reste = 0.0f;
        for (auto& c : composantes_) {
            if (c.amplitude < 1e-6f) continue;
            somme += static_cast<float>(std::sin(c.phase)) * c.amplitude;
            c.phase += c.increment;
            if (c.phase > vsm::audio::dsp::kTwoPi) c.phase -= vsm::audio::dsp::kTwoPi;
            c.amplitude *= c.damping;
            reste += c.amplitude;
        }
        if (reste < 1e-5f) active_ = false;
        return somme;
    }

private:
    struct Composante { double phase = 0.0, increment = 0.0; float amplitude = 0.0f, damping = 0.999f; };

    void frapper(const Params& p) {
        const float velocity = static_cast<float>(velocity_) / 127.0f;
        const float durete = std::clamp(p.hardness + p.velocityToHardness * (velocity - 0.5f), 0.0f, 1.0f);
        // La note demandée est la PRIME (la hauteur qu'on entend) : le bourdon
        // sonne une octave dessous, la nominale une octave dessus.
        const float prime = 440.0f * std::exp2f((static_cast<float>(note_) - 69.0f) / 12.0f);
        const float tierce = 1.2f + (1.25f - 1.2f) * std::clamp(p.tierce, 0.0f, 1.0f);
        const std::array<float, kPartials> ratios{{0.5f, 1.0f, tierce, 1.5f, 2.0f, 2.514f, 2.662f, 3.011f}};
        // Ce que le battant réveille : la nominale et la prime fort, le bourdon
        // peu (il n'est pas au point de frappe), et les hauts selon la dureté.
        const std::array<float, kPartials> injection{{0.25f, 0.9f, 0.8f, 0.55f, 1.0f, 0.5f, 0.45f, 0.4f}};

        for (int i = 0; i < kPartials; ++i) {
            const float ratio = ratios[static_cast<size_t>(i)];
            const float hz = prime * ratio;
            const float pente = 0.2f + 2.2f * (1.0f - durete);
            // La pente du battant ne s'applique qu'AU-DESSUS de la prime : le
            // bourdon n'est pas plus réveillé qu'elle (il est loin du point de
            // frappe), il est seulement plus long.
            const float amplitude = injection[static_cast<size_t>(i)] * std::pow(std::max(ratio, 1.0f), -pente)
                                    * (0.35f + 0.65f * velocity);
            // τ = τ_bourdon / ratio^tilt (le bourdon a ratio 0,5 : il tient PLUS
            // longtemps que la prime — c'est ce qu'on entend).
            const float t60 = std::max(0.05f, p.humDecay) / std::pow(ratio / 0.5f, std::max(0.0f, p.decayTilt));
            const float damping = std::exp(-6.9078f / (t60 * static_cast<float>(sampleRate_)));
            // LE DOUBLET : deux composantes à ± split/2, l'écart croissant avec
            // le rang (les modes hauts d'une cloche asymétrique se séparent
            // davantage). Amplitudes égales : le battement va jusqu'au silence.
            const float split = p.doublet * ratio;
            for (int k = 0; k < 2; ++k) {
                auto& c = composantes_[static_cast<size_t>(i * 2 + k)];
                const float f = hz + (k == 0 ? -0.5f : 0.5f) * split;
                if (f <= 0.0f || f > static_cast<float>(sampleRate_) * 0.45f) { c.amplitude = 0.0f; continue; }
                c.amplitude = amplitude * 0.5f;
                c.phase = 0.0;
                c.increment = static_cast<double>(f) * vsm::audio::dsp::kTwoPi / sampleRate_;
                c.damping = damping;
            }
        }
    }

    double sampleRate_ = 48000.0;
    std::array<Composante, kPartials * 2> composantes_{};
    bool active_ = false, pendingStrike_ = false;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class CarillonSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 12;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kTierce = 1, kHumDecay, kDecayTilt, kDoublet, kHardness, kVelocityToHardness, kOutputLevel,
    };

    CarillonSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Carillon (la cloche et sa tierce mineure)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<CarillonVoice, kMaxVoices> voiceManager_;
};

} // namespace vsm::plugins::carillon
