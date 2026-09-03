#pragma once
#include "vsm/audio/dsp/Biquad.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/dsp/StringWaveguide.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::banjo {

/// LE BANJO — la corde pincée dont la table est une PEAU.
///
/// POURQUOI CETTE MACHINE. Toutes les cordes du parc rayonnent par une table
/// de bois, que personne ne modélise : le chevalet est une simple perte. Le
/// banjo, lui, tend ses cordes sur une PEAU DE TAMBOUR : le chevalet pose
/// sur la membrane, et c'est elle qui rayonne. D'où deux traits qu'aucune
/// autre corde n'a : **la peau chante ses propres modes**, à des fréquences
/// qui ne dépendent pas de la note (les zéros de Bessel d'une membrane
/// circulaire, les mêmes que `vsm.membrane`), et **la peau mange la corde** —
/// elle prend son énergie pour la rayonner, si bien que la note est brève et
/// claquante là où une guitare tiendrait.
///
/// ```
///   pincement ──> CORDE (guide d'ondes) ──> chevalet ──> PEAU : banque de
///                                                        résonateurs aux modes
///                                                        de Bessel ──> rayonnement
///                          ^                               │
///                          └── la peau ABSORBE : t60 de la corde raccourci ──┘
/// ```
///
/// La corde est celle de `vsm.string` (pincement, position, vélocité), la
/// peau est une banque de résonateurs à deux pôles accordés sur les six
/// premiers modes d'une membrane encastrée — alimentés par le chevalet et non
/// frappés, ce qui est toute la différence avec `vsm.membrane`.
///
/// APPROXIMATIONS ASSUMÉES (§ 8), statut « dérivé » : le couplage
/// corde-peau est à sens unique (la peau ne renvoie rien à la corde, sinon une
/// perte) ; six modes là où une peau en a des dizaines ; le résonateur (le
/// pot) et la corde de bourdon (la cinquième, courte) ne sont pas modélisés.
class BanjoVoice {
public:
    struct Params {
        float decay = 1.2f;
        float damping = 0.3f;
        float pickPosition = 0.15f;
        float pickHardness = 0.8f;
        float velocitySensitivity = 0.6f;
        float headMix = 0.6f;          // ce que la peau prend à la corde
        float bendSemitones = 0.0f;
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        corde_.prepare(sampleRate_, 40.0f);
        rng_ = vsm::util::DeterministicRng(seed);
        niveau_ = 0.0f;
    }
    bool isActive() const { return niveau_ > 1e-5f; }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel;
        note_ = note;
        velocity_ = velocity;
        corde_.reset();
        niveau_ = 1.0f;
        relachee_ = false;
        salveRestante_ = std::max(3, static_cast<int>(corde_.loopDelay() * 0.25f));
        salveLongueur_ = salveRestante_;
        bruitLisse_ = 0.0f;
    }
    void noteOff(uint8_t) { relachee_ = true; }

    /// Le signal AU CHEVALET : c'est lui qui va à la peau.
    float render(const Params& p) {
        if (!isActive()) return 0.0f;
        const float hz = 440.0f * std::exp2f((static_cast<float>(note_) + p.bendSemitones - 69.0f) / 12.0f);
        // LA PEAU MANGE LA CORDE : plus le chevalet lui livre d'énergie, plus
        // vite la corde se tait. C'est le premier trait, et il tient à ce
        // facteur -- une guitare aurait le t60 entier.
        const float t60 = (relachee_ ? 0.08f : p.decay) * (1.0f - 0.6f * p.headMix);
        corde_.setTuning(hz, p.damping, 0.03f, t60);

        const float velocity = static_cast<float>(velocity_) / 127.0f;
        const float force = 1.0f - p.velocitySensitivity * (1.0f - velocity);
        float drive = 0.0f;
        if (salveRestante_ > 0) {
            const float phase = std::clamp(
                1.0f - static_cast<float>(salveRestante_) / static_cast<float>(salveLongueur_), 0.0f, 1.0f);
            const float fenetre = std::min(1.0f, phase * 12.0f) * (1.0f - phase);
            // Un onglet dur laisse passer le bruit ; le doigt l'adoucit.
            const float lissage = 0.25f + 0.7f * p.pickHardness;
            bruitLisse_ += lissage * (rng_.nextBipolar() - bruitLisse_);
            drive = force * fenetre * bruitLisse_ * 2.8f;
            --salveRestante_;
        }
        const auto contact = static_cast<size_t>(
            std::max(1.0f, std::clamp(p.pickPosition, 0.02f, 0.5f) * corde_.loopDelay()));
        const float boucle = corde_.advance();
        const float x = corde_.inject(boucle, drive, contact);
        const float absolu = std::abs(x);
        niveau_ = absolu > niveau_ ? absolu : niveau_ + (absolu - niveau_) * 0.0002f;
        return x;
    }

