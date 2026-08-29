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

namespace vsm::plugins::perc {

using DecayEnv = vsm::audio::dsp::DecayEnvelope;

/// PERCUSSIONS À PEAUX ET BARRES — congas, bongos, timbales, cloche, bois,
/// shaker, tambourin — par synthèse MODALE.
///
/// POURQUOI CETTE MACHINE, ET LA MESURE QUI LA JUSTIFIE. Le § 7 de
/// `docs/CDC-machines-manquantes.md` demande de juger une machine sur la
/// COUVERTURE qu'elle ajoute, pas sur le catalogue. Celle-ci ouvre une famille
/// que rien ne couvrait, et le trou avait été mesuré sur un morceau réel avant
/// qu'une ligne ne soit écrite : sur *Sky and Sand*, la chaîne a imprimé
/// « clap : famille sans voix déclarée dans `vsm.drums`, jouée sur la note 39
/// (559 frappes) ». Le détecteur nomme six familles (kick, snare, hihat,
/// openhat, clap, tom) ; `vsm.drums` n'en joue que cinq, la TR-808 non plus.
/// Et au-delà de ces six, les percussions au sens large -- congas, bongos,
/// timbales, cloche, claves, blocs de bois, shaker, tambourin -- ne sont
/// jouables par AUCUNE machine du parc, ni nommables par le détecteur
/// (ROADMAP-apprentissage.md, A2.3 : « le modèle ne nomme que ce qu'il a
/// entendu »). Une machine qui les joue est la condition pour que le corpus de
/// frappes puisse un jour les apprendre.
///
/// CE QUI LA DISTINGUE DES TROIS AUTRES, ET C'EST TESTÉ. Les boîtes du parc
/// fabriquent leurs peaux avec un sinus et une enveloppe de hauteur : leur
/// spectre est HARMONIQUE, ou presque pur. Une vraie membrane ne l'est pas.
/// Les modes d'une peau circulaire tendue sont les zéros de la fonction de
/// Bessel J0, dans des rapports **1 ; 1,594 ; 2,136 ; 2,296 ; 2,653…** — des
/// rapports IRRATIONNELS, qui sont précisément ce qui fait qu'un tambour rend
/// un son et non une note. Une barre libre-libre, elle, sonne à
/// **1 ; 2,756 ; 5,404** : c'est le timbre d'un bloc de bois ou d'une claves.
/// C'est ce que `perc_membrane_modes_are_inharmonic` et
/// `perc_bar_modes_are_inharmonic` vérifient, et c'est la seule chose qui
/// justifie une machine de plus.
///
/// LE MODÈLE. Chaque pièce est une somme de MODES : un mode est un sinus qui
/// décroît exponentiellement, d'amplitude et de durée propres. C'est la
/// définition même de la réponse d'un objet linéaire percuté, et cela suffit
/// à rendre les peaux et les barres. Deux écarts assumés :
///
///  - **l'excitation est un instant, pas une main.** Un conga frappé du plat de
///    la main ou du bout des doigts n'excite pas les mêmes modes ; ici, le
///    contenu de la frappe est fixé par pièce et la vélocité n'en change que le
///    NIVEAU (et, pour la peau, un peu de tension d'attaque). Mettre la main
///    dans le modèle demanderait un jeu de gestes que le MIDI ne transporte
///    pas.
///  - **le shaker et le tambourin ne sont pas modaux du tout** : ce sont des
///    dizaines de petits chocs, et on les rend par du bruit filtré à enveloppe
///    rapide -- pour le tambourin, du bruit PLUS deux modes de cymbalette, qui
///    est ce qui le distingue d'un shaker.
///
/// NUMÉROTATION MIDI : celle du standard General MIDI pour les percussions,
/// sans exception. Un fichier MIDI écrit pour un module GM joue donc juste ici
/// sans traduction, et le projet d'analyse peut adresser ces pièces avec les
/// numéros qu'il connaît déjà.
///
/// APPROXIMATIONS DOCUMENTÉES (§ 8 de CDC-nouvelle-machine.md), statut
/// « dérivé » : aucune mesure sur un instrument réel n'a servi à régler cette
/// machine. Les rapports de modes viennent de la physique (zéros de Bessel pour
/// la peau, modes de flexion d'une barre libre) ; les amplitudes et les durées
/// sont choisies à l'oreille du modèle, pas relevées sur un conga.

/// Un mode : un sinus qui décroît. La brique de toute la machine.
class Mode {
public:
    void setSampleRate(double sr) { sampleRate_ = sr; env_.setSampleRate(sr); }

