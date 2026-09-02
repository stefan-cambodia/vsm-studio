#pragma once
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DecayEnvelope.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/dsp/StringWaveguide.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::sitar {

/// CORDES SYMPATHIQUES ET CHEVALET PLAT — l'instrument qui sonne sur des
/// notes qu'on n'a pas jouées.
///
/// POURQUOI CETTE MACHINE, ET LE PARC LE DISAIT DÉJÀ. « Aucune résonance
/// sympathique entre notes » est une approximation ASSUMÉE de `vsm.piano`,
/// écrite dans son en-tête ; le § 28 d'ARCHITECTURE range la question du côté
/// de la modélisation en la renvoyant à cette machine-là, et le CDC du
/// multisample l'écarte à son tour. Trois documents constatent le trou sans
/// que personne ne le comble. **Aucune machine du parc ne produit de son sur
/// une corde qu'on n'a pas touchée** — et c'est pourtant la définition d'une
/// famille entière d'instruments : sitar, tampura, viole d'amour, hardanger.
///
/// ```
///   pincement ──> corde jouée ──> chevalet PLAT (jawari) ──┬──> sortie
///                                                          │
///                        ┌── couplage faible ──────────────┘
///                        v
///     11 cordes SYMPATHIQUES accordées sur la gamme, jamais pincées
///                        │
///                        └──> elles sonnent, et continuent APRÈS le silence
/// ```
///
/// DEUX TRAITS, ET AUCUN N'EXISTE AILLEURS DANS LE PARC.
///
/// **1. Les cordes sympathiques survivent à la note.** Relâchez tout, laissez
/// le release finir : l'instrument continue de sonner pendant des secondes,
/// sur des hauteurs que personne n'a jouées. Partout ailleurs dans le parc,
/// silence des notes égale silence de la machine. Et la réponse est SÉLECTIVE,
/// ce qui est le phénomène même : une note accordée sur une corde sympathique
/// la met en branle ; une note à un demi-ton de là ne la trouve pas, et
/// l'instrument reste presque muet après le relâchement. Le test mesure les
/// deux moitiés, car sans la seconde on aurait seulement fabriqué une réverbe.
///
/// **2. Le jawari fait dépendre le timbre de l'AMPLITUDE, pas du temps.** Le
/// chevalet d'un sitar est PLAT, non anguleux : la corde qui vibre fort vient
/// le lécher, et ce contact intermittent est ce qui produit le bourdonnement
/// caractéristique. La conséquence se mesure et va à l'envers de toutes les
/// cordes du parc : **le son est d'autant plus brillant qu'il est FORT**, si
/// bien qu'une note pincée doucement ne bourdonne pas du tout. Ailleurs — la
/// corde de `vsm.string`, la lame de `vsm.epiano`, le rang aigu de
/// `vsm.chebyshev` — la brillance suit l'enveloppe parce que les aigus se
/// perdent plus vite ; ici elle suit l'AMPLITUDE INSTANTANÉE, ce qui n'est
/// pas la même chose et s'entend sur les notes douces.
///
/// LES CORDES SYMPATHIQUES SONT PARTAGÉES, comme sur l'instrument : c'est un
/// seul jeu de onze cordes derrière le manche, pas onze par voix. Deux notes
/// jouées ensemble les excitent donc ENSEMBLE, et une voix volée ne les
/// interrompt pas — elles ne savent rien des voix.
///
/// APPROXIMATIONS ASSUMÉES (§ 8), statut « dérivé » : le couplage entre la
/// corde jouée et les sympathiques est UNIDIRECTIONNEL. Sur l'instrument, les
/// sympathiques renvoient un peu d'énergie vers le chevalet, donc vers la
/// corde jouée ; ici elles écoutent sans répondre. C'est un choix, et il a une
/// raison qui n'est pas la paresse : un couplage bidirectionnel est une BOUCLE
/// DE RÉACTION entre douze résonateurs à gain proche de un, c'est-à-dire
/// exactement la famille de montages dont ce dépôt a mesuré cinq divergences
/// (§ 33 et § 44 d'ARCHITECTURE, les tentatives de vent). Le sens unique est
/// stable par construction. Le jour où quelqu'un voudra la rétroaction, qu'il
/// écrive d'abord comment il borne son gain de boucle.
class SympatheticBank {
public:
    static constexpr int kMaxStrings = 13;
    /// Les degrés d'une gamme majeure, en demi-tons. Les cordes se répartissent
    /// dessus puis montent d'octave — un accordage de raga simplifié. Ce qui
    /// compte pour la machine n'est pas le choix de la gamme mais le fait
    /// qu'elle laisse des trous : une note hors gamme doit pouvoir ne PAS
    /// trouver de corde, sans quoi le second test ne mesurerait rien.
    static constexpr std::array<int, 7> kDegres{0, 2, 4, 5, 7, 9, 11};

    void prepare(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        for (auto& corde : cordes_) corde.prepare(sampleRate_, 20.0f);
        reset();
    }

    void reset() {
        for (auto& corde : cordes_) corde.reset();
    }

    /// Accorde les cordes à partir d'une tonique MIDI. Hors chemin critique
    /// au sens des allocations (`prepare` a déjà dimensionné les lignes) :
    /// `setTuning` ne fait que du calcul.
    void accorder(float toniqueMidi, int nombre, float t60, float amortissement) {
        nombre_ = std::clamp(nombre, 1, kMaxStrings);
        for (int i = 0; i < nombre_; ++i) {
            const int degre = kDegres[static_cast<size_t>(i % kDegres.size())];
            const int octave = i / static_cast<int>(kDegres.size());
            const float midi = toniqueMidi + static_cast<float>(degre + 12 * octave);
            const float hz = 440.0f * std::exp2f((midi - 69.0f) / 12.0f);
            cordes_[static_cast<size_t>(i)].setTuning(hz, amortissement, 0.0f, t60);
        }
    }

    /// Fait entendre les sympathiques pour un échantillon d'excitation.
    float process(float excitation) {
        float somme = 0.0f;
        for (int i = 0; i < nombre_; ++i) {
            auto& corde = cordes_[static_cast<size_t>(i)];
            const float boucle = corde.advance();
            // Le couplage est FAIBLE : le chevalet transmet une fraction de
            // l'énergie. Trop fort, les sympathiques couvriraient la corde
            // jouée et l'instrument sonnerait comme une réverbe accordée.
            somme += corde.inject(boucle, excitation * kCouplage, 1);
        }
        return somme / std::sqrt(static_cast<float>(std::max(1, nombre_)));
    }

private:
    static constexpr float kCouplage = 0.16f;

    double sampleRate_ = 48000.0;
    std::array<vsm::audio::dsp::StringWaveguide, kMaxStrings> cordes_;
    int nombre_ = 11;
};

class SitarVoice {
public:
    struct Params {
        float decay = 6.0f;
        float damping = 0.22f;
        float pickPosition = 0.22f;
        float jawari = 0.6f;
        float velocitySensitivity = 0.5f;
        float bendSemitones = 0.0f;
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        corde_.prepare(sampleRate_, 20.0f);
        rng_ = vsm::util::DeterministicRng(seed);
        amorti_ = false;
    }

    bool isActive() const { return niveau_ > 1e-5f; }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel;
        note_ = note;
        velocity_ = velocity;
        corde_.reset();
        chevalet1_ = chevalet2_ = 0.0f;
        impulsion_ = 0.0f;
        precedent_ = 0.0f;
        niveau_ = 1.0f;
        amorti_ = false;
        // La salve dure UN QUART DE PÉRIODE : c'est la durée pendant laquelle
        // le plectre reste en contact. Une impulsion d'un seul échantillon,
        // essayée d'abord, étale son énergie sur tout le spectre et ne donne
        // presque pas d'amplitude (rms 0,0019 mesuré) -- la corde existait à
        // peine, et les sympathiques, qu'elle est censée alimenter, encore
        // moins.
        salveRestante_ = std::max(2, static_cast<int>(corde_.loopDelay() * 0.25f));
        salveLongueur_ = salveRestante_;
    }
    /// Relâcher une corde de sitar ne la coupe pas : la main l'ÉTOUFFE, ce qui
    /// est une décroissance rapide et non un silence. Les sympathiques, elles,
    /// n'entendent rien de ce geste — c'est tout l'intérêt.
    void noteOff(uint8_t) { amorti_ = true; }

    float render(const Params& p) {
        if (!isActive()) return 0.0f;

        const float hz = 440.0f * std::exp2f(
            (static_cast<float>(note_) + p.bendSemitones - 69.0f) / 12.0f);
        const float t60 = amorti_ ? kEtouffementSecondes : p.decay;
        corde_.setTuning(hz, p.damping, 0.0f, t60);

        const float velocity = static_cast<float>(velocity_) / 127.0f;
        const float force = 1.0f - p.velocitySensitivity * (1.0f - velocity);

        const float boucle = corde_.advance();

        float drive = 0.0f;
        if (salveRestante_ > 0) {
            // Fenêtre ASYMÉTRIQUE, comme sur la corde du parc : montée brève,
            // puis décroissance. Le plectre libère la corde d'un coup.
            const float phase = std::clamp(
                1.0f - static_cast<float>(salveRestante_) / static_cast<float>(salveLongueur_), 0.0f, 1.0f);
            const float fenetre = std::min(1.0f, phase * 12.0f) * (1.0f - phase);
            bruitLisse_ += 0.4f * (rng_.nextBipolar() - bruitLisse_);
            drive = force * fenetre * bruitLisse_ * 3.5f;
            --salveRestante_;
        }

        const auto contact = static_cast<size_t>(
            std::max(1.0f, std::clamp(p.pickPosition, 0.02f, 0.5f) * corde_.loopDelay()));
        float x = corde_.inject(boucle, drive, contact);

        // --- LE JAWARI : le chevalet PLAT ------------------------------------
        // UNE SÉRIE DE MICRO-CHOCS, et il a fallu DEUX modèles faux avant
        // celui-ci -- les deux écrits dans l'ordre où la mesure les a défaits.
        //
        // 1. Compression douce au-dessus d'un seuil : mesuré, elle faisait
        //    BAISSER la brillance (centroïde 1495 -> 1448 Hz). Arrondir une
        //    crête enlève des harmoniques, c'est l'inverse d'un buzz.
        // 2. Écrêtage DUR : mieux à décroissance courte (+12 %), mais mesuré à
        //    quatre secondes de décroissance il baissait encore (0,971), en
        //    retirant un cinquième de l'énergie (rms 0,0193 -> 0,0154). Un
        //    écrêteur SOUSTRAIT ; il ne peut pas fabriquer le bourdonnement.
        //
        // Ce qu'est réellement le jawari : la corde qui vibre fort vient
        // TOUCHER le chevalet une fois par cycle, et chaque contact est un
        // CHOC. Le bourdonnement est cette série de chocs, à la fréquence de
        // la corde. On la modélise donc en AJOUTANT, à chaque franchissement
        // du seuil, une brève oscillation amortie -- le bruit du contact -- au
        // lieu de raboter la crête.
        //
        // Le seuil descend quand le jawari monte : le chevalet se rapproche.
        // En dessous, il ne se passe RIEN, et c'est le trait de la machine :
        // une note pincée doucement ne touche pas le chevalet du tout.
        if (p.jawari > 0.0f) {
            const float seuil = 0.5f - 0.44f * std::clamp(p.jawari, 0.0f, 1.0f);
            if (x > seuil && precedent_ <= seuil)
                impulsion_ = std::min(0.6f, (x - seuil) * 2.2f * p.jawari);
            precedent_ = x;
            // LE CHEVALET A SON PROPRE MODE, et c'est lui qu'on entend. Une
            // version précédente faisait alterner le choc d'un échantillon à
            // l'autre, ce qui le plaçait à Nyquist sur deux : inaudible, hors
            // de la bande du filtre de sortie, et le centroïde baissait encore
            // (0,970). Un choc n'est pas un contenu « aigu » abstrait, c'est
            // l'excitation d'un objet qui sonne à SA fréquence -- ici la pièce
            // d'os du chevalet, autour de trois kilohertz.
            const float y = kChevaletA1 * chevalet1_ - kChevaletA2 * chevalet2_ + impulsion_;
            chevalet2_ = chevalet1_;
            chevalet1_ = y;
            impulsion_ = 0.0f;
            x += y;
        } else {
            precedent_ = x;
        }

        // Suivi d'amplitude, uniquement pour savoir quand la voix est finie.
        const float absolu = std::abs(x);
        niveau_ = absolu > niveau_ ? absolu : niveau_ + (absolu - niveau_) * kSuiviNiveau;
        return x;
    }

private:
    static constexpr float kEtouffementSecondes = 0.25f;
    /// Résonateur du chevalet : environ 3 kHz, décroissance de quelques
    /// millisecondes. `a1 = 2·r·cos(w)`, `a2 = r²`, à 48 kHz.
    static constexpr float kChevaletA1 = 1.6180f;
    static constexpr float kChevaletA2 = 0.9880f;
    /// Coefficient de descente du suiveur d'amplitude (environ 0,5 s).
    static constexpr float kSuiviNiveau = 0.00005f;

    double sampleRate_ = 48000.0;
    vsm::audio::dsp::StringWaveguide corde_;
    vsm::util::DeterministicRng rng_{0x5117A5ULL};
    float niveau_ = 0.0f;
    float bruitLisse_ = 0.0f;
    float precedent_ = 0.0f;
    float impulsion_ = 0.0f;
    float chevalet1_ = 0.0f, chevalet2_ = 0.0f;
    int salveRestante_ = 0;
    int salveLongueur_ = 1;
    bool amorti_ = false;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class SitarSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 6;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kSympatheticLevel = 1, kSympatheticCount, kSympatheticDecay, kSympatheticRoot,
        kSympatheticDamping,
        kJawari, kDecay, kDamping, kPickPosition,
        kCutoff, kResonance,
        kVelocitySensitivity, kOutputLevel,
    };

    SitarSynth();

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
    const char* machineName() const override { return "Sitar (les cordes qu'on ne joue pas)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<SitarVoice, kMaxVoices> voiceManager_;
    SympatheticBank sympathiques_;
    vsm::audio::dsp::StateVariableFilter filtre_;
    std::atomic<float> bendSemitones_{0.0f};
};

} // namespace vsm::plugins::sitar
