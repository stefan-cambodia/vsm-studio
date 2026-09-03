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

namespace vsm::plugins::kalimba {

/// LE KALIMBA — la lame ENCASTRÉE d'un seul côté, le buzz du contact, la
/// caisse qu'on bouche.
///
/// POURQUOI CETTE MACHINE, ET POURQUOI ELLE N'EST PAS `vsm.vibraphone`. Une
/// barre de vibraphone est LIBRE aux deux bouts (1 : 2,76 : 5,40, puis
/// creusée à 1 : 4 : 10). Une lame de kalimba est tenue d'un côté sous une
/// barre de pression et libre de l'autre : c'est une poutre encastrée-libre,
/// et ses modes sont ailleurs — (βL)² = 3,516, 22,03, 61,70, soit des
/// rapports **1 : 6,27 : 17,55**. Rien à l'octave, rien à la douzième : un
/// son presque pur, une pointe très haute, et c'est ce qu'on entend. Trois
/// traits qu'aucune machine du parc n'a, et que le banc mesure :
///
///  1. **LES PARTIELS D'UNE POUTRE ENCASTRÉE** : 6,27·f0, pas 2·f0.
///  2. **LE BUZZ DU CONTACT.** La lame qui vibre fort vient TOUCHER la barre
///     à chaque cycle ; chaque contact est un choc (le sitar a le même
///     mécanisme, § 10 du CDC), et chaque contact MANGE de l'amplitude —
///     si bien que le crépitement s'éteint de lui-même, quand la lame
///     retombe sous l'écart. Un pouce doux ne touche jamais.
///  3. **LA CAISSE QU'ON BOUCHE.** Des trous au dos, que les doigts couvrent :
///     la résonance de Helmholtz descend, et c'est le « wah » du kalimba. La
///     molette de modulation et l'aftertouch sont les doigts.
///
/// ```
///   pouce (dureté, vélocité) ──> 3 modes encastrés (1 · 6,27 · 17,55) ──┬──> Σ voix ──> caisse (Helmholtz, bouchée par CC 1) ──> sortie
///                                   │ |x| > écart ? choc ──> chevalet ─┘
/// ```
///
/// APPROXIMATIONS ASSUMÉES (§ 8), statut « dérivé » : trois modes là où une
/// lame en a plus (le quatrième, à 34,4·f0, est hors bande dès le médium) ;
/// les rapports sont ceux de la poutre uniforme, une lame réelle est
/// effilée et lestée (on les entend à quelques pour cent près) ; le contact
/// est un choc sur un mode de chevalet et un amortissement, pas une
/// collision résolue ; la caisse est un seul mode de Helmholtz plus un mode
/// de table fixe.
class KalimbaVoice {
public:
    static constexpr int kModes = 3;

    struct Params {
        float tineDecay = 2.5f;          // T60 du fondamental, en secondes, à do4
        float decayTilt = 1.2f;          // les partiels hauts meurent en tau / ratio^tilt
        float hardness = 0.6f;           // ongle (1) ou pulpe (0)
        float buzz = 0.35f;              // rapproche la barre : 0 = jamais de contact
        float velocitySensitivity = 0.7f;
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        rng_ = vsm::util::DeterministicRng(seed);
        for (auto& m : modes_) m = {};
        chevalet1_ = chevalet2_ = 0.0f;
        active_ = false;
        // Le chevalet sonne à sa fréquence, autour de 3,2 kHz, 1,5 ms de vie.
        const double w = 2.0 * M_PI * 3200.0 / sampleRate_;
        const float r = std::exp(-1.0f / (0.0015f * static_cast<float>(sampleRate_)));
        chevaletA1_ = 2.0f * r * static_cast<float>(std::cos(w));
        chevaletA2_ = r * r;
    }
    bool isActive() const { return active_; }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel;
        note_ = note;
        velocity_ = velocity;
        pendingPluck_ = true;
        active_ = true;
    }
    /// Une lame n'a pas d'étouffoir : lâcher la touche ne fait rien.
    void noteOff(uint8_t) {}

    float render(const Params& p) {
        if (!active_) return 0.0f;
        if (pendingPluck_) { pincer(p); pendingPluck_ = false; }
        float x = 0.0f, reste = 0.0f;
        for (auto& m : modes_) {
            if (m.amplitude < 1e-6f) continue;
            x += static_cast<float>(std::sin(m.phase)) * m.amplitude;
            m.phase += m.increment;
            if (m.phase > vsm::audio::dsp::kTwoPi) m.phase -= vsm::audio::dsp::kTwoPi;
            m.amplitude *= m.damping;
            reste += m.amplitude;
        }
        // LE TIC DU POUCE : un souffle d'un millième de seconde, plus clair
        // sous l'ongle.
        float tic = 0.0f;
        if (ticRestant_ > 0) {
            const float phase = 1.0f - static_cast<float>(ticRestant_) / static_cast<float>(ticLongueur_);
            ticBruit_ += (0.2f + 0.7f * p.hardness) * (rng_.nextBipolar() - ticBruit_);
            tic = ticBruit_ * (1.0f - phase) * ticNiveau_;
            --ticRestant_;
        }
        // LE CONTACT : au-delà de l'écart, la lame touche la barre. Un choc
        // par franchissement, sur le mode du chevalet, et un amortissement
        // tant qu'on est en contact — c'est lui qui éteint le buzz.
        float impulsion = 0.0f;
        const float absolu = std::abs(x);
        if (ecart_ < 1.0e9f) {
            if (absolu > ecart_) {
                if (precedent_ <= ecart_) impulsion = std::min(0.5f, (absolu - ecart_) * 3.0f);
                for (auto& m : modes_) m.amplitude *= 0.9998f;
            }
            precedent_ = absolu;
        }
        const float y = chevaletA1_ * chevalet1_ - chevaletA2_ * chevalet2_ + impulsion;
        chevalet2_ = chevalet1_;
        chevalet1_ = y;
        if (reste < 1e-5f && ticRestant_ <= 0 && std::abs(y) < 1e-6f) active_ = false;
        return x + y + tic;
    }

private:
    struct Mode { double phase = 0.0, increment = 0.0; float amplitude = 0.0f, damping = 0.999f; };