private:
    double sampleRate_ = 48000.0;
    vsm::audio::dsp::StringWaveguide corde_;
    vsm::util::DeterministicRng rng_{0x42414E4AULL};   // "BANJ"
    float niveau_ = 0.0f, bruitLisse_ = 0.0f;
    int salveRestante_ = 0, salveLongueur_ = 1;
    bool relachee_ = false;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

/// LA PEAU : six modes d'une membrane circulaire, chacun un résonateur à deux
/// pôles alimenté par le chevalet. Sa fréquence de base est la TENSION de la
/// peau (une clé sur le cercle), son Q l'amortissement.
class HeadResonator {
public:
    static constexpr int kModes = 6;
    static constexpr std::array<float, kModes> kBessel{{1.000f, 1.593f, 2.136f, 2.296f, 2.653f, 2.918f}};

    void prepare(double sampleRate) { sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0; reset(); }
    void reset() { for (auto& r : etats_) r = {}; }

    void setTuning(float fondamentaleHz, float damping) {
        for (int i = 0; i < kModes; ++i) {
            const double hz = std::min(static_cast<double>(fondamentaleHz) * kBessel[static_cast<size_t>(i)],
                                       sampleRate_ * 0.45);
            // Une peau perd ses modes hauts vite : le rayon du pôle recule
            // avec le rang, et avec l'amortissement demandé.
            const double bande = (25.0 + 220.0 * damping) * (1.0 + 0.35 * i);
            const double r = std::exp(-vsm::audio::dsp::kPi * bande / sampleRate_);
            const double w = 2.0 * vsm::audio::dsp::kPi * hz / sampleRate_;
            coefs_[static_cast<size_t>(i)] = {static_cast<float>(1.0 - r), static_cast<float>(-2.0 * r * std::cos(w)),
                                              static_cast<float>(r * r)};
            gains_[static_cast<size_t>(i)] = 1.0f / (1.0f + 0.5f * static_cast<float>(i));
        }
    }

    float process(float entree) {
        float somme = 0.0f;
        for (int i = 0; i < kModes; ++i) {
            auto& e = etats_[static_cast<size_t>(i)];
            const auto& c = coefs_[static_cast<size_t>(i)];
            const float y = c.b0 * entree - c.a1 * e.y1 - c.a2 * e.y2;
            e.y2 = e.y1;
            e.y1 = y;
            somme += y * gains_[static_cast<size_t>(i)];
        }
        return somme;
    }

private:
    struct Coefs { float b0 = 0.0f, a1 = 0.0f, a2 = 0.0f; };
    struct Etat { float y1 = 0.0f, y2 = 0.0f; };
    double sampleRate_ = 48000.0;
    std::array<Coefs, kModes> coefs_{};
    std::array<Etat, kModes> etats_{};
    std::array<float, kModes> gains_{};
};

class BanjoSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kHeadTension = 1, kHeadDamping, kHeadMix, kPickPosition, kPickHardness,
        kDecay, kDamping, kVelocitySensitivity, kCutoff, kOutputLevel,
    };

    BanjoSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Banjo (la corde sur la peau)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<BanjoVoice, kMaxVoices> voiceManager_;
    HeadResonator peau_;
    vsm::audio::dsp::StateVariableFilter filtre_;
    std::atomic<float> bendSemitones_{0.0f};
};

} // namespace vsm::plugins::banjo
