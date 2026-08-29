#pragma once
#include "vsm/audio/dsp/AnalogDrift.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace vsm::plugins::divider {

/// DIVISEUR DE FRÉQUENCE — les cordes électroniques, et une ARCHITECTURE que le
/// parc n'avait pas.
///
/// POURQUOI CETTE MACHINE. Les trente et une autres se ressemblent sur un point
/// qu'on ne remarque pas tant qu'on n'a que celles-là : **chaque voix a son
/// oscillateur**. On appuie sur trois touches, trois oscillateurs démarrent,
/// libres et indépendants. Les cordes électroniques des années 1970 -- Solina,
/// Logan, Elka -- sont bâties à l'envers : **douze** oscillateurs seulement, un
/// par nom de note, qui tournent EN PERMANENCE au sommet du clavier ; toutes
/// les autres octaves sont obtenues en DIVISANT ces douze-là par deux, par
/// quatre, par huit, à coups de bascules. Une touche n'allume pas un
/// oscillateur : elle ouvre une porte sur une fréquence qui existait déjà.
///
/// LE TRAIT DISTINCTIF, ET IL DÉCOULE DIRECTEMENT DE CETTE ARCHITECTURE. Deux
/// notes à l'OCTAVE viennent forcément du même oscillateur maître : leur
/// rapport est exactement deux, pour toujours, quelle que soit la dérive du
/// composant -- puisque la dérive est COMMUNE. Elles ne peuvent donc pas
/// battre. Sur n'importe quelle autre machine du parc, deux oscillateurs
/// indépendants dérivent chacun de leur côté et l'octave bat lentement.
///
/// C'est ce que le test mesure, et il mesure les deux moitiés : avec la dérive
/// poussée au maximum, l'octave ne bat PAS (l'enveloppe du mélange reste
/// plate), tandis qu'une QUINTE -- deux maîtres différents -- bat. Sans la
/// seconde moitié, le test passerait aussi sur une machine qui n'aurait
/// simplement pas de dérive du tout.
///
/// LE SON, LUI, VIENT DE L'ENSEMBLE. Une corde électronique sans son chorus
/// n'est qu'un orgue pauvre : ce qui fait le timbre est un chorus à TROIS
/// lignes de retard modulées par trois oscillateurs lents de fréquences
/// incommensurables, qui transforme un signal statique en une nappe qui bouge.
/// C'est un effet, mais il est INSÉPARABLE de la machine -- le séparer serait
/// livrer un instrument que personne ne reconnaîtrait --, et un second test
/// vérifie qu'il fait bien quelque chose.
///
/// APPROXIMATIONS ASSUMÉES (§ 8 de `CDC-nouvelle-machine.md`), statut
/// « dérivé », aucune mesure sur un instrument réel :
///
///  - **Les diviseurs sont parfaits.** Un vrai diviseur à bascules produit une
///    onde carrée dont les fronts ont une gigue ; ici la division est exacte.
///  - **PARAPHONIQUE, et c'est fidèle** : une seule enveloppe par note (attaque
///    et extinction, rien d'autre), pas de filtre par voix. Ces machines
///    n'avaient qu'un filtre global et deux registres.
///  - **Deux registres** (16 et 8 pieds) au lieu des quatre de certains
///    modèles : ce sont les deux qui font le son de cordes, et un registre de
///    plus serait un réglage de plus à chercher pour peu de couverture.
///  - **Le chorus est à trois voix**, comme l'ensemble d'origine, mais ses
///    retards sont interpolés linéairement là où un BBD analogique a sa propre
///    couleur.

/// Un oscillateur maître : une dent de scie libre, qui ne s'arrête jamais.
class Master {
public:
    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate;
        drift_.setSampleRate(sampleRate);
        drift_.setSeed(seed);
        drift_.setRateHz(0.07f);
        drift_.setAmount(0.0f);
    }
    void setBaseHz(float hz) { baseHz_ = hz; }
    void setDriftAmount(float a) { drift_.setAmount(a); }

    /// Avance d'un échantillon et rend la FRÉQUENCE COURANTE du maître, dérive
    /// comprise. C'est cette fréquence que les notes divisent -- et c'est ce
    /// qui les rend inséparables : deux notes à l'octave la reçoivent
    /// identique au même instant, donc leur rapport reste exactement deux quoi
    /// que fasse la dérive.
    ///
    /// UNE ERREUR À NE PAS REFAIRE, ET ELLE ÉTAIT DANS LA PREMIÈRE VERSION :
    /// diviser la PHASE du maître par deux ne donne pas la note à l'octave
    /// inférieure. Une phase qui monte de 0 à 1 à la fréquence du maître,
    /// divisée par huit, monte de 0 à 0,125 -- à la MÊME fréquence, avec un
    /// huitième de l'amplitude. Mesuré : la dent de scie ne franchissait même
    /// plus zéro. Ce qu'une bascule divise, c'est la FRÉQUENCE.
    float advance() {
        hz_ = baseHz_ * std::pow(2.0f, drift_.nextValue() * 0.06f / 12.0f);
        return hz_;
    }
    float hz() const { return hz_; }

