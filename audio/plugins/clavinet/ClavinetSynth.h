#pragma once
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
#include <vector>

namespace vsm::plugins::clavinet {

/// LE CLAVINET — la corde que la TOUCHE tient contre l'enclume, et qui sonne
/// ENTIÈRE, plus bas, l'instant où on la lâche.
///
/// POURQUOI CETTE MACHINE, ET POURQUOI ELLE N'EST NI `vsm.clavichord` NI
/// `vsm.epiano`. Le clavicorde (H15) presse la corde avec la tangente : appuyer
/// plus fort MONTE la note. Le clavinet (Hohner D6, 1971) fait autre chose :
/// un embout de caoutchouc sous la touche frappe la corde et la TIENT contre
/// une enclume pendant toute la note — la longueur qui sonne va de l'enclume
/// au chevalet, et le reste de la corde, derrière, ne sonne pas. Quand la
/// touche remonte, l'embout lâche : la corde ENTIÈRE vibre un instant, à sa
/// hauteur propre, plus BASSE, jusqu'à ce que la laine tressée au bout la
/// taise en quelques centièmes de seconde. C'est le « thump » de relâchement
/// qui fait le clavinet, et aucune machine du parc ne relâche une note en
/// changeant sa hauteur. Deux micros sous les cordes (manche, chevalet), en
/// somme ou en différence, et un curseur de sourdine : le reste de l'usine.
///
/// ```
///   touche ──> embout (dureté) ──> CORDE tenue (guide d'ondes de vsm.string)
///                                    │ relâchement : corde ENTIÈRE, f/(1+derrière), τ laine
///                                    ├──> micro MANCHE (peigne à p·N) ─┐ somme
///                                    └──> micro CHEVALET (peigne)      ─┘ ou différence ──> tonalité
/// ```
///
/// APPROXIMATIONS ASSUMÉES (§ 8), statut « dérivé » : la part de corde
/// derrière l'embout est un réglage, pas la géométrie de chaque touche ; les
/// micros sont des peignes sur la sortie de la boucle (pas une lecture dans
/// la ligne) ; le claquement du relâchement est une salve, pas le contact.
class ClavinetVoice {
public:
    struct Params {
        float tipHardness = 0.7f;
        float decay = 3.0f;              // T60 tenue
        float mute = 0.0f;               // curseur de sourdine : raccourcit la tenue
        float stringBehind = 0.35f;      // part de corde derrière l'embout (hauteur de relâchement)
        float yarnDamping = 0.08f;       // T60 sous la laine, après le relâchement
        float releaseClick = 0.5f;
        float velocitySensitivity = 0.7f;
        float pickupMix = 0.5f;          // 0 manche, 1 chevalet
        bool pickupDifference = false;   // A − B au lieu de A + B
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        corde_.prepare(sampleRate_, 30.0f);
        rng_ = vsm::util::DeterministicRng(seed);
        peigne_.assign(static_cast<size_t>(sampleRate_ / 30.0) + 8, 0.0f);
        ecrit_ = 0;
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
        std::fill(peigne_.begin(), peigne_.end(), 0.0f);
        niveau_ = 1.0f;
        tenue_ = true;
        salveRestante_ = std::max(3, static_cast<int>(corde_.loopDelay() * 0.2f));
        salveLongueur_ = salveRestante_;
        bruitLisse_ = 0.0f;
    }
    /// LE RELÂCHEMENT : l'embout lâche, la corde entière sonne, la laine
    /// l'étouffe. Un claquement pour le départ de l'embout.
    void noteOff(uint8_t) {
        if (!tenue_) return;
        tenue_ = false;
        salveRestante_ = std::max(2, static_cast<int>(corde_.loopDelay() * 0.05f));
        salveLongueur_ = salveRestante_;
        relachementSalve_ = true;
    }

