#include "MembraneSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::membrane {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

MembraneSynth::MembraneSynth() {
    // DÉFAUT À MI-CHARGE : ni la timbale pure (0) ni le tabla pleinement
    // accordé (1), mais le régime où l'on entend que l'objet EST accordable —
    // celui qui rend le réglage `Loading` intelligible à qui le découvre.
    parameterList_ = {
        {kLoading, "Loading", 0.0f, 1.0f, 0.55f, ""},
        {kStrikeRadius, "Strike Radius", 0.0f, 0.95f, 0.6f, ""},
        {kHardness, "Mallet Hardness", 0.0f, 1.0f, 0.5f, ""},
        {kDecay, "Decay", 0.05f, 10.0f, 1.2f, "s"},
        {kDecayTilt, "Decay Tilt", 0.0f, 3.0f, 1.0f, ""},
        {kModeCount, "Modes", 1.0f, 12.0f, 10.0f, ""},
        // UN SEUL RÉGLAGE DE VÉLOCITÉ, et c'est délibéré. Un seizième
        // paramètre « Velocity Sensitivity » avait été écrit puis retiré :
        // `process` ne le lisait nulle part, la vélocité agissant déjà sur le
        // niveau (dans `frapper`) et sur la dureté du maillet (ci-dessous). Un
        // réglage qui ne fait rien est pire qu'un réglage absent — il ment au
        // musicien, et il coûte une dimension à la recherche pour rien.
        {kVelocityToHardness, "Velocity to Hardness", 0.0f, 1.0f, 0.3f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void MembraneSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t graine = 0x4D454D42ULL;
    voiceManager_.forEachVoice([&](MembraneVoice& voice) { voice.prepare(sampleRate, graine++); });
}

void MembraneSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void MembraneSynth::process(const MidiNoteEvent* events, int numEvents,
                            float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    MembraneVoice::Params p;
    p.loading = params_[kLoading].load(std::memory_order_relaxed);
    p.strikeRadius = params_[kStrikeRadius].load(std::memory_order_relaxed);
    p.hardness = params_[kHardness].load(std::memory_order_relaxed);
    p.decay = params_[kDecay].load(std::memory_order_relaxed);
    p.decayTilt = params_[kDecayTilt].load(std::memory_order_relaxed);
    p.modes = params_[kModeCount].load(std::memory_order_relaxed);
    p.velocityToHardness = params_[kVelocityToHardness].load(std::memory_order_relaxed);
    p.bendSemitones = bendSemitones_.load(std::memory_order_relaxed);

    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    constexpr float kVoiceGain = 0.5f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](MembraneVoice& voice) { sum += voice.render(p); });
        sum *= kVoiceGain * outputLevel;
        outputL[i] = sum;
        outputR[i] = sum;
    }
}

void MembraneSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float MembraneSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState MembraneSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.membrane";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void MembraneSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.membrane", "Membrane (la peau tendue)", MembraneSynth);

} // namespace vsm::plugins::membrane
