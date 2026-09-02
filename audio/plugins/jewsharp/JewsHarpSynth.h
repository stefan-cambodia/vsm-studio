#pragma once
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::jewsharp {

/// LA GUIMBARDE — la seule machine du parc qui REFUSE de suivre le clavier.
///
/// POURQUOI, ET CE N'EST PAS UN DÉFAUT. Une guimbarde a une lame d'acier dont
/// la fréquence est FIXE : elle ne change pas, quoi que fasse le joueur. Ce qui
/// change, c'est la cavité buccale, qui filtre ce bourdon et fait ressortir tel
/// ou tel harmonique. **Le musicien ne joue pas des notes, il joue des
/// FORMANTS sur une note unique** — et une machine qui ferait suivre la
/// hauteur au clavier ne serait pas une guimbarde, ce serait un synthétiseur
/// avec un timbre de guimbarde.
///
/// **C'est le miroir exact de `vsm.vocal`.** Sur la voix, la hauteur chantée
/// bouge et les formants restent où ils sont : c'est ce qui fait reconnaître un
/// « a » à 110 comme à 220 Hz. Ici, terme à terme : la hauteur reste, et c'est
/// le formant qui bouge. Le parc a maintenant les deux faces de la même idée.
///
/// ```
///   lame d'acier, fréquence FIXE ──> bourdon riche en harmoniques
///                                          │
///   note MIDI ──> cavité buccale ──────────┘  (deux résonances qui balaient)
///                                          │
///                                          v   l'harmonique qui tombe dans
///                                              le formant ressort seul
/// ```
///
/// CE QUE LE § 10 DU CDC NOUVELLE-MACHINE Y GAGNE : un cas qu'il n'avait pas.
/// Il traite des machines qui REFUSENT un contrôleur en le disant ; en voici
/// une qui refuse la HAUTEUR de note elle-même. Elle ne peut pas se contenter
/// de l'ignorer en silence — un musicien croirait la machine cassée —, donc
/// elle en fait son geste principal : la note choisit le formant, et la façade
/// comme le mode d'emploi le disent avant qu'on s'en étonne.
///
/// APPROXIMATIONS ASSUMÉES (§ 8), statut « inspiré » : la cavité est rendue
/// par deux résonateurs à Q élevé, là où une bouche réelle en a davantage et
/// les déplace ensemble ; le pincement de la lame est une salve fenêtrée et
/// non un modèle de contact ; et la respiration du joueur — qui module
/// l'intensité et parfois la lame elle-même — n'est pas modélisée.
class JewsHarpVoice {
public:
    struct Params {
        float reedHz = 82.0f;        // LA lame : sa fréquence, la même pour tout
        float formantLow = 300.0f;   // le formant de la note la plus grave
        float formantHigh = 3000.0f; // celui de la plus aiguë
        float formantQ = 0.9f;
        float twang = 0.6f;          // richesse du bourdon
        float decay = 1.6f;
        float velocitySensitivity = 0.5f;
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        formant1_.setSampleRate(sampleRate_);
        formant2_.setSampleRate(sampleRate_);
        formant1_.setMode(vsm::audio::dsp::StateVariableFilter::Mode::BandPass);
        formant2_.setMode(vsm::audio::dsp::StateVariableFilter::Mode::BandPass);
        formant1_.reset();
        formant2_.reset();
        env_.setSampleRate(sampleRate_);
        rng_ = vsm::util::DeterministicRng(seed);
        phase_ = 0.0;
    }

    bool isActive() const { return env_.isActive(); }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }
    void setEnvelope(const vsm::audio::dsp::AdsrSettings& s) { env_.setSettings(s); }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel;
        note_ = note;
        velocity_ = velocity;
        env_.noteOn();
        // La lame repart de la même phase : deux rendus du même projet doivent
        // être identiques au bit près.
        phase_ = 0.0;
    }
    void noteOff(uint8_t) { env_.noteOff(); }

    float render(const Params& p) {
        if (!env_.isActive()) return 0.0f;

        // LA LAME NE SAIT RIEN DE LA NOTE. Cette ligne est tout le trait de la
        // machine : `note_` n'y apparaît pas, et ne doit pas y apparaître.
        const float hz = std::clamp(p.reedHz, 20.0f, 400.0f);
        phase_ += static_cast<double>(hz) / sampleRate_;
        if (phase_ >= 1.0) phase_ -= 1.0;

        // Un bourdon très riche : une guimbarde n'a presque pas de fondamental
        // audible, ce sont ses harmoniques hauts que la bouche va cueillir. On
        // le construit par une impulsion étroite plutôt que par une sinusoïde.
        // LA LARGEUR SE COMPTE EN ÉCHANTILLONS, PAS EN FRACTION DE PÉRIODE, et
        // c'est une mesure qui l'a imposé. Exprimée en fraction (0,09 de la
        // période), l'impulsion faisait cinquante-quatre échantillons de large
        // à 82 Hz : le bourdon n'avait plus rien au-dessus du kilohertz
        // (mesuré : 0,000000 au douzième harmonique), si bien que le formant,
        // même placé à 2 240 Hz, n'avait rien à cueillir — le trait de la
        // machine ne s'entendait pas du tout. En échantillons, la richesse du
        // bourdon ne dépend plus de la lame qu'on a choisie.
        const float largeurEnEchantillons = 1.6f + 9.0f * (1.0f - std::clamp(p.twang, 0.0f, 1.0f));
        const float largeur = largeurEnEchantillons * hz / static_cast<float>(sampleRate_);
        const auto x = static_cast<float>(phase_);
        const float pulse = std::exp(-(x * x) / (largeur * largeur))
                          + std::exp(-((1.0f - x) * (1.0f - x)) / (largeur * largeur));
        const float bourdon = pulse - kContinuDeLImpulsion;

        // LA NOTE JOUE LA CAVITÉ : c'est elle qui décide où sont les deux
        // résonances, donc quel harmonique du bourdon ressort. Balayage
        // logarithmique du grave à l'aigu du clavier, ce qui correspond à la
        // façon dont une bouche s'ouvre.
        const float t = std::clamp((static_cast<float>(note_) - 36.0f) / 48.0f, 0.0f, 1.0f);
        const float f1 = p.formantLow * std::pow(p.formantHigh / p.formantLow, t);
        formant1_.setCutoffHz(std::clamp(f1, 80.0f, 12000.0f));
        formant2_.setCutoffHz(std::clamp(f1 * 1.62f, 80.0f, 14000.0f));
        const float q = std::clamp(p.formantQ, 0.0f, 0.95f);
        formant1_.setResonance(q);
        formant2_.setResonance(q);

        const float velocity = static_cast<float>(velocity_) / 127.0f;
        const float gain = 1.0f - p.velocitySensitivity * (1.0f - velocity);
        const float voisee = formant1_.process(bourdon) + 0.5f * formant2_.process(bourdon);
        return voisee * env_.nextSample() * gain * kGain;
    }