    float render(const Params& p) {
        if (!isActive()) return 0.0f;
        const float hzTenue = 440.0f * std::exp2f((static_cast<float>(note_) - 69.0f) / 12.0f);
        // Tenue : la longueur enclume-chevalet. Lâchée : la corde ENTIÈRE,
        // plus longue de la part derrière l'embout, donc plus basse.
        const float hz = tenue_ ? hzTenue : hzTenue / (1.0f + std::clamp(p.stringBehind, 0.0f, 1.0f));
        const float t60 = tenue_ ? p.decay * (1.0f - 0.9f * std::clamp(p.mute, 0.0f, 1.0f))
                                 : std::max(0.02f, p.yarnDamping);
        corde_.setTuning(hz, 0.25f, 0.02f, t60);

        const float velocity = static_cast<float>(velocity_) / 127.0f;
        const float force = 1.0f - p.velocitySensitivity * (1.0f - velocity);
        float drive = 0.0f;
        if (salveRestante_ > 0) {
            const float phase = std::clamp(
                1.0f - static_cast<float>(salveRestante_) / static_cast<float>(salveLongueur_), 0.0f, 1.0f);
            const float fenetre = std::min(1.0f, phase * 10.0f) * (1.0f - phase);
            const float lissage = 0.2f + 0.75f * p.tipHardness;
            bruitLisse_ += lissage * (rng_.nextBipolar() - bruitLisse_);
            const float gain = relachementSalve_ ? 1.2f * p.releaseClick : 2.6f * force;
            drive = fenetre * bruitLisse_ * gain;
            if (--salveRestante_ == 0) relachementSalve_ = false;
        }
        const auto contact = static_cast<size_t>(std::max(1.0f, 0.12f * corde_.loopDelay()));
        const float boucle = corde_.advance();
        const float x = corde_.inject(boucle, drive, contact);

        // LES MICROS. Le déplacement de la corde à la position p (du
        // chevalet, en fraction de la longueur qui sonne) est la somme de
        // l'onde aller et de l'onde retour : y_p(t) = x(t − (1−p)·N/2) −
        // x(t − (1+p)·N/2), N étant le tour de boucle. Le rang n y pèse
        // sin(n·π·p), à une phase temporelle qui NE dépend PAS de p — c'est
        // ce retard de propagation (1−p)·N/2 qui le garantit, et sans lui
        // deux micros symétriques s'annulaient en somme au lieu de se
        // retrancher. Les deux micros du D6 sont près des deux bouts : en
        // différence, les rangs impairs (même signe aux deux bouts) se
        // creusent et les pairs se doublent — le son « nasal ».
        const size_t capacite = peigne_.size();
        peigne_[ecrit_] = x;
        const float N = corde_.loopDelay();
        auto lire = [&](float retardEchantillons) {
            const size_t retard = std::clamp(static_cast<size_t>(std::max(0.0f, retardEchantillons)), size_t{0}, capacite - 2);
            return peigne_[(ecrit_ + capacite - retard) % capacite];
        };
        auto micro = [&](float position) {
            return lire((1.0f - position) * N * 0.5f) - lire((1.0f + position) * N * 0.5f);
        };
        const float chevalet = micro(0.08f);
        const float manche = micro(0.90f);
        ecrit_ = (ecrit_ + 1) % capacite;
        const float mix = std::clamp(p.pickupMix, 0.0f, 1.0f);
        const float sortie = p.pickupDifference ? (manche * (1.0f - mix) - chevalet * mix) * 1.4f
                                                : manche * (1.0f - mix) + chevalet * mix;

        const float absolu = std::abs(x);
        niveau_ = absolu > niveau_ ? absolu : niveau_ + (absolu - niveau_) * 0.0003f;
        return sortie;
    }

private:
    double sampleRate_ = 48000.0;
    vsm::audio::dsp::StringWaveguide corde_;
    vsm::util::DeterministicRng rng_{0x434C4156ULL};   // "CLAV"
    std::vector<float> peigne_;
    size_t ecrit_ = 0;
    float niveau_ = 0.0f, bruitLisse_ = 0.0f;
    int salveRestante_ = 0, salveLongueur_ = 1;
    bool tenue_ = false, relachementSalve_ = false;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class ClavinetSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 12;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kTipHardness = 1, kDecay, kMute, kStringBehind, kYarnDamping, kReleaseClick,
        kVelocitySensitivity, kPickupMix, kPickupPhase, kCutoff, kOutputLevel,
    };

    ClavinetSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Clavinet (la corde qui sonne entière au relâchement)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<ClavinetVoice, kMaxVoices> voiceManager_;
    vsm::audio::dsp::StateVariableFilter filtre_;
};

} // namespace vsm::plugins::clavinet
