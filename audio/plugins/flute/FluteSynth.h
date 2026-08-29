#pragma once
#include "vsm/audio/dsp/AnalogDrift.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>
#include <cmath>
#include <algorithm>
#include <vector>

namespace vsm::plugins::flute {

/// FLÛTE — un JET D'AIR contre un biseau, et non une anche.
///
/// POURQUOI CETTE MACHINE, ET POURQUOI ELLE N'EST PAS UNE REPRISE DU PROTOTYPE
/// CONIQUE. La dernière ligne sans machine du tableau de couverture réunit trois
/// instruments sous un même intitulé -- « saxophone, hautbois, flûte » --, et
/// c'est une commodité de rédaction qui cache une différence de nature. Les deux
/// premiers sont des ANCHES sur perce conique ; le prototype `vsm.cone` a
/// échoué à les faire osciller par quatre routes différentes, et cette mesure
/// est écrite (§ 33 d'ARCHITECTURE.md, en-tête de `ConeSynth.h`).
///
/// La flûte n'a pas d'anche du tout. Son moteur est un JET d'air laminaire qui
/// vient frapper un biseau et bascule alternativement de part et d'autre : c'est
/// le « son de bord », un mécanisme d'oscillation entièrement différent, que
/// personne n'a tenté ici. Ce n'est donc pas une cinquième tentative sur le même
/// problème, c'est un premier essai sur un autre.
///
/// LE MODÈLE. Deux lignes à retard et une non-linéarité, dans l'esprit des
/// modèles de Verge et de Cook :
///
/// ```
///   souffle ──> jet (retard L/2) ──> non-linéarité ──> tuyau (retard L) ──> son
///                     ^                                      │
///                     └────────── pression de retour ────────┘
/// ```
///
///  - **Le tuyau est OUVERT AUX DEUX BOUTS**, donc sa boucle est NON
///    INVERSANTE à retard complet : il porte la série harmonique COMPLÈTE. Une
///    flûte octavie quand on souffle plus fort, là où une clarinette saute à la
///    douzième -- c'est la même différence que celle qui a motivé la machine
///    conique, obtenue ici par une autre porte.
///  - **Le jet met du temps à traverser la bouche**, et ce retard vaut environ
///    la moitié de celui du tuyau. C'est lui qui décale la réaction et permet à
///    l'oscillation de s'entretenir.
/// LES DÉFAUTS SONT UN POINT DE FONCTIONNEMENT MESURÉ, pas des valeurs
/// choisies au jugé. Balayés sur trois gains de boucle, deux amortissements et
/// deux retards de jet : à gain 0,5, amortissement 0,55 et jet 0,5, le
/// fondamental domine avec un rang 2 à 0,74 -- une flûte. Poussés plus haut,
/// deux régimes apparaissent et aucun n'est celui qu'on veut par défaut : le
/// SURBOUFFLE (le rang 2 passe devant le fondamental, ce qui est juste
/// physiquement mais ne doit pas être l'état de repos) et la SATURATION (une
/// note tenue à pleine échelle). Les deux restent atteignables au réglage.
///
///  - **La non-linéarité est cubique saturante** (`x - x³`, bornée) : c'est la
///    caractéristique d'un jet qui bascule, et c'est elle qui apporte le gain de
///    boucle au démarrage puis le limite une fois l'oscillation établie.
///
/// LE TRAIT DISTINCTIF, et il se mesure : la flûte porte ses harmoniques PAIRES
/// -- son rang 2 est présent -- là où `vsm.wind`, cylindre à anche, ne peut
/// structurellement pas en avoir. Le test est l'exact miroir de
/// `wind_bore_supports_only_odd_harmonics`.
///
/// APPROXIMATIONS ASSUMÉES (§ 8 de `CDC-nouvelle-machine.md`), statut
/// « dérivé », aucune mesure sur un instrument réel :
///
///  - **Perce sans trous** : la longueur acoustique suit la hauteur, alors
///    qu'une vraie flûte la change en ouvrant des cheminées.
///  - **Le souffle est un niveau, pas un geste.** Une attaque de flûte se fait
///    à la langue et son bruit varie pendant la note ; ici le bruit de souffle
///    est stationnaire et réglable.
///  - **Pas de registre.** Une vraie flûte octavie en changeant l'angle du
///    jet ; on peut ici pousser le souffle, mais le passage n'est pas modélisé
///    comme un geste.
///  - **Polyphonique à quatre voix**, comme `vsm.wind` : une flûte est
///    monophonique, et la polyphonie ne prétend couvrir qu'un PUPITRE.

class FluteVoice {
public:
    struct Params {
        float breath = 0.6f;        // pression de souffle
        float noise = 0.15f;        // part de bruit dans le souffle
        float jetRatio = 0.5f;      // retard du jet, en fraction de celui du tuyau
        float jetGain = 0.5f;       // ce que le jet renvoie dans le tuyau
        float damping = 0.55f;      // pertes au pavillon, croissantes avec la fréquence
        float vibratoRate = 5.0f;
        float vibratoDepth = 0.2f;
        float vibratoDelay = 0.4f;
        float velocityToBreath = 0.4f;
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate;
        env_.setSampleRate(sampleRate);
        drift_.setSampleRate(sampleRate);
        drift_.setSeed(seed);
        drift_.setRateHz(0.1f);
        drift_.setAmount(0.0f);
        rng_ = vsm::util::DeterministicRng{seed ^ 0x464C555445ULL};
        // Alloué ICI : de quoi tenir la note la plus grave du clavier.
        bore_.assign(static_cast<size_t>(sampleRate / 20.0) + 4, 0.0f);
        jet_.assign(bore_.size(), 0.0f);
        boreWrite_ = jetWrite_ = 0;
        filtre_ = 0.0f;
        elapsed_ = 0.0f;
    }

