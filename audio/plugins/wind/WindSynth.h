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
#include <vector>

namespace vsm::plugins::wind {

/// Vents — anche et lèvres, par guide d'ondes.
///
/// POURQUOI CETTE MACHINE. C'était la DERNIÈRE case vide du tableau de
/// couverture des sources (§ 1 de docs/CDC-machines-manquantes.md) : « cuivres,
/// bois — non couvert : ni corde ni lame, il faudrait un modèle à anche ou à
/// lèvre ». La voici. Elle ne complète pas un catalogue, elle ferme un trou :
/// un stem de saxophone, de clarinette ou de cuivres n'avait aucune machine
/// cible, et le sampler, qui servait de repli universel, est désormais réservé
/// à la voix.
///
/// LE MODÈLE : UN TUYAU, ET UNE VALVE QUI L'ENTRETIENT
///
/// ```
///   souffle ──> valve non linéaire (anche / lèvres) ──> tuyau ──> pavillon
///                        ^                                 │
///                        └─────── pression de retour ───────┘
/// ```
///
/// Ce n'est PAS une enveloppe qui module un oscillateur : c'est une boucle de
/// réaction. Le souffle ouvre la valve, l'onde part dans le tuyau, revient, et
/// c'est cette pression de retour qui referme la valve. L'oscillation naît de
/// ce dialogue, comme dans l'instrument. On n'a donc rien à faire pour obtenir
/// l'attaque, l'accroche, le petit retard d'établissement : ils sont dans la
/// physique.
///
/// CE QU'ELLE COUVRE, ET CE QU'ELLE NE COUVRE PAS — mesuré, pas supposé
///
/// Un tuyau cylindrique fermé du côté de la valve ne résonne que sur les
/// harmoniques IMPAIRES. C'est la clarinette, son creux caractéristique et son
/// saut à la douzième plutôt qu'à l'octave ; c'est aussi, en première
/// approximation, la famille des cuivres. **La machine couvre cela, et bien.**
///
/// Elle ne couvre PAS les perces coniques — saxophone, hautbois, basson — ni
/// les flûtes, et ce n'est pas faute d'avoir essayé. Le tableau ci-dessous est
/// le résultat de l'expérience, quatre topologies de boucle mises à l'épreuve
/// avec la même anche :
///
/// ```
///   cylindre (réflexion inversante, D/2)   oscille, harmoniques impaires
///   non inversante, D                      NE S'AMORCE PAS
///   non inversante, D, + dérivateur        DIVERGE
///   inversante, D/2, + dérivateur          DIVERGE
/// ```
///
/// La raison est structurelle et vaut la peine d'être écrite, parce qu'elle
/// évitera d'y revenir : une boucle à réflexion inversante et demi-longueur
/// impose `x(t + T/2) = -x(t)`. Cette SYMÉTRIE DEMI-ONDE interdit
/// mathématiquement les harmoniques paires, quel que soit le filtre qu'on
/// place dans la boucle. Un premier essai a cru pouvoir les faire apparaître
/// par un évasement (passe-tout à coefficient positif, qui allonge le chemin
/// des aigus) : mesuré, la deuxième harmonique reste à 0,0001 contre 0,04 pour
/// la troisième, sur toute la course du réglage. Le réglage ne faisait rien,
/// et un réglage qui ne fait rien est pire qu'un réglage absent — il ment, et
/// il coûte une dimension à la recherche. Il a donc été retiré.
///
/// Ce qui casserait la symétrie est la réflexion à l'apex d'un cône, qui n'est
/// pas un simple changement de signe mais un filtre du premier ordre. Les deux
/// topologies essayées dans cette direction divergent, faute d'un gain de
/// boucle borné. C'est du travail à part entière, et il n'est pas fait.
///
/// POURQUOI PAS `dsp::StringWaveguide`. La boucle se ressemble, mais trois
/// choses diffèrent et chacune compte : la réflexion peut être INVERSANTE (une
/// corde ne l'est jamais), il n'y a pas de dispersion (un tuyau d'air n'est pas
/// raide), et la perte n'est pas un amortissement interne mais un RAYONNEMENT
/// par le pavillon, qui emporte d'autant plus d'énergie que la fréquence est
/// haute. Partager la brique aurait demandé de lui ajouter trois options dont
/// la corde n'a que faire ; c'est le cas inverse de `vsm.piano`, qui partage
/// EXACTEMENT la même physique.
///
/// APPROXIMATIONS ASSUMÉES (§ 8 de CDC-nouvelle-machine.md, § 27
/// d'ARCHITECTURE.md) — aucune mesure sur un instrument réel, statut
/// « dérivé » :
///
///  - **Perce sans trous.** Un vrai bois change de longueur ACOUSTIQUE en
///    ouvrant des trous, ce qui déplace aussi son timbre note à note. Ici la
///    longueur suit la hauteur, comme sur un instrument idéal.
///  - **Pavillon réduit à un filtre de perte.** Un vrai pavillon rayonne
///    différemment selon la fréquence ET la direction ; on n'en garde que la
///    perte croissante avec la fréquence.
///  - **Le cuivre « brille » par une non-linéarité de boucle**, pas par la
///    propagation non linéaire réelle dans le tube (qui raidit l'onde le long
///    du parcours). L'effet audible est là — plus on souffle, plus ça claque —
///    le mécanisme est simplifié.
///  - **Polyphonique à quatre voix.** Un instrument à vent est monophonique ;
///    une voix EST un instrumentiste. La polyphonie sert à reconstruire un
///    PUPITRE, et c'est le seul usage qu'elle prétend couvrir.

/// Tuyau : ligne à retard, réflexion (directe ou inversante), perte de
/// rayonnement, et retard fractionnaire pour que la hauteur soit juste.
class Bore {
public:
    void prepare(double sampleRate, float lowestHz) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        lowestHz_ = std::max(1.0f, lowestHz);
        line_.assign(static_cast<size_t>(sampleRate_ / static_cast<double>(lowestHz_)) + 8, 0.0f);
        reset();
    }

