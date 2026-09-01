#pragma once
#include "vsm/audio/dsp/AnalogDrift.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/LadderFilterZDFx4.h"
#include "vsm/audio/dsp/Oscillator.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::prophet {

enum ParamIds : vsm::audio::plugin::ParamId {
    kOscALevel = 0,
    kOscAShape,        // 0 = saw, 1 = pulse
    kOscAPulseWidth,
    kOscBLevel,
    kOscBShape,        // 0 = saw, 1 = triangle, 2 = pulse
    kOscBPulseWidth,
    kOscBDetune,       // demi-tons
    kSync,             // 0/1 : hard-sync osc B -> osc A
    kFilterCutoff,
    kFilterResonance,
    kFilterEnvAmount,
    kFilterKeyTrack,
    kFilterAttack,
    kFilterDecay,
    kFilterSustain,
    kFilterRelease,
    kAmpAttack,
    kAmpDecay,
    kAmpSustain,
    kAmpRelease,
    kLfoRate,
    kLfoToPitch,
    kLfoToFilter,
    kPolyModFiltEnvAmount, // source 1 : enveloppe de filtre
    kPolyModOscBAmount,    // source 2 : oscillateur B
    kPolyModToFreqA,       // destination : fréquence osc A (cross-mod / FM)
    kPolyModToPwA,         // destination : largeur d'impulsion osc A
    kPolyModToFilter,      // destination : coupure du filtre
    kAnalogCharacter,
    kNumParams
};

/// Paramètres partagés lus une fois par bloc et passés à chaque voix.
struct ProphetParams {
    float oscALevel, oscAPw; int oscAShape;
    float oscBLevel, oscBPw, oscBDetune; int oscBShape;
    bool sync;
    float cutoffBase, resonance, filterEnvAmount, keyTrack;
    float lfoToPitch, lfoToFilter;
    float polyFiltEnvAmt, polyOscBAmt;
    bool polyToFreqA, polyToPwA, polyToFilter;
    float analogCharacter;
    // Molette de hauteur, en octaves (les exposants se somment en octaves).
    // À zéro l'addition est exacte : empreinte inchangée au bit.
    float bendOctaves = 0.0f;
};

/// Une voix Prophet-style : deux oscillateurs (A/B) + hard-sync + Poly-Mod +
/// filtre 4 pôles résonant + DEUX enveloppes ADSR (filtre et ampli, comme le
/// hardware, contrairement au Juno qui n'en a qu'une).
///
/// Poly-Mod (la signature du Prophet) : un signal de modulation
///   polyMod = (amount_FiltEnv * enveloppeFiltre) + (amount_OscB * oscB)
/// est routé, selon des interrupteurs de DESTINATION, vers :
///   - la fréquence de l'osc A (cross-mod audio -> timbres FM/clangoreux),
///   - la largeur d'impulsion de l'osc A,
///   - la coupure du filtre.
///
/// Approximations (section 27) : filtre Curtis/SSM réel modélisé par le
/// ladder ZDF 4 pôles (même pente/résonance). Le HARD-SYNC est fait par
/// simple remise à zéro de la phase de l'osc A au passage de cycle de l'osc
/// B (pas de correction BLEP du point de sync -> léger repliement au sync,
/// raffinement Phase 6). Clavier Prophet-5 non sensible à la vélocité ->
/// VCA piloté par l'enveloppe seule. Aucune mesure sur un Prophet matériel.
class ProphetVoice {
public:
    void prepare(double sampleRate, uint64_t seed) {
        using namespace vsm::audio::dsp;
        sampleRate_ = sampleRate;
        oscA_.setSampleRate(sampleRate);
        oscB_.setSampleRate(sampleRate);
        // Le filtre n'appartient plus à la voix : il est partagé par groupe de
        // quatre voix et configuré par le synthé (ProphetSynth::voiceFilters_).
        ampEnv_.setSampleRate(sampleRate);
        filterEnv_.setSampleRate(sampleRate);
        driftA_.setSampleRate(sampleRate); driftA_.setSeed(seed); driftA_.setRateHz(0.13f);
        driftB_.setSampleRate(sampleRate); driftB_.setSeed(seed ^ 0xABCDULL); driftB_.setRateHz(0.11f);
        cutoffDrift_.setSampleRate(sampleRate); cutoffDrift_.setSeed(seed ^ 0x5555ULL); cutoffDrift_.setRateHz(0.09f);
    }

