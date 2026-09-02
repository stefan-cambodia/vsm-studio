#pragma once
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::mellotron {

/// LECTURE DE BANDE — un Mellotron : une bande MAGNÉTIQUE par touche, et elle
/// FINIT.
///
/// POURQUOI CETTE MACHINE. Le parc lit déjà des échantillons (`vsm.sampler`,
/// `vsm.multisample`, `vsm.pcmhybrid`), et il les lit comme un ordinateur :
/// une note tenue tient aussi longtemps qu'on la tient, et jouer plus haut
/// relit le même enregistrement plus vite. Un Mellotron ne fait NI l'un NI
/// l'autre, et c'est exactement ce qui lui donne son caractère :
///
/// 1. **LA BANDE FINIT.** Sous chaque touche, un morceau de bande de huit
///    secondes. La touche appuyée, la tête lit ; huit secondes plus tard,
///    la bande est finie et le son S'ARRÊTE — l'enveloppe a beau tenir son
///    sustain, il n'y a plus rien à lire. Aucune autre machine du parc ne
///    s'interrompt d'elle-même, et c'est cette limite qui a écrit la manière
///    de jouer de l'instrument : on relâche pour laisser la bande revenir.
/// 2. **UNE BANDE PAR TOUCHE, DONC PAS DE TRANSPOSITION.** Chaque note a son
///    enregistrement à sa hauteur. Jouer deux octaves plus haut ne raccourcit
///    donc PAS la durée disponible — là où un échantillonneur qui transpose
///    verrait sa bande passer de huit à deux secondes.
/// 3. **LE PLEURAGE EST PROPRE À CHAQUE BANDE.** Chaque brin a son défaut
///    d'entraînement : deux touches tenues ensemble ne pleurent pas en
///    mesure. C'est le contraire exact d'un LFO de vibrato, qui les ferait
///    onduler à l'unisson — et c'est ce qui donne au chœur de Mellotron son
///    grain « vivant » qu'aucun ensemble accordé ne reproduit.
/// 4. **LE RETOUR DE LA TÊTE PREND DU TEMPS.** Touche relâchée, un ressort
///    ramène la bande à son début, mais pas instantanément. Rejouer trop tôt
///    reprend la lecture là où le rembobinage en était : **la seconde note
///    dure moins longtemps que la première.** Les quatre tests mesurent ces
///    quatre faits.
///
/// APPROXIMATION ASSUMÉE, ET ELLE EST GROSSE (§ 8), statut « inspiré » : le
/// CONTENU de la bande n'est pas un enregistrement mais une petite banque de
/// partiels avec son souffle. Ce que cette machine modélise et apporte au
/// parc, c'est le COMPORTEMENT DU TRANSPORT — la bande qui finit, qui pleure
/// et qui se rembobine — et non le timbre d'un orchestre de 1963. Qui veut
/// le timbre passe par `vsm.multisample` et ses profils ; qui veut le
/// comportement vient ici. Le dire est la condition pour que la machine ne
/// mente pas sur ce qu'elle est.
class MellotronVoice {
public:
    static constexpr int kPartials = 6;

    struct Params {
        float tone = 0.45f;          // pente du spectre : sourd ↔ clair
        float hiss = 0.15f;          // souffle de bande
        float wowDepth = 12.0f;      // cents
        float wowRate = 0.6f;        // Hz
        float flutterDepth = 4.0f;   // cents
        float cutoff = 4500.0f;
        float resonance = 0.1f;
        float velocityToLevel = 0.4f;
        float bendSemitones = 0.0f;
        float tapeLength = 8.0f;     // secondes de bande sous la touche
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        env_.setSampleRate(sampleRate_);
        filtre_.setSampleRate(sampleRate_);
        filtre_.reset();
        rng_ = vsm::util::DeterministicRng(seed);
    }

    /// LA BANDE COMMANDE, PAS L'ENVELOPPE. Une voix dont la bande est finie
    /// se déclare LIBRE même si son enveloppe tient encore son sustain : sans
    /// cela, une note tenue immobiliserait sa voix pour toujours. C'est la
    /// traduction, dans la mécanique du parc, du fait qui définit la machine.
    bool isActive() const { return env_.isActive() && !bandeTerminee_; }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }
    void setEnvelope(const vsm::audio::dsp::AdsrSettings& s) { env_.setSettings(s); }

    /// La position de la bande sous cette touche, en secondes. Le synthé la
    /// reprend quand la voix s'éteint : la BANDE appartient à la touche, pas
    /// à la voix qui la lit — c'est ce qui permet au rembobinage de continuer
    /// après que la note s'est tue.
    float positionBande() const { return position_; }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel;
        note_ = note;
        velocity_ = velocity;
        env_.noteOn();
        for (int n = 0; n < kPartials; ++n) phases_[static_cast<size_t>(n)] = 0.0;
        position_ = 0.0f;
        finie_ = false;
        bandeTerminee_ = false;
        fondu_ = 0.0f;
        tempsWow_ = 0.0f;
    }
    void noteOff(uint8_t) { env_.noteOff(); }

    /// LA BANDE REPREND OÙ LE REMBOBINAGE EN EST : c'est le trait n° 4, et il
    /// s'écrit ici parce que `VoiceManager::noteOn` impose sa signature à trois
    /// arguments. Le synthé appelle donc ceci juste après, sur la voix qu'il
    /// vient de recevoir, avec la position que la TOUCHE avait gardée.
    void placerBande(float position) { position_ = position; }

    float render(const Params& p) {
        if (!env_.isActive() || bandeTerminee_) return 0.0f;

        const float dt = 1.0f / static_cast<float>(sampleRate_);

        // --- LE TRANSPORT ----------------------------------------------------
        // La bande défile en TEMPS RÉEL, à un pouce par seconde qui ne sait
        // rien de la note qu'on joue : c'est le trait n° 2. La position ne
        // s'incrémente donc pas d'une phase mais d'un temps.
        if (!finie_) {
            position_ += dt;
            if (position_ >= p.tapeLength) {
                // FIN DE BANDE. On n'ARRÊTE pas net -- une coupure franche
                // claque et s'entendrait comme un défaut -- mais on ouvre un
                // fondu court, celui de la bande qui quitte la tête.
                finie_ = true;
                fondu_ = kFonduSecondes;
            }
        }
        float gainFondu = 1.0f;
        if (finie_) {
            fondu_ -= dt;
            if (fondu_ <= 0.0f) {
                fondu_ = 0.0f;
                // La voix se tait pour de bon : l'enveloppe n'a plus voix au
                // chapitre, et c'est bien le point de toute la machine.
                bandeTerminee_ = true;
                return 0.0f;
            }
            gainFondu = fondu_ / kFonduSecondes;
        }

        // --- LE PLEURAGE, PROPRE À CETTE BANDE -------------------------------
        // Trait n° 3. La phase ET la vitesse du défaut d'entraînement sont
        // tirées du NUMÉRO DE TOUCHE, de façon déterministe : deux rendus du
        // même projet sont identiques au bit près (règle du parc), mais deux
        // touches voisines pleurent différemment.
        const float sel = static_cast<float>(note_);
        const float phaseWow = std::fmod(sel * 0.6180339f, 1.0f);
        const float vitesseWow = 0.75f + 0.5f * std::fmod(sel * 0.7548776f, 1.0f);
        const float phaseFlutter = std::fmod(sel * 0.3247179f, 1.0f);
        tempsWow_ += dt;
        const float wow = std::sin(vsm::audio::dsp::kTwoPi
                                   * (tempsWow_ * p.wowRate * vitesseWow + phaseWow));
        const float flutter = std::sin(vsm::audio::dsp::kTwoPi
                                       * (tempsWow_ * 6.7f + phaseFlutter));
        const float cents = p.wowDepth * wow + p.flutterDepth * flutter;

        // --- LE CONTENU (l'approximation assumée) ----------------------------
        const float hz = 440.0f * std::exp2f(
            (static_cast<float>(note_) + p.bendSemitones - 69.0f) / 12.0f
            + cents / 1200.0f);

        float son = 0.0f;
        for (int n = 0; n < kPartials; ++n) {
            const auto s = static_cast<size_t>(n);
            const double inc = static_cast<double>(hz) * (n + 1) / sampleRate_;
            if (inc >= 0.5) break;                 // rien au-dessus de Nyquist
            phases_[s] += inc;
            if (phases_[s] >= 1.0) phases_[s] -= 1.0;
            const float rang = static_cast<float>(n + 1);
            const float poids = 1.0f / std::pow(rang, 1.8f - p.tone);
            son += poids * static_cast<float>(std::sin(phases_[s] * vsm::audio::dsp::kTwoPi));
        }
        // LE SOUFFLE fait partie du son de l'instrument, pas de ses défauts :
        // un Mellotron muet ne l'est jamais tout à fait.
        son += p.hiss * 0.25f * rng_.nextBipolar();

        const float velocity = static_cast<float>(velocity_) / 127.0f;
        const float niveau = 1.0f - p.velocityToLevel * (1.0f - velocity);
        filtre_.setCutoffHz(p.cutoff);
        filtre_.setResonance(p.resonance);
        return filtre_.process(son) * env_.nextSample() * niveau * gainFondu;
    }

