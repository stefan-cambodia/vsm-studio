#pragma once
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/dsp/MorphOscillator.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::granular {

/// SYNTHÈSE GRANULAIRE — le son comme NUAGE de grains fenêtrés.
///
/// POURQUOI CETTE MACHINE (CDC machines § 9, addendum du 02/09/2026). Le parc
/// sait osciller, filtrer, modéliser, échantillonner ; aucune machine ne sait
/// faire d'une note un NUAGE — des dizaines de grains courts, chacun fenêtré,
/// chacun légèrement à côté des autres en hauteur et en temps, dont la somme
/// va du timbre net à la texture qui scintille. C'est une famille de synthèse
/// (Gabor, Xenakis, Roads), pas un nom de machine — et elle est ajoutée au
/// titre que le § 7 autorise : le jeu, sans rien promettre à la
/// reconstruction.
///
/// LE MODÈLE.
///
/// ```
///   horloge de grains ──> [grain] = fenêtre de Hann × oscillateur morphable
///        (density,           │        (note + dispersion de hauteur,
///         time spray)        │         + octave si le shimmer tire)
///                            └──> somme des grains actifs ──> VCF ──> VCA
/// ```
///
///  - **Un grain** est une bouffée de quelques dizaines de millisecondes de
///    l'oscillateur morphable du parc (`dsp/MorphOscillator.h`), sous fenêtre
///    de Hann — pas de clic possible, la fenêtre s'annule aux deux bouts.
///  - **La dispersion fait le nuage.** `Pitch Spray` tire la hauteur de
///    chaque grain dans ± N demi-tons ; `Time Spray` dérègle l'horloge ;
///    `Shimmer` donne à certains grains l'octave supérieure — le
///    scintillement classique des textures granulaires. Tout l'aléatoire est
///    SEEDÉ et remis à zéro à chaque note : deux rendus du même projet sont
///    identiques au bit près, la règle du parc.
///  - **À dispersion nulle, la machine redevient périodique** : grains
///    alignés, hauteur nette — c'est la moitié témoin du trait distinctif,
///    et c'est ce qui fait de la dispersion un VRAI réglage (un continuum de
///    la note à la texture, pas un interrupteur).
///  - **Les grains se répartissent en stéréo** (`Stereo Spread`) : un nuage
///    est un espace, pas un point.
///
/// APPROXIMATIONS ASSUMÉES (§ 8 du CDC nouvelle-machine) : la source des
/// grains est un oscillateur, pas un échantillon (granuler un fichier est le
/// travail d'un sampler granulaire — le parc a `vsm.sampler` et
/// `vsm.multisample` pour lire des fichiers, et cette machine-ci n'en dépend
/// d'aucun) ; l'enveloppe de grain est une Hann fixe (les formes de grain
/// exotiques attendront qu'on les mesure) ; pas de position de lecture
/// puisqu'il n'y a pas de fichier. Statut « dérivé ».
class GranularVoice {
public:
    static constexpr int kMaxGrains = 16;

    struct Params {
        float grainSizeMs = 80.0f;    // 5..400 ms
        float density = 25.0f;        // grains par seconde
        float pitchSpray = 0.0f;      // ± demi-tons
        float timeSpray = 0.0f;       // 0..1, fraction de l'intervalle
        float shimmer = 0.0f;         // 0..1, probabilité de l'octave
        float shape = 0.0f;           // 0..3, forme de la source
        float stereoSpread = 0.5f;    // 0..1
        float cutoff = 9000.0f, resonance = 0.15f;
        float velocityToLevel = 0.5f;
        // Molette de hauteur, en demi-tons — les grains À VENIR la suivent
        // (un grain déjà né garde la sienne, comme il garde sa fenêtre). À
        // zéro l'addition est exacte : empreinte inchangée au bit.
        float bendSemitones = 0.0f;
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        seed_ = seed;
        for (auto& grain : grains_) {
            grain.osc.setSampleRate(sampleRate_);
            grain.remaining = 0;
        }
        filterL_.setSampleRate(sampleRate_);
        filterR_.setSampleRate(sampleRate_);
        ampEnv_.setSampleRate(sampleRate_);
    }

    bool isActive() const { return ampEnv_.isActive(); }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void setEnvelope(const vsm::audio::dsp::AdsrSettings& amp) { ampEnv_.setSettings(amp); }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel;
        note_ = note;
        velocity_ = velocity;
        ampEnv_.noteOn();
        // LE NUAGE EST REJOUABLE : l'aléatoire repart de la graine de la voix
        // à chaque note, l'horloge de zéro, et le premier grain part tout de
        // suite -- une attaque sans grain serait un silence qu'aucune
        // enveloppe n'explique.
        rng_ = vsm::util::DeterministicRng(seed_);
        samplesToNext_ = 0;
        masterPhase_ = 0.0;
        for (auto& grain : grains_) grain.remaining = 0;
    }
    void noteOff(uint8_t) { ampEnv_.noteOff(); }

    void render(const Params& p, float& outL, float& outR) {
        if (!ampEnv_.isActive()) return;

        // LA PHASE MAÎTRESSE. Chaque grain remettait sa phase à zéro en
        // naissant ; à dispersion NULLE, des grains espacés d'un intervalle
        // quelconque se peignaient entre eux et le fondamental s'affaissait
        // (mesuré : h1 sous 0,01 là où le témoin exige une hauteur nette).
        // La voix tient donc une phase continue à la hauteur de la note, et
        // chaque grain en HÉRITE à sa naissance : à dispersion nulle, tous
        // les grains sont des tranches du même oscillateur -- cohérents --
        // et la dispersion garde tout son effet, une phase commune entre des
        // fréquences différentes n'alignant rien.
        const float baseHz = 440.0f * std::exp2f(
            (static_cast<float>(note_) + p.bendSemitones - 69.0f) / 12.0f);
        masterPhase_ += static_cast<double>(baseHz) / sampleRate_;
        if (masterPhase_ >= 1.0) masterPhase_ -= 1.0;

        // L'HORLOGE DE GRAINS. L'intervalle nominal est 1/density ; le time
        // spray le tire entre (1-s) et (1+s) fois cette valeur.
        if (samplesToNext_ <= 0) {
            spawnGrain(p);
            const double intervalle = sampleRate_ / std::max(2.0, static_cast<double>(p.density));
            const double tirage = 1.0 + static_cast<double>(p.timeSpray) * rng_.nextBipolar();
            samplesToNext_ = std::max(1, static_cast<int>(intervalle * std::max(0.1, tirage)));
        }
        --samplesToNext_;

        float sumL = 0.0f, sumR = 0.0f;
        for (auto& grain : grains_) {
            if (grain.remaining == 0) continue;
            const size_t n = grain.length - grain.remaining;
            // Fenêtre de Hann : nulle aux deux bouts, aucun clic possible.
            const float fenetre = 0.5f - 0.5f * std::cos(
                static_cast<float>(vsm::audio::dsp::kTwoPi)
                * static_cast<float>(n) / static_cast<float>(grain.length));
            const float s = grain.osc.nextSample(p.shape, 0.5f) * fenetre;
            sumL += s * grain.gainL;
            sumR += s * grain.gainR;
            --grain.remaining;
        }

        filterL_.setCutoffHz(p.cutoff);
        filterL_.setResonance(p.resonance);
        filterR_.setCutoffHz(p.cutoff);
        filterR_.setResonance(p.resonance);
        const float envLevel = ampEnv_.nextSample();
        const float velocity = static_cast<float>(velocity_) / 127.0f;
        const float gain = (1.0f - p.velocityToLevel * (1.0f - velocity)) * envLevel;
        outL += filterL_.process(sumL) * gain;
        outR += filterR_.process(sumR) * gain;
    }

private:
    struct Grain {
        vsm::audio::dsp::MorphOscillator osc;
        size_t length = 0;
        size_t remaining = 0;
        float gainL = 0.7f, gainR = 0.7f;
    };

    void spawnGrain(const Params& p) {
        for (auto& grain : grains_) {
            if (grain.remaining != 0) continue;
            const float spray = p.pitchSpray * rng_.nextBipolar();
            // Le SHIMMER tire l'octave : c'est un tirage par grain, pas un
            // réglage de mélange -- certains grains scintillent, les autres
            // portent, et c'est cette inégalité qui fait la texture.
            const float octave = (rng_.nextUnipolar() < p.shimmer) ? 12.0f : 0.0f;
            const float hz = 440.0f * std::exp2f(
                (static_cast<float>(note_) + spray + octave + p.bendSemitones - 69.0f) / 12.0f);
            grain.osc.setFrequency(hz);
            grain.osc.reset(masterPhase_);
            grain.length = std::max<size_t>(
                16, static_cast<size_t>(sampleRate_ * static_cast<double>(
                        std::clamp(p.grainSizeMs, 5.0f, 400.0f)) / 1000.0));
            grain.remaining = grain.length;
            // Répartition stéréo à puissance constante.
            const float pan = 0.5f + 0.5f * p.stereoSpread * rng_.nextBipolar();
            grain.gainL = std::cos(pan * static_cast<float>(vsm::audio::dsp::kTwoPi) * 0.25f);
            grain.gainR = std::sin(pan * static_cast<float>(vsm::audio::dsp::kTwoPi) * 0.25f);
            return;
        }
        // Tous les emplacements occupés : le grain est SAUTÉ, pas volé. Voler
        // le plus ancien couperait sa fenêtre en plein vol -- un clic, le
        // défaut exact que la fenêtre existe pour interdire.
    }

    double sampleRate_ = 48000.0;
    uint64_t seed_ = 0;
    std::array<Grain, kMaxGrains> grains_{};
    int samplesToNext_ = 0;
    double masterPhase_ = 0.0;
    vsm::audio::dsp::StateVariableFilter filterL_, filterR_;
    vsm::audio::dsp::AdsrEnvelope ampEnv_;
    vsm::util::DeterministicRng rng_{0x4752414E5F00ULL};
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class GranularSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 6;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kGrainSize = 1, kDensity, kPitchSpray, kTimeSpray, kShimmer,
        kShape, kStereoSpread,
        kCutoff, kResonance,
        kAmpAttack, kAmpDecay, kAmpSustain, kAmpRelease,
        kVelocitySensitivity, kOutputLevel,
    };

    GranularSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override {
        // La molette de hauteur, comme tout synthé (§ 10) : les grains à
        // venir la suivent. Le CC 1 est refusé en le disant -- pas de LFO
        // ici, la vie du son est dans la dispersion.
        if (event.kind == vsm::audio::plugin::MidiControlEvent::Kind::PitchBend) {
            bendSemitones_.store(event.value, std::memory_order_relaxed);
            return true;
        }
        return false;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Granular (le nuage de grains)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<GranularVoice, kMaxVoices> voiceManager_;
    // Molette de hauteur (demi-tons), même contrat que params_.
    std::atomic<float> bendSemitones_{0.0f};
};

} // namespace vsm::plugins::granular
