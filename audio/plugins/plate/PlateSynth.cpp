#include "PlateSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::plate {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

PlateSynth::PlateSynth() {
    parameterList_ = {
        {kCoupling, "Coupling", 0.0f, 1.0f, 0.6f, ""},
        {kDecay, "Decay", 0.2f, 20.0f, 6.0f, "s"},
        {kDecayTilt, "Decay Tilt", 0.0f, 3.0f, 0.6f, ""},
        {kStrikeHardness, "Mallet Hardness", 0.0f, 1.0f, 0.4f, ""},
        {kCutoff, "Filter Cutoff", 500.0f, 16000.0f, 14000.0f, "Hz"},
        {kVelocitySensitivity, "Velocity Sensitivity", 0.0f, 1.0f, 0.6f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void PlateSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t graine = 0x504C4154ULL;
    voiceManager_.forEachVoice([&](PlateVoice& voice) { voice.prepare(sampleRate, graine++); });
}

void PlateSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void PlateSynth::process(const MidiNoteEvent* events, int numEvents,
                         float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    PlateVoice::Params p;
    p.coupling = params_[kCoupling].load(std::memory_order_relaxed);
    p.decay = params_[kDecay].load(std::memory_order_relaxed);
    p.decayTilt = params_[kDecayTilt].load(std::memory_order_relaxed);
    p.strikeHardness = params_[kStrikeHardness].load(std::memory_order_relaxed);
    p.cutoff = params_[kCutoff].load(std::memory_order_relaxed);
    p.velocitySensitivity = params_[kVelocitySensitivity].load(std::memory_order_relaxed);
    p.bendSemitones = bendSemitones_.load(std::memory_order_relaxed);

    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    constexpr float kVoiceGain = 0.45f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](PlateVoice& voice) { sum += voice.render(p); });
        sum *= kVoiceGain * outputLevel;
        outputL[i] = sum;
        outputR[i] = sum;
    }
}

void PlateSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float PlateSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState PlateSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.plate";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void PlateSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.plate", "Plate (le gong qui s'éclaircit)", PlateSynth);

} // namespace vsm::plugins::plate
