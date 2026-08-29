#pragma once
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DecayEnvelope.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::fmdrums {

using DecayEnv = vsm::audio::dsp::DecayEnvelope;

/// PERCUSSIONS PAR MODULATION DE FRÉQUENCE — la famille métallique.
///
/// POURQUOI CETTE MACHINE, ET CE QU'ELLE AJOUTE. Le parc a quatre façons de
/// faire une batterie, et aucune ne sait faire celle-ci :
///
///  - **TR-808 et TR-909** : percussion analogique. Un sinus, une enveloppe de
///    hauteur, du bruit filtré. Le spectre d'un kick y est HARMONIQUE ou
///    presque pur.
///  - **`vsm.drums`** : kit acoustique modélisé, peaux et métal.
///  - **`vsm.perc`** : peaux et barres par synthèse MODALE -- des modes
///    inharmoniques, mais FIXES, ceux d'une membrane ou d'une barre.
///  - **`vsm.sampler`** : le report d'un coup enregistré.
///
/// Manque ce qui a fait le son des boîtes NUMÉRIQUES des années 80 et de la
/// techno qui a suivi : des percussions dont les partiels sont inharmoniques et
/// **MOBILES**. En FM, les composantes tombent à `|porteuse ± n·modulante|` :
/// il suffit d'un rapport non entier pour que rien ne soit harmonique, et
/// l'indice de modulation étant sous enveloppe, **le spectre change pendant la
/// frappe**. C'est ce qui donne le « clang » d'un kick FM et le métal d'une
/// cloche numérique, et aucune des quatre autres routes ne l'atteint : la
/// modale a des rapports figés, l'analogique n'a pas de partiels du tout.
///
/// LE TRAIT DISTINCTIF, ET IL EST TESTÉ. À rapport ENTIER (2,0) le spectre est
/// harmonique, comme celui d'une boîte analogique. Au rapport non entier de
/// défaut (1,414 -- racine de deux, qui ne peut être le rapport d'aucun
/// harmonique), l'énergie se trouve à des fréquences que la série harmonique ne
/// contient pas. Le test mesure les deux et compare : c'est le rapport qui
/// décide, pas le hasard d'un filtre.
///
/// NUMÉROTATION MIDI : celle des boîtes du parc (36 kick, 38 caisse, 42/46
/// charlestons, 39 clap, 45 tom, 49 cymbale), pour qu'un motif écrit pour la
/// TR-909 se joue ici sans traduction -- et pour que l'arbitrage de la chaîne
/// d'analyse puisse mettre les deux en concurrence sur le même stem.
///
/// APPROXIMATIONS ASSUMÉES (§ 8 de `CDC-nouvelle-machine.md`), statut
/// « dérivé », aucune mesure sur une machine réelle :
///
///  - **Deux opérateurs par pièce**, une porteuse et une modulante, là où les
///    boîtes d'origine en ont quatre ou six. Deux suffisent à l'inharmonicité
///    et gardent la façade jouable ; six demanderaient une matrice
///    d'algorithmes, c'est-à-dire le DX7, qui existe déjà dans le parc.
///  - **Pas de rétroaction d'opérateur.** Elle sert surtout à fabriquer du
///    bruit ; ici le bruit est explicite, et réglable.
///  - **Les charlestons sont du bruit filtré**, pas de la FM : une charleston
///    FM sonne comme une cloche courte, ce qui n'est pas ce qu'on attend de la
///    pièce qui marque le temps. C'est un choix de justesse, et il est écrit
///    plutôt que caché.

/// Une voix FM percussive : porteuse, modulante, deux enveloppes de descente.
class FmVoice {
public:
    void setSampleRate(double sr) {
        sampleRate_ = sr;
        amp_.setSampleRate(sr);
        index_.setSampleRate(sr);
        pitch_.setSampleRate(sr);
    }

    void configure(float carrierHz, float ratio, float indexAmount,
                   float decaySeconds, float level, float pitchSweep = 0.0f) {
        carrierHz_ = carrierHz;
        ratio_ = ratio;
        indexAmount_ = indexAmount;
        decay_ = decaySeconds;
        level_ = level;
        pitchSweep_ = pitchSweep;
    }

    bool isActive() const { return amp_.isActive(); }

    void trigger(float velGain) {
        velGain_ = velGain;
        phase_ = 0.0f;
        modPhase_ = 0.0f;
        amp_.setDecaySeconds(decay_);
        // L'INDICE DESCEND PLUS VITE QUE L'AMPLITUDE, et c'est tout le sujet :
        // une percussion FM est métallique à l'attaque puis se referme sur son
        // fondamental. Un indice constant donnerait un bourdon métallique.
        index_.setDecaySeconds(decay_ * 0.35f);
        pitch_.setDecaySeconds(0.04f);
        amp_.trigger();
        index_.trigger();
        pitch_.trigger();
    }

    float render() {
        if (!amp_.isActive()) return 0.0f;
        const float sweep = pitch_.next() * pitchSweep_;
        const float f = carrierHz_ * (1.0f + sweep);
        modPhase_ += static_cast<float>(vsm::audio::dsp::kTwoPi) * f * ratio_
                   / static_cast<float>(sampleRate_);
        if (modPhase_ > static_cast<float>(vsm::audio::dsp::kTwoPi))
            modPhase_ -= static_cast<float>(vsm::audio::dsp::kTwoPi);
        const float mod = std::sin(modPhase_) * index_.next() * indexAmount_;

        phase_ += static_cast<float>(vsm::audio::dsp::kTwoPi) * f
                / static_cast<float>(sampleRate_);
        if (phase_ > static_cast<float>(vsm::audio::dsp::kTwoPi))
            phase_ -= static_cast<float>(vsm::audio::dsp::kTwoPi);

        return std::sin(phase_ + mod) * amp_.next() * level_ * velGain_;
    }

private:
    double sampleRate_ = 48000.0;
    DecayEnv amp_, index_, pitch_;
    float carrierHz_ = 60.0f, ratio_ = 1.414f, indexAmount_ = 4.0f;
    float decay_ = 0.3f, level_ = 1.0f, pitchSweep_ = 0.0f;
    float velGain_ = 1.0f, phase_ = 0.0f, modPhase_ = 0.0f;
};

/// Charleston : du BRUIT filtré, pas de la FM. Voir l'en-tête -- une charleston
/// FM sonne comme une cloche courte, et ce n'est pas la pièce qui marque le
/// temps.
class NoiseHat {
public:
    void setSampleRate(double sr) {
        env_.setSampleRate(sr);
        hp_.setSampleRate(sr);
        hp_.setMode(vsm::audio::dsp::StateVariableFilter::Mode::HighPass);
        hp_.setResonance(0.5f);
    }
    void configure(float toneHz, float decaySeconds, float level) {
        decay_ = decaySeconds; level_ = level; hp_.setCutoffHz(toneHz);
    }
    bool isActive() const { return env_.isActive(); }
    void choke() { env_.setDecaySeconds(0.012f); }
    void trigger(float velGain) {
        velGain_ = velGain; env_.setDecaySeconds(decay_); env_.trigger();
    }
    float render() {
        if (!env_.isActive()) return 0.0f;
        return hp_.process(rng_.nextBipolar()) * env_.next() * level_ * velGain_ * 0.8f;
    }

private:
    DecayEnv env_;
    vsm::audio::dsp::StateVariableFilter hp_;
    vsm::util::DeterministicRng rng_{0x464D48415400ULL};
    float decay_ = 0.06f, level_ = 1.0f, velGain_ = 1.0f;
};

class FmDrumsSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    enum ParamIds : vsm::audio::plugin::ParamId {
        kKickLevel = 0, kKickTune, kKickDecay, kKickRatio, kKickIndex,
        kSnareLevel, kSnareTune, kSnareDecay, kSnareRatio, kSnareIndex,
        kTomLevel, kTomTune, kTomDecay, kTomRatio, kTomIndex,
        kBellLevel, kBellTune, kBellDecay, kBellRatio, kBellIndex,
        kHatLevel, kHatTone, kClosedHatDecay, kOpenHatDecay,
        kClapLevel, kClapDecay,
        kAccent,
        kNumParams
    };

    /// Numérotation des boîtes du parc : un motif écrit pour la TR-909 se joue
    /// ici sans traduction.
    enum Note : uint8_t {
        kNoteKick = 36, kNoteSnare = 38, kNoteClap = 39,
        kNoteClosedHat = 42, kNoteOpenHat = 46, kNoteTom = 45, kNoteBell = 49
    };

    FmDrumsSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override;
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "FM Drums (percussions métalliques)"; }
    int activeVoiceCount() const override;

private:
    void triggerNote(uint8_t note, uint8_t velocity);
    void applyConfig();

    FmVoice kick_, snare_, tom_, bell_, clap_;
    NoiseHat closedHat_, openHat_;

    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
    double sampleRate_ = 48000.0;
};

} // namespace vsm::plugins::fmdrums
