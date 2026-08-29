#pragma once
#include "vsm/audio/dsp/AnalogDrift.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/Oscillator.h"
#include "vsm/audio/dsp/ParameterSmoother.h"
#include "vsm/audio/engine/MonoVoiceAllocator.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::ms20 {

/// Filtre résonant 2 pôles style MS-20 (Korg Sallen-Key), utilisable en
/// passe-haut OU passe-bas. C'est du DSP SPÉCIFIQUE à cette machine (le guide
/// §4 : une brique ne va dans dsp/ que si elle est réutilisable ; ici la
/// caractéristique — résonance très agressive, auto-oscillation SATURÉE,
/// grain distordu — est le coeur du son MS-20, on la garde donc locale au
/// plugin plutôt que de la présenter comme un filtre générique).
///
/// Coeur : SVF ZDF (TPT, d'après Zavalishin/Simper) — stable même à
/// résonance extrême. Différence essentielle avec le StateVariableFilter
/// générique de la Phase 2 : les états des intégrateurs sont SATURÉS
/// (tanh) à chaque échantillon. Pour un petit signal, tanh(x) ~= x, donc
/// l'accord et la réponse restent corrects ; mais quand la résonance pousse
/// le filtre vers l'auto-oscillation, la saturation BORNE l'amplitude au
/// lieu de diverger — c'est ce qui produit le cri caractéristique de la
/// MS-20 sans faire exploser le calcul (propriété vérifiée par test :
/// ms20_extreme_resonance_stays_bounded).
///
/// Approximation assumée (section 27) : le circuit Sallen-Key réel (et sa
/// distorsion de diodes en contre-réaction) est modélisé, pas mesuré ; la
/// saturation par tanh sur les états approxime le clipping du circuit.
class MS20Filter {
public:
    enum class Mode { LowPass, HighPass };

    void setSampleRate(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        updateCoefficients();
    }
    void setMode(Mode m) { mode_ = m; }
    void setCutoffHz(float hz) { cutoffHz_ = hz; updateCoefficients(); }

    /// 0 = amorti … 1 = auto-oscillation (bornée par la saturation d'états).
    void setResonance(float r) {
        resonance_ = std::clamp(r, 0.0f, 1.0f);
        // k = 2/Q ; r->1 donne k->0 (résonance maximale). Un plancher évite
        // une résonance strictement infinie tout en autorisant le cri.
        k_ = 2.0f * (1.0f - resonance_) + 0.02f;
        updateCoefficients();
    }
    /// Gain d'entrée (grain MS-20) : sature davantage le filtre.
    void setDrive(float d) { drive_ = std::max(0.1f, d); }

    void reset() { ic1eq_ = ic2eq_ = 0.0f; }

    float process(float input) {
        using vsm::audio::dsp::flushDenormalToZero;
        const float x = std::tanh(input * drive_);
        const float v3 = x - ic2eq_;
        const float v1 = a1_ * ic1eq_ + a2_ * v3;
        const float v2 = ic2eq_ + a2_ * ic1eq_ + a3_ * v3;
        // Saturation d'états -> auto-oscillation bornée (le coeur du modèle).
        ic1eq_ = flushDenormalToZero(std::tanh(2.0f * v1 - ic1eq_));
        ic2eq_ = flushDenormalToZero(std::tanh(2.0f * v2 - ic2eq_));
        return mode_ == Mode::LowPass ? v2 : (x - k_ * v1 - v2);
    }

private:
    void updateCoefficients() {
        const float nyquist = static_cast<float>(sampleRate_) * 0.5f;
        const float fc = std::clamp(cutoffHz_, 10.0f, nyquist * 0.99f);
        g_ = std::tan(static_cast<float>(vsm::audio::dsp::kPi) * fc / static_cast<float>(sampleRate_));
        a1_ = 1.0f / (1.0f + g_ * (g_ + k_));
        a2_ = g_ * a1_;
        a3_ = g_ * a2_;
    }

    double sampleRate_ = 48000.0;
    Mode mode_ = Mode::LowPass;
    float cutoffHz_ = 1000.0f, resonance_ = 0.0f, k_ = 2.0f, drive_ = 1.0f;
    float g_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f;
    float ic1eq_ = 0.0f, ic2eq_ = 0.0f;
};