    bool isActive() const { return env_.isActive(); }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void setSettings(const vsm::audio::dsp::AdsrSettings& s) { env_.setSettings(s); }
    void setDriftAmount(float a) { drift_.setAmount(a); }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel;
        note_ = note;
        velocity_ = velocity;
        baseHz_ = 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
        env_.noteOn();
        elapsed_ = 0.0f;
        vibPhase_ = 0.0f;
        std::fill(bore_.begin(), bore_.end(), 0.0f);
        std::fill(jet_.begin(), jet_.end(), 0.0f);
        filtre_ = 0.0f;
    }

    void noteOff(uint8_t) { env_.noteOff(); }

    /// LA NON-LINÉARITÉ DU JET : `x - x³`, SATURÉE À SON SOMMET.
    ///
    /// Sa pente vaut 1 au repos -- c'est ce qui apporte le gain de boucle au
    /// démarrage -- et s'annule au sommet, ce qui limite l'oscillation sans
    /// écrêter brutalement.
    ///
    /// UNE ERREUR QUI RENDAIT LA MACHINE MUETTE, et il faut la comprendre pour
    /// ne pas la refaire. La première version bornait l'ENTRÉE à ±1 puis
    /// appliquait `x - x³`. Or ce polynôme S'ANNULE en x = ±1 : un jet poussé à
    /// fond ne rendait donc PLUS RIEN, et la flûte se taisait exactement quand
    /// on soufflait le plus fort. Mesuré : à pression 1,0, sortie nulle, à
    /// toutes les hauteurs. Aucune flûte ne fait ça -- soufflée trop fort, elle
    /// crie ou passe à l'octave, elle ne s'éteint pas.
    ///
    /// La correction est aussi la bonne physique : au-delà du sommet (en
    /// `1/sqrt(3)`), le débit du jet SATURE au lieu de redescendre. On tient
    /// donc la valeur du sommet, signe compris.
    static float jetTable(float x) {
        constexpr float kSommet = 0.57735027f;          // 1/sqrt(3)
        constexpr float kValeur = 0.38490018f;          // 2/(3*sqrt(3))
        if (x > kSommet) return kValeur;
        if (x < -kSommet) return -kValeur;
        return x - x * x * x;
    }

