#include "WaveSequenceSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::wavesequence {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

WaveSequenceSynth::WaveSequenceSynth() {
    parameterList_ = {
        {kStep1, "Step 1 Wave", 0.0f, 4.0f, 0.10f, ""},
        {kStep2, "Step 2 Wave", 0.0f, 4.0f, 0.85f, ""},
        {kStep3, "Step 3 Wave", 0.0f, 4.0f, 1.30f, ""},
        {kStep4, "Step 4 Wave", 0.0f, 4.0f, 1.80f, ""},
        {kStep5, "Step 5 Wave", 0.0f, 4.0f, 2.20f, ""},
        {kStep6, "Step 6 Wave", 0.0f, 4.0f, 2.70f, ""},
        {kStep7, "Step 7 Wave", 0.0f, 4.0f, 3.10f, ""},
        {kStep8, "Step 8 Wave", 0.0f, 4.0f, 3.60f, ""},
        {kStepTime, "Step Time", 10.0f, 2000.0f, 200.0f, "ms"},
        {kCrossfade, "Crossfade", 0.0f, 1.0f, 0.3f, ""},
        {kLoopStart, "Loop Start", 1.0f, 8.0f, 1.0f, ""},
        {kKeyRestart, "Key Restart", 0.0f, 1.0f, 1.0f, ""},
        {kCutoff, "Filter Cutoff", 20.0f, 18000.0f, 6000.0f, "Hz"},
        {kResonance, "Filter Resonance", 0.0f, 1.0f, 0.2f, ""},
        {kAttack, "Attack", 0.001f, 4.0f, 0.02f, "s"},
        {kDecay, "Decay", 0.001f, 8.0f, 0.3f, "s"},
        {kSustain, "Sustain", 0.0f, 1.0f, 0.8f, ""},
        {kRelease, "Release", 0.001f, 8.0f, 0.4f, "s"},
        {kVelocitySensitivity, "Velocity Sensitivity", 0.0f, 1.0f, 0.6f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void WaveSequenceSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    voiceManager_.forEachVoice([&](WaveSequenceVoice& voice) { voice.prepare(sampleRate_); });
    horloge_ = 0;
}

bool WaveSequenceSynth::handleControlEvent(const MidiControlEvent& event) {
    // C'EST UN SYNTHÉ : la molette de hauteur est honorée. Le reste, non.
    if (event.kind == MidiControlEvent::Kind::PitchBend) {
        bend_.store(event.value, std::memory_order_relaxed);
        return true;
    }
    return false;
}

void WaveSequenceSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0) {
        if (auto* voix = voiceManager_.noteOn(event.channel, event.note, event.velocity))
            voix->setDepart(horloge_);
    } else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void WaveSequenceSynth::process(const MidiNoteEvent* events, int numEvents,
                                float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;
    const WaveTableBank& bank = WaveTableBank::shared();
    WaveSequenceVoice::Params p;
    for (int i = 0; i < WaveSequenceVoice::kSteps; ++i)
        p.waves[static_cast<size_t>(i)] = params_[static_cast<size_t>(kStep1 + i)].load(std::memory_order_relaxed);
    p.stepSeconds = params_[kStepTime].load(std::memory_order_relaxed) * 0.001f;
    p.crossfade = params_[kCrossfade].load(std::memory_order_relaxed);
    p.loopStart = static_cast<int>(std::lround(params_[kLoopStart].load(std::memory_order_relaxed))) - 1;
    p.keyRestart = params_[kKeyRestart].load(std::memory_order_relaxed) >= 0.5f;
    p.cutoffHz = params_[kCutoff].load(std::memory_order_relaxed);
    p.resonance = params_[kResonance].load(std::memory_order_relaxed);
    p.adsr.attackSeconds = params_[kAttack].load(std::memory_order_relaxed);
    p.adsr.decaySeconds = params_[kDecay].load(std::memory_order_relaxed);
    p.adsr.sustainLevel = params_[kSustain].load(std::memory_order_relaxed);
    p.adsr.releaseSeconds = params_[kRelease].load(std::memory_order_relaxed);
    p.velocitySensitivity = params_[kVelocitySensitivity].load(std::memory_order_relaxed);
    p.bendSemitones = bend_.load(std::memory_order_relaxed);
    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    constexpr float kVoiceGain = 0.35f;
    voiceManager_.forEachVoice([&](WaveSequenceVoice& voice) { voice.setBend(p.bendSemitones); });

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);
        float somme = 0.0f;
        voiceManager_.forEachVoice([&](WaveSequenceVoice& voice) { somme += voice.render(bank, p, horloge_); });
        ++horloge_;
        const float out = somme * kVoiceGain * outputLevel;
        outputL[i] = out;
        outputR[i] = out;
    }
}

void WaveSequenceSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float WaveSequenceSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState WaveSequenceSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.wavesequence";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void WaveSequenceSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.wavesequence", "Wave Sequence (le timbre est une séquence)", WaveSequenceSynth);

} // namespace vsm::plugins::wavesequence