    void configure(float freqHz, float decaySeconds, float amplitude) {
        freqHz_ = freqHz;
        decay_ = decaySeconds;
        amplitude_ = amplitude;
    }

    bool isActive() const { return env_.isActive(); }

    void trigger(float velGain, float phase = 0.0f) {
        velGain_ = velGain;
        phase_ = phase;
        env_.setDecaySeconds(decay_);
        env_.trigger();
    }

    float render() {
        if (!env_.isActive()) return 0.0f;
        phase_ += static_cast<float>(vsm::audio::dsp::kTwoPi) * freqHz_
                / static_cast<float>(sampleRate_);
        if (phase_ > static_cast<float>(vsm::audio::dsp::kTwoPi))
            phase_ -= static_cast<float>(vsm::audio::dsp::kTwoPi);
        return std::sin(phase_) * env_.next() * amplitude_ * velGain_;
    }

private:
    double sampleRate_ = 48000.0;
    DecayEnv env_;
    float freqHz_ = 200.0f, decay_ = 0.3f, amplitude_ = 1.0f;
    float velGain_ = 1.0f, phase_ = 0.0f;
};

/// PEAU TENDUE : les quatre premiers modes d'une membrane circulaire.
///
/// Les rapports sont les zéros de J0 rapportés au premier (2,405) : 1 ; 1,594 ;
/// 2,136 ; 2,296. Ils ne sont pas ajustables -- un réglage qui déplacerait les
/// modes ne modéliserait plus une peau, il ferait autre chose. Ce qui se règle
/// est la HAUTEUR du premier mode (la tension) et la DURÉE (l'amortissement de
/// la peau et de la caisse).
class Membrane {
public:
    static constexpr int kModes = 4;
    // Zéros de J0 : 2,405 ; 3,832 ; 5,136 ; 5,520 -- rapportés au premier.
    static constexpr float kRatios[kModes] = {1.0f, 1.5933f, 2.1355f, 2.2954f};
    // Le fondamental porte l'essentiel ; les modes hauts meurent plus vite,
    // c'est l'amortissement de la peau qui croît avec la fréquence.
    static constexpr float kAmps[kModes] = {1.0f, 0.42f, 0.22f, 0.15f};
    static constexpr float kDecayScale[kModes] = {1.0f, 0.55f, 0.35f, 0.28f};

    void setSampleRate(double sr) {
        for (auto& m : modes_) m.setSampleRate(sr);
        strike_.setSampleRate(sr);
        strikeFilter_.setSampleRate(sr);
        strikeFilter_.setMode(vsm::audio::dsp::StateVariableFilter::Mode::BandPass);
        strikeFilter_.setResonance(0.4f);
    }

    void configure(float tuneHz, float decaySeconds, float level) {
        tuneHz_ = tuneHz;
        level_ = level;
        for (int k = 0; k < kModes; ++k)
            modes_[k].configure(tuneHz * kRatios[k], decaySeconds * kDecayScale[k], kAmps[k]);
        strikeFilter_.setCutoffHz(std::min(tuneHz * 6.0f, 6000.0f));
    }

    bool isActive() const {
        for (const auto& m : modes_) if (m.isActive()) return true;
        return strike_.isActive();
    }

    void trigger(float velGain) {
        velGain_ = velGain;
        for (auto& m : modes_) m.trigger(velGain);
        // LE CHOC DE LA MAIN : un bruit très court, filtré autour des modes.
        // Sans lui, une peau modale sonne comme une cloche douce -- ce qu'on
        // reconnaît d'un tambour est d'abord son attaque.
        strike_.setDecaySeconds(0.006f);
        strike_.trigger();
    }

