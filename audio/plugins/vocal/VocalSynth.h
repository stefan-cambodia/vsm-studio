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

namespace vsm::plugins::vocal {

/// LA VOIX, MODÉLISÉE — la dernière source que le parc reportait sans la faire.
///
/// POURQUOI CETTE MACHINE. Le § 1 de `ROADMAP-fusion.md` écrit noir sur blanc
/// l'état du parc : « chaque source a une machine qui la MODÉLISE, **sauf la
/// voix**, qui est reportée telle quelle et présentée comme telle ». C'est la
/// dernière case nommée du tableau de couverture, et la seule qui restait
/// atteignable -- celle des bois coniques ayant été mesurée hors de portée
/// quatre fois (§ 1 et `ConeSynth.h`).
///
/// Reporter la voix reste le bon choix par défaut pour reconstruire un disque :
/// une voix humaine n'est pas synthétisable à l'identique, et la chaîne le dit.
/// Mais « pas à l'identique » n'est pas « pas du tout » : un chœur, une nappe
/// vocale, un pad qui prononce une voyelle sont d'un usage courant, et le parc
/// ne pouvait en produire aucun. Cette machine ne remplace pas le sampler sur
/// un couplet ; elle donne au parc le TIMBRE vocal, qui lui manquait.
///
/// LE TRAIT DISTINCTIF, ET C'EST LA DÉFINITION MÊME D'UNE VOIX. Les résonances
/// du conduit vocal -- les FORMANTS -- ne suivent PAS la hauteur chantée. Un
/// même « a » à 110 Hz et à 220 Hz a ses deux premiers formants au même endroit,
/// vers 700 et 1200 Hz : c'est ce qui fait qu'on reconnaît la voyelle
/// indépendamment de la note, et c'est ce qui distingue une voix de tout
/// instrument dont le spectre se transpose en bloc.
///
/// Aucune machine du parc ne sait faire ça. Un filtre soustractif suit le
/// clavier (`filter.keyTracking`) ou ne le suit pas, mais il n'a qu'une seule
/// résonance ; il ne peut pas tenir TROIS pics à des fréquences absolues tout
/// en laissant le fondamental se déplacer. Le test joue la même voyelle à une
/// octave d'écart et vérifie que les pics ne bougent pas, pendant que le
/// fondamental double.
///
/// LE MODÈLE : source-filtre, celui de la phonétique depuis 1960. Une SOURCE
/// glottique -- une impulsion périodique dont la forme se règle, plus du
/// souffle -- traverse trois RÉSONATEURS en parallèle accordés sur les formants
/// de la voyelle. C'est le modèle le plus simple qui produise une voyelle
/// reconnaissable, et il tient en trois filtres.
///
/// APPROXIMATIONS ASSUMÉES (§ 8 de `CDC-nouvelle-machine.md`), statut
/// « dérivé » -- les fréquences de formants viennent des tables de phonétique
/// publiées, aucune n'a été relevée sur un chanteur :
///
///  - **Cinq voyelles seulement** (a, e, i, o, u), et le réglage passe de
///    l'une à l'autre EN CONTINU. Les consonnes n'y sont pas : elles demandent
///    des transitoires et des occlusions, c'est-à-dire un modèle de geste, pas
///    de conduit. Cette machine chante des voyelles ; elle ne parle pas, et
///    c'est écrit.
///  - **Trois formants**, là où la phonétique en compte cinq utiles. Les deux
///    premiers font la voyelle, le troisième fait la couleur ; les suivants
///    ajoutent surtout de la présence, qu'on obtient ici par le souffle.
///  - **Pas de nasalité, pas de chant diphonique.** Il faudrait un
///    anti-formant, donc un zéro dans la fonction de transfert.
///  - **Le vibrato est un LFO**, pas une simulation du souffle et du geste
///    d'un chanteur -- mais il est retardé, parce qu'une voix ne commence pas
///    une note en vibrant.

class VocalVoice {
public:
    static constexpr int kFormants = 3;

    /// Les cinq voyelles, formants en hertz. Valeurs d'une voix moyenne
    /// (registre médium), telles que les tables de phonétique les donnent.
    /// L'ordre est celui du trapèze vocalique : a, e, i, o, u -- pour que le
    /// réglage, en balayant de 0 à 4, suive un chemin qui a un sens dans la
    /// bouche et non un ordre alphabétique.
    static constexpr int kVowels = 5;
    static constexpr float kFormantHz[kVowels][kFormants] = {
        {  730.0f, 1090.0f, 2440.0f },  // a
        {  530.0f, 1840.0f, 2480.0f },  // e
        {  270.0f, 2290.0f, 3010.0f },  // i
        {  570.0f,  840.0f, 2410.0f },  // o
        {  300.0f,  870.0f, 2240.0f },  // u
    };
    /// Amplitudes relatives : le premier formant porte, les suivants colorent.
    static constexpr float kFormantGain[kFormants] = { 1.0f, 0.55f, 0.30f };

