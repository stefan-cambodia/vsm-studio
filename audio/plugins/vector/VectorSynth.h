#pragma once
#include "vsm/audio/dsp/AnalogDrift.h"
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

namespace vsm::plugins::vector {

/// SYNTHÈSE VECTORIELLE — quatre timbres aux coins d'un carré, un point qui
/// s'y déplace (type Prophet VS, SY22/TG33).
///
/// POURQUOI CETTE MACHINE (CDC machines § 9, addendum du 02/09/2026). Le parc
/// couvre la soustraction, la FM, les tables d'ondes, l'additif, le granulaire
/// de forme (stochastic)… mais aucune machine n'a le geste VECTORIEL : un
/// mélange de QUATRE sources commandé par UNE position, et surtout un TRAJET
/// de cette position qui devient le timbre. Sur un VS, le son n'est pas un
/// point de l'espace, c'est une ORBITE : la couleur bouge sans qu'aucun
/// filtre ne bouge. C'est une famille de synthèse, pas un nom sur une liste —
/// et elle est ajoutée au titre que le § 7 autorise : le jeu, sans rien
/// promettre à la reconstruction.
///
/// LE MODÈLE.
///
/// ```
///   A(forme, désaccord) ──┐               position (x, y)
///   B(forme, désaccord) ──┼── mélange ────── + orbite (rate, depth) ──> VCF -> VCA
///   C(forme, désaccord) ──┤   bilinéaire
///   D(forme, désaccord) ──┘
/// ```
///
///  - **Le mélange est BILINÉAIRE** : wA=(1-x)(1-y), wB=x(1-y), wC=(1-x)y,
///    wD=xy. Aux coins, une seule source sonne — c'est ce que le trait
///    distinctif mesure : coin A en sinus, pas d'harmoniques ; coin B en
///    scie, des harmoniques. Au centre, les quatre à parts égales.
///  - **L'orbite remplace l'enveloppe vectorielle du VS**, et c'est une
///    approximation ASSUMÉE (§ 8 du CDC nouvelle-machine) : le VS enregistre
///    un trajet en quatre segments programmables ; ici le point décrit un
///    cercle autour de la position de repos, à fréquence et rayon réglables,
///    phase remise à zéro à chaque note (le trajet fait partie de l'attaque,
///    comme sur l'original). Un trajet libre demanderait un éditeur de
///    courbe ; le cercle donne le GESTE — la couleur qui tourne — pour deux
///    réglages.
///  - **Les quatre sources sont l'oscillateur morphable du parc**
///    (`dsp/MorphOscillator.h`, promu de `vsm.generic` pour l'occasion) :
///    forme continue sinus → triangle → scie → carré, polyBLEP compris. Un
///    coin se règle donc en timbre ET en hauteur (désaccord en demi-tons,
///    jusqu'à ±24 : les quintes et octaves entre coins sont le pain du VS).
///
/// APPROXIMATIONS ASSUMÉES, en plus de l'orbite : pas de table d'ondes
/// échantillonnée aux coins (le VS lisait des PCM courts — le parc a
/// `vsm.pcmhybrid` et `vsm.wavetable` pour cela), pas d'enveloppe par coin.
/// Aucune mesure sur un Prophet VS réel : statut « dérivé ».
class VectorVoice {
public:
    struct Params {
        std::array<float, 4> shape{0.0f, 2.0f, 1.0f, 3.0f};
        std::array<float, 4> detune{0.0f, 0.0f, 0.0f, 0.0f};  // demi-tons
        float vectorX = 0.5f, vectorY = 0.5f;
        float orbitRate = 0.6f;      // Hz
        float orbitDepth = 0.0f;     // 0..1, rayon en fraction du carré
        float cutoff = 6000.0f, resonance = 0.2f, envAmount = 0.3f, keyTrack = 0.3f;
        float velocityToLevel = 0.5f;
        // Molette de hauteur, en demi-tons. À zéro l'addition est exacte :
        // empreinte inchangée au bit.
        float bendSemitones = 0.0f;
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        for (auto& osc : oscs_) osc.setSampleRate(sampleRate_);
        filter_.setSampleRate(sampleRate_);
        ampEnv_.setSampleRate(sampleRate_);
        filterEnv_.setSampleRate(sampleRate_);
        drift_.setSampleRate(sampleRate_);
        drift_.setSeed(seed);
        drift_.setRateHz(0.12f);
        drift_.setAmount(0.0f);
    }