    void reset() {
        std::fill(line_.begin(), line_.end(), 0.0f);
        writeIndex_ = 0;
        lossState_ = 0.0f;
        allpassX1_ = allpassY1_ = 0.0f;
    }

    /// Le tuyau est TOUJOURS fermé côté valve : c'est la condition pour qu'une
    /// anche ou des lèvres puissent l'entretenir. La réflexion y est donc
    /// inversante, et la boucle ne fait que la moitié du chemin pour une
    /// période acoustique.
    ///
    void setTuning(float hz, float bellDamping) {
        hz = std::clamp(hz, lowestHz_, static_cast<float>(sampleRate_) * 0.25f);
        const float total = static_cast<float>(sampleRate_) / hz * 0.5f;
        sign_ = -1.0f;

        // Perte au pavillon : d'autant plus forte que la fréquence est haute,
        // parce qu'un pavillon rayonne mieux l'aigu -- et ce qui est rayonné
        // ne revient pas.
        lossB_ = 0.05f + 0.44f * std::clamp(bellDamping, 0.0f, 1.0f);

        const float remainder = total - lossB_;
        float integerPart = std::floor(remainder - 0.5f);
        if (integerPart < 2.0f) integerPart = 2.0f;
        const float maxInteger = static_cast<float>(line_.size() - 2);
        if (integerPart > maxInteger) integerPart = maxInteger;
        const float fraction = std::max(0.05f, remainder - integerPart);
        delaySamples_ = static_cast<size_t>(integerPart);
        allpassA_ = (1.0f - fraction) / (1.0f + fraction);
    }

    /// Pression de retour au niveau de la valve.
    float returning() {
        const size_t capacity = line_.size();
        const size_t readIndex = (writeIndex_ + capacity - delaySamples_) % capacity;
        const float delayed = line_[readIndex];
        const float lossy = (1.0f - lossB_) * delayed + lossB_ * lossState_;
        lossState_ = delayed;
        const float y = allpassA_ * lossy + allpassX1_ - allpassA_ * allpassY1_;
        allpassX1_ = lossy;
        allpassY1_ = y;
        return y * sign_;
    }

