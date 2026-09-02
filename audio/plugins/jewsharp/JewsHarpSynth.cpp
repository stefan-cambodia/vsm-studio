#include "JewsHarpSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::jewsharp {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

JewsHarpSynth::JewsHarpSynth() {
    // LA LAME EST UN RÉGLAGE, PAS UNE CONSTANTE : les guimbardes se vendent
    // par tonalité, et un joueur en a plusieurs. Mais elle ne suit JAMAIS le
    // clavier — c'est le musicien qui change d'instrument, pas l'instrument
    // qui change de note.
    parameterList_ = {
        {kReedHz, "Reed Pitch", 40.0f, 260.0f, 82.0f, "Hz"},
        {kFormantLow, "Formant Low", 150.0f, 900.0f, 320.0f, "Hz"},
        {kFormantHigh, "Formant High", 900.0f, 6000.0f, 3000.0f, "Hz"},
        {kFormantQ, "Formant Q", 0.0f, 0.95f, 0.9f, ""},
        {kTwang, "Twang", 0.0f, 1.0f, 0.6f, ""},
        {kAttack, "Attack", 0.001f, 1.0f, 0.002f, "s"},
        {kDecay, "Decay", 0.05f, 8.0f, 1.6f, "s"},
        {kSustain, "Sustain", 0.0f, 1.0f, 0.0f, ""},
        {kRelease, "Release", 0.005f, 4.0f, 0.25f, "s"},
        {kVelocitySensitivity, "Velocity Sensitivity", 0.0f, 1.0f, 0.5f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void JewsHarpSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t graine = 0x4A455753ULL;
    voiceManager_.forEachVoice([&](JewsHarpVoice& voice) { voice.prepare(sampleRate, graine++); });
}

void JewsHarpSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void JewsHarpSynth::process(const MidiNoteEvent* events, int numEvents,
                            float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    JewsHarpVoice::Params p;
    p.reedHz = params_[kReedHz].load(std::memory_order_relaxed);
    p.formantLow = params_[kFormantLow].load(std::memory_order_relaxed);
    p.formantHigh = params_[kFormantHigh].load(std::memory_order_relaxed);
    p.formantQ = params_[kFormantQ].load(std::memory_order_relaxed);
    p.twang = params_[kTwang].load(std::memory_order_relaxed);
    p.velocitySensitivity = params_[kVelocitySensitivity].load(std::memory_order_relaxed);

    const AdsrSettings env{
        params_[kAttack].load(std::memory_order_relaxed),
        params_[kDecay].load(std::memory_order_relaxed),
        params_[kSustain].load(std::memory_order_relaxed),
        params_[kRelease].load(std::memory_order_relaxed)};
    voiceManager_.forEachVoice([&](JewsHarpVoice& voice) { voice.setEnvelope(env); });

    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](JewsHarpVoice& voice) { sum += voice.render(p); });
        sum *= outputLevel;
        outputL[i] = sum;
        outputR[i] = sum;
    }
}

void JewsHarpSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float JewsHarpSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState JewsHarpSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.jewsharp";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void JewsHarpSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.jewsharp", "Jew's Harp (la note qui ne bouge pas)", JewsHarpSynth);

} // namespace vsm::plugins::jewsharp