    bool isActive() const { return ampEnv_.isActive(); }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void setEnvelopes(const vsm::audio::dsp::AdsrSettings& amp,
                      const vsm::audio::dsp::AdsrSettings& filter) {
        ampEnv_.setSettings(amp);
        filterEnv_.setSettings(filter);
    }
    void setDriftAmount(float amount) { drift_.setAmount(amount); }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel;
        note_ = note;
        velocity_ = velocity;
        ampEnv_.noteOn();
        filterEnv_.noteOn();
        // PHASES ET ORBITE REMISES À ZÉRO : le trajet vectoriel fait partie
        // de l'attaque, et deux rendus du même projet doivent être identiques
        // au bit près.
        for (auto& osc : oscs_) osc.reset(0.0);
        orbitPhase_ = 0.0;
    }
    void noteOff(uint8_t) { ampEnv_.noteOff(); filterEnv_.noteOff(); }

    float render(const Params& p) {
        if (!ampEnv_.isActive()) return 0.0f;

        // L'ORBITE : le point tourne autour de la position de repos, borné au
        // carré. La phase démarre à zéro (côté +x) à chaque note.
        orbitPhase_ += static_cast<double>(std::max(0.0f, p.orbitRate)) / sampleRate_;
        if (orbitPhase_ >= 1.0) orbitPhase_ -= 1.0;
        const float angle = static_cast<float>(orbitPhase_ * vsm::audio::dsp::kTwoPi);
        const float rayon = 0.5f * std::clamp(p.orbitDepth, 0.0f, 1.0f);
        const float x = std::clamp(p.vectorX + rayon * std::cos(angle), 0.0f, 1.0f);
        const float y = std::clamp(p.vectorY + rayon * std::sin(angle), 0.0f, 1.0f);

        const float wA = (1.0f - x) * (1.0f - y);
        const float wB = x * (1.0f - y);
        const float wC = (1.0f - x) * y;
        const float wD = x * y;
        const float poids[4] = {wA, wB, wC, wD};

        const float driftSemis = drift_.nextValue() * 0.05f;
        float mix = 0.0f;
        for (int i = 0; i < 4; ++i) {
            const float hz = 440.0f * std::exp2f(
                (static_cast<float>(note_) + p.detune[static_cast<size_t>(i)]
                 + driftSemis + p.bendSemitones - 69.0f) / 12.0f);
            oscs_[static_cast<size_t>(i)].setFrequency(hz);
            mix += oscs_[static_cast<size_t>(i)].nextSample(
                       p.shape[static_cast<size_t>(i)], 0.5f)
                 * poids[static_cast<size_t>(i)];
        }

        const float envLevel = filterEnv_.nextSample();
        const float coupure = std::clamp(
            p.cutoff * std::exp2f(p.envAmount * envLevel * 4.0f
                                  + p.keyTrack * (static_cast<float>(note_) - 60.0f) / 12.0f),
            20.0f, static_cast<float>(sampleRate_) * 0.45f);
        filter_.setCutoffHz(coupure);
        filter_.setResonance(p.resonance);
        const float filtre = filter_.process(mix);

        const float velocity = static_cast<float>(velocity_) / 127.0f;
        const float gain = 1.0f - p.velocityToLevel * (1.0f - velocity);
        return filtre * ampEnv_.nextSample() * gain;
    }

private:
    double sampleRate_ = 48000.0;
    std::array<vsm::audio::dsp::MorphOscillator, 4> oscs_{};
    vsm::audio::dsp::StateVariableFilter filter_;
    vsm::audio::dsp::AdsrEnvelope ampEnv_, filterEnv_;
    vsm::audio::dsp::AnalogDrift drift_;
    double orbitPhase_ = 0.0;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class VectorSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kVectorX = 1, kVectorY, kOrbitRate, kOrbitDepth,
        kShapeA, kDetuneA, kShapeB, kDetuneB,
        kShapeC, kDetuneC, kShapeD, kDetuneD,
        kCutoff, kResonance, kEnvAmount, kKeyTrack,
        kAmpAttack, kAmpDecay, kAmpSustain, kAmpRelease,
        kFilterAttack, kFilterDecay, kFilterSustain, kFilterRelease,
        kVelocitySensitivity, kAnalogCharacter, kOutputLevel,
    };

    VectorSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override {
        // La molette de hauteur, comme tout synthé (§ 10 du CDC) ; le CC 1
        // est refusé en le disant — cette machine n'a pas de LFO vers la
        // hauteur, son mouvement est le VECTEUR, pas un vibrato.
        if (event.kind == vsm::audio::plugin::MidiControlEvent::Kind::PitchBend) {
            bendSemitones_.store(event.value, std::memory_order_relaxed);
            return true;
        }
        return false;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Vector (quatre coins, un trajet)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<VectorVoice, kMaxVoices> voiceManager_;
    // Molette de hauteur (demi-tons), même contrat que params_.
    std::atomic<float> bendSemitones_{0.0f};
};

} // namespace vsm::plugins::vector
