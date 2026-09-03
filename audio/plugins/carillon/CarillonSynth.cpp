#include "CarillonSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::carillon {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

CarillonSynth::CarillonSynth() {
    parameterList_ = {
        {kTierce, "Tierce", 0.0f, 1.0f, 0.0f, ""},
        {kHumDecay, "Hum Decay", 0.5f, 40.0f, 12.0f, "s"},
        {kDecayTilt, "Decay Tilt", 0.0f, 3.0f, 1.4f, ""},
        {kDoublet, "Doublet", 0.0f, 3.0f, 0.8f, "Hz"},
        {kHardness, "Clapper Hardness", 0.0f, 1.0f, 0.6f, ""},
        {kVelocityToHardness, "Velocity to Hardness", 0.0f, 1.0f, 0.5f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void CarillonSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    voiceManager_.forEachVoice([&](CarillonVoice& voice) { voice.prepare(sampleRate_); });
}

bool CarillonSynth::handleControlEvent(const MidiControlEvent&) {
    // Une cloche n'a ni molette ni pédale : refusé en connaissance de cause.
    return false;
}

void CarillonSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void CarillonSynth::process(const MidiNoteEvent* events, int numEvents,
                            float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;
    CarillonVoice::Params p;
    p.tierce = params_[kTierce].load(std::memory_order_relaxed);
    p.humDecay = params_[kHumDecay].load(std::memory_order_relaxed);
    p.decayTilt = params_[kDecayTilt].load(std::memory_order_relaxed);
    p.doublet = params_[kDoublet].load(std::memory_order_relaxed);
    p.hardness = params_[kHardness].load(std::memory_order_relaxed);
    p.velocityToHardness = params_[kVelocityToHardness].load(std::memory_order_relaxed);
    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    constexpr float kVoiceGain = 0.4f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);
        float somme = 0.0f;
        voiceManager_.forEachVoice([&](CarillonVoice& voice) { somme += voice.render(p); });
        const float out = somme * kVoiceGain * outputLevel;
        outputL[i] = out;
        outputR[i] = out;
    }
}

void CarillonSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float CarillonSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState CarillonSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.carillon";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void CarillonSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.carillon", "Carillon (la cloche et sa tierce mineure)", CarillonSynth);

} // namespace vsm::plugins::carillon