    float render() {
        if (!isActive()) return 0.0f;
        float sum = 0.0f;
        for (auto& m : modes_) sum += m.render();
        const float choc = strikeFilter_.process(rng_.nextBipolar()) * strike_.next() * 0.7f;
        return (sum * 0.55f + choc) * level_ * velGain_;
    }

private:
    std::array<Mode, kModes> modes_;
    DecayEnv strike_;
    vsm::audio::dsp::StateVariableFilter strikeFilter_;
    vsm::util::DeterministicRng rng_{0x9E3779B97F4A7C15ULL};
    float tuneHz_ = 200.0f, level_ = 1.0f, velGain_ = 1.0f;
};

/// BARRE LIBRE-LIBRE : bloc de bois, claves, temple block.
///
/// Les modes de flexion d'une barre libre aux deux bouts sont dans les rapports
/// 1 ; 2,756 ; 5,404 -- très écartés, ce qui donne le « toc » sec et sans
/// hauteur franche d'un bloc de bois. Le troisième mode est court : c'est lui
/// qui fait le claquement.
class Bar {
public:
    static constexpr int kModes = 3;
    static constexpr float kRatios[kModes] = {1.0f, 2.756f, 5.404f};
    static constexpr float kAmps[kModes] = {1.0f, 0.55f, 0.28f};
    static constexpr float kDecayScale[kModes] = {1.0f, 0.45f, 0.22f};

    void setSampleRate(double sr) { for (auto& m : modes_) m.setSampleRate(sr); }

    void configure(float tuneHz, float decaySeconds, float level) {
        level_ = level;
        for (int k = 0; k < kModes; ++k)
            modes_[k].configure(tuneHz * kRatios[k], decaySeconds * kDecayScale[k], kAmps[k]);
    }

    bool isActive() const {
        for (const auto& m : modes_) if (m.isActive()) return true;
        return false;
    }

    void trigger(float velGain) { for (auto& m : modes_) m.trigger(velGain); }

    float render() {
        if (!isActive()) return 0.0f;
        float sum = 0.0f;
        for (auto& m : modes_) sum += m.render();
        return sum * 0.5f * level_;
    }

private:
    std::array<Mode, kModes> modes_;
    float level_ = 1.0f;
};

/// CLOCHE À VACHE : deux modes proches et volontairement DÉSACCORDÉS, passés en
/// passe-bande. Une cloche est une plaque pliée : ses deux premiers modes sont
/// voisins et battent l'un contre l'autre, et c'est ce battement qu'on
/// reconnaît. Le rapport 1,5 est celui que la TR-808 a rendu célèbre ; il n'est
/// pas physique, il est CULTUREL, et c'est écrit plutôt que maquillé.
class Cowbell {
public:
    void setSampleRate(double sr) {
        a_.setSampleRate(sr); b_.setSampleRate(sr);
        bp_.setSampleRate(sr);
        bp_.setMode(vsm::audio::dsp::StateVariableFilter::Mode::BandPass);
        bp_.setResonance(0.35f);
    }

    void configure(float tuneHz, float decaySeconds, float level) {
        level_ = level;
        a_.configure(tuneHz, decaySeconds, 1.0f);
        b_.configure(tuneHz * 1.5f, decaySeconds * 0.85f, 0.9f);
        bp_.setCutoffHz(std::min(tuneHz * 1.8f, 8000.0f));
    }

    bool isActive() const { return a_.isActive() || b_.isActive(); }
    void trigger(float velGain) { a_.trigger(velGain); b_.trigger(velGain, 1.7f); }

    float render() {
        if (!isActive()) return 0.0f;
        return bp_.process(a_.render() + b_.render()) * 0.6f * level_;
    }

private:
    Mode a_, b_;
    vsm::audio::dsp::StateVariableFilter bp_;
    float level_ = 1.0f;
};

/// GRAINS SECOUÉS : shaker, maracas. Des dizaines de chocs minuscules, donc du
/// bruit -- pas de modes, et prétendre le contraire serait de la décoration.
/// L'attaque est très rapide, la queue courte, et la bande haute.
class Shaker {
public:
    void setSampleRate(double sr) {
        env_.setSampleRate(sr);
        hp_.setSampleRate(sr);
        hp_.setMode(vsm::audio::dsp::StateVariableFilter::Mode::HighPass);
        hp_.setResonance(0.5f);
    }