private:
    static constexpr float kFonduSecondes = 0.03f;

    double sampleRate_ = 48000.0;
    std::array<double, kPartials> phases_{};
    vsm::audio::dsp::AdsrEnvelope env_;
    vsm::audio::dsp::StateVariableFilter filtre_;
    vsm::util::DeterministicRng rng_{0x7A9E5ULL};
    float position_ = 0.0f;
    float tempsWow_ = 0.0f;
    float fondu_ = 0.0f;
    bool finie_ = false;
    bool bandeTerminee_ = false;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class MellotronSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kTapeLength = 1, kRewindTime,
        kWowDepth, kWowRate, kFlutterDepth,
        kTone, kHiss,
        kCutoff, kResonance,
        kAttack, kDecay, kSustain, kRelease,
        kVelocitySensitivity, kOutputLevel,
    };

    MellotronSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override {
        if (event.kind == vsm::audio::plugin::MidiControlEvent::Kind::PitchBend) {
            bendSemitones_.store(event.value, std::memory_order_relaxed);
            return true;
        }
        return false;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Mellotron (la bande qui finit)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);
    /// Ramène vers zéro les bandes des touches relâchées, une fois par bloc.
    void rembobiner(int numSamples);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<MellotronVoice, kMaxVoices> voiceManager_;
    /// UNE BANDE PAR TOUCHE, et elle survit à la voix qui l'a lue : c'est ce
    /// qui distingue cette machine d'un échantillonneur, où l'état vit dans
    /// la voix et meurt avec elle.
    std::array<float, 128> positionBande_{};
    std::array<bool, 128> toucheEnfoncee_{};
    std::atomic<float> bendSemitones_{0.0f};
};

} // namespace vsm::plugins::mellotron