private:
    double sampleRate_ = 48000.0;
    vsm::audio::dsp::AnalogDrift drift_;
    float baseHz_ = 440.0f, hz_ = 440.0f;
};

/// Chorus « ensemble » à trois lignes : le son de ces machines.
class Ensemble {
public:
    void prepare(double sampleRate) {
        sampleRate_ = sampleRate;
        // Alloué ICI, jamais dans process() : 60 ms suffisent largement aux
        // retards d'un ensemble, qui vivent autour de 5 à 15 ms.
        ligne_.assign(static_cast<size_t>(sampleRate * 0.06) + 4, 0.0f);
        ecriture_ = 0;
        for (auto& p : phases_) p = 0.0f;
        phases_[1] = 0.37f;
        phases_[2] = 0.71f;
    }

    /// Trois modulations de fréquences INCOMMENSURABLES : c'est ce qui empêche
    /// le chorus de se replier sur un battement unique et audible.
    static constexpr float kRates[3] = {0.61f, 1.03f, 1.57f};

    float process(float entree, float profondeur) {
        if (ligne_.empty()) return entree;
        ligne_[ecriture_] = entree;
        const float taille = static_cast<float>(ligne_.size());
        float somme = 0.0f;
        for (int k = 0; k < 3; ++k) {
            phases_[static_cast<size_t>(k)] += kRates[k] / static_cast<float>(sampleRate_);
            if (phases_[static_cast<size_t>(k)] >= 1.0f) phases_[static_cast<size_t>(k)] -= 1.0f;
            const float lfo = std::sin(static_cast<float>(vsm::audio::dsp::kTwoPi)
                                       * phases_[static_cast<size_t>(k)]);
            const float retardMs = 7.0f + 5.0f * profondeur * lfo + 2.0f * static_cast<float>(k);
            const float retard = retardMs * 0.001f * static_cast<float>(sampleRate_);
            float lecture = static_cast<float>(ecriture_) - retard;
            while (lecture < 0.0f) lecture += taille;
            const size_t i0 = static_cast<size_t>(lecture) % ligne_.size();
            const size_t i1 = (i0 + 1) % ligne_.size();
            const float f = lecture - std::floor(lecture);
            somme += ligne_[i0] * (1.0f - f) + ligne_[i1] * f;
        }
        ecriture_ = (ecriture_ + 1) % ligne_.size();
        // Mélange sec/traité : à profondeur nulle, la machine sort son signal
        // brut, ce qui rend l'effet du réglage mesurable sur toute sa course.
        return entree * (1.0f - 0.5f * profondeur) + somme * (0.33f * profondeur);
    }

private:
    double sampleRate_ = 48000.0;
    std::vector<float> ligne_;
    size_t ecriture_ = 0;
    std::array<float, 3> phases_{};
};

class DividerSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    /// Douze maîtres : un par nom de note, comme sur l'objet réel.
    static constexpr int kMasters = 12;
    /// Polyphonie : ces machines étaient intégralement polyphoniques -- toutes
    /// les touches à la fois --, ce qu'aucune autre du parc ne fait.
    static constexpr int kMaxNotes = 61;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kLevel16 = 0, kLevel8,
        kEnsemble, kTone,
        kAttack, kRelease,
        kAnalogCharacter, kOutputLevel,
        kNumParams
    };

    DividerSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Divider (cordes électroniques)"; }
    int activeVoiceCount() const override;

private:
    struct Touche {
        vsm::audio::dsp::AdsrEnvelope env;
        /// La phase du DIVISEUR, qui tourne en permanence comme le maître dont
        /// elle descend : une bascule ne s'arrête pas quand on lâche la touche.
        /// Ne jamais la remettre à zéro sur une note -- c'est ce qui distingue
        /// cette machine de toutes les autres du parc.
        ///
        /// DEUX PHASES, UNE PAR REGISTRE, et c'est la même leçon qu'au maître :
        /// le 16 pieds est une DIVISION DE PLUS, donc une bascule de plus, donc
        /// sa propre phase avancée à la moitié de la fréquence. Multiplier la
        /// phase du 8 pieds par un demi ne descend pas d'une octave : ça
        /// réduit l'amplitude en gardant la fréquence -- mesuré, la dent de
        /// scie du 16 pieds ne franchissait plus zéro du tout.
        float phase8 = 0.0f;
        float phase16 = 0.0f;
        uint8_t note = 0;
        bool tenue = false;
    };

    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    std::array<std::atomic<float>, kNumParams> params_;
    std::array<Master, kMasters> masters_;
    std::array<Touche, kMaxNotes> touches_;
    Ensemble ensembleL_, ensembleR_;
    vsm::audio::dsp::StateVariableFilter tone_;
};

} // namespace vsm::plugins::divider
