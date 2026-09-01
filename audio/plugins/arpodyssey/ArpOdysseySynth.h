#pragma once
#include "vsm/audio/dsp/AnalogDrift.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/dsp/LadderFilterZDF.h"
#include "vsm/audio/dsp/Oscillator.h"
#include "vsm/audio/dsp/ParameterSmoother.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace vsm::plugins::arpodyssey {

enum ParamIds : vsm::audio::plugin::ParamId {
    kVco1Level = 0,
    kVco1Shape,         // 0 = saw, 1 = pulse
    kVco1PulseWidth,
    kVco2Level,
    kVco2Shape,         // 0 = saw, 1 = pulse
    kVco2PulseWidth,
    kVco2Detune,        // demi-tons
    kRingModLevel,      // niveau du modulateur en anneau (VCO1 x VCO2)
    kNoiseLevel,
    kSync,              // 0/1 : hard-sync VCO-2 -> VCO-1
    kHpfCutoff,         // passe-haut non résonant (spécificité Odyssey)
    kFilterCutoff,
    kFilterResonance,
    kFilterEnvAmount,
    kFilterKeyTrack,
    kLfoRate,
    kLfoWaveform,       // 0 tri, 1 carré, 2 sample & hold
    kLfoToPitch,
    kLfoToFilter,
    kAttack,
    kDecay,
    kSustain,
    kRelease,
    kGlideTime,
    kAnalogCharacter,
    kNumParams
};

/// Suivi des notes tenues pour l'allocation DUOPHONIQUE de l'Odyssey. Une
/// seule chaîne audio (un filtre, un VCA, une enveloppe), mais les DEUX
/// oscillateurs peuvent suivre DEUX touches différentes : VCO-1 sur la note
/// la plus GRAVE tenue, VCO-2 sur la plus AIGUË (comportement duophonique
/// classique ARP). Tenir une seule touche -> les deux VCO à l'unisson.
///
/// Capacité fixe (128 numéros de note MIDI) : aucune allocation dans
/// noteOn/noteOff, donc sûr sur le thread audio.
class DuophonicKeyState {
public:
    void reset() { present_.fill(false); count_ = 0; }

    /// @return true si une NOUVELLE note vient d'être ajoutée alors qu'aucune
    /// n'était tenue (front de gate montant -> l'appelant retrigge l'enveloppe).
    bool noteOn(uint8_t note) {
        const bool wasSilent = (count_ == 0);
        if (!present_[note]) { present_[note] = true; ++count_; }
        return wasSilent;
    }
    void noteOff(uint8_t note) {
        if (present_[note]) { present_[note] = false; --count_; }
    }

    bool anyHeld() const { return count_ > 0; }
    int heldCount() const { return count_; }

    uint8_t lowest() const {
        for (int n = 0; n < 128; ++n) if (present_[static_cast<size_t>(n)]) return static_cast<uint8_t>(n);
        return 60;
    }
    uint8_t highest() const {
        for (int n = 127; n >= 0; --n) if (present_[static_cast<size_t>(n)]) return static_cast<uint8_t>(n);
        return 60;
    }

private:
    std::array<bool, 128> present_{};
    int count_ = 0;
};

/// ARP-Odyssey-style : synthé DUOPHONIQUE. Deux VCO (saw/pulse) pouvant
/// suivre deux touches, modulateur en anneau (VCO1 x VCO2), bruit, hard-sync,
/// passe-haut non résonant + VCF passe-bas résonant, LFO (tri/carré/sample &
/// hold) routable vers le pitch et le filtre, enveloppe ADSR partagée.
///
/// Traits authentiques : allocation duophonique (grave -> VCO1, aigu ->
/// VCO2) ; clavier non vélocité-sensible -> VCA piloté par l'enveloppe seule.
///
/// Approximations assumées (section 27) : le VCF réel (4023 2 pôles des
/// premières séries, puis 4035/4075) est modélisé par le ladder ZDF 4 pôles
/// déjà présent -- pente et résonance proches, topologie différente. Le
/// modulateur en anneau est une multiplication idéale des deux VCO (pas la
/// non-linéarité exacte du circuit à diodes). Hard-sync sans correction BLEP.
/// Aucune mesure comparative avec un Odyssey matériel n'a été faite.
class ArpOdysseySynth : public vsm::audio::plugin::ISynthPlugin {
public:
    ArpOdysseySynth();

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

    const char* machineName() const override { return "ARP-Odyssey-style Duophonic"; }
    int activeVoiceCount() const override {
        if (!env_.isActive() && !keys_.anyHeld()) return 0;
        return keys_.heldCount() >= 2 ? 2 : 1; // duophonique : au plus 2
    }

private:
    std::atomic<float> bendSemitones_{0.0f};
    // Molette de modulation (CC 1), 0..1 : elle DOSE le vibrato au LFO.
    std::atomic<float> modWheel_{0.0f};
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& ev);
    float renderLfo(int waveform) const;
    static vsm::audio::dsp::Waveform shapeToWave(int shape) {
        return shape == 1 ? vsm::audio::dsp::Waveform::Square : vsm::audio::dsp::Waveform::Saw;
    }

    static constexpr float kMaxPitchDriftSemitones = 0.06f;
    static constexpr float kMaxCutoffDriftOctaves = 0.15f;
    static constexpr float kFilterEnvRangeOctaves = 6.0f;
    static constexpr float kLfoPitchRangeSemitones = 7.0f;
    static constexpr float kLfoFilterRangeOctaves = 4.0f;
    // Vibrato ajouté par la molette de MODULATION à fond : une demi-note,
    // le geste classique du panneau (D0.5 : les contrôleurs qui ont un sens).
    static constexpr float kWheelVibratoSemitones = 0.5f;

    vsm::audio::dsp::BandLimitedOscillator vco1_, vco2_;
    vsm::audio::dsp::StateVariableFilter hpf_;
    vsm::audio::dsp::LadderFilterZDF lpf_;
    vsm::audio::dsp::AdsrEnvelope env_;
    vsm::audio::dsp::ParameterSmoother glide1_, glide2_;
    vsm::audio::dsp::AnalogDrift pitchDrift1_, pitchDrift2_, cutoffDrift_;
    vsm::util::DeterministicRng noiseRng_{0x4152504F44595353ULL};      // "ARPODYSS"
    vsm::util::DeterministicRng lfoRng_{0x4152504F44590001ULL};
    DuophonicKeyState keys_;

    double lfoPhase_ = 0.0;
    double lfoIncrement_ = 0.0;
    float lfoRandom_ = 0.0f;
    float syncPhase_ = 0.0f;

    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
    double sampleRate_ = 48000.0;
};

} // namespace vsm::plugins::arpodyssey
