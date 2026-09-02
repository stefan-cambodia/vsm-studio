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

namespace vsm::plugins::clavichord {

/// LE CLAVICORDE — le seul clavier du parc où appuyer plus fort MONTE la note.
///
/// POURQUOI CETTE MACHINE. La tangente de laiton d'un clavicorde ne rebondit
/// pas comme un marteau de piano : elle frappe la corde ET **reste en
/// contact**, définissant elle-même la longueur vibrante. Appuyer plus fort
/// sur une touche déjà enfoncée tend donc la corde et fait monter la note —
/// c'est le *Bebung*, et c'est la seule façon de faire un vibrato sur un
/// instrument à clavier.
///
/// ```
///   touche ──> TANGENTE ──┬── reste en contact : elle EST le sillet
///                         │   (appuyer = tendre = monter)
///        corde vibrante ──┘
///                         └── l'autre bout est tressé de FEUTRE :
///                             touche relâchée, tout s'arrête net
/// ```
///
/// CE QUE LE PARC AVAIT DÉJÀ, ET CE QUI LUI MANQUAIT. `vsm.cs80` reçoit une
/// pression PAR VOIX, mais elle ouvre un filtre : le timbre change, la hauteur
/// non. `vsm.reed` déplace bien la hauteur sous la pression — mais vers le
/// BAS (mesuré : +3,1 à −8,9 cents), la charge d'air alourdissant la lame.
/// **Aucune machine ne la faisait monter**, et aucune ne reliait la pression à
/// la TENSION d'une corde.
///
/// SECOND TRAIT, AUSSI NET : relâcher la touche ÉTOUFFE. La tangente quitte la
/// corde, dont l'autre extrémité est tressée de feutre ; il n'y a ni résonance
/// ni traîne. C'est le contraire du piano à pédale et de `vsm.sitar`, et le
/// contraire aussi de toutes les cordes du parc, où le relâchement ouvre une
/// décroissance. **Ici il coupe**, et le réglage de décroissance de la corde
/// n'y peut rien : ce n'est pas la corde qui décide, c'est le feutre.
///
/// APPROXIMATIONS ASSUMÉES (§ 8), statut « dérivé » : la tension suit la
/// pression par une loi affine, là où la mécanique réelle passe par la flexion
/// de la corde au-dessus de la tangente ; les cordes sont individuelles alors
/// qu'un clavicorde LIÉ en fait partager une à plusieurs touches (ce qui
/// interdit certains intervalles — un trait de jeu réel, mais qui punirait un
/// séquenceur sans le prévenir) ; et le rayonnement du petit coffre n'est pas
/// modélisé, seulement une perte dépendant de la fréquence.
class ClavichordVoice {
public:
    struct Params {
        float decay = 3.0f;           // t60 de la corde, touche ENFONCÉE
        float damping = 0.35f;
        float tangentPosition = 0.13f;
        float pressureToTension = 1.0f;   // ampleur du Bebung
        float velocitySensitivity = 0.7f;
        float bendSemitones = 0.0f;
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        corde_.prepare(sampleRate_, 20.0f);
        rng_ = vsm::util::DeterministicRng(seed);
        niveau_ = 0.0f;
        pression_ = 0.0f;
        pressionLissee_ = 0.0f;
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
        etouffee_ = false;
        pression_ = 0.0f;
        pressionLissee_ = 0.0f;
        salveRestante_ = std::max(2, static_cast<int>(corde_.loopDelay() * 0.12f));
        salveLongueur_ = salveRestante_;
    }

    /// LA TOUCHE RELÂCHÉE COUPE LE SON, elle ne le laisse pas mourir. C'est le
    /// second trait de la machine, et il tient à cette ligne.
    void noteOff(uint8_t) { etouffee_ = true; }

    /// La pression de la touche, entre 0 et 1 : c'est le geste du *Bebung*.
    void setPressure(float p) { pression_ = std::clamp(p, 0.0f, 1.0f); }

    float render(const Params& p) {
        if (!isActive()) return 0.0f;

        // La pression est LISSÉE : un saut de tension ferait un clic, et le
        // doigt d'un claveciniste ne saute pas.
        pressionLissee_ += (pression_ - pressionLissee_) * kLissagePression;

        // LA TANGENTE TEND LA CORDE — la ligne qui porte tout le trait. Deux
        // pour cent de tension en plus au maximum, soit une trentaine de
        // cents : c'est l'ordre de grandeur du Bebung sur l'instrument, où le
        // geste sert à colorer une note tenue, pas à la transposer.
        const float montee = 1.0f + 0.017f * p.pressureToTension * pressionLissee_;
        const float hz = 440.0f * std::exp2f(
            (static_cast<float>(note_) + p.bendSemitones - 69.0f) / 12.0f) * montee;

        // Touche relâchée : le feutre. Le t60 tombe à quelques dizaines de
        // millisecondes, et le réglage `decay` de la corde n'a plus voix au
        // chapitre — c'est ce que le test vérifie.
        const float t60 = etouffee_ ? kEtouffementSecondes : p.decay;
        corde_.setTuning(hz, p.damping, 0.0f, t60);

        const float velocity = static_cast<float>(velocity_) / 127.0f;
        const float force = 1.0f - p.velocitySensitivity * (1.0f - velocity);

        float drive = 0.0f;
        if (salveRestante_ > 0) {
            // La tangente est DURE et petite : sa salve est brève, ce qui
            // donne au clavicorde son attaque métallique et son niveau faible.
            const float phase = std::clamp(
                1.0f - static_cast<float>(salveRestante_) / static_cast<float>(salveLongueur_), 0.0f, 1.0f);
            const float fenetre = std::min(1.0f, phase * 16.0f) * (1.0f - phase);
            bruitLisse_ += 0.6f * (rng_.nextBipolar() - bruitLisse_);
            drive = force * fenetre * bruitLisse_ * 3.0f;
            --salveRestante_;
        }

        const auto contact = static_cast<size_t>(
            std::max(1.0f, std::clamp(p.tangentPosition, 0.02f, 0.5f) * corde_.loopDelay()));
        const float boucle = corde_.advance();
        const float x = corde_.inject(boucle, drive, contact);

        const float absolu = std::abs(x);
        niveau_ = absolu > niveau_ ? absolu : niveau_ + (absolu - niveau_) * kSuiviNiveau;
        return x;
    }

private:
    static constexpr float kEtouffementSecondes = 0.035f;
    static constexpr float kSuiviNiveau = 0.0002f;
    static constexpr float kLissagePression = 0.0008f;

    double sampleRate_ = 48000.0;
    vsm::audio::dsp::StringWaveguide corde_;
    vsm::util::DeterministicRng rng_{0x434C4156ULL};   // "CLAV"
    float niveau_ = 0.0f;
    float bruitLisse_ = 0.0f;
    float pression_ = 0.0f, pressionLissee_ = 0.0f;
    int salveRestante_ = 0, salveLongueur_ = 1;
    bool etouffee_ = false;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class ClavichordSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kPressureToTension = 1, kDecay, kDamping, kTangentPosition,
        kCutoff, kResonance, kVelocitySensitivity, kOutputLevel,
    };

    ClavichordSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Clavichord (le clavier qui vibre)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<ClavichordVoice, kMaxVoices> voiceManager_;
    vsm::audio::dsp::StateVariableFilter filtre_;
    std::atomic<float> bendSemitones_{0.0f};
};

} // namespace vsm::plugins::clavichord
