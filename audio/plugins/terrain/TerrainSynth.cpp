#include "TerrainSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::terrain {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

TerrainSynth::TerrainSynth() {
    parameterList_ = {
        {kRadius, "Orbit Radius", 0.05f, 1.0f, 0.55f, ""},
        {kRoughness, "Roughness", 0.0f, 1.0f, 0.5f, ""},
        {kEllipse, "Orbit Ellipse", 0.0f, 1.0f, 0.0f, ""},
        {kDriftRate, "Drift Rate", 0.0f, 2.0f, 0.15f, "Hz"},
        {kCutoff, "Filter Cutoff", 500.0f, 16000.0f, 10000.0f, "Hz"},
        {kResonance, "Filter Resonance", 0.0f, 0.95f, 0.08f, ""},
        {kAttack, "Attack", 0.001f, 4.0f, 0.01f, "s"},
        {kDecay, "Decay", 0.005f, 8.0f, 0.6f, "s"},
        {kSustain, "Sustain", 0.0f, 1.0f, 0.8f, ""},
        {kRelease, "Release", 0.005f, 8.0f, 0.3f, "s"},
        {kVelocityToRadius, "Velocity to Radius", 0.0f, 1.0f, 0.35f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void TerrainSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t graine = 0x54455252ULL;
    voiceManager_.forEachVoice([&](TerrainVoice& voice) { voice.prepare(sampleRate, graine++); });
}

void TerrainSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void TerrainSynth::process(const MidiNoteEvent* events, int numEvents,
                           float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    TerrainVoice::Params p;
    // La molette AJOUTE au réglage sans le remplacer : le musicien qui n'en a
    // pas garde exactement le son de son preset.
    p.radius = std::clamp(params_[kRadius].load(std::memory_order_relaxed)
                          + 0.45f * molette_.load(std::memory_order_relaxed), 0.05f, 1.0f);
    p.roughness = params_[kRoughness].load(std::memory_order_relaxed);
    p.ellipse = params_[kEllipse].load(std::memory_order_relaxed);
    p.driftRate = params_[kDriftRate].load(std::memory_order_relaxed);
    p.cutoff = params_[kCutoff].load(std::memory_order_relaxed);
    p.resonance = params_[kResonance].load(std::memory_order_relaxed);
    p.velocityToRadius = params_[kVelocityToRadius].load(std::memory_order_relaxed);
    p.bendSemitones = bendSemitones_.load(std::memory_order_relaxed);

    const AdsrSettings env{
        params_[kAttack].load(std::memory_order_relaxed),
        params_[kDecay].load(std::memory_order_relaxed),
        params_[kSustain].load(std::memory_order_relaxed),
        params_[kRelease].load(std::memory_order_relaxed)};
    voiceManager_.forEachVoice([&](TerrainVoice& voice) { voice.setEnvelope(env); });

    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](TerrainVoice& voice) { sum += voice.render(p); });
        sum *= outputLevel;
        outputL[i] = sum;
        outputR[i] = sum;
    }
}

void TerrainSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float TerrainSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState TerrainSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.terrain";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void TerrainSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.terrain", "Terrain (le chemin fait le timbre)", TerrainSynth);

} // namespace vsm::plugins::terrain
