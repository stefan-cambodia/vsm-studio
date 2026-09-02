#pragma once
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::scanned {

/// SYNTHÈSE BALAYÉE — la forme d'onde EST l'état d'un objet en mouvement.
///
/// POURQUOI CETTE MACHINE (Verplank, Mathews et Shaw, 2000). Le parc a deux
/// machines dont le timbre bouge tout seul, et aucune ne bouge de cette
/// façon : `vsm.wavetable` interpole entre des tables FIGÉES (ce qui change,
/// c'est un pointeur qu'un LFO promène), `vsm.stochastic` déplace ses points
/// de brisure par une MARCHE ALÉATOIRE (du bruit borné, sans mémoire ni
/// inertie). Ici, la forme d'onde est la photographie d'une CHAÎNE DE MASSES
/// reliées par des ressorts, bouclée en anneau, qu'on a frappée et qui
/// continue d'osciller pour son compte.
///
/// ```
///   noteOn ──> pincement de la chaîne
///                    │
///        chaîne de 32 masses ─ ressorts ─ anneau   (dynamique LENTE, en Hz)
///                    │
///        lecture de la FORME à la fréquence de la note (audio)
/// ```
///
/// LE TRAIT DISTINCTIF, ET IL DIT LA NATURE DE LA FAMILLE : **la vitesse
/// d'évolution du timbre ne dépend PAS de la note jouée.** La chaîne vit en
/// temps réel, à quelques hertz ; la note ne fait que décider à quelle
/// vitesse on la LIT. Jouer deux octaves plus haut ne fait donc pas évoluer
/// le timbre quatre fois plus vite — alors que c'est exactement ce qui
/// arriverait à `vsm.stochastic`, dont la forme change à chaque période. Le
/// test mesure cela, et il mesure les deux moitiés : l'évolution existe, et
/// elle est indépendante de la hauteur.
///
/// LA CHAÎNE EST PARTAGÉE PAR TOUTES LES VOIX, comme sur l'instrument
/// d'origine : c'est UN objet qu'on écoute par plusieurs fenêtres. Deux
/// notes tenues ensemble partagent donc leur évolution de timbre — une
/// cohérence qu'aucune machine à LFO par voix ne peut produire, et une
/// économie qui n'est pas la raison du choix mais qui ne gâte rien.
///
/// APPROXIMATIONS ASSUMÉES (§ 8), statut « dérivé » : la dynamique est
/// intégrée par Euler semi-implicite à taux RÉDUIT (un pas tous les 32
/// échantillons, soit 1,5 kHz — très au-dessus des quelques hertz du
/// mouvement, très en dessous du coût d'un pas par échantillon) ; les masses
/// sont toutes égales, là où Verplank en variait le profil ; le « pincement »
/// est une bosse fixe, pas un geste que le musicien place.
class ScannedChain {
public:
    static constexpr int kMasses = 32;
    /// Un pas de dynamique tous les 32 échantillons : 1,5 kHz à 48 kHz.
    static constexpr int kDecimation = 32;
    /// L'amplitude efficace vers laquelle l'état est ramené à chaque pas.
    static constexpr float kAmplitudeCible = 0.5f;

    void prepare(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
    }

    void reset() {
        positions_.fill(0.0f);
        vitesses_.fill(0.0f);
        compteur_ = 0;
        amplitude_ = 0.0f;
    }

    /// PINCER LA CHAÎNE : une bosse centrée sur `position`, d'autant plus
    /// étroite que le pincement est « dur ». C'est l'excitation, et elle
    /// s'ADDITIONNE — frapper une chaîne déjà en mouvement ne l'efface pas.
    void pincer(float position, float durete, float force) {
        const float centre = std::clamp(position, 0.0f, 1.0f) * (kMasses - 1);
        const float largeur = std::max(0.6f, (1.0f - std::clamp(durete, 0.0f, 1.0f)) * 8.0f);
        for (int i = 0; i < kMasses; ++i) {
            const float d = (static_cast<float>(i) - centre) / largeur;
            positions_[static_cast<size_t>(i)] += force * std::exp(-d * d);
        }
    }

    /// Avance la dynamique d'un pas si le compteur de décimation l'exige.
    void avancer(float tension, float amortissement, float rappel) {
        if (++compteur_ < kDecimation) return;
        compteur_ = 0;

        // Pas de temps effectif du modèle, borné pour rester stable : le
        // couple (tension, dt) doit satisfaire dt < 2/sqrt(k), et le clamp
        // ci-dessous garde une marge de deux -- une chaîne qui diverge ne
        // fait pas un son, elle fait un NaN.
        const float dt = std::min(0.25f, static_cast<float>(kDecimation) / static_cast<float>(sampleRate_) * 40.0f);
        const float k = 20.0f + 900.0f * std::clamp(tension, 0.0f, 1.0f);
        const float d = 0.05f + 6.0f * std::clamp(amortissement, 0.0f, 1.0f);
        const float c = 0.5f + 40.0f * std::clamp(rappel, 0.0f, 1.0f);

        for (int i = 0; i < kMasses; ++i) {
            const size_t s = static_cast<size_t>(i);
            const size_t avant = static_cast<size_t>((i + kMasses - 1) % kMasses);
            const size_t apres = static_cast<size_t>((i + 1) % kMasses);
            // Laplacien discret : la force de rappel d'un ressort vers ses
            // deux voisins. C'est cela qui fait circuler une onde dans
            // l'anneau plutôt que de faire vibrer chaque masse dans son coin.
            const float laplacien = positions_[avant] - 2.0f * positions_[s] + positions_[apres];
            const float force = k * laplacien - d * vitesses_[s] - c * positions_[s];
            vitesses_[s] += force * dt;
        }
        // Euler SEMI-implicite : les positions se mettent à jour APRÈS toutes
        // les vitesses, sans quoi la moitié de la chaîne verrait l'état neuf
        // de l'autre moitié et l'énergie ne se conserverait plus du tout.
        double somme = 0.0;
        for (int i = 0; i < kMasses; ++i) {
            const size_t s = static_cast<size_t>(i);
            positions_[s] = std::clamp(positions_[s] + vitesses_[s] * dt, -4.0f, 4.0f);
            somme += static_cast<double>(positions_[s]) * positions_[s];
        }
        // LA CHAÎNE DONNE LA FORME, PAS L'AMPLITUDE — et c'est une mesure
        // qui l'a imposé, pas un raisonnement. Laissée à elle-même, la chaîne
        // se vide de son énergie et le son MEURT en deux secondes et demie
        // (rms 0,038 → 0,00000, relevé à la sonde) alors que l'enveloppe tient
        // son sustain : la machine ne pouvait pas tenir une note. Pire, la
        // forme se figeait sur son mode le plus lent et le timbre cessait de
        // bouger — l'évolution, qui est sa raison d'être, ne s'entendait plus
        // que pendant l'attaque.
        //
        // On RENORMALISE L'ÉTAT (positions ET vitesses ensemble, pour ne pas
        // fausser la dynamique) vers une amplitude constante. C'est un
        // ENTRETIEN, au sens de l'archet qui tient une corde : le mouvement
        // dure, mais sa FORME reste celle que la physique dicte, puisque tous
        // les modes sont mis à la même échelle et que leurs rapports — c'est-
        // à-dire le timbre — continuent d'évoluer librement. L'enveloppe dit
        // alors ce qu'elle seule doit dire : le volume.
        //
        // Le SEUIL est ce qui garde le silence silencieux, et il fait plus que
        // cela : une chaîne trop amortie passe dessous et s'y arrête pour de
        // bon, au lieu de voir son bruit numérique amplifié jusqu'à devenir un
        // signal. Amortissement au maximum vaut donc bien étouffement, et non
        // chaos — ce qu'une première version, qui divisait la SORTIE par
        // l'amplitude instantanée, faisait exactement.
        amplitude_ = std::sqrt(static_cast<float>(somme / kMasses));
        if (amplitude_ > 1.0e-5f) {
            const float echelle = kAmplitudeCible / amplitude_;
            for (int i = 0; i < kMasses; ++i) {
                const size_t s = static_cast<size_t>(i);
                positions_[s] *= echelle;
                vitesses_[s] *= echelle;
            }
            amplitude_ = kAmplitudeCible;
        }
    }

    /// Lit la forme à la phase donnée (0..1), par interpolation linéaire
    /// entre deux masses voisines.
    float lire(double phase) const {
        const double x = phase * kMasses;
        const auto i = static_cast<int>(x) % kMasses;
        const auto j = (i + 1) % kMasses;
        const auto f = static_cast<float>(x - std::floor(x));
        return positions_[static_cast<size_t>(i)] * (1.0f - f)
             + positions_[static_cast<size_t>(j)] * f;
    }

private:
    double sampleRate_ = 48000.0;
    std::array<float, kMasses> positions_{};
    std::array<float, kMasses> vitesses_{};
    int compteur_ = 0;
    /// Amplitude efficace courante de la chaîne, recalculée à chaque pas :
    /// elle sert à rapporter la forme à elle-même (voir `avancer`).
    float amplitude_ = 0.0f;
};

class ScannedVoice {
public:
    struct Params {
        float cutoff = 9000.0f, resonance = 0.1f;
        float velocityToLevel = 0.4f;
        float bendSemitones = 0.0f;
    };

    void prepare(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        env_.setSampleRate(sampleRate_);
        filtre_.setSampleRate(sampleRate_);
    }

    bool isActive() const { return env_.isActive(); }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }
    void setEnvelope(const vsm::audio::dsp::AdsrSettings& s) { env_.setSettings(s); }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel;
        note_ = note;
        velocity_ = velocity;
        env_.noteOn();
        phase_ = 0.0;
    }
    void noteOff(uint8_t) { env_.noteOff(); }

    float render(const Params& p, const ScannedChain& chaine) {
        if (!env_.isActive()) return 0.0f;
        const float hz = 440.0f * std::exp2f(
            (static_cast<float>(note_) + p.bendSemitones - 69.0f) / 12.0f);
        phase_ += static_cast<double>(hz) / sampleRate_;
        if (phase_ >= 1.0) phase_ -= 1.0;

        filtre_.setCutoffHz(p.cutoff);
        filtre_.setResonance(p.resonance);
        const float velocity = static_cast<float>(velocity_) / 127.0f;
        const float gain = 1.0f - p.velocityToLevel * (1.0f - velocity);
        return filtre_.process(chaine.lire(phase_)) * env_.nextSample() * gain;
    }

private:
    double sampleRate_ = 48000.0;
    double phase_ = 0.0;
    vsm::audio::dsp::AdsrEnvelope env_;
    vsm::audio::dsp::StateVariableFilter filtre_;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class ScannedSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kTension = 1, kDamping, kCentering,
        kPluckPosition, kPluckHardness, kPluckForce,
        kCutoff, kResonance,
        kAttack, kDecay, kSustain, kRelease,
        kVelocitySensitivity, kOutputLevel,
    };

    ScannedSynth();

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
    const char* machineName() const override { return "Scanned (la forme qui vit)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<ScannedVoice, kMaxVoices> voiceManager_;
    /// UNE chaîne pour toute la machine : c'est un objet qu'on écoute par
    /// plusieurs fenêtres, pas un objet par touche.
    ScannedChain chaine_;
    std::atomic<float> bendSemitones_{0.0f};
};

} // namespace vsm::plugins::scanned