    float render(const Params& p) {
        if (!env_.isActive()) return 0.0f;

        const float vel = static_cast<float>(velocity_) / 127.0f;

        float vibrato = 0.0f;
        if (elapsed_ > p.vibratoDelay) {
            vibPhase_ += p.vibratoRate / static_cast<float>(sampleRate_);
            if (vibPhase_ >= 1.0f) vibPhase_ -= 1.0f;
            const float montee = std::min((elapsed_ - p.vibratoDelay) / 0.3f, 1.0f);
            vibrato = std::sin(static_cast<float>(vsm::audio::dsp::kTwoPi) * vibPhase_)
                    * p.vibratoDepth * montee * 0.3f;
        }
        const float f0 = baseHz_ * std::pow(2.0f,
                            (vibrato + drift_.nextValue() * 0.06f) / 12.0f);

        // LE TUYAU EST OUVERT AUX DEUX BOUTS : retard COMPLET, boucle non
        // inversante, donc série harmonique complète. C'est la différence de
        // structure avec `vsm.wind`, dont le cylindre à anche impose la symétrie
        // demi-onde et interdit les rangs pairs.
        const float retardTuyau = std::clamp(static_cast<float>(sampleRate_) / f0,
                                             4.0f, static_cast<float>(bore_.size() - 2));
        const float retardJet = std::max(2.0f, retardTuyau * std::clamp(p.jetRatio, 0.1f, 0.9f));

        const float retour = lire(bore_, boreWrite_, retardTuyau);

        // Le souffle, plus son bruit : c'est l'air qui passe.
        const float souffle = p.breath * (1.0f + p.velocityToBreath * vel) * env_.nextSample();
        const float excitation = souffle + rng_.nextBipolar() * p.noise * souffle
                               - retour * p.jetGain;

        // Le jet met du temps à traverser la bouche.
        ecrire(jet_, jetWrite_, excitation);
        const float jetRetarde = lire(jet_, jetWrite_, retardJet);
        const float debit = jetTable(jetRetarde);

        // Pertes au pavillon : un passe-bas d'ordre un dans la boucle, comme
        // pour toutes les perces du parc.
        const float entree = debit + retour * (1.0f - p.damping);
        filtre_ += (entree - filtre_) * std::clamp(1.0f - p.damping * 0.6f, 0.05f, 0.99f);
        ecrire(bore_, boreWrite_, filtre_);

        elapsed_ += 1.0f / static_cast<float>(sampleRate_);
        return retour * (0.4f + 0.6f * vel);
    }

private:
    static void ecrire(std::vector<float>& ligne, size_t& tete, float v) {
        ligne[tete] = v;
        tete = (tete + 1) % ligne.size();
    }
    static float lire(const std::vector<float>& ligne, size_t tete, float retard) {
        const float taille = static_cast<float>(ligne.size());
        float pos = static_cast<float>(tete) - retard;
        while (pos < 0.0f) pos += taille;
        const size_t i0 = static_cast<size_t>(pos) % ligne.size();
        const size_t i1 = (i0 + 1) % ligne.size();
        const float f = pos - std::floor(pos);
        return ligne[i0] * (1.0f - f) + ligne[i1] * f;
    }

    double sampleRate_ = 48000.0;
    vsm::audio::dsp::AdsrEnvelope env_;
    vsm::audio::dsp::AnalogDrift drift_;
    vsm::util::DeterministicRng rng_{0x464C555445ULL};
    std::vector<float> bore_, jet_;
    size_t boreWrite_ = 0, jetWrite_ = 0;
    float filtre_ = 0.0f, baseHz_ = 261.6f, elapsed_ = 0.0f, vibPhase_ = 0.0f;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class FluteSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 4;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kBreath = 1, kNoise, kJetRatio, kJetGain, kDamping,
        kVibratoRate, kVibratoDepth, kVibratoDelay,
        kAttack, kRelease,
        kVelocityToBreath, kAnalogCharacter, kOutputLevel,
    };

    FluteSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Flute (jet d'air sur biseau)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<FluteVoice, kMaxVoices> voiceManager_;
};

} // namespace vsm::plugins::flute
