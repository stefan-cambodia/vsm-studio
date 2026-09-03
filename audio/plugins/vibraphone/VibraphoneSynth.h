#pragma once
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::vibraphone {

/// LE VIBRAPHONE — la barre CREUSÉE, le tube qu'un MOTEUR ouvre et ferme,
/// le feutre qu'une PÉDALE soulève.
///
/// POURQUOI CETTE MACHINE, ET POURQUOI ELLE N'EST PAS `vsm.modal` AVEC UN
/// TRÉMOLO. `vsm.modal` sait sonner une barre libre-libre, dont le second
/// partiel est à 2,76·f0 — et c'est exactement ce qu'un facteur de
/// vibraphone REFUSE : il creuse le dessous de la barre jusqu'à ce que ses
/// partiels tombent à 1 : 4 : 10, deux octaves puis trois octaves et une
/// tierce. Un vibraphone est accordé DANS le partiel, pas seulement dans la
/// note ; c'est le premier trait, et le test le mesure. Trois autres traits
/// qu'aucune machine du parc n'a :
///
///  1. **LE TUBE.** Sous chaque barre, un tube fermé accordé sur f0 : il
///     renforce le fondamental et RIEN d'autre (un tube fermé ne résonne
///     qu'aux rangs impairs, et la barre n'en a aucun au-dessus de f0).
///  2. **LE MOTEUR.** Au sommet des tubes, des disques tournent sur un axe
///     commun : le tube s'ouvre et se ferme, et c'est le « vibrato » du
///     vibraphone — qui n'est ni un vibrato (la hauteur ne bouge pas) ni un
///     trémolo ordinaire (seul le fondamental ondule, les partiels hauts, que
///     le tube ne porte pas, restent droits). Un seul moteur pour toutes les
///     barres : toutes les notes ondulent EN PHASE. Mesurable, mesuré.
///  3. **LA PÉDALE.** Une barre d'aluminium tient six secondes ; un feutre
///     la tait en un quart de seconde. Le pied choisit — CC 64, la pédale de
///     sustain d'un clavier, est exactement ce geste et la machine l'honore.
///     Une touche tenue vaut la pédale (le musicien qui garde la baguette
///     posée ne fait pas autre chose) ; une touche lâchée sans pédale rend
///     la barre au feutre.
///
/// ```
///   maillet (dureté) ──> BARRE : 6 modes 1:4:10 (creusée) ou 1:2,76:5,4 (libre)
///                          │                     ^ feutre : τ court / τ long (pédale)
///                          ├──────────────────────────────────> direct
///                          └──> TUBE (2 pôles à f0) ──> × ouverture(moteur) ──> Σ
/// ```
///
/// APPROXIMATIONS ASSUMÉES (§ 8), statut « dérivé » : le tube ne renvoie
/// rien à la barre (un tube réel la fait sonner plus fort et plus court) ;
/// les formes modales d'une barre libre-libre sont prises pour des
/// cosinus et sinus centrés ; pas d'archet (le vibraphone frotté existe,
/// pas ici) ; le bruit de contact du maillet est omis.
class VibraphoneVoice {
public:
    static constexpr int kModes = 6;

    struct Params {
        float undercut = 1.0f;          // 0 = barre libre (1:2,76:5,4), 1 = creusée (1:4:10)
        float decay = 6.0f;             // T60 du fondamental, barre libre du feutre
        float decayTilt = 1.2f;         // les modes hauts meurent en tau/n^tilt
        float damperDecay = 0.3f;       // T60 sous le feutre
        float hardness = 0.5f;          // maillet : 0 laine, 1 plastique
        float strikeOffset = 0.15f;     // 0 = centre de la barre, 1 = bout
        float velocityToHardness = 0.5f;
        bool pedalDown = false;         // CC 64 : le feutre est soulevé
    };

    struct Output { float bar = 0.0f, tube = 0.0f; };

    void prepare(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        for (auto& m : modes_) m = {};
        tube_ = {};
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
        held_ = true;
        active_ = true;
    }
    /// Lâcher la touche ne frappe pas : cela rend la barre au feutre — sauf
    /// si la pédale le tient soulevé, auquel cas rien ne change.
    void noteOff(uint8_t) { held_ = false; }

    Output render(const Params& p) {
        Output out;
        if (!active_) return out;
        if (pendingStrike_) { frapper(p); pendingStrike_ = false; }

        // LE FEUTRE : touche tenue ou pédale enfoncée, la barre est libre ;
        // sinon elle meurt sous le feutre, en un quart de seconde.
        const bool libre = held_ || p.pedalDown;
        float reste = 0.0f;
        for (auto& m : modes_) {
            if (m.amplitude < 1e-6f) continue;
            out.bar += static_cast<float>(std::sin(m.phase)) * m.amplitude;
            m.phase += m.increment;
            if (m.phase > vsm::audio::dsp::kTwoPi) m.phase -= vsm::audio::dsp::kTwoPi;
            m.amplitude *= libre ? m.dampingFree : m.dampingFelt;
            reste += m.amplitude;
        }
        // LE TUBE : un résonateur à deux pôles sur f0, à gain unité au pic.
        const float y = tube_.b0 * out.bar - tube_.a1 * tube_.y1 - tube_.a2 * tube_.y2;
        tube_.y2 = tube_.y1;
        tube_.y1 = y;
        out.tube = y;
        if (reste < 1e-5f && std::abs(y) < 1e-6f) active_ = false;
        return out;
    }

private:
    struct Mode {
        double phase = 0.0, increment = 0.0;
        float amplitude = 0.0f, dampingFree = 0.999f, dampingFelt = 0.99f;
    };
    struct Tube { float b0 = 0.0f, a1 = 0.0f, a2 = 0.0f, y1 = 0.0f, y2 = 0.0f; };

    /// Le rapport du mode n : barre libre-libre `((2n+1)/3)²`, tirée vers
    /// 1 : 4 : 10 par le creusement. Au-delà du troisième mode, que le
    /// facteur n'accorde pas, la barre creusée garde l'étirement du
    /// troisième.
    static float ratioOf(int n, float undercut) {
        const float k = (2.0f * static_cast<float>(n) + 1.0f) / 3.0f;
        const float libre = k * k;
        static constexpr std::array<float, 3> kAccorde{{1.0f, 4.0f, 10.0f}};
        const float cible = n <= 3 ? kAccorde[static_cast<size_t>(n - 1)]
                                   : libre * (10.0f / ((7.0f / 3.0f) * (7.0f / 3.0f)));
        return libre + (cible - libre) * std::clamp(undercut, 0.0f, 1.0f);
    }

    static float dampingFor(float t60, double sampleRate) {
        return std::exp(-6.9078f / (std::max(0.02f, t60) * static_cast<float>(sampleRate)));
    }

    void frapper(const Params& p) {
        const float velocity = static_cast<float>(velocity_) / 127.0f;
        const float durete = std::clamp(p.hardness + p.velocityToHardness * (velocity - 0.5f), 0.0f, 1.0f);
        const float f0 = 440.0f * std::exp2f((static_cast<float>(note_) - 69.0f) / 12.0f);
        const float x = std::clamp(p.strikeOffset, 0.0f, 1.0f);

        for (int i = 0; i < kModes; ++i) {
            auto& m = modes_[static_cast<size_t>(i)];
            const int n = i + 1;
            const float hz = f0 * ratioOf(n, p.undercut);
            if (hz > static_cast<float>(sampleRate_) * 0.45f) { m.amplitude = 0.0f; continue; }
            // LA FORME DU MODE AU POINT DE FRAPPE : les modes impairs d'une
            // barre libre ont un ventre au centre, les pairs y ont un nœud.
            // Frapper au centre exact tait le second partiel (le 4·f0), et
            // c'est pourquoi on frappe un peu à côté.
            const float arg = static_cast<float>(n) * static_cast<float>(M_PI) * x * 0.5f;
            const float forme = (n % 2 == 1) ? std::abs(std::cos(arg)) : std::abs(std::sin(arg));
            // UN MAILLET DE LAINE N'A PAS D'AIGUS : l'injection tombe avec le
            // rang, d'autant plus vite qu'il est mou.
            const float pente = 0.6f + 3.4f * (1.0f - durete);
            const float injection = std::pow(static_cast<float>(n), -pente);
            m.amplitude = forme * injection * (0.35f + 0.65f * velocity);
            m.phase = 0.0;
            m.increment = static_cast<double>(hz) * vsm::audio::dsp::kTwoPi / sampleRate_;
            const float rang = std::pow(static_cast<float>(n), std::max(0.0f, p.decayTilt));
            m.dampingFree = dampingFor(p.decay / rang, sampleRate_);
            m.dampingFelt = dampingFor(p.damperDecay / rang, sampleRate_);
        }

        // LE TUBE est accordé sur f0, à Q modéré (un tube ouvert d'un côté
        // n'est pas une cloche), et normalisé au gain unité au pic pour que
        // « Resonator Mix » veuille dire ce qu'il dit.
        const double w = 2.0 * vsm::audio::dsp::kPi * std::min(static_cast<double>(f0), sampleRate_ * 0.45) / sampleRate_;
        const double bande = static_cast<double>(f0) / 18.0;
        const double r = std::exp(-vsm::audio::dsp::kPi * bande / sampleRate_);
        // |H| au pic vaut b0 / ((1 - r)·|1 - r·e^{-2jw}|) : on pose b0 égal
        // au dénominateur, et le tube rend au fondamental ce qu'il reçoit.
        const double re = 1.0 - r * std::cos(2.0 * w);
        const double im = r * std::sin(2.0 * w);
        const double gainPic = (1.0 - r) * std::sqrt(re * re + im * im);
        tube_.b0 = static_cast<float>(gainPic);
        tube_.a1 = static_cast<float>(-2.0 * r * std::cos(w));
        tube_.a2 = static_cast<float>(r * r);
        tube_.y1 = tube_.y2 = 0.0f;
    }

    double sampleRate_ = 48000.0;
    std::array<Mode, kModes> modes_{};
    Tube tube_;
    bool active_ = false, held_ = false, pendingStrike_ = false;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class VibraphoneSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 16;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kUndercut = 1, kDecay, kDecayTilt, kDamperDecay, kHardness, kStrikeOffset,
        kVelocityToHardness, kResonatorMix, kMotorSpeed, kMotorDepth, kStereoSpread, kOutputLevel,
    };

    VibraphoneSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Vibraphone (la barre, le tube et le moteur)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<VibraphoneVoice, kMaxVoices> voiceManager_;
    std::atomic<bool> pedal_{false};
    double motorPhase_ = 0.0;   // UN SEUL MOTEUR pour toutes les barres
};

} // namespace vsm::plugins::vibraphone
