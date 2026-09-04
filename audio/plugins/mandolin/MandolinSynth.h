#pragma once
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/StringWaveguide.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::mandolin {

/// LA MANDOLINE — les cordes vont par DEUX, et le plectre ne s'arrête pas.
///
/// POURQUOI CETTE MACHINE, ET POURQUOI ELLE N'EST PAS `vsm.string`. Toutes les
/// cordes du parc sont SEULES : une note, une corde. La mandoline, la guitare
/// à douze cordes, le bouzouki, le saz tendent leurs cordes par CHŒURS — deux
/// cordes par note, que le plectre attaque presque ensemble et qui ne sont
/// jamais tout à fait à la même hauteur. Trois traits en découlent :
///
///  1. **LE BATTEMENT.** Deux cordes à quelques cents d'écart battent à la
///     différence de leurs fréquences : l'enveloppe de la note ONDULE, et
///     c'est le chatoiement d'une douze-cordes. Ce n'est pas un chorus posé
///     après coup : c'est deux cordes.
///  2. **L'OCTAVE.** Sur les chœurs graves d'une douze-cordes, la seconde
///     corde est à l'OCTAVE aiguë : la note porte son octave dès la frappe,
///     et un accord grave sonne clair.
///  3. **LE TRÉMOLO DU PLECTRE.** Une mandoline ne tient pas une note : le
///     plectre la REFRAPPE, huit à quatorze fois par seconde, tant que le
///     doigt reste. C'est une technique, pas un effet — la machine la fait
///     tant que la touche est enfoncée, et chaque coup est une nouvelle
///     pince avec son bruit.
///
/// ```
///   plectre (dureté, position) ──> CORDE A (guide d'ondes de vsm.string)  ─┐
///        └─ quelques ms plus tard ─> CORDE B (± cents, ou à l'octave)     ─┴─> Σ
///   trémolo : le plectre revient toutes les 1/rate secondes tant que la touche tient
/// ```
///
/// APPROXIMATIONS ASSUMÉES (§ 8), statut « dérivé » : deux cordes là où la
/// douze-cordes en a deux par chœur et la mandoline deux aussi — mais leur
/// couplage par le chevalet n'est pas modélisé (elles s'ignorent) ; pas de
/// caisse (le corps de `vsm.string` n'est pas repris, le son est celui des
/// cordes) ; le trémolo frappe toujours dans le même sens.
class MandolinVoice {
public:
    struct Params {
        float courseDetune = 6.0f;      // cents entre les deux cordes du chœur
        float octavePair = 0.0f;        // 0 = unisson, 1 = la seconde corde à l'octave
        float strumSpread = 3.0f;       // ms entre la première corde et la seconde
        float tremoloRate = 0.0f;       // Hz, 0 = pas de trémolo
        float pickPosition = 0.12f;
        float pickHardness = 0.85f;
        float decay = 2.5f;
        float damping = 0.25f;
        float velocitySensitivity = 0.7f;
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        cordeA_.prepare(sampleRate_, 40.0f);
        cordeB_.prepare(sampleRate_, 40.0f);
        rngA_ = vsm::util::DeterministicRng(seed);
        rngB_ = vsm::util::DeterministicRng(seed ^ 0x5A5A5A5A5A5AULL);
        niveau_ = 0.0f;
    }
    bool isActive() const { return niveau_ > 1e-5f; }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel;
        note_ = note;
        velocity_ = velocity;
        cordeA_.reset();
        cordeB_.reset();
        niveau_ = 1.0f;
        tenue_ = true;
        pincer();
        depuisPince_ = 0;
    }
    void noteOff(uint8_t) { tenue_ = false; }

    float render(const Params& p) {
        if (!isActive()) return 0.0f;
        const float hz = 440.0f * std::exp2f((static_cast<float>(note_) - 69.0f) / 12.0f);
        const float hzB = hz * std::exp2f(p.courseDetune / 1200.0f) * (p.octavePair >= 0.5f ? 2.0f : 1.0f);
        const float t60 = tenue_ ? p.decay : 0.12f;
        cordeA_.setTuning(hz, p.damping, 0.02f, t60);
        cordeB_.setTuning(hzB, p.damping, 0.02f, t60);

        // LE TRÉMOLO : tant que la touche tient, le plectre revient.
        if (tenue_ && p.tremoloRate > 0.05f) {
            const int periode = static_cast<int>(sampleRate_ / static_cast<double>(p.tremoloRate));
            if (++depuisPince_ >= periode) { pincer(); depuisPince_ = 0; }
        }

        const float velocity = static_cast<float>(velocity_) / 127.0f;
        const float force = 1.0f - p.velocitySensitivity * (1.0f - velocity);
        // DEUX CONTACTS DE PLECTRE, DEUX BRUITS INDÉPENDANTS : chaque corde a
        // son générateur, si bien que retarder la seconde ne change pas un
        // échantillon de la première (le banc le vérifie au bit près).
        auto salve = [&](int& restante, int longueur, float& bruit, vsm::util::DeterministicRng& rng) {
            if (restante <= 0) return 0.0f;
            const float phase = std::clamp(1.0f - static_cast<float>(restante) / static_cast<float>(std::max(1, longueur)), 0.0f, 1.0f);
            const float fenetre = std::min(1.0f, phase * 12.0f) * (1.0f - phase);
            const float lissage = 0.25f + 0.7f * p.pickHardness;
            bruit += lissage * (rng.nextBipolar() - bruit);
            --restante;
            return force * fenetre * bruit * 2.8f;
        };
        const float driveA = salve(salveA_, salveLongueur_, bruitA_, rngA_);
        // La seconde corde attend son tour de plectre.
        float driveB = 0.0f;
        if (attenteB_ > 0) --attenteB_;
        else driveB = salve(salveB_, salveLongueur_, bruitB_, rngB_);

        const auto contactA = static_cast<size_t>(std::max(1.0f, std::clamp(p.pickPosition, 0.02f, 0.5f) * cordeA_.loopDelay()));
        const auto contactB = static_cast<size_t>(std::max(1.0f, std::clamp(p.pickPosition, 0.02f, 0.5f) * cordeB_.loopDelay()));
        const float a = cordeA_.inject(cordeA_.advance(), driveA, contactA);
        const float b = cordeB_.inject(cordeB_.advance(), driveB, contactB);
        const float x = (a + b) * 0.5f;
        const float absolu = std::abs(x);
        niveau_ = absolu > niveau_ ? absolu : niveau_ + (absolu - niveau_) * 0.0002f;
        return x;
    }

    /// La position du plectre au moment de pincer, pour le retard de B.
    void setStrumSpreadSamples(int n) { retardBEchantillons_ = std::max(0, n); }

private:
    void pincer() {
        salveLongueur_ = std::max(3, static_cast<int>(cordeA_.loopDelay() * 0.25f));
        salveA_ = salveLongueur_;
        salveB_ = salveLongueur_;
        attenteB_ = retardBEchantillons_;
        bruitA_ = bruitB_ = 0.0f;
    }

    double sampleRate_ = 48000.0;
    vsm::audio::dsp::StringWaveguide cordeA_, cordeB_;
    vsm::util::DeterministicRng rngA_{0x4D414E44ULL};   // "MAND"
    vsm::util::DeterministicRng rngB_{0x4D414E44ULL ^ 0x5A5A5A5A5A5AULL};
    float niveau_ = 0.0f, bruitA_ = 0.0f, bruitB_ = 0.0f;
    int salveA_ = 0, salveB_ = 0, salveLongueur_ = 1, attenteB_ = 0, retardBEchantillons_ = 144, depuisPince_ = 0;
    bool tenue_ = false;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class MandolinSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kCourseDetune = 1, kOctavePair, kStrumSpread, kTremoloRate, kPickPosition, kPickHardness,
        kDecay, kDamping, kVelocitySensitivity, kOutputLevel,
    };

    MandolinSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Mandolin (les cordes par deux)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<MandolinVoice, kMaxVoices> voiceManager_;
};

} // namespace vsm::plugins::mandolin