enum ParamIds : vsm::audio::plugin::ParamId {
    kVco1Level = 0,
    kVco1Shape,        // 0 = triangle, 1 = saw, 2 = pulse
    kVco1PulseWidth,
    kVco2Level,
    kVco2Shape,        // 0 = saw, 1 = pulse, 2 = ring (VCO1 x VCO2)
    kVco2Pitch,        // demi-tons (sélecteur d'octave/intervalle)
    kNoiseLevel,
    kHpfCutoff,
    kHpfResonance,
    kLpfCutoff,
    kLpfResonance,
    kFilterDrive,
    kEgToLpf,          // enveloppe -> coupure passe-bas
    kMgRate,           // Modulation Generator (LFO)
    kMgWaveform,       // 0 = triangle, 1 = saw descendante
    kMgToPitch,
    kMgToLpf,
    kAttack,
    kDecay,
    kSustain,
    kRelease,
    kGlideTime,
    kAnalogCharacter,
    kNumParams
};

/// MS-20-style : monosynthé semi-modulaire. Deux VCO (avec ring mod et
/// bruit), un Modulation Generator (LFO tri/saw), une enveloppe ADSR, et
/// surtout le DOUBLE filtre résonant HPF -> LPF (MS20Filter) qui fait tout
/// le caractère de la machine.
///
/// Traits authentiques : monophonique à priorité dernière note + glide
/// (MonoVoiceAllocator) ; clavier non vélocité-sensible -> VCA piloté par
/// l'enveloppe seule.
///
/// Approximations assumées (section 27) : le patch-bay semi-modulaire du
/// hardware (ESP, entrées/sorties CV en façade) n'est pas exposé — un
/// routage fixe et musicalement utile est câblé, le séquencement venant de
/// l'hôte MIDI (cohérent avec l'architecture DAW). Filtres : voir MS20Filter.
/// Aucune mesure comparative avec une MS-20 matérielle n'a été faite.
class MS20Synth : public vsm::audio::plugin::ISynthPlugin {
public:
    MS20Synth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;

    /// Molette de hauteur, en demi-tons, appliquée aux DEUX oscillateurs :
    /// les bender ensemble est ce qui garde leur désaccord constant, et donc
    /// le battement qui fait le son.
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override;

    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override;

    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;

    const char* machineName() const override { return "MS-20-style Semi-modular"; }
    int activeVoiceCount() const override {
        return (env_.isActive() || voiceAllocator_.hasHeldNotes()) ? 1 : 0;
    }

private:
    std::atomic<float> bendSemitones_{0.0f};
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& ev);
    float renderMg(int waveform) const;
    static vsm::audio::dsp::Waveform vco1Wave(int shape) {
        using vsm::audio::dsp::Waveform;
        switch (shape) { case 1: return Waveform::Saw; case 2: return Waveform::Square; default: return Waveform::Triangle; }
    }
    static vsm::audio::dsp::Waveform vco2Wave(int shape) {
        using vsm::audio::dsp::Waveform;
        return shape == 1 ? Waveform::Square : Waveform::Saw; // 2 (ring) part d'une saw
    }

    static constexpr float kMaxPitchDriftSemitones = 0.07f;
    static constexpr float kMaxCutoffDriftOctaves = 0.15f;
    static constexpr float kEgLpfRangeOctaves = 6.0f;
    static constexpr float kMgPitchRangeSemitones = 7.0f;
    static constexpr float kMgLpfRangeOctaves = 4.0f;

    vsm::audio::dsp::BandLimitedOscillator vco1_, vco2_;
    MS20Filter hpf_, lpf_;
    vsm::audio::dsp::AdsrEnvelope env_;
    vsm::audio::dsp::ParameterSmoother pitchGlide_;
    vsm::audio::dsp::AnalogDrift pitchDrift_, cutoffDrift_;
    vsm::util::DeterministicRng noiseRng_{0x4D533230000000FFULL}; // "MS20"
    vsm::audio::engine::MonoVoiceAllocator voiceAllocator_;

    double mgPhase_ = 0.0, mgIncrement_ = 0.0;

    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
    double sampleRate_ = 48000.0;
};

} // namespace vsm::plugins::ms20