    void configure(float toneHz, float decaySeconds, float level) {
        decay_ = decaySeconds;
        level_ = level;
        hp_.setCutoffHz(toneHz);
    }

    bool isActive() const { return env_.isActive(); }
    void trigger(float velGain) { velGain_ = velGain; env_.setDecaySeconds(decay_); env_.trigger(); }

    float render() {
        if (!env_.isActive()) return 0.0f;
        return hp_.process(rng_.nextBipolar()) * env_.next() * level_ * velGain_ * 0.8f;
    }

private:
    DecayEnv env_;
    vsm::audio::dsp::StateVariableFilter hp_;
    vsm::util::DeterministicRng rng_{0xD1B54A32D192ED03ULL};
    float decay_ = 0.06f, level_ = 1.0f, velGain_ = 1.0f;
};

/// TAMBOURIN : le bruit d'un shaker PLUS deux modes de cymbalette. Ce sont ces
/// deux modes métalliques qui le distinguent d'un shaker, et rien d'autre.
class Tambourine {
public:
    void setSampleRate(double sr) {
        shaker_.setSampleRate(sr);
        for (auto& m : jingles_) m.setSampleRate(sr);
    }

    void configure(float decaySeconds, float level) {
        level_ = level;
        shaker_.configure(5200.0f, decaySeconds * 0.6f, 1.0f);
        jingles_[0].configure(6300.0f, decaySeconds, 0.5f);
        jingles_[1].configure(8900.0f, decaySeconds * 0.8f, 0.35f);
    }

    bool isActive() const {
        if (shaker_.isActive()) return true;
        for (const auto& m : jingles_) if (m.isActive()) return true;
        return false;
    }

    void trigger(float velGain) {
        shaker_.trigger(velGain);
        jingles_[0].trigger(velGain);
        jingles_[1].trigger(velGain, 2.4f);
    }

    float render() {
        if (!isActive()) return 0.0f;
        float sum = shaker_.render();
        for (auto& m : jingles_) sum += m.render();
        return sum * 0.7f * level_;
    }

private:
    Shaker shaker_;
    std::array<Mode, 2> jingles_;
    float level_ = 1.0f;
};

class PercSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    enum ParamIds : vsm::audio::plugin::ParamId {
        kCongaLevel = 0, kCongaTune, kCongaDecay,
        kBongoLevel, kBongoTune, kBongoDecay,
        kTimbaleLevel, kTimbaleTune, kTimbaleDecay,
        kCowbellLevel, kCowbellTune, kCowbellDecay,
        kWoodLevel, kWoodTune, kWoodDecay,
        kShakerLevel, kShakerTone, kShakerDecay,
        kTambourineLevel, kTambourineDecay,
        kAccent,
        kNumParams
    };

    /// Numérotation General MIDI des percussions, sans écart.
    enum Note : uint8_t {
        kNoteTambourine = 54,
        kNoteCowbell = 56,
        kNoteHiBongo = 60, kNoteLowBongo = 61,
        kNoteMuteHiConga = 62, kNoteOpenHiConga = 63, kNoteLowConga = 64,
        kNoteHiTimbale = 65, kNoteLowTimbale = 66,
        kNoteMaracas = 70,
        kNoteClaves = 75,
        kNoteHiWoodBlock = 76, kNoteLowWoodBlock = 77
    };

    PercSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;

    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override;

    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;

    const char* machineName() const override { return "Percussion (peaux et barres, modal)"; }
    int activeVoiceCount() const override;

private:
    void triggerNote(uint8_t note, uint8_t velocity);
    void applyConfig();

    // Deux congas et deux bongos : ce sont des instruments qui vont par
    // paires, et la paire EST le geste -- alterner grave et aigu est ce qu'on
    // entend d'un conguero. `mute` est la même peau, étouffée : durée courte.
    Membrane lowConga_, hiConga_, muteConga_;
    Membrane lowBongo_, hiBongo_;
    Membrane lowTimbale_, hiTimbale_;
    Cowbell cowbell_;
    Bar hiWood_, lowWood_, claves_;
    Shaker maracas_;
    Tambourine tambourine_;

    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
    double sampleRate_ = 48000.0;
};

} // namespace vsm::plugins::perc