    void pincer(const Params& p) {
        const float velocity = static_cast<float>(velocity_) / 127.0f;
        const float force = 1.0f - std::clamp(p.velocitySensitivity, 0.0f, 1.0f) * (1.0f - velocity);
        const float f0 = 440.0f * std::exp2f((static_cast<float>(note_) - 69.0f) / 12.0f);
        // Une lame courte meurt plus vite : T60 en 1/sqrt(f), do4 pour repère.
        const float t60 = std::clamp(p.tineDecay * std::sqrt(261.63f / f0), 0.05f, 30.0f);
        // La poutre encastrée-libre : (βL)² = 3,516 · 22,03 · 61,70.
        const std::array<float, kModes> ratios{{1.0f, 6.267f, 17.55f}};
        // Ce que le pouce réveille : le fondamental surtout (un déplacement
        // de l'extrémité projette presque tout sur le premier mode), les
        // hauts selon la dureté.
        const float durete = 0.3f + 0.7f * std::clamp(p.hardness, 0.0f, 1.0f);
        const std::array<float, kModes> injection{{1.0f, 0.12f * durete, 0.05f * durete * durete}};
        for (int i = 0; i < kModes; ++i) {
            auto& m = modes_[static_cast<size_t>(i)];
            const float hz = f0 * ratios[static_cast<size_t>(i)];
            if (hz > static_cast<float>(sampleRate_) * 0.45f) { m.amplitude = 0.0f; continue; }
            const float t = t60 / std::pow(ratios[static_cast<size_t>(i)], std::max(0.0f, p.decayTilt));
            m.amplitude = injection[static_cast<size_t>(i)] * force;
            m.phase = 0.0;
            m.increment = static_cast<double>(hz) * vsm::audio::dsp::kTwoPi / sampleRate_;
            m.damping = std::exp(-6.9078f / (t * static_cast<float>(sampleRate_)));
        }
        // L'écart de la barre : Buzz 0 = jamais atteint, Buzz 1 = tout près.
        ecart_ = p.buzz > 0.0f ? 1.05f - 0.9f * std::clamp(p.buzz, 0.0f, 1.0f) : 1.0e9f;
        precedent_ = 0.0f;
        ticLongueur_ = std::max(8, static_cast<int>(0.0012 * sampleRate_));
        ticRestant_ = ticLongueur_;
        ticBruit_ = 0.0f;
        ticNiveau_ = 0.25f * force * durete;
    }

    double sampleRate_ = 48000.0;
    vsm::util::DeterministicRng rng_{0x4B414C49ULL};   // "KALI"
    std::array<Mode, kModes> modes_{};
    float chevalet1_ = 0.0f, chevalet2_ = 0.0f, chevaletA1_ = 0.0f, chevaletA2_ = 0.0f;
    float ecart_ = 1.0e9f, precedent_ = 0.0f;
    float ticBruit_ = 0.0f, ticNiveau_ = 0.0f;
    int ticRestant_ = 0, ticLongueur_ = 1;
    bool active_ = false, pendingPluck_ = false;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class KalimbaSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 17;   // dix-sept lames sur un kalimba courant

    enum ParamIds : vsm::audio::plugin::ParamId {
        kTineDecay = 1, kDecayTilt, kHardness, kBuzz, kBodyResonance, kBodyLevel, kHoleCover,
        kVelocitySensitivity, kOutputLevel,
    };

    KalimbaSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Kalimba (la lame encastrée et la caisse qu’on bouche)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    /// Un mode de caisse : passe-bande à gain de crête unité.
    struct Caisse {
        float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
        float b0 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        void regler(double hz, double q, double sampleRate) {
            const double w = 2.0 * M_PI * std::clamp(hz, 20.0, sampleRate * 0.45) / sampleRate;
            const double alpha = std::sin(w) / (2.0 * q);
            const double a0 = 1.0 + alpha;
            b0 = static_cast<float>(alpha / a0);
            a1 = static_cast<float>(-2.0 * std::cos(w) / a0);
            a2 = static_cast<float>((1.0 - alpha) / a0);
        }
        float traiter(float x) {
            const float y = b0 * (x - x2) - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
        void reset() { x1 = x2 = y1 = y2 = 0.0f; }
    };

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<KalimbaVoice, kMaxVoices> voiceManager_;
    Caisse helmholtz_, table_;
    std::atomic<float> molette_{0.0f}, pression_{0.0f};
    float couvertLisse_ = 0.0f;
};

} // namespace vsm::plugins::kalimba
