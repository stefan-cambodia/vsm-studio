#include "HarpsichordSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::harpsichord {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

HarpsichordSynth::HarpsichordSynth() {
    parameterList_ = {
        {kRegister8, "Register 8'", 0.0f, 1.0f, 1.0f, ""},
        {kRegister4, "Register 4'", 0.0f, 1.0f, 0.35f, ""},
        {kLuteStop, "Lute Stop", 0.0f, 1.0f, 0.0f, ""},
        {kPluckPosition, "Pluck Position", 0.02f, 0.5f, 0.09f, ""},
        {kReleasePluck, "Release Pluck", 0.0f, 1.0f, 0.5f, ""},
        {kDecay, "String Decay", 0.5f, 8.0f, 2.5f, "s"},
        {kDamping, "String Damping", 0.0f, 1.0f, 0.25f, ""},
        {kDamperTime, "Damper Time", 0.01f, 0.2f, 0.04f, "s"},
        {kCutoff, "Filter Cutoff", 500.0f, 16000.0f, 11000.0f, "Hz"},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void HarpsichordSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t graine = 0x48415250ULL;
    voiceManager_.forEachVoice([&](HarpsichordVoice& voice) { voice.prepare(sampleRate, graine++); });
    filtre_.setSampleRate(sampleRate);
    filtre_.reset();
}

bool HarpsichordSynth::handleControlEvent(const MidiControlEvent& /*event*/) {
    // UN CLAVECIN N'A NI MOLETTE NI PRESSION : la corde est pincée puis
    // laissée à elle-même, et rien sur l'instrument ne la retouche. Le refus
    // est en connaissance de cause, et le moteur le compte.
    return false;
}

void HarpsichordSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void HarpsichordSynth::process(const MidiNoteEvent* events, int numEvents,
                               float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    HarpsichordVoice::Params p;
    p.decay = params_[kDecay].load(std::memory_order_relaxed);
    p.damping = params_[kDamping].load(std::memory_order_relaxed);
    p.pluckPosition = params_[kPluckPosition].load(std::memory_order_relaxed);
    p.register8 = params_[kRegister8].load(std::memory_order_relaxed);
    p.register4 = params_[kRegister4].load(std::memory_order_relaxed);
    p.releasePluck = params_[kReleasePluck].load(std::memory_order_relaxed);
    p.damperTime = params_[kDamperTime].load(std::memory_order_relaxed);
    p.luteStop = params_[kLuteStop].load(std::memory_order_relaxed);

    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    filtre_.setCutoffHz(params_[kCutoff].load(std::memory_order_relaxed));
    filtre_.setResonance(0.05f);
    constexpr float kVoiceGain = 0.45f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](HarpsichordVoice& voice) { sum += voice.render(p); });
        sum = filtre_.process(sum * kVoiceGain * outputLevel);
        outputL[i] = sum;
        outputR[i] = sum;
    }
}

void HarpsichordSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float HarpsichordSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState HarpsichordSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.harpsichord";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void HarpsichordSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.harpsichord", "Harpsichord (le clavecin)", HarpsichordSynth);

} // namespace vsm::plugins::harpsichord
