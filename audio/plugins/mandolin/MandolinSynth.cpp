#include "MandolinSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::mandolin {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

MandolinSynth::MandolinSynth() {
    parameterList_ = {
        {kCourseDetune, "Course Detune", 0.0f, 30.0f, 6.0f, "cents"},
        {kOctavePair, "Octave Pair", 0.0f, 1.0f, 0.0f, ""},
        {kStrumSpread, "Strum Spread", 0.0f, 15.0f, 3.0f, "ms"},
        {kTremoloRate, "Tremolo Rate", 0.0f, 16.0f, 0.0f, "Hz"},
        {kPickPosition, "Pick Position", 0.02f, 0.5f, 0.12f, ""},
        {kPickHardness, "Pick Hardness", 0.0f, 1.0f, 0.85f, ""},
        {kDecay, "String Decay", 0.2f, 8.0f, 2.5f, "s"},
        {kDamping, "String Damping", 0.0f, 1.0f, 0.25f, ""},
        {kVelocitySensitivity, "Velocity Sensitivity", 0.0f, 1.0f, 0.7f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void MandolinSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    uint64_t graine = 0x4D414E44ULL;
    voiceManager_.forEachVoice([&](MandolinVoice& voice) { voice.prepare(sampleRate_, graine++); });
}

bool MandolinSynth::handleControlEvent(const MidiControlEvent&) {
    // Deux cordes frettées ne se tirent pas ensemble : pas de molette. Refusé.
    return false;
}

void MandolinSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void MandolinSynth::process(const MidiNoteEvent* events, int numEvents,
                            float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;
    MandolinVoice::Params p;
    p.courseDetune = params_[kCourseDetune].load(std::memory_order_relaxed);
    p.octavePair = params_[kOctavePair].load(std::memory_order_relaxed);
    p.strumSpread = params_[kStrumSpread].load(std::memory_order_relaxed);
    p.tremoloRate = params_[kTremoloRate].load(std::memory_order_relaxed);
    p.pickPosition = params_[kPickPosition].load(std::memory_order_relaxed);
    p.pickHardness = params_[kPickHardness].load(std::memory_order_relaxed);
    p.decay = params_[kDecay].load(std::memory_order_relaxed);
    p.damping = params_[kDamping].load(std::memory_order_relaxed);
    p.velocitySensitivity = params_[kVelocitySensitivity].load(std::memory_order_relaxed);
    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    const int retardB = static_cast<int>(std::max(0.0f, p.strumSpread) * 0.001f * static_cast<float>(sampleRate_));
    voiceManager_.forEachVoice([&](MandolinVoice& voice) { voice.setStrumSpreadSamples(retardB); });
    constexpr float kVoiceGain = 0.55f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);
        float somme = 0.0f;
        voiceManager_.forEachVoice([&](MandolinVoice& voice) { somme += voice.render(p); });
        const float out = somme * kVoiceGain * outputLevel;
        outputL[i] = out;
        outputR[i] = out;
    }
}

void MandolinSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float MandolinSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState MandolinSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.mandolin";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void MandolinSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.mandolin", "Mandolin (les cordes par deux)", MandolinSynth);

} // namespace vsm::plugins::mandolin
