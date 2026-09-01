#pragma once
#include "vsm/audio/dsp/AnalogDrift.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::westcoast {

/// SYNTHÈSE « CÔTE OUEST » — on AJOUTE des harmoniques, on n'en retire pas.
///
/// POURQUOI CETTE MACHINE. Le parc compte dix soustractifs, et le § 7 de
/// `CDC-machines-manquantes.md` interdit d'en ajouter un onzième : ce serait un
/// nom sur une liste. Mais tous les dix, plus la table d'ondes et l'hybride
/// PCM, procèdent de la même idée -- partir d'une onde RICHE et lui enlever ce
/// qu'on ne veut pas, au filtre. C'est l'école de la côte est (Moog, Roland,
/// Sequential). L'autre école existe, elle est absente d'ici, et elle fait
/// l'inverse : partir d'un SINUS -- l'onde la plus pauvre qui soit -- et lui
/// fabriquer des harmoniques par PLIAGE. Aucune machine du parc ne sait ça, et
/// aucun filtre ne le peut : un filtre est une opération linéaire, il ne crée
/// jamais une fréquence qui n'était pas là.
///
/// LE TRAIT DISTINCTIF, ET IL EST DÉCISIF. À pliage nul, la machine sort un
/// sinus pur -- rangs 2 et 3 sous le millième. À pliage maximal, les mêmes
/// réglages par ailleurs, le spectre est riche. **Le nombre d'harmoniques
/// dépend de l'AMPLITUDE, pas d'une coupure**, et c'est exactement ce qu'un
/// soustractif ne peut pas faire. Le test le vérifie dans les deux sens.
///
/// LE SECOND TRAIT, PLUS SUBTIL ET NON MOINS RÉEL : LA PORTE PASSE-BAS. Sur ces
/// machines, il n'y a pas d'ampli d'un côté et de filtre de l'autre : un seul
/// organe -- une photorésistance pilotée par une lampe, le « vactrol » --
/// baisse À LA FOIS le volume ET la brillance, et il le fait avec la LENTEUR
/// d'un composant chauffé. C'est pour cela qu'une note de Buchla s'éteint en
/// devenant sourde, au lieu de s'éteindre en restant claire. Ici, une seule
/// enveloppe commande le gain et la coupure, et elle est lissée par une
/// constante de temps propre : le test vérifie que le centroïde spectral
/// DESCEND pendant l'extinction, ce qu'un ampli seul ne produirait pas.
///
/// APPROXIMATIONS ASSUMÉES (§ 8 de `CDC-nouvelle-machine.md`), statut
/// « dérivé » -- aucune mesure sur un instrument réel :
///
///  - **Le pliage est une fonction fixe** (une chaîne de replis triangulaires),
///    là où chaque fabricant a la sienne. Ce qui se règle est la QUANTITÉ de
///    signal qu'on y envoie, comme sur l'original.
///  - **Le vactrol est un simple lissage asymétrique** -- il monte vite et
///    descend lentement --, pas un modèle de photorésistance. C'est le
///    comportement que l'oreille retient, et il est réglable.
///  - **Deux oscillateurs seulement** : le principal et le modulant. Une
///    « complex oscillator » d'origine en a autant, mais avec un routage bien
///    plus riche que ce que six réglages peuvent exposer.

class WestCoastVoice {
public:
    struct Params {
        float ratio = 2.0f;          // rapport modulant/principal
        float fmIndex = 0.0f;        // profondeur de modulation, en demi-tons
        float fold = 0.0f;           // quantité envoyée dans le plieur
        float symmetry = 0.5f;       // asymétrie du pliage : 0,5 = symétrique
        float gateCutoff = 6000.0f;  // coupure de la porte, ouverte
        float gateLag = 0.25f;       // lenteur du vactrol, en secondes
        float velocityToFold = 0.5f;
        float drift = 0.15f;
        // Molette de hauteur, en demi-tons. À zéro l'addition est exacte :
        // empreinte inchangée au bit.
        float bendSemitones = 0.0f;
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate;
        ampEnv_.setSampleRate(sampleRate);
        gate_.setSampleRate(sampleRate);
        gate_.setMode(vsm::audio::dsp::StateVariableFilter::Mode::LowPass);
        gate_.setResonance(0.15f);
        drift_.setSampleRate(sampleRate);
        drift_.setSeed(seed);
        drift_.setRateHz(0.08f);
        drift_.setAmount(0.0f);
        // CHAQUE VOIX DÉMARRE À UNE PHASE DIFFÉRENTE : sur un polyphonique
        // analogique, huit oscillateurs libres ne sont jamais en phase, rien ne
        // les synchronise. Le décalage est DÉRIVÉ DE LA GRAINE, donc fixe --
        // deux rendus de la même session restent identiques au bit près, ce qui
        // est la condition des empreintes de non-régression.
        //
        // CE QUE ÇA NE RÈGLE PAS, et il faut le dire parce que je l'ai cru :
        // ça ne suffit PAS à empêcher un accord de crêter. Mesuré, l'accord de
        // huit notes passait de 1,746 à 1,664 -- cinq pour cent. Sur une
        // seconde, huit fréquences différentes finissent de toute façon par se
        // croiser en phase, quel que soit leur point de départ. Le niveau est
        // donc calibré à part, voir `kVoiceGain`.
        phaseOffset_ = static_cast<float>(vsm::audio::dsp::kTwoPi)
                     * static_cast<float>(seed % 997u) / 997.0f;
        phase_ = phaseOffset_;
        modPhase_ = phaseOffset_ * 0.61f;
        vactrol_ = 0.0f;
    }

