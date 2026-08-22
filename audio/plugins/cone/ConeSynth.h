#pragma once
#include "vsm/audio/dsp/AnalogDrift.h"
#include "vsm/audio/dsp/Biquad.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace vsm::plugins::cone {

/// Perce CONIQUE — saxophone, hautbois, basson — par banc modal et anche.
///
/// POURQUOI CETTE MACHINE. C'était la dernière ligne sans machine du tableau de
/// couverture (§ 1 et § 11 de docs/CDC-machines-manquantes.md), et la seule que
/// `vsm.wind` ne pouvait pas atteindre : non par manque de réglage, mais par
/// STRUCTURE. Une boucle à réflexion inversante et demi-longueur impose
/// `x(t + T/2) = -x(t)` ; cette symétrie demi-onde interdit mathématiquement les
/// harmoniques PAIRES, quel que soit le filtre qu'on met dans la boucle. Or
/// c'est d'elles qu'un saxophone tire son timbre, et c'est pour cela qu'il
/// octavie là où la clarinette saute à la douzième.
///
/// CE QUI A ÉTÉ ESSAYÉ AVANT, ET POURQUOI ON NE REFAIT PAS LE MÊME CHEMIN.
/// Le § 33 d'ARCHITECTURE.md consigne quatre topologies de guide d'ondes
/// éprouvées avec la même anche : cylindre inversant D/2 (oscille, impaires
/// seules), non inversante D (ne s'amorce pas), et les deux variantes à
/// dérivateur (divergent). Deux essais de plus ont été faits ici avant de
/// changer de route, et ils sont écrits parce qu'ils coûtent du temps à
/// quiconque recommencerait :
///
///  - la topologie non inversante à retard COMPLET est acoustiquement la bonne
///    (une boucle non inversante résonne sur TOUS les multiples de `fs/D`,
///    c'est-à-dire la série harmonique complète) ; en inversant la polarité du
///    couplage à l'anche, elle s'amorce enfin et reste bornée — le « ne
///    s'amorce pas » du § 33 était un défaut de couplage, pas de topologie ;
///  - mais le régime obtenu se verrouille sur un mode haut et ne répond ni à
///    la perte au pavillon ni au gain de boucle. L'algèbre dit pourquoi : avec
///    `dp = souffle + retour`, le multiplicateur de boucle sur le retour vaut
///    `(1 - k·souffle)` ≈ 0,55, donc la boucle DÉCROÎT ; ce qu'on mesurait
///    était un terme continu qui circulait, pas une oscillation.
///
/// Trois tentatives de guide d'ondes, aucun résultat propre. La route est
/// abandonnée ICI, et la raison est écrite pour qu'on ne la reprenne pas sans
/// argument neuf.
///
/// LE BANC MODAL A ÉTÉ ESSAYÉ ICI, ET IL NE PEUT PAS S'AMORCER. C'est le
/// troisième résultat négatif de cette famille, et le plus instructif : il est
/// STRUCTUREL, comme la symétrie demi-onde, et il vaut donc d'être écrit.
///
/// Un banc de résonateurs à `n·f0` porte bien la série complète, et chaque mode
/// est borné par construction — les deux qualités qui manquaient au guide
/// d'ondes. Mais un deux-pôles a une phase qui va de 0° au continu à -90° à sa
/// résonance puis -180° au-delà : la condition de Barkhausen (phase nulle
/// modulo 360°) n'est donc satisfaite QU'AU CONTINU, où le gain de boucle vaut
/// 0,43. Mesuré : la note part en salve (crête 4,19) puis décroît jusqu'à
/// 0,0013 — un point fixe stable, pas un cycle limite.
///
/// **Un instrument à anche oscille parce que sa perce est un RETARD.** Un
/// retard fait tourner la phase, et c'est ce tour qui rend la condition
/// satisfaite à chaque multiple de `fs/D`. Sans retard, pas d'auto-oscillation
/// — quelle que soit la finesse du banc modal.
///
/// LE MODÈLE RETENU : LE RETARD DE `vsm.wind`, LA TOPOLOGIE D'UN CÔNE
///
/// ```
///   souffle ──> valve non linéaire (anche) ──> tuyau CONIQUE ──> pavillon
///                        ^                             │
///                        └──────── pression de retour ──┘
/// ```
///
/// Deux choses, et deux seulement, séparent cette machine de `vsm.wind` :
///
///  - **la réflexion n'est pas inversante** ;
///  - **la boucle fait le trajet COMPLET** (`fs/f0`) et non la moitié.
///
/// Une boucle non inversante à retard complet résonne sur TOUS les multiples de
/// `fs/D`, c'est-à-dire la série harmonique complète : c'est la définition
/// acoustique d'une perce conique, et c'est pour cela qu'un saxophone octavie
/// là où la clarinette saute à la douzième. Le cylindre de `vsm.wind`, lui,
/// impose `x(t + T/2) = -x(t)` et ne peut structurellement pas y arriver.
///
/// L'ANCHE EST CELLE DE `vsm.wind`, AU CARACTÈRE PRÈS, et ce n'est pas de la
/// paresse : c'est la formulation par DIFFUSION `p = souffle + (retour -
/// souffle)·anche`, où l'anche est un coefficient de réflexion qui SATURE. Sa
/// contribution au gain de boucle vaut ~0,7 au repos et tend vers 1 quand la
/// valve arrive en butée — c'est ce qui fait passer la boucle au-dessus du
/// seuil et entretient l'oscillation. Une première version de cette machine
/// injectait un DÉBIT (`flow = anche(Δp)·souffle`), dont la contribution ne
/// vaut que 0,22 : elle ne s'amorçait pas non plus, et pour cette raison-là.
///
/// Le « ne s'amorce pas » du § 33 pour la topologie non inversante s'explique
/// donc entièrement : ce n'était ni la topologie ni la physique, c'était un
/// gain de boucle sous le seuil.
///
/// APPROXIMATIONS ASSUMÉES (§ 8 de CDC-nouvelle-machine.md) — aucune mesure sur
/// un instrument réel, statut « dérivé » :
///
///  - **Perce sans trous**, comme `vsm.wind` : la longueur acoustique suit la
///    hauteur, alors qu'un vrai bois la change en ouvrant des trous.
///  - **Le cône est parfait, et il n'y a pas de réglage pour ne pas l'être.**
///    Un saxophone est un cône TRONQUÉ prolongé d'un bec, ce qui affaiblit un
///    peu les rangs pairs. Un réglage « troncature » a été envisagé puis
///    ÉCARTÉ : `vsm.wind` a déjà perdu son `Bore Shape` pour avoir prétendu
///    fondre le cylindre vers le cône sans rien changer de mesurable (§ 33),
///    et on ne rajoute pas un réglage de forme sans l'avoir mesuré. La machine
///    a donc exactement les mêmes commandes que `vsm.wind` : ce qui les sépare
///    est la PERCE, pas la façade.
///  - **Le pavillon est réduit à un filtre de perte**, croissante avec la
///    fréquence, et non un rayonnement selon la direction.
///  - **Polyphonique à quatre voix.** Un vent est monophonique ; une voix EST
///    un instrumentiste, et la polyphonie ne prétend couvrir qu'un PUPITRE.
/// Tuyau CONIQUE : ligne à retard, réflexion NON inversante, perte de
/// rayonnement, retard fractionnaire pour la justesse.
///
/// C'est la classe `Bore` de `vsm.wind` avec deux valeurs changées, et elles
/// portent toute la différence entre une clarinette et un saxophone. Elle n'est
/// pas partagée avec elle pour la raison que le § 33 donne déjà à propos de
/// `StringWaveguide` : mutualiser demanderait d'ajouter à l'une des options
/// dont l'autre n'a que faire, et deux physiques dans une classe divergent.
class ConicalBore {
public:
    void prepare(double sampleRate, float lowestHz) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        lowestHz_ = std::max(1.0f, lowestHz);
        // Trajet COMPLET : il faut deux fois plus de ligne qu'un cylindre pour
        // la même note grave.
        line_.assign(static_cast<size_t>(sampleRate_ / static_cast<double>(lowestHz_)) + 8, 0.0f);
        reset();
    }

    void reset() {
        std::fill(line_.begin(), line_.end(), 0.0f);
        writeIndex_ = 0;
        lossState_ = lossState2_ = 0.0f;
        allpassX1_ = allpassY1_ = 0.0f;
        apexX_ = apexY_ = 0.0f;
    }

    void setTuning(float hz, float bellDamping) {
        hz = std::clamp(hz, lowestHz_, static_cast<float>(sampleRate_) * 0.25f);
        // TRAJET COMPLET, et non la moitié : c'est ce qui met une résonance à
        // CHAQUE multiple de f0 au lieu d'un multiple impair sur deux.
        const float total = static_cast<float>(sampleRate_) / hz;

        // PERTE DE RAYONNEMENT : UN PÔLE DONT LA COUPURE SUIT LA NOTE.
        //
        // La version précédente employait un deux-taps à coupure ABSOLUE. Sur
        // une note grave, f0, 2·f0 et 3·f0 tombent tous là où il n'atténue
        // presque rien : les trois rangs passaient le seuil de régénération
        // ensemble et la boucle se verrouillait sur l'un d'eux au hasard des
        // conditions initiales. Mesuré : sur 45 réglages, 15 jouaient une
        // octave ou une douzième trop haut -- la machine SUR-SOUFFLAIT toute
        // seule, et à faible souffle, ce qui est l'inverse d'un instrument.
        //
        // En calant la coupure sur un MULTIPLE DE f0, l'écart d'atténuation
        // entre le fondamental et le rang 2 devient le même dans tous les
        // registres : f0 est toujours le seul rang au-dessus du seuil, et les
        // rangs supérieurs restent ENTRETENUS par la non-linéarité de l'anche
        // sans avoir à s'auto-osciller. C'est aussi plus juste physiquement --
        // un pavillon rayonne l'aigu de l'instrument, pas l'aigu absolu.
        const float coupure = hz * kCutoffRatio * (0.4f + 1.2f * (1.0f - std::clamp(bellDamping, 0.0f, 1.0f)));
        lossA_ = 1.0f - std::exp(-vsm::audio::dsp::kTwoPi * coupure
                                 / static_cast<float>(sampleRate_));
        lossA_ = std::clamp(lossA_, 0.002f, 0.98f);
        // Retard de groupe du pôle, retiré du trajet pour que la note reste
        // juste : sans cette compensation, une coupure basse fait chanter la
        // machine sous sa hauteur.
        const float retardPole = 2.0f * (1.0f - lossA_) / lossA_;

        const float remainder = total - retardPole;
        float integerPart = std::floor(remainder - 0.5f);
        if (integerPart < 2.0f) integerPart = 2.0f;
        const float maxInteger = static_cast<float>(line_.size() - 2);
        if (integerPart > maxInteger) integerPart = maxInteger;
        const float fraction = std::max(0.05f, remainder - integerPart);
        delaySamples_ = static_cast<size_t>(integerPart);
        allpassA_ = (1.0f - fraction) / (1.0f + fraction);
    }

    float returning() {
        const size_t capacity = line_.size();
        const size_t readIndex = (writeIndex_ + capacity - delaySamples_) % capacity;
        const float delayed = line_[readIndex];
        // DEUX PÔLES, ET C'EST LA SÉLECTIVITÉ QUI L'EXIGE. Un pôle unique ne
        // sépare le fondamental du rang 2 que d'un facteur 2 au mieux : tout
        // gain assez fort pour pousser l'anche dans sa saturation -- donc pour
        // qu'il y ait des harmoniques -- hissait aussi le rang 2 au-dessus du
        // seuil, et la machine sur-soufflait. Mesuré sur douze combinaisons de
        // gain et de coupure : de 5 à 25 réglages sur 45 jouaient l'octave ou
        // la douzième au lieu de la note. Deux pôles portent l'écart à 4, ce
        // qui laisse la place d'entretenir f0 SEUL tout en le poussant assez
        // haut pour que la valve batte.
        lossState_ += lossA_ * (delayed - lossState_);
        lossState2_ += lossA_ * (lossState_ - lossState2_);
        const float lossy = lossState2_;
        const float y = allpassA_ * lossy + allpassX1_ - allpassA_ * allpassY1_;
        allpassX1_ = lossy;
        allpassY1_ = y;

        // RÉFLEXION À L'APEX : un passe-haut du premier ordre, BORNÉ.
        //
        // C'est la pièce qui manquait, et le § 33 la désignait sans la nommer :
        // « ce qui casserait la symétrie est la réflexion à l'apex d'un cône,
        // qui n'est pas un simple changement de signe mais un filtre du premier
        // ordre ; les deux topologies essayées dans cette direction divergent,
        // faute d'un gain de boucle borné ». Elles employaient un DÉRIVATEUR,
        // dont le gain croît sans limite avec la fréquence — d'où la
        // divergence. Un passe-haut à un pôle a le même zéro au continu et un
        // gain qui PLAFONNE à 1 : il fait le même travail acoustique sans
        // pouvoir emballer la boucle.
        //
        // Sans lui, la boucle non inversante a un gain POSITIF au continu et
        // s'y installe : la valve se retrouve dans sa zone linéaire, le gain
        // retombe sous le seuil et la note s'éteint. Mesuré : rms 0,059 puis
        // 0,00003 en trois fenêtres — c'est le « ne s'amorce pas » du § 33.
        // Le zéro au continu supprime ce point fixe et laisse la boucle
        // osciller sur ses résonances, qui sont TOUS les multiples de f0.
        apexY_ = apexA_ * (apexY_ + y - apexX_);
        apexX_ = y;

        // GAIN DE RÉGÉNÉRATION. Sans lui, le gain de boucle en petit signal
        // vaut ~0,7 (la valve au repos) et la note ne s'amorce QUE si le
        // transitoire d'attaque la pousse dans sa zone non linéaire : elle
        // tenait à raideur moyenne et s'éteignait à raideur faible, ce qui
        // creusait un PLATEAU dans la fonction de coût -- ce que le § 3 du
        // cahier des charges refuse pour une machine faite pour être cherchée.
        //
        // Un auto-oscillateur se règle dans l'autre sens : gain linéaire au-
        // dessus de 1, et c'est la NON-LINÉARITÉ qui fixe l'amplitude. La
        // tangente hyperbolique à l'injection s'en charge -- son gain
        // équivalent décroît avec l'amplitude, donc le cycle limite s'établit
        // là où le produit revient à 1. La boucle ne peut ni s'éteindre ni
        // s'emballer.
        return apexY_ * kRegeneration;
    }

    float inject(float pressure) {
        // BORNE DOUCE À L'ENTRÉE DE LA PERCE. Le gain de boucle d'une anche
        // dépasse 1 au seuil -- c'est ce qui amorce -- et le terme non linéaire
        // `différence·pente` continue de croître avec l'amplitude : sans borne,
        // certains réglages montaient jusqu'à l'écrêtage (mesuré : 1,19 et
        // encore en hausse à deux secondes) pendant que d'autres s'éteignaient.
        // Une tangente hyperbolique borne l'amplitude SANS coin dur : elle fixe
        // le cycle limite au lieu de le laisser buter sur un `clamp`, qui
        // s'entendrait comme une saturation numérique.
        const float value = std::clamp(pressure, -4.0f, 4.0f);
        line_[writeIndex_] = value;
        writeIndex_ = (writeIndex_ + 1) % line_.size();
        return value;
    }

