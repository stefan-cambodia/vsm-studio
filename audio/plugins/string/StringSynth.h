#pragma once
#include "vsm/audio/dsp/AnalogDrift.h"
#include "vsm/audio/dsp/Biquad.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <vector>

namespace vsm::plugins::string_machine {

/// Corde pincée et frottée — guide d'ondes, pas échantillonnage.
///
/// POURQUOI CETTE MACHINE (docs/ROADMAP-fusion.md § 1, dernière limite
/// écrite) : « basse, guitare et cordes réelles passent toujours par le
/// sampler faute de modèle dédié ». Le § 1 de
/// docs/CDC-machines-manquantes.md marque trois sources NON COUVERTES —
/// basse électrique, guitare, cordes — et c'est la seule case de couverture
/// qui restait vide après les six machines du § 9. Ce n'est donc pas une
/// machine de caractère de plus : elle ouvre une FAMILLE DE SYNTHÈSE absente
/// du parc, la modélisation physique par guide d'ondes, et c'est la seule qui
/// puisse répondre honnêtement à un stem de basse jouée ou de cordes.
///
/// LE MODÈLE
///
///     excitation ──> [ position de pincement : 1 - z^-pD ]
///                          │
///                          v
///          ┌──> ligne à retard (N échantillons) ──> retard fractionnaire ──┐
///          │                                                              │
///          └── gain de boucle <── dispersion (raideur) <── amortissement <─┘
///                                                                         │
///                                                    corps (résonances) <──┘
///
/// Une seule ligne à retard porte l'aller-retour de l'onde. La boucle contient
/// quatre organes, et chacun répond à un paramètre que le musicien connaît :
///
///  1. **Amortissement** — un filtre passe-bas d'ordre un, `(1-b)x[n] + b
///     x[n-1]`. Il vaut exactement 1 en continu, ce qui est la condition pour
///     que « String Decay » soit vraiment le T60 du fondamental et pas une
///     approximation.
///  2. **Raideur** — trois passe-tout d'ordre un. Une corde raide (une corde
///     de basse filée, un piano grave) transmet les aigus PLUS VITE que les
///     graves : ses partiels ne tombent pas sur des multiples entiers, ils
///     sont progressivement trop hauts. Sans ce terme, une basse sonne comme
///     une harpe.
///
///     Le coefficient de ces passe-tout DÉPEND DE LA NOTE, et c'est la
///     mesure qui l'a imposé. Un coefficient fixe donne une inharmonicité
///     qui suit la fréquence ABSOLUE, pas le rang du partiel : à -0,55, le
///     16e partiel d'un la 440 monte de 39 cents mais celui d'un mi grave à
///     82 Hz de 0,5 cent — c'est-à-dire rien, précisément sur les cordes
///     graves où la raideur s'entend le plus. Pire pour la recherche : à
///     coefficient proportionnel au réglage, les trois premiers quarts de la
///     course du bouton ne produisaient AUCUN effet audible sur une basse,
///     soit exactement la falaise que le § 3 de CDC-machines-manquantes
///     interdit.
///
///     Le coefficient est donc résolu pour que l'inharmonicité VISÉE au 16e
///     partiel soit linéaire dans le réglage : 0 à 25 cents. La loi
///     `|a| = exp(-k · w16)` avec `k = 3,26 · cents^-0,368` a été obtenue en
///     inversant numériquement la réponse du peigne (le facteur k s'est
///     révélé indépendant de la note à 1 % près sur cinq octaves, ce qui est
///     ce qui rend la formule utilisable). La justesse du fondamental n'en
///     souffre pas : l'erreur mesurée reste sous 0,2 cent.
///  3. **Retard fractionnaire** — un passe-tout d'ordre un accordé pour que la
///     boucle fasse exactement SR/f0. Sans lui, la hauteur se quantifierait à
///     l'échantillon près : à 4 kHz, un échantillon de retard vaut plus d'un
///     demi-ton.
///  4. **Gain de boucle** — la décroissance, calculée depuis le T60 demandé.
///
/// DEUX EXCITATIONS, ET UN FONDU CONTINU ENTRE ELLES
///
///  - **Pincement** : une salve de bruit courte injectée en un point de la
///    corde. Le point d'injection produit à lui seul le peigne `1 - z^-pD`,
///    c'est-à-dire le facteur `sin(n.pi.p)` de la corde idéale : pincer au
///    milieu ne peut pas exciter les harmoniques paires. Mais ce facteur ne
///    suffit pas, et la mesure l'a montré — il manquait la PENTE. Le
///    déplacement initial d'une corde pincée est un TRIANGLE, dont le contenu
///    harmonique décroît en 1/n², soit -12 dB par octave. Avec une salve
///    presque blanche, le second harmonique sortait plus fort que le
///    fondamental (0,77 contre 1,00 sur un violoncelle pizzicato réel) — ce
///    qu'aucun instrument à cordes ne fait. La salve passe donc par DEUX
///    passe-bas d'ordre un, dont la fréquence de coupure est ce que règle
///    « Pick Hardness » : très bas pour un pouce (pente franche sur toute
///    l'étendue, timbre rond), plusieurs kilohertz pour un médiator dur.
///  - **Archet** : une force de frottement CONTINUE, table de friction de type
///    stick-slip — l'archet entraîne la corde tant que l'adhérence tient, puis
///    décroche. C'est ce cycle qui fait le son d'un violon, et il n'est pas
///    imitable par une enveloppe.
///
/// Le paramètre `Excitation` fond de l'un à l'autre SANS PALIER. C'est
/// délibéré et c'est la leçon du § 3 de CDC-machines-manquantes : un sélecteur
/// discret creuse une falaise dans la fonction de coût, et cette machine est
/// faite pour être CHERCHÉE par le projet d'analyse, pas seulement jouée.
///
/// APPROXIMATIONS ASSUMÉES (§ 8 de CDC-nouvelle-machine.md, § 27
/// d'ARCHITECTURE.md) — aucune mesure n'a été faite sur un instrument réel,
/// le statut honnête est « dérivé » :
///
///  - **Une seule ligne à retard**, là où la physique en demande deux (onde
///    montante et onde descendante). L'archet agit donc sur l'onde résultante
///    et non sur une jonction entre deux ondes ; le cycle d'adhérence-
///    décrochement est conservé, sa forme d'onde exacte non.
///  - **Corps en résonances série** et non en modes couplés : trois cloches
///    de Biquad qui colorent, pas une caisse qui rayonne. À `Body Level = 0`
///    la machine est exactement transparente, ce qui est ce dont une basse
///    électrique a besoin.
///  - **Raideur rognée dans l'aigu** : le retard qu'exigent les passe-tout de
///    dispersion ne peut pas dépasser 40 % de la boucle, faute de quoi une
///    note très aiguë n'aurait plus de ligne à retard du tout. La raideur
///    demandée est donc réduite au-dessus d'environ 1 kHz — la note reste
///    juste, elle est seulement moins inharmonique que réglée.
///  - **Chaque note repart d'une corde au repos** : on ne modélise pas le
///    repincement d'une corde qui vibre encore.
class StringVoice {
public:
    struct Params {
        float pickPosition = 0.24f;
        float pickHardness = 0.5f;
        float excitation = 0.0f;      ///< 0 = pincée, 1 = frottée, continu
        float bowPressure = 0.5f;
        float bowSpeed = 0.5f;
        float decaySeconds = 4.0f;
        float damping = 0.35f;
        float stiffness = 0.2f;
        float releaseSeconds = 0.25f;
        float velocitySensitivity = 0.7f;
    };

