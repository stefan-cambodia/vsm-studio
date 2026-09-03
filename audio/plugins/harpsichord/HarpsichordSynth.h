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

namespace vsm::plugins::harpsichord {

/// LE CLAVECIN — le clavier du parc qui REFUSE la vélocité, et dont la touche
/// relâchée pince une seconde fois.
///
/// POURQUOI CETTE MACHINE. Un clavecin ne frappe pas ses cordes : un
/// SAUTEREAU monte avec la touche et son bec (une plume, ou du delrin) PINCE la
/// corde en passant. La corde est pincée quand le bec la lâche, et il la lâche
/// toujours de la même façon : appuyer plus vite ou plus fort ne change ni la
/// force du pincement ni le son. C'est le trait qui a fait naître le
/// piano-forte, et c'est ce que le parc n'avait pas : toutes ses cordes
/// écoutent la vélocité. Ici elle est IGNORÉE, en connaissance de cause, et le
/// test le vérifie au bit près.
///
/// ```
///   touche ──> SAUTEREAU monte ──> le BEC pince la corde en passant ──> 8'
///                                       (même force, quelle que soit la touche)     (+ 4' si le registre est tiré)
///   touche relâchée ──> le sautereau RETOMBE : le bec frôle la corde une
///                        seconde fois (pincement faible), puis l'ÉTOUFFOIR de
///                        feutre se pose et coupe
/// ```
///
/// SECOND TRAIT, ET IL S'ENTEND : le relâchement N'EST PAS SILENCIEUX. En
/// retombant, le bec doit repasser la corde ; une languette de peau lui permet
/// de s'effacer, mais elle frôle la corde, et ce frôlement est un petit
/// pincement qu'on entend sur tout clavecin — puis l'étouffoir se pose. Aucune
/// autre machine du parc ne produit un son AU relâchement : le clavicorde
/// coupe net, le piano laisse mourir, les synthés relâchent une enveloppe.
///
/// TROISIÈME TRAIT, LES REGISTRES : un clavecin a plusieurs cordes par touche
/// — le 8' et, un octave au-dessus, le 4' — que des registres mettent en jeu
/// ou retirent d'un geste ; et le JEU DE LUTH pose une languette de peau sur
/// les cordes du 8', qui sonne alors court et mat. C'est une REGISTRATION,
/// comme celle d'un orgue, et non un réglage continu : mais les réglages
/// continus se cherchent, et une registration ne se cherche pas ; ils restent
/// donc continus, avec leurs positions d'usage aux bornes.
///
/// APPROXIMATIONS ASSUMÉES (§ 8), statut « dérivé » : le pincement est une
/// salve brève injectée au point du bec (même modèle que `vsm.string`, sans
/// vélocité) ; le frôlement du relâchement est une salve plus faible, au même
/// point ; les deux cordes d'une touche ne se couplent pas par le chevalet
/// (elles s'additionnent) ; le coffre n'est pas modélisé, seulement une perte
/// dépendant de la fréquence et un filtre de sortie.
class HarpsichordVoice {
public:
    struct Params {
        float decay = 2.5f;           // t60 de la corde, touche enfoncée
        float damping = 0.25f;
        float pluckPosition = 0.09f;  // le bec pince près du sillet : brillant
        float register8 = 1.0f;
        float register4 = 0.35f;
        float releasePluck = 0.5f;    // force du frôlement au relâchement (0 = bec parfait)
        float damperTime = 0.04f;     // temps de pose de l'étouffoir (s)
        float luteStop = 0.0f;        // jeu de luth : la peau sur les cordes du 8'
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        corde8_.prepare(sampleRate_, 20.0f);
        corde4_.prepare(sampleRate_, 40.0f);
        rng_ = vsm::util::DeterministicRng(seed);
        niveau_ = 0.0f;
    }

    /// Une touche ENFONCÉE tient la voix éveillée même quand la corde s'est
    /// tue : sinon le gestionnaire de voix la croit libre et ne lui transmet
    /// pas le relâchement -- et le frôlement du sautereau n'aurait pas lieu.
    bool isActive() const { return niveau_ > 1e-5f || enfoncee_; }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    /// LA VÉLOCITÉ EST REÇUE ET IGNORÉE : le bec pince toujours de la même
    /// façon. C'est la ligne qui porte le premier trait.
    void noteOn(uint8_t channel, uint8_t note, uint8_t /*velocity*/) {
        channel_ = channel;
        note_ = note;
        corde8_.reset();
        corde4_.reset();
        niveau_ = 1.0f;
        enfoncee_ = true;
        relachee_ = false;
        etouffoirPose_ = false;
        salveRestante_ = std::max(3, static_cast<int>(corde8_.loopDelay() * 0.18f));
        salveLongueur_ = salveRestante_;
        forceSalve_ = 1.0f;
    }

