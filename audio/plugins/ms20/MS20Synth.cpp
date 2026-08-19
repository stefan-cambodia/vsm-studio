#include "MS20Synth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>

namespace vsm::plugins::ms20 {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

MS20Synth::MS20Synth() {
    parameterList_ = {
        {kVco1Level, "VCO-1 Level", 0.0f, 1.0f, 0.8f, ""},
        {kVco1Shape, "VCO-1 Shape", 0.0f, 2.0f, 1.0f, ""},
        {kVco1PulseWidth, "VCO-1 Pulse Width", 0.05f, 0.95f, 0.5f, ""},
        {kVco2Level, "VCO-2 Level", 0.0f, 1.0f, 0.5f, ""},
        {kVco2Shape, "VCO-2 Shape", 0.0f, 2.0f, 0.0f, ""},
        {kVco2Pitch, "VCO-2 Pitch", -24.0f, 24.0f, -12.0f, "st"},
        {kNoiseLevel, "Noise Level", 0.0f, 1.0f, 0.0f, ""},
        {kHpfCutoff, "HPF Cutoff", 20.0f, 12000.0f, 20.0f, "Hz"},
        {kHpfResonance, "HPF Resonance", 0.0f, 1.0f, 0.0f, ""},
        {kLpfCutoff, "LPF Cutoff", 20.0f, 16000.0f, 1400.0f, "Hz"},
        {kLpfResonance, "LPF Resonance", 0.0f, 1.0f, 0.3f, ""},
        {kFilterDrive, "Filter Drive", 0.5f, 4.0f, 1.0f, ""},
        {kEgToLpf, "EG to LPF", -1.0f, 1.0f, 0.5f, ""},
        {kMgRate, "MG Rate", 0.05f, 30.0f, 5.0f, "Hz"},
        {kMgWaveform, "MG Waveform", 0.0f, 1.0f, 0.0f, ""},
        {kMgToPitch, "MG to Pitch", 0.0f, 1.0f, 0.0f, ""},
        {kMgToLpf, "MG to LPF", 0.0f, 1.0f, 0.0f, ""},
        {kAttack, "Amp Attack", 0.001f, 4.0f, 0.005f, "s"},
        {kDecay, "Amp Decay", 0.001f, 8.0f, 0.3f, "s"},
        {kSustain, "Amp Sustain", 0.0f, 1.0f, 0.8f, ""},
        {kRelease, "Amp Release", 0.001f, 8.0f, 0.2f, "s"},
        {kGlideTime, "Glide Time", 0.0f, 3.0f, 0.0f, "s"},
        {kAnalogCharacter, "Analog Character", 0.0f, 1.0f, 0.3f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void MS20Synth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    vco1_.setSampleRate(sampleRate);
    vco2_.setSampleRate(sampleRate);
    hpf_.setSampleRate(sampleRate); hpf_.setMode(MS20Filter::Mode::HighPass);
    lpf_.setSampleRate(sampleRate); lpf_.setMode(MS20Filter::Mode::LowPass);
    env_.setSampleRate(sampleRate);
    pitchGlide_.setSampleRate(sampleRate); pitchGlide_.reset(60.0f);
    pitchDrift_.setSampleRate(sampleRate); pitchDrift_.setSeed(0x4D53320000000001ULL); pitchDrift_.setRateHz(0.15f);
    cutoffDrift_.setSampleRate(sampleRate); cutoffDrift_.setSeed(0x4D53320000000002ULL); cutoffDrift_.setRateHz(0.1f);
    mgPhase_ = 0.0;
    voiceAllocator_.reset();
}

void MS20Synth::applyNoteEvent(const MidiNoteEvent& ev) {
    using vsm::audio::engine::MonoVoiceAllocator;
    if (ev.kind == MidiNoteEvent::Kind::NoteOn && ev.velocity > 0) {
        const bool wasIdle = !voiceAllocator_.hasHeldNotes();
        MonoVoiceAllocator::Result r = voiceAllocator_.noteOn(ev.note, ev.velocity);
        pitchGlide_.setTarget(static_cast<float>(r.note));
        if (wasIdle) pitchGlide_.reset(static_cast<float>(r.note));
        if (r.retrigger) env_.noteOn();
    } else {
        MonoVoiceAllocator::Result r = voiceAllocator_.noteOff(ev.note);
        if (r.shouldPlay) {
            pitchGlide_.setTarget(static_cast<float>(r.note));
            if (r.retrigger) env_.noteOn();
        } else {
            env_.noteOff();
        }
    }
}

float MS20Synth::renderMg(int waveform) const {
    const float phase = static_cast<float>(mgPhase_);
    if (waveform == 1) return 1.0f - 2.0f * phase;             // saw descendante
    return 4.0f * std::abs(phase - 0.5f) - 1.0f;              // triangle
}

void MS20Synth::process(const MidiNoteEvent* events, int numEvents,
                        float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    const float vco1Level = params_[kVco1Level].load(std::memory_order_relaxed);
    const int vco1Shape = static_cast<int>(std::lround(params_[kVco1Shape].load(std::memory_order_relaxed)));
    const float vco1Pw = params_[kVco1PulseWidth].load(std::memory_order_relaxed);
    const float vco2Level = params_[kVco2Level].load(std::memory_order_relaxed);
    const int vco2Shape = static_cast<int>(std::lround(params_[kVco2Shape].load(std::memory_order_relaxed)));
    const float vco2Pitch = params_[kVco2Pitch].load(std::memory_order_relaxed);
    const float noiseLevel = params_[kNoiseLevel].load(std::memory_order_relaxed);
    const float hpfCutoff = params_[kHpfCutoff].load(std::memory_order_relaxed);
    const float hpfRes = params_[kHpfResonance].load(std::memory_order_relaxed);
    const float lpfCutoffBase = params_[kLpfCutoff].load(std::memory_order_relaxed);
    const float lpfRes = params_[kLpfResonance].load(std::memory_order_relaxed);
    const float filterDrive = params_[kFilterDrive].load(std::memory_order_relaxed);
    const float egToLpf = params_[kEgToLpf].load(std::memory_order_relaxed);
    const float mgRate = params_[kMgRate].load(std::memory_order_relaxed);
    const int mgWave = static_cast<int>(std::lround(params_[kMgWaveform].load(std::memory_order_relaxed)));
    const float mgToPitch = params_[kMgToPitch].load(std::memory_order_relaxed);
    const float mgToLpf = params_[kMgToLpf].load(std::memory_order_relaxed);
    const float analogCharacter = params_[kAnalogCharacter].load(std::memory_order_relaxed);

    env_.setSettings(AdsrSettings{
        params_[kAttack].load(std::memory_order_relaxed),
        params_[kDecay].load(std::memory_order_relaxed),
        params_[kSustain].load(std::memory_order_relaxed),
        params_[kRelease].load(std::memory_order_relaxed),
    });
    pitchGlide_.setSmoothingTimeMs(params_[kGlideTime].load(std::memory_order_relaxed) * 1000.0f);
    pitchDrift_.setAmount(analogCharacter);
    cutoffDrift_.setAmount(analogCharacter);

    // Réglages de filtre constants par bloc.
    hpf_.setCutoffHz(hpfCutoff);
    hpf_.setResonance(hpfRes);
    hpf_.setDrive(filterDrive);
    lpf_.setResonance(lpfRes);
    lpf_.setDrive(filterDrive);

    mgIncrement_ = mgRate / sampleRate_;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i) {
            applyNoteEvent(events[eventIndex]);
            ++eventIndex;
        }

        mgPhase_ += mgIncrement_;
        if (mgPhase_ >= 1.0) mgPhase_ -= 1.0;
        const float mg = renderMg(mgWave);

        const float noteNumber = pitchGlide_.nextValue();
        const float pitchDriftSemis = pitchDrift_.nextValue() * kMaxPitchDriftSemitones;
        const float vibratoSemis = mg * mgToPitch * kMgPitchRangeSemitones;
        const float base = noteNumber + pitchDriftSemis + vibratoSemis;

        const float hz1 = 440.0f * std::exp2f((base - 69.0f) / 12.0f);
        const float hz2 = 440.0f * std::exp2f((base + vco2Pitch - 69.0f) / 12.0f);

        vco1_.setFrequency(hz1);
        vco1_.setWaveform(vco1Wave(vco1Shape));
        if (vco1Shape == 2) vco1_.setPulseWidth(vco1Pw);
        const float raw1 = vco1_.nextSample();

        vco2_.setFrequency(hz2);
        vco2_.setWaveform(vco2Wave(vco2Shape));
        if (vco2Shape == 1) vco2_.setPulseWidth(0.5f);
        float raw2 = vco2_.nextSample();
        if (vco2Shape == 2) raw2 = raw1 * raw2; // ring mod

        const float noise = noiseRng_.nextBipolar();
        float mixed = raw1 * vco1Level + raw2 * vco2Level + noise * noiseLevel;
        mixed *= 0.5f;

        const float envLevel = env_.nextSample();

        // Passe-haut résonant, puis passe-bas résonant (le double filtre MS-20).
        float sig = hpf_.process(mixed);

        const float cutoffDriftOct = cutoffDrift_.nextValue() * kMaxCutoffDriftOctaves;
        const float egOct = egToLpf * envLevel * kEgLpfRangeOctaves;
        const float mgOct = mg * mgToLpf * kMgLpfRangeOctaves;
        const float lpfCutoff = lpfCutoffBase * std::exp2f(egOct + mgOct + cutoffDriftOct);
        lpf_.setCutoffHz(lpfCutoff);
        sig = lpf_.process(sig);

        const float sample = sig * envLevel; // pas de vélocité (clavier MS-20)
        outputL[i] = sample;
        outputR[i] = sample;
    }
}

void MS20Synth::setParameter(ParamId id, float value) {
    if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}
float MS20Synth::getParameter(ParamId id) const {
    return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}
const ParameterList& MS20Synth::parameterList() const { return parameterList_; }

PresetState MS20Synth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.ms20";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}
void MS20Synth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.ms20", "MS-20-style Semi-modular", MS20Synth);

} // namespace vsm::plugins::ms20