    void prepare(double sampleRate, uint64_t seed);

    bool isActive() const { return active_; }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    void noteOff(uint8_t) { released_ = true; }
    void setDriftAmount(float amount) { drift_.setAmount(amount); }

    /// Règle la géométrie de la boucle pour la note en cours. Appelé une fois
    /// par bloc : c'est là que vivent les `std::pow` et les divisions, pas
    /// dans la boucle par échantillon.
    void updateTuning(const Params& p);

    float render(const Params& p);

private:
    /// Passe-tout d'ordre un, H(z) = (a + z^-1) / (1 + a z^-1).
    /// Retard de phase en continu : (1-a)/(1+a).
    struct Allpass {
        float a = 0.0f, x1 = 0.0f, y1 = 0.0f;
        void reset() { x1 = y1 = 0.0f; }
        float process(float x) {
            const float y = a * x + x1 - a * y1;
            x1 = x;
            y1 = y;
            return y;
        }
    };

    /// Retard de phase en continu d'un passe-tout de coefficient `a`.
    static float allpassDelay(float a) { return (1.0f - a) / (1.0f + a); }

    /// Table de friction de l'archet : coefficient de réflexion en fonction de
    /// la vitesse relative archet/corde. Forme empirique en |v|^-4, adhérence
    /// totale près de zéro puis décrochement — c'est le cycle de Helmholtz.
    static float bowFriction(float relativeVelocity, float slope) {
        const float s = (relativeVelocity + 0.001f) * slope;
        const float r = std::pow(std::abs(s) + 0.75f, -4.0f);
        return r < 1.0f ? r : 1.0f;
    }

    double sampleRate_ = 48000.0;
    std::vector<float> line_;
    size_t writeIndex_ = 0;
    size_t delaySamples_ = 100;      ///< partie entière N de la boucle
    size_t pickOffset_ = 20;         ///< point d'injection, en échantillons
    float loopGain_ = 0.999f;
    float dampingB_ = 0.2f;
    float dampingState_ = 0.0f;
    static constexpr int kDispersionStages = 3;
    Allpass tuning_{};
    std::array<Allpass, kDispersionStages> dispersion_{};

    int pluckRemaining_ = 0;
    int pluckLength_ = 1;
    bool pendingPluck_ = false;      ///< salve demandée, longueur pas encore connue
    float pickLpState_ = 0.0f, pickLpState2_ = 0.0f;
    float pickLpCoef_ = 0.5f;
    float pickNoiseGain_ = 1.0f;     ///< compense l'énergie perdue par le passe-bas
    float pluckGain_ = 1.0f;
    float bowGain_ = 0.0f;

    float dcX1_ = 0.0f, dcY1_ = 0.0f;
    float level_ = 0.0f;             ///< suiveur de crête, décide de la fin de la note
    bool active_ = false;
    bool released_ = false;

    vsm::audio::dsp::AnalogDrift drift_;
    vsm::util::DeterministicRng rng_{0x535452494E47ULL}; // "STRING"
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class StringSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kPickPosition = 1, kPickHardness, kExcitation, kBowPressure, kBowSpeed,
        kStringDecay, kStringDamping, kStiffness, kRelease,
        kBodyLevel, kBodySize, kVelocitySensitivity,
        kDrive, kAnalogCharacter, kOutputLevel,
    };

    StringSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "String (corde pincée / frottée)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<StringVoice, kMaxVoices> voiceManager_;
    std::array<vsm::audio::dsp::Biquad, 3> body_{};
};

} // namespace vsm::plugins::string_machine