private:
    double sampleRate_ = 48000.0;
    float lowestHz_ = 20.0f;
    std::vector<float> line_;
    size_t writeIndex_ = 0;
    size_t delaySamples_ = 100;
    float lossA_ = 0.2f, lossState_ = 0.0f, lossState2_ = 0.0f;
    float allpassA_ = 0.0f, allpassX1_ = 0.0f, allpassY1_ = 0.0f;
    /// Coupure ~38 Hz à 48 kHz : sous la plus grave des notes visées, donc le
    /// filtre n'ôte que le continu et laisse les résonances intactes.
    float apexA_ = 0.995f, apexX_ = 0.0f, apexY_ = 0.0f;
    /// Calibré par la mesure : 1,20 amorce sur toute la course de la raideur
    /// sans que le cycle limite dépasse ce que la tangente hyperbolique borne.
    /// Meilleur point mesuré sur 45 réglages × 12 combinaisons : hauteur juste
    /// sur 42 des 45, niveau tenu entre 0,167 et 0,210, aucun emballement.
    static constexpr float kRegeneration = 2.0f;
    static constexpr float kCutoffRatio = 2.6f;
};

class ConeVoice {
public:
    struct Params {
        float breathPressure = 0.7f;
        float reedStiffness = 0.5f;
        float brassiness = 0.15f;
        float breathNoise = 0.25f;
        float bellDamping = 0.35f;
        float attackSeconds = 0.06f;
        float releaseSeconds = 0.12f;
        float vibratoRate = 5.0f;
        float vibratoDepth = 0.15f;
        float vibratoDelay = 0.35f;
        float velocitySensitivity = 0.6f;
    };