    /// Renvoie la pression dans le tuyau après injection du débit de la valve.
    float inject(float pressure) {
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
    float lossB_ = 0.2f, lossState_ = 0.0f;
    float allpassA_ = 0.0f, allpassX1_ = 0.0f, allpassY1_ = 0.0f;
    float sign_ = -1.0f;
};

class WindVoice {
public:
    struct Params {
        float breathPressure = 0.7f;
        float reedStiffness = 0.5f;
        float brassiness = 0.2f;
        float breathNoise = 0.25f;
        float bellDamping = 0.35f;
        float attackSeconds = 0.06f;
        float releaseSeconds = 0.12f;
        float vibratoRate = 5.0f;
        float vibratoDepth = 0.15f;
        float vibratoDelay = 0.35f;
        float velocitySensitivity = 0.6f;
        // Molette de hauteur, en demi-tons — la perce se réaccorde, comme la
        // lèvre qui pousse la note. À zéro l'addition est exacte : empreinte
        // inchangée au bit.
        float bendSemitones = 0.0f;
        // Molette de MODULATION (CC 1), 0..1 : profondeur de vibrato AJOUTÉE
        // à celle du panneau, même LFO, même montée. Additif, exact à zéro.
        float wheelVibrato = 0.0f;
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
    /// Valve non linéaire — anche de bois ou lèvres de cuivre.
    ///
    /// L'ouverture varie LINÉAIREMENT avec la différence de pression, puis
    /// SATURE : la valve ne peut ni s'ouvrir au-delà de sa butée ni se fermer
    /// plus que complètement. C'est cette saturation, et elle seule, qui
    /// entretient l'oscillation — sans elle la boucle serait linéaire et
    /// s'amortirait.
    ///
    /// Une première version modélisait l'ouverture par une cloche
    /// `1 - k|Δp|`, ce qui paraissait plus « physique ». Mesuré : à raideur
    /// moyenne, la valve se fermait complètement dès 0,35 de pression, si bien
    /// qu'aucun débit n'entrait dans le tuyau et que la machine ne sonnait
    /// pas du tout (crête 0,013, et qui décroissait). La caractéristique
    /// linéaire saturée est celle des modèles d'anche éprouvés, et c'est elle
    /// qui est retenue.
    ///
    /// La RAIDEUR fait passer d'une anche souple (bois, doux) à des lèvres
    /// tendues (cuivre, mordant) : plus la pente est forte, plus la valve
    /// atteint tôt sa butée et plus le spectre est riche.
    static float reedTable(float pressureDifference, float stiffness) {
        const float slope = -(0.10f + 0.55f * stiffness);
        return std::clamp(0.7f + slope * pressureDifference, -1.0f, 1.0f);
    }

    double sampleRate_ = 48000.0;
    Bore bore_{};

    float breath_ = 0.0f;            ///< enveloppe de souffle, montée et chute
    float attackCoeff_ = 0.01f, releaseCoeff_ = 0.01f;
    float target_ = 0.0f;

    float velocityGain_ = 1.0f;
    double vibratoPhase_ = 0.0;
    double vibratoIncrement_ = 0.0;
    float vibratoRamp_ = 0.0f, vibratoRampCoeff_ = 0.001f;
    float noiseLp_ = 0.0f;

    float dcX1_ = 0.0f, dcY1_ = 0.0f;
    bool active_ = false;
    bool released_ = false;

    vsm::audio::dsp::AnalogDrift drift_;
    vsm::util::DeterministicRng rng_{0x57494E44ULL}; // "WIND"
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class WindSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 4;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kBreathPressure = 1, kReedStiffness, kBrassiness, kBreathNoise,
        kBellDamping, kAttack, kRelease,
        kVibratoRate, kVibratoDepth, kVibratoDelay,
        kToneBass, kToneTreble, kVelocitySensitivity,
        kAnalogCharacter, kOutputLevel,
    };

    WindSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override {
        // Molette de hauteur et molette de modulation (CC 1) ; le reste est
        // refusé en le disant -- le moteur compte le refus.
        if (event.kind == vsm::audio::plugin::MidiControlEvent::Kind::PitchBend) {
            bendSemitones_.store(event.value, std::memory_order_relaxed);
            return true;
        }
        if (event.kind == vsm::audio::plugin::MidiControlEvent::Kind::ControlChange
            && event.index == 1) {
            modWheel_.store(event.value, std::memory_order_relaxed);
            return true;
        }
        return false;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Wind (anche et lèvres)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<WindVoice, kMaxVoices> voiceManager_;
    vsm::audio::dsp::Biquad bassShelf_, trebleShelf_;
    // Molettes de hauteur (demi-tons) et de modulation (CC 1, 0..1), même
    // contrat que params_.
    std::atomic<float> bendSemitones_{0.0f};
    std::atomic<float> modWheel_{0.0f};
};

} // namespace vsm::plugins::wind