    struct Params {
        float vowel = 0.0f;         // 0..4, en continu entre les voyelles
        float formantShift = 0.0f;  // demi-tons : la TAILLE du conduit
        float breath = 0.2f;        // part de souffle dans la source
        float tension = 0.5f;       // forme de l'impulsion glottique
        float vibratoRate = 5.2f;
        float vibratoDepth = 0.25f;
        float vibratoDelay = 0.35f;
        float velocityToBreath = 0.3f;
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate;
        ampEnv_.setSampleRate(sampleRate);
        for (auto& f : formants_) {
            f.setSampleRate(sampleRate);
            f.setMode(vsm::audio::dsp::StateVariableFilter::Mode::BandPass);
            // RÉSONANCE ÉLEVÉE : un formant est un pic étroit. Trop faible, la
            // voyelle ne s'entend plus ; trop forte, le filtre sonne comme une
            // cloche et la voix devient un synthétiseur qui siffle.
            f.setResonance(0.86f);
        }
        drift_.setSampleRate(sampleRate);
        drift_.setSeed(seed);
        drift_.setRateHz(0.11f);
        drift_.setAmount(0.0f);
        rng_ = vsm::util::DeterministicRng{seed ^ 0x564F4341ULL};
        phase_ = 0.0f;
        vibPhase_ = 0.0f;
        elapsed_ = 0.0f;
    }

    bool isActive() const { return ampEnv_.isActive(); }
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
        phase_ = 0.0f;
        vibPhase_ = 0.0f;
        elapsed_ = 0.0f;
    }

    void noteOff(uint8_t) { ampEnv_.noteOff(); }

    float render(const Params& p) {
        if (!ampEnv_.isActive()) return 0.0f;

        const float vel = static_cast<float>(velocity_) / 127.0f;

        // VIBRATO RETARDÉ : une voix ne commence pas une note en vibrant, elle
        // s'installe d'abord. Le retard est un réglage parce que le geste
        // diffère d'un chanteur à l'autre.
        float vibrato = 0.0f;
        if (elapsed_ > p.vibratoDelay) {
            vibPhase_ += static_cast<float>(vsm::audio::dsp::kTwoPi) * p.vibratoRate
                       / static_cast<float>(sampleRate_);
            if (vibPhase_ > static_cast<float>(vsm::audio::dsp::kTwoPi))
                vibPhase_ -= static_cast<float>(vsm::audio::dsp::kTwoPi);
            const float montee = std::min((elapsed_ - p.vibratoDelay) / 0.3f, 1.0f);
            vibrato = std::sin(vibPhase_) * p.vibratoDepth * montee * 0.4f;
        }
        const float f0 = baseHz_
                       * std::pow(2.0f, (vibrato + drift_.nextValue() * 0.08f) / 12.0f);

        // LA SOURCE GLOTTIQUE. Une impulsion périodique dont la LARGEUR se
        // règle : serrée, la source est riche et la voix est tendue ; large,
        // elle est douce et la voix est soufflée. C'est la « tension », et elle
        // agit sur le spectre de la source, pas sur les formants -- ce qui est
        // exactement la division du modèle source-filtre.
        phase_ += f0 / static_cast<float>(sampleRate_);
        if (phase_ >= 1.0f) phase_ -= 1.0f;
        const float largeur = 0.06f + (1.0f - p.tension) * 0.30f;
        float source = 0.0f;
        if (phase_ < largeur) {
            // Une demi-période de sinus : c'est la forme d'onde glottique la
            // plus simple qui n'ait pas de discontinuité, donc pas de repliement
            // brutal.
            const float x = phase_ / largeur;
            source = std::sin(static_cast<float>(M_PI) * x) * 2.0f - 0.6f;
        } else {
            source = -0.6f;
        }
        const float souffle = p.breath * (1.0f + p.velocityToBreath * vel);
        source = source * (1.0f - souffle * 0.6f) + rng_.nextBipolar() * souffle * 0.7f;

        // LES TROIS FORMANTS, À DES FRÉQUENCES ABSOLUES. Le décalage est un
        // TRANSPOSITION du conduit entier -- une gorge plus courte a tous ses
        // formants plus haut --, et c'est le seul réglage qui les déplace. La
        // note jouée, elle, ne les touche pas : c'est tout le sujet.
        const float glissement = std::pow(2.0f, p.formantShift / 12.0f);
        const int bas = static_cast<int>(p.vowel);
        const int haut = std::min(bas + 1, kVowels - 1);
        const float melange = p.vowel - static_cast<float>(bas);

        float sortie = 0.0f;
        for (int k = 0; k < kFormants; ++k) {
            const float hz = (kFormantHz[bas][k] * (1.0f - melange)
                            + kFormantHz[haut][k] * melange) * glissement;
            formants_[static_cast<size_t>(k)].setCutoffHz(
                std::min(hz, static_cast<float>(sampleRate_) * 0.45f));
            sortie += formants_[static_cast<size_t>(k)].process(source) * kFormantGain[k];
        }

        elapsed_ += 1.0f / static_cast<float>(sampleRate_);
        return sortie * ampEnv_.nextSample() * (0.4f + 0.6f * vel);
    }

private:
    double sampleRate_ = 48000.0;
    vsm::audio::dsp::AdsrEnvelope ampEnv_;
    std::array<vsm::audio::dsp::StateVariableFilter, kFormants> formants_;
    vsm::audio::dsp::AnalogDrift drift_;
    vsm::util::DeterministicRng rng_{0x564F4341ULL};
    float phase_ = 0.0f, vibPhase_ = 0.0f, elapsed_ = 0.0f, baseHz_ = 261.6f;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class VocalSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kVowel = 1, kFormantShift, kBreath, kTension,
        kVibratoRate, kVibratoDepth, kVibratoDelay,
        kAmpAttack, kAmpDecay, kAmpSustain, kAmpRelease,
        kVelocityToBreath, kAnalogCharacter, kOutputLevel,
    };

    VocalSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Vocal (conduit vocal, voyelles)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<VocalVoice, kMaxVoices> voiceManager_;
};

} // namespace vsm::plugins::vocal