    /// LE SAUTEREAU RETOMBE : le bec frôle la corde (un second pincement, plus
    /// faible), puis l'étouffoir se pose. Second trait.
    void noteOff(uint8_t) {
        if (relachee_) return;
        relachee_ = true;
        enfoncee_ = false;
        // La voix REVIT pour le frôlement : une corde éteinte depuis longtemps
        // (touche tenue) s'était endormie sous le seuil d'activité, et le
        // relâchement n'aurait rien pincé -- le premier banc l'a montré.
        niveau_ = std::max(niveau_, 1.0f);
        salveRestante_ = std::max(2, static_cast<int>(corde8_.loopDelay() * 0.10f));
        salveLongueur_ = salveRestante_;
        forceSalve_ = -1.0f;   // signe : le bec passe dans l'autre sens
        etouffoirDans_ = 0;    // compte à rebours avant la pose, en échantillons
    }

    float render(const Params& p) {
        if (!isActive()) return 0.0f;

        const float hz = 440.0f * std::exp2f((static_cast<float>(note_) - 69.0f) / 12.0f);
        // Le jeu de luth pose une peau sur le 8' : la corde meurt vite et
        // perd ses aigus. C'est un amortissement, pas un filtre de sortie.
        const float t60 = etouffoirPose_ ? kEtouffoirSecondes
                        : p.decay * (1.0f - 0.8f * p.luteStop);
        const float amort8 = std::min(1.0f, p.damping + 0.5f * p.luteStop);
        corde8_.setTuning(hz, amort8, 0.02f, t60);
        corde4_.setTuning(hz * 2.0f, p.damping, 0.05f,
                          etouffoirPose_ ? kEtouffoirSecondes : p.decay * 0.7f);

        // L'étouffoir se pose un peu après le relâchement : le temps que le
        // sautereau retombe. Après quoi tout meurt en quelques dizaines de ms.
        if (relachee_ && !etouffoirPose_) {
            etouffoirDans_ += 1;
            if (static_cast<double>(etouffoirDans_) >= p.damperTime * sampleRate_) etouffoirPose_ = true;
        }

        float drive = 0.0f;
        if (salveRestante_ > 0) {
            const float phase = std::clamp(
                1.0f - static_cast<float>(salveRestante_) / static_cast<float>(salveLongueur_), 0.0f, 1.0f);
            // Un bec est raide : la salve est brève et son front raide, ce qui
            // donne l'attaque nasillarde du clavecin.
            const float fenetre = std::min(1.0f, phase * 24.0f) * (1.0f - phase) * (1.0f - phase);
            bruitLisse_ += 0.75f * (rng_.nextBipolar() - bruitLisse_);
            // Le frôlement vaut une fraction du pincement : mesuré au banc,
            // -14 dB sous l'attaque à fond, -20 dB au réglage par défaut --
            // un petit pincement, pas une seconde note.
            const float force = forceSalve_ > 0.0f ? 3.2f : 3.2f * 0.12f * p.releasePluck;
            drive = force * fenetre * (0.7f * bruitLisse_ + 0.3f * (forceSalve_ > 0.0f ? 1.0f : -1.0f));
            --salveRestante_;
        }

        const auto contact8 = static_cast<size_t>(
            std::max(1.0f, std::clamp(p.pluckPosition, 0.02f, 0.5f) * corde8_.loopDelay()));
        const auto contact4 = static_cast<size_t>(
            std::max(1.0f, std::clamp(p.pluckPosition, 0.02f, 0.5f) * corde4_.loopDelay()));
        const float b8 = corde8_.advance();
        const float x8 = corde8_.inject(b8, drive, contact8);
        const float b4 = corde4_.advance();
        const float x4 = corde4_.inject(b4, drive * 0.8f, contact4);
        const float x = x8 * p.register8 + x4 * p.register4;

        const float absolu = std::abs(x8) + std::abs(x4);
        niveau_ = absolu > niveau_ ? absolu : niveau_ + (absolu - niveau_) * kSuiviNiveau;
        return x;
    }

private:
    static constexpr float kEtouffoirSecondes = 0.03f;
    static constexpr float kSuiviNiveau = 0.0002f;

    double sampleRate_ = 48000.0;
    vsm::audio::dsp::StringWaveguide corde8_, corde4_;
    vsm::util::DeterministicRng rng_{0x48415250ULL};   // "HARP"
    float niveau_ = 0.0f;
    float bruitLisse_ = 0.0f;
    float forceSalve_ = 1.0f;
    int salveRestante_ = 0, salveLongueur_ = 1;
    int etouffoirDans_ = 0;
    bool relachee_ = false, etouffoirPose_ = false, enfoncee_ = false;
    uint8_t note_ = 60, channel_ = 0;
};

class HarpsichordSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 10;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kRegister8 = 1, kRegister4, kLuteStop, kPluckPosition, kReleasePluck,
        kDecay, kDamping, kDamperTime, kCutoff, kOutputLevel,
    };

    HarpsichordSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Harpsichord (le clavecin)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<HarpsichordVoice, kMaxVoices> voiceManager_;
    vsm::audio::dsp::StateVariableFilter filtre_;
};

} // namespace vsm::plugins::harpsichord