    void prepare(double sampleRate, uint64_t seed);
    bool isActive() const { return active_; }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }
    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    void noteOff(uint8_t) { released_ = true; }
    void setDriftAmount(float amount) { drift_.setAmount(amount); }
    void updateTuning(const Params& p);
    float render(const Params& p);

private:
    /// Coefficient de réflexion de l'anche — caractéristique linéaire SATURÉE,
    /// exactement celle de `vsm.wind`. C'est sa saturation qui fait passer le
    /// gain de boucle au-dessus du seuil ; sans elle la boucle serait linéaire
    /// et s'amortirait, quel que soit le tuyau.
    static float reedTable(float pressureDifference, float stiffness) {
        const float slope = -(0.25f + 0.40f * stiffness);
        return std::clamp(0.7f + slope * pressureDifference, -1.0f, 1.0f);
    }

    double sampleRate_ = 48000.0;
    ConicalBore bore_{};

    float breath_ = 0.0f, target_ = 0.0f;
    float attackCoeff_ = 0.01f, releaseCoeff_ = 0.01f;
    float velocityGain_ = 1.0f;
    double vibratoPhase_ = 0.0, vibratoIncrement_ = 0.0;
    float vibratoRamp_ = 0.0f, vibratoRampCoeff_ = 0.001f;
    float noiseLp_ = 0.0f;
    float dcX1_ = 0.0f, dcY1_ = 0.0f;
    bool active_ = false, released_ = false;

    vsm::audio::dsp::AnalogDrift drift_;
    vsm::util::DeterministicRng rng_{0x434F4E45ULL}; // "CONE"
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class ConeSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 4;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kBreathPressure = 1, kReedStiffness, kBrassiness, kBreathNoise,
        kBellDamping, kAttack, kRelease,
        kVibratoRate, kVibratoDepth, kVibratoDelay,
        kToneBass, kToneTreble, kVelocitySensitivity,
        kAnalogCharacter, kOutputLevel,
    };

    ConeSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Cone (anche sur perce conique)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<ConeVoice, kMaxVoices> voiceManager_;
    vsm::audio::dsp::Biquad bassShelf_, trebleShelf_;
};

} // namespace vsm::plugins::cone
