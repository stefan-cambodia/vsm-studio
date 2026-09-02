#include "GlassSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::glass {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

GlassSynth::GlassSynth() {
    parameterList_ = {
        {kPressure, "Finger Pressure", 0.0f, 1.0f, 0.55f, ""},
        {kSpeed, "Rim Speed", 0.0f, 1.0f, 0.5f, ""},
        {kBrightness, "Brightness", 0.0f, 1.0f, 0.35f, ""},
        {kRing, "Ring Time", 0.5f, 20.0f, 6.0f, "s"},
        {kCutoff, "Filter Cutoff", 500.0f, 16000.0f, 12000.0f, "Hz"},
        {kResonance, "Filter Resonance", 0.0f, 0.95f, 0.05f, ""},
        {kVelocitySensitivity, "Velocity Sensitivity", 0.0f, 1.0f, 0.3f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void GlassSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t graine = 0x474C4153ULL;
    voiceManager_.forEachVoice([&](GlassVoice& voice) { voice.prepare(sampleRate, graine++); });
    filtre_.setSampleRate(sampleRate);
    filtre_.reset();
}

void GlassSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void GlassSynth::process(const MidiNoteEvent* events, int numEvents,
                         float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    GlassVoice::Params p;
    const float canal = pressionDeCanal_.load(std::memory_order_relaxed);
    p.pressure = canal >= 0.0f ? canal : params_[kPressure].load(std::memory_order_relaxed);
    p.speed = params_[kSpeed].load(std::memory_order_relaxed);
    p.brightness = params_[kBrightness].load(std::memory_order_relaxed);
    p.ring = params_[kRing].load(std::memory_order_relaxed);
    p.velocitySensitivity = params_[kVelocitySensitivity].load(std::memory_order_relaxed);
    p.bendSemitones = bendSemitones_.load(std::memory_order_relaxed);

    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    filtre_.setCutoffHz(params_[kCutoff].load(std::memory_order_relaxed));
    filtre_.setResonance(params_[kResonance].load(std::memory_order_relaxed));

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](GlassVoice& voice) { sum += voice.render(p); });
        sum = filtre_.process(sum * outputLevel);
        outputL[i] = sum;
        outputR[i] = sum;
    }
}

void GlassSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float GlassSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState GlassSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.glass";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void GlassSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.glass", "Glass (le verre frotté)", GlassSynth);

} // namespace vsm::plugins::glass