    // Contrat VoiceManager
    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel; note_ = note; velocity_ = velocity;
        ampEnv_.noteOn(); filterEnv_.noteOn();
        oscA_.reset(0.0); oscB_.reset(0.0); syncPhase_ = 0.0f;
    }
    void noteOff(uint8_t /*velocity*/) { ampEnv_.noteOff(); filterEnv_.noteOff(); }
    bool isActive() const { return ampEnv_.isActive(); }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void setSettings(const vsm::audio::dsp::AdsrSettings& amp,
                     const vsm::audio::dsp::AdsrSettings& filt) {
        ampEnv_.setSettings(amp); filterEnv_.setSettings(filt);
    }
    void setDriftAmount(float a) { driftA_.setAmount(a); driftB_.setAmount(a); cutoffDrift_.setAmount(a); }

    // Rendu en deux temps : les cinq voix partagent deux filtres vectorisés
    // (quatre lignes chacun). Voir SimdFloat4.h pour le raisonnement.
    float renderPreFilter(const ProphetParams& p, float lfo) {
        using namespace vsm::audio::dsp;
        active_ = ampEnv_.isActive();
        if (!active_) {
            pendingCutoff_ = p.cutoffBase; // ligne SIMD calculée quand même : valeur saine
            pendingAmpLevel_ = 0.0f;
            return 0.0f;
        }

        const float baseHz = 440.0f * std::exp2f((static_cast<float>(note_) - 69.0f) / 12.0f);
        const float filtEnvLevel = filterEnv_.nextSample();
        const float ampLevel = ampEnv_.nextSample();

        // --- Oscillateur B (aussi source de modulation) ---
        const float drBoct = driftB_.nextValue() * kDriftSemis / 12.0f;
        const float freqB = baseHz * std::exp2f(p.oscBDetune / 12.0f + drBoct
                                                + p.bendOctaves);
        oscB_.setFrequency(freqB);
        oscB_.setWaveform(shapeToWave(p.oscBShape));
        if (p.oscBShape == 2) oscB_.setPulseWidth(p.oscBPw);
        const float bRaw = oscB_.nextSample();

        // --- Signal Poly-Mod ---
        const float polyMod = p.polyFiltEnvAmt * filtEnvLevel + p.polyOscBAmt * bRaw;

        // --- Hard-sync : reset de la phase de A au cycle de B ---
        syncPhase_ += freqB / static_cast<float>(sampleRate_);
        if (syncPhase_ >= 1.0f) { syncPhase_ -= 1.0f; if (p.sync) oscA_.reset(0.0); }

        // --- Oscillateur A (destination de la Poly-Mod) ---
        const float drAsemi = driftA_.nextValue() * kDriftSemis;
        const float freqAOct = (p.polyToFreqA ? polyMod * kPolyFreqOctaves : 0.0f)
                             + lfo * p.lfoToPitch * kLfoPitchSemis / 12.0f
                             + drAsemi / 12.0f + p.bendOctaves;
        const float freqA = baseHz * std::exp2f(freqAOct);
        oscA_.setFrequency(freqA);
        oscA_.setWaveform(shapeToWave(p.oscAShape == 0 ? 0 : 2));
        if (p.oscAShape != 0) {
            const float pw = std::clamp(p.oscAPw + (p.polyToPwA ? polyMod * 0.4f : 0.0f), 0.05f, 0.95f);
            oscA_.setPulseWidth(pw);
        }
        const float aRaw = oscA_.nextSample();

        float mix = (aRaw * p.oscALevel + bRaw * p.oscBLevel) * 0.5f;

        // --- Filtre ---
        const float cutoffDriftOct = cutoffDrift_.nextValue() * kCutoffDriftOct;
        const float envOct = p.filterEnvAmount * filtEnvLevel * kFilterEnvOctaves;
        const float lfoOct = lfo * p.lfoToFilter * kLfoFilterOctaves;
        const float trackOct = p.keyTrack * (static_cast<float>(note_) - 60.0f) / 12.0f;
        const float polyOct = p.polyToFilter ? polyMod * kPolyFilterOctaves : 0.0f;
        pendingCutoff_ = p.cutoffBase * std::exp2f(envOct + lfoOct + trackOct + polyOct + cutoffDriftOct);
        pendingAmpLevel_ = ampLevel;
        return mix;
    }

    /// VCA (pas de vélocité : le Prophet-5 n'y est pas sensible, trait
    /// authentique déjà couvert par un test).
    float applyFilterOutput(float filtered) const {
        return active_ ? filtered * pendingAmpLevel_ : 0.0f;
    }

    float pendingCutoffHz() const { return pendingCutoff_; }

