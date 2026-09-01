#pragma once
#include "vsm/audio/dsp/AnalogDrift.h"
#include "vsm/audio/dsp/Biquad.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/StringWaveguide.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>

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
///     excitation ──> [ point de contact : 1 - z^-pD ]
///                          │
///                          v
///                 `dsp::StringWaveguide` ──> corps (résonances)
///
/// La boucle elle-même — amortissement, raideur, retard fractionnaire, gain —
/// vit dans `dsp/StringWaveguide.h`, qu'elle partage avec `vsm.piano` : c'est
/// la MÊME physique, et deux copies d'une même physique divergent toujours.
/// Ce qui reste ici est ce qui distingue cette machine : comment on excite la
/// corde, et ce qui rayonne ensuite.
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
/// le statut honnête est « dérivé ». Celles de la BOUCLE (ligne à retard
/// unique, raideur rognée dans l'aigu) sont documentées dans
/// `dsp/StringWaveguide.h` ; celles qui appartiennent à cette machine :
///
///  - **L'archet agit sur l'onde résultante** et non sur une jonction entre
///    deux ondes : le cycle d'adhérence-décrochement est conservé, sa forme
///    d'onde exacte non.
///  - **Corps en résonances série** et non en modes couplés : trois cloches
///    de Biquad qui colorent, pas une caisse qui rayonne. À `Body Level = 0`
///    la machine est exactement transparente, ce dont une basse électrique a
///    besoin.
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
        // Molette de hauteur, en demi-tons — l'accord de la boucle la suit,
        // comme un doigt qui glisse. À zéro l'addition est exacte : empreinte
        // inchangée au bit.
        float bendSemitones = 0.0f;
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
    /// Table de friction de l'archet : coefficient de réflexion en fonction de
    /// la vitesse relative archet/corde. Forme empirique en |v|^-4, adhérence
    /// totale près de zéro puis décrochement — c'est le cycle de Helmholtz.
    static float bowFriction(float relativeVelocity, float slope) {
        const float s = (relativeVelocity + 0.001f) * slope;
        const float r = std::pow(std::abs(s) + 0.75f, -4.0f);
        return r < 1.0f ? r : 1.0f;
    }

    double sampleRate_ = 48000.0;
    vsm::audio::dsp::StringWaveguide string_;
    size_t contactOffset_ = 20;      ///< point d'attaque, en échantillons

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
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override {
        // La molette de hauteur, comme sur les monophoniques (D0.5) ; le
        // reste est refusé en le disant -- le moteur compte le refus.
        if (event.kind != vsm::audio::plugin::MidiControlEvent::Kind::PitchBend) return false;
        bendSemitones_.store(event.value, std::memory_order_relaxed);
        return true;
    }
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
    // Molette de hauteur (demi-tons), même contrat que params_.
    std::atomic<float> bendSemitones_{0.0f};
};

} // namespace vsm::plugins::string_machine
