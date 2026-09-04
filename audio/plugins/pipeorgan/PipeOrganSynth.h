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

namespace vsm::plugins::pipeorgan {

/// L'ORGUE À TUYAUX — une soufflerie COMMUNE, et des tuyaux qui parlent.
///
/// POURQUOI CETTE MACHINE, ET POURQUOI ELLE N'EST PAS `vsm.tonewheel`. Le
/// Hammond est électrique : des roues, des tirettes, une pression qui ne
/// bouge pas. Un orgue à tuyaux est un instrument à VENT dont toutes les
/// notes boivent au même réservoir : plus on tient de touches, plus la
/// pression baisse pour toutes (le *sag*) — une note tenue BAISSE quand un
/// accord s'y ajoute. Et un tuyau à bouche ne s'installe pas d'un coup : à
/// l'attaque, son OCTAVE sort la première, avec un souffle (le *chiff*).
/// Le son se TIRE par jeux (principal, flûte 4', fourniture) et le tremblant
/// fait onduler la pression — hauteur et niveau ensemble.
///
/// ```
///   soufflerie : pression = 1 − sag·(voix actives − 1)/7 ; tremblant (LFO) ──┐
///   par voix : principal 8' (8 harmoniques) + flûte 4' + fourniture (12e, 15e)│
///              chiff : l'octave monte vite, la fondamentale lentement, souffle │
///                    └──> hauteur × pression, niveau × pression ──> Σ ──> sortie
/// ```
///
/// APPROXIMATIONS ASSUMÉES (§ 8), statut « dérivé » : des tuyaux additifs
/// (huit harmoniques au principal), pas un jet modélisé — la flûte à jet est
/// un résultat négatif du dépôt (ARCHITECTURE § 44), et un tuyau d'orgue à
/// pression constante n'en a pas besoin pour ses quatre traits ; un sag
/// linéaire dans le nombre de voix ; le chiff est une différence de temps
/// d'attaque entre l'octave et la fondamentale, plus un souffle, pas une
/// suroscillation résolue.
class PipeOrganVoice {
public:
    static constexpr int kHarmonics = 8;

    struct Params {
        float principal = 1.0f, flute4 = 0.4f, mixture = 0.0f;
        float chiff = 0.6f;
        float attackMs = 25.0f, releaseMs = 60.0f;
        float pressure = 1.0f;        // la soufflerie : niveau ET hauteur
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        rng_ = vsm::util::DeterministicRng(seed);
        active_ = false;
    }
    bool isActive() const { return active_; }
    /// La soupape est-elle OUVERTE ? C'est elle qui boit le vent -- un tuyau
    /// qui s'éteint après le relâchement n'en tire plus (mesuré : compter les
    /// voix actives retardait la remontée du vent de tout le relâchement).
    bool isHeld() const { return active_ && tenue_; }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    /// LA VÉLOCITÉ EST REFUSÉE : une soupape s'ouvre, vite ou lentement c'est
    /// le même vent. Elle n'est pas lue.
    void noteOn(uint8_t channel, uint8_t note, uint8_t) {
        channel_ = channel;
        note_ = note;
        for (auto& p : phases_) p = 0.0;
        envFond_ = envOct_ = 0.0f;
        souffle_ = 1.0f;
        tenue_ = true;
        active_ = true;
    }
    void noteOff(uint8_t) { tenue_ = false; }

    float render(const Params& p) {
        if (!active_) return 0.0f;
        const float f0 = 440.0f * std::exp2f((static_cast<float>(note_) - 69.0f) / 12.0f)
                         * (0.97f + 0.03f * p.pressure);   // le vent qui baisse fait baisser la hauteur (jusqu'à −53 cents à vent nul)
        // LE CHIFF : l'octave s'installe vite, la fondamentale lentement --
        // c'est l'ordre dans lequel un tuyau parle. Le chiff règle l'écart.
        const float montee = std::max(1.0f, p.attackMs * 0.001f * static_cast<float>(sampleRate_));
        const float lente = montee * (1.0f + 4.0f * p.chiff);
        const float descente = std::max(1.0f, p.releaseMs * 0.001f * static_cast<float>(sampleRate_));
        if (tenue_) {
            envFond_ += (1.0f - envFond_) / lente;
            envOct_ += (1.0f - envOct_) / montee;
        } else {
            envFond_ -= envFond_ / descente;
            envOct_ -= envOct_ / descente;
            if (envFond_ < 1e-4f && envOct_ < 1e-4f) { active_ = false; return 0.0f; }
        }
        // Le souffle du chiff : un bruit bref à l'ouverture de la soupape.
        souffle_ *= 1.0f - 1.0f / (montee * 1.5f);
        const float bruit = (rng_.nextBipolar()) * souffle_ * p.chiff * 0.12f;

        float somme = 0.0f;
        for (int h = 1; h <= kHarmonics; ++h) {
            const auto i = static_cast<size_t>(h - 1);
            const float hz = f0 * static_cast<float>(h);
            if (hz > static_cast<float>(sampleRate_) * 0.45f) break;
            const float s = static_cast<float>(std::sin(phases_[i]));
            phases_[i] += static_cast<double>(hz) * vsm::audio::dsp::kTwoPi / sampleRate_;
            if (phases_[i] > vsm::audio::dsp::kTwoPi) phases_[i] -= vsm::audio::dsp::kTwoPi;
            // Le principal : un spectre en 1/h, la fondamentale à l'enveloppe
            // lente, le reste à la rapide.
            float amp = p.principal / static_cast<float>(h);
            // La flûte 4' : l'octave, presque pure.
            if (h == 2) amp += p.flute4 * 0.8f;
            if (h == 4) amp += p.flute4 * 0.08f;
            // La fourniture : la douzième (3) et la quinzième (4), rangs fixes.
            if (h == 3) amp += p.mixture * 0.8f;
            if (h == 4) amp += p.mixture * 0.5f;
            somme += s * amp * (h == 1 ? envFond_ : envOct_);
        }
        return (somme * 0.35f + bruit) * p.pressure;
    }

private:
    double sampleRate_ = 48000.0;
    vsm::util::DeterministicRng rng_{0x4F524755ULL};   // "ORGU"
    std::array<double, kHarmonics> phases_{};
    float envFond_ = 0.0f, envOct_ = 0.0f, souffle_ = 0.0f;
    bool tenue_ = false, active_ = false;
    uint8_t note_ = 60, channel_ = 0;
};

class PipeOrganSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 16;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kPrincipal = 1, kFlute4, kMixture, kChiff, kWindSag, kTremulantRate, kTremulantDepth,
        kAttack, kRelease, kOutputLevel,
    };

    PipeOrganSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Pipe Organ (une soufflerie commune)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<PipeOrganVoice, kMaxVoices> voiceManager_;
    double tremulantPhase_ = 0.0;
    float pressionLisse_ = 1.0f;
};

} // namespace vsm::plugins::pipeorgan
