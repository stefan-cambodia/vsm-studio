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

namespace vsm::plugins::musicbox {

/// LA BOÎTE À MUSIQUE — la seule machine du parc qui REFUSE une note.
///
/// POURQUOI, ET C'EST MÉCANIQUE. Chaque note est une LAME d'acier qu'une
/// goupille du cylindre soulève puis lâche. Une fois pincée, la lame met un
/// temps à revenir sous la goupille : **si le cylindre la redemande avant, il
/// n'y a rien à pincer, et la note ne sonne pas du tout.** C'est pour cela
/// qu'un mécanisme comporte souvent deux lames accordées à l'unisson pour les
/// notes répétées — sans quoi le trille est impossible.
///
/// **Aucune autre machine du parc ne refuse une note.** Toutes acceptent
/// n'importe quel débit : au pire une nouvelle note vole une voix, mais elle
/// sonne. Ici elle est MUETTE, et c'est le trait — après `vsm.jewsharp` qui
/// refuse la hauteur, voici une machine qui refuse la note elle-même.
///
/// LE SECOND TRAIT SUIT DU MÊME OBJET. Une lame ENCASTRÉE d'un côté et libre
/// de l'autre a des partiels très écartés : **6,267 · 17,55 · 34,39** fois le
/// fondamental. C'est ce qui donne à la boîte à musique son timbre de verre,
/// où l'on entend à peine autre chose que le fondamental et un aigu lointain.
///
/// **Et `vsm.modal` ne peut pas le faire**, le calcul est fait sur son code :
/// son `ratioOf` interpole entre la corde (2) et la barre LIBRE-LIBRE (2,778)
/// puis applique `spread^((n-1)/10)`, ce qui couvre exactement [1,866 ; 2,978]
/// au second partiel. 6,267 est hors de portée à tout réglage — une barre
/// libre aux deux bouts et une lame encastrée d'un côté sont deux lois
/// différentes, pas deux points d'un même segment.
///
/// APPROXIMATIONS ASSUMÉES (§ 8), statut « dérivé » : le peigne réel a une
/// lame par note et cette machine en donne une par TOUCHE, ce qui revient au
/// même tant qu'on ne joue pas deux octaves de la même note ; le temps de
/// retour est le même pour toutes les lames, alors qu'une lame grave est plus
/// lente ; et la caisse de résonance en bois, qui fait beaucoup du son réel,
/// n'est pas modélisée.
class MusicBoxVoice {
public:
    static constexpr int kModes = 4;
    /// Les rapports d'une lame encastrée-libre (Euler-Bernoulli) : le carré du
    /// rapport des racines de l'équation caractéristique.
    static constexpr std::array<float, kModes> kRatios{1.000f, 6.267f, 17.55f, 34.39f};

    struct Params {
        float decay = 3.5f;
        float decayTilt = 1.1f;
        float brightness = 0.35f;
        float velocitySensitivity = 0.6f;
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
        aPincer_ = true;
    }
    /// Relâcher n'étouffe rien : la lame sonne jusqu'au bout, il n'y a pas de
    /// sourdine dans une boîte à musique.
    void noteOff(uint8_t) {}

    float render(const Params& p) {
        if (!active_) return 0.0f;
        if (aPincer_) {
            pincer(p);
            aPincer_ = false;
        }

        float somme = 0.0f, reste = 0.0f;
        for (int i = 0; i < kModes; ++i) {
            auto& mode = modes_[static_cast<size_t>(i)];
            if (mode.increment <= 0.0) continue;
            somme += static_cast<float>(std::sin(mode.phase)) * mode.amplitude;
            mode.phase += mode.increment;
            if (mode.phase > vsm::audio::dsp::kTwoPi) mode.phase -= vsm::audio::dsp::kTwoPi;
            mode.amplitude *= mode.damping;
            reste += mode.amplitude;
        }
        if (reste < 1e-5f) active_ = false;
        return somme;
    }

private:
    struct Mode {
        double phase = 0.0;
        double increment = 0.0;
        float amplitude = 0.0f;
        float damping = 0.999f;
    };

    void pincer(const Params& p) {
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
            const float pente = 2.4f - 1.6f * std::clamp(p.brightness, 0.0f, 1.0f);
            mode.amplitude = force * std::pow(ratio, -pente);
            mode.phase = 0.0;
            mode.increment = static_cast<double>(hz) * vsm::audio::dsp::kTwoPi / sampleRate_;
            const float tau = std::max(0.05f, p.decay)
                            / std::pow(ratio, std::max(0.0f, p.decayTilt));
            mode.damping = std::exp(-1.0f / (tau * static_cast<float>(sampleRate_)));
        }
    }

    double sampleRate_ = 48000.0;
    std::array<Mode, kModes> modes_{};
    vsm::util::DeterministicRng rng_{0x4D425858ULL};
    bool active_ = false, aPincer_ = false;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class MusicBoxSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 12;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kReturnTime = 1, kDecay, kDecayTilt, kBrightness,
        kVelocitySensitivity, kOutputLevel,
    };

    MusicBoxSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    /// La molette est refusée : on n'accorde pas une lame d'acier en jouant.
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent&) override { return false; }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Music Box (la lame qui doit revenir)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

    /// COMBIEN DE NOTES ONT ÉTÉ REFUSÉES faute de lame revenue. Panne muette
    /// interdite : ce que la machine ignore, elle le compte, pour que
    /// l'interface puisse un jour dire pourquoi un trille ne s'entend pas.
    int refusedNotes() const { return refusees_.load(std::memory_order_relaxed); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event, int position);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<MusicBoxVoice, kMaxVoices> voiceManager_;
    /// Quand chaque lame a été pincée pour la dernière fois, en échantillons
    /// depuis le début du rendu. UNE PAR TOUCHE, comme le peigne en a une par
    /// note.
    std::array<double, 128> dernierPincement_{};
    double horloge_ = 0.0;
    std::atomic<int> refusees_{0};
};

} // namespace vsm::plugins::musicbox