private:
    /// La composante continue de l'impulsion gaussienne, retirée une fois pour
    /// toutes : un train d'impulsions POSITIVES en porte une, et le § 44
    /// raconte ce qu'un continu non retiré coûte.
    static constexpr float kContinuDeLImpulsion = 0.16f;
    static constexpr float kGain = 0.5f;

    double sampleRate_ = 48000.0;
    double phase_ = 0.0;
    vsm::audio::dsp::StateVariableFilter formant1_, formant2_;
    vsm::audio::dsp::AdsrEnvelope env_;
    vsm::util::DeterministicRng rng_{0x4A455753ULL};   // "JEWS"
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class JewsHarpSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 4;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kReedHz = 1, kFormantLow, kFormantHigh, kFormantQ, kTwang,
        kAttack, kDecay, kSustain, kRelease,
        kVelocitySensitivity, kOutputLevel,
    };

    JewsHarpSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    /// LA MOLETTE DE HAUTEUR EST REFUSÉE, ET C'EST COHÉRENT. Une guimbarde n'a
    /// aucun geste de hauteur : sa lame est en acier, on ne la plie pas en
    /// jouant. La refuser plutôt que de l'appliquer à un formant serait mentir
    /// deux fois — le moteur compte le refus (`ignoredControlEvents`), et
    /// l'interface pourra dire pourquoi la modulation ne s'entend pas.
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent&) override { return false; }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Jew's Harp (la note qui ne bouge pas)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<JewsHarpVoice, kMaxVoices> voiceManager_;
};

} // namespace vsm::plugins::jewsharp