private:
    static vsm::audio::dsp::Waveform shapeToWave(int shape) {
        using vsm::audio::dsp::Waveform;
        switch (shape) {
            case 1: return Waveform::Triangle;
            case 2: return Waveform::Square;
            default: return Waveform::Saw;
        }
    }

    static constexpr float kDriftSemis = 0.05f;
    static constexpr float kCutoffDriftOct = 0.12f;
    static constexpr float kFilterEnvOctaves = 6.0f;
    static constexpr float kLfoPitchSemis = 7.0f;
    static constexpr float kLfoFilterOctaves = 4.0f;
    static constexpr float kPolyFreqOctaves = 3.0f;
    static constexpr float kPolyFilterOctaves = 4.0f;

    double sampleRate_ = 48000.0;
    vsm::audio::dsp::BandLimitedOscillator oscA_, oscB_;
    float pendingCutoff_ = 1500.0f;
    float pendingAmpLevel_ = 0.0f;
    bool active_ = false;
    vsm::audio::dsp::AdsrEnvelope ampEnv_, filterEnv_;
    vsm::audio::dsp::AnalogDrift driftA_, driftB_, cutoffDrift_;
    float syncPhase_ = 0.0f;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

/// Prophet-style : polysynthé 5 voix (comme le Prophet-5), 2 oscillateurs par
/// voix, hard-sync, Poly-Mod, double enveloppe, filtre 4 pôles.
class ProphetSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 5;

    ProphetSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;

    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override;
    const vsm::audio::plugin::ParameterList& parameterList() const override;

    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;

    const char* machineName() const override { return "Prophet-style Polysynth"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& ev);

    vsm::audio::engine::VoiceManager<ProphetVoice, kMaxVoices> voiceManager_;

    // Cinq voix pour des lignes SIMD par quatre : deux filtres, trois lignes
    // inutilisées. Reste gagnant (deux filtres vectorisés ~124 ns contre cinq
    // scalaires ~358 ns) ; les lignes vides reçoivent du silence et une
    // coupure valide.
    static constexpr size_t kLanes = vsm::audio::dsp::LadderFilterZDFx4::kLanes;
    static constexpr size_t kVoiceGroups = (kMaxVoices + kLanes - 1) / kLanes;
    std::array<vsm::audio::dsp::LadderFilterZDFx4, kVoiceGroups> voiceFilters_;
    double lfoPhase_ = 0.0, lfoIncrement_ = 0.0;

    // Molette de hauteur (demi-tons), même contrat que params_.
    std::atomic<float> bendSemitones_{0.0f};

    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
    double sampleRate_ = 48000.0;
};

} // namespace vsm::plugins::prophet