    bool isActive() const { return ampEnv_.isActive() || vactrol_ > 1e-4f; }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void setSettings(const vsm::audio::dsp::AdsrSettings& amp) { ampEnv_.setSettings(amp); }
    void setDriftAmount(float amount) { drift_.setAmount(amount); }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel;
        note_ = note;
        velocity_ = velocity;
        baseHz_ = 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
        ampEnv_.noteOn();
        phase_ = phaseOffset_;
        modPhase_ = phaseOffset_ * 0.61f;
    }

    void noteOff(uint8_t) { ampEnv_.noteOff(); }

    /// LE PLIEUR. Un signal qui dépasse 1 est REPLIÉ vers l'intérieur au lieu
    /// d'être écrêté : c'est toute la différence entre une distorsion, qui
    /// arrondit le sommet, et un pliage, qui le renvoie d'où il vient en
    /// fabriquant des harmoniques hautes. On replie plusieurs fois, parce
    /// qu'un signal poussé fort dépasse encore après le premier repli.
    static float fold(float x) {
        for (int i = 0; i < 6; ++i) {
            if (x > 1.0f) x = 2.0f - x;
            else if (x < -1.0f) x = -2.0f - x;
            else break;
        }
        return x;
    }

    float render(const Params& p) {
        if (!isActive()) return 0.0f;

        const float vel = static_cast<float>(velocity_) / 127.0f;
        const float f0 = baseHz_ * std::pow(
            2.0f, (drift_.nextValue() * 0.05f + p.bendSemitones) / 12.0f);

        // MODULATION DE HAUTEUR par le second oscillateur, en demi-tons : c'est
        // le « timbre » d'une complex oscillator avant le plieur.
        modPhase_ += static_cast<float>(vsm::audio::dsp::kTwoPi) * f0 * p.ratio
                   / static_cast<float>(sampleRate_);
        if (modPhase_ > static_cast<float>(vsm::audio::dsp::kTwoPi))
            modPhase_ -= static_cast<float>(vsm::audio::dsp::kTwoPi);
        const float mod = std::sin(modPhase_);
        const float freq = f0 * std::pow(2.0f, mod * p.fmIndex / 12.0f);

        phase_ += static_cast<float>(vsm::audio::dsp::kTwoPi) * freq
                / static_cast<float>(sampleRate_);
        if (phase_ > static_cast<float>(vsm::audio::dsp::kTwoPi))
            phase_ -= static_cast<float>(vsm::audio::dsp::kTwoPi);

        // Le SINUS entre dans le plieur, poussé d'autant plus fort que `fold`
        // est grand. À `fold` nul, le gain vaut 1 : rien ne dépasse, rien n'est
        // plié, et il ressort exactement le sinus qu'il était.
        const float quantite = p.fold * (1.0f + p.velocityToFold * vel);
        const float gain = 1.0f + quantite * 6.0f;
        // L'ASYMÉTRIE décale le signal avant le pliage : les replis ne tombent
        // plus au même endroit en haut et en bas, ce qui fait apparaître les
        // rangs PAIRS. Symétrique, un plieur ne donne que des impairs.
        const float decalage = (p.symmetry - 0.5f) * 2.0f * quantite;
        const float plie = fold(std::sin(phase_) * gain + decalage);

        // LA PORTE PASSE-BAS : une seule commande pour le volume ET la
        // brillance, lissée par la lenteur du vactrol.
        const float cible = ampEnv_.nextSample();
        // Montée rapide, descente lente : c'est le comportement de la lampe.
        const float lag = std::max(p.gateLag, 0.001f);
        const float coeff = (cible > vactrol_)
            ? 1.0f - std::exp(-1.0f / (0.004f * static_cast<float>(sampleRate_)))
            : 1.0f - std::exp(-1.0f / (lag * static_cast<float>(sampleRate_)));
        vactrol_ += (cible - vactrol_) * coeff;

        // La coupure suit la MÊME commande : deux décades entre fermé et
        // ouvert. C'est ce couplage qui fait qu'une note s'éteint en devenant
        // sourde.
        const float coupure = 80.0f + (p.gateCutoff - 80.0f) * vactrol_ * vactrol_;
        gate_.setCutoffHz(std::min(coupure, static_cast<float>(sampleRate_) * 0.45f));

        return gate_.process(plie) * vactrol_ * (0.3f + 0.7f * vel);
    }

private:
    double sampleRate_ = 48000.0;
    vsm::audio::dsp::AdsrEnvelope ampEnv_;
    vsm::audio::dsp::StateVariableFilter gate_;
    vsm::audio::dsp::AnalogDrift drift_;
    float phase_ = 0.0f, modPhase_ = 0.0f, vactrol_ = 0.0f, phaseOffset_ = 0.0f;
    float baseHz_ = 261.6f;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class WestCoastSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kFold = 1, kSymmetry, kRatio, kFmIndex,
        kGateCutoff, kGateLag,
        kAmpAttack, kAmpDecay, kAmpSustain, kAmpRelease,
        kVelocityToFold, kAnalogCharacter, kOutputLevel,
    };

    WestCoastSynth();

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
    const char* machineName() const override { return "West Coast (pliage et porte passe-bas)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<WestCoastVoice, kMaxVoices> voiceManager_;
    // Molette de hauteur (demi-tons), même contrat que params_.
    std::atomic<float> bendSemitones_{0.0f};
};

} // namespace vsm::plugins::westcoast
