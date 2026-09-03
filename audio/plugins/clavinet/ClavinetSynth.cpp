#include "ClavinetSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::clavinet {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

ClavinetSynth::ClavinetSynth() {
    parameterList_ = {
        {kTipHardness, "Tip Hardness", 0.0f, 1.0f, 0.7f, ""},
        {kDecay, "String Decay", 0.3f, 8.0f, 3.0f, "s"},
        {kMute, "Mute", 0.0f, 1.0f, 0.0f, ""},
        {kStringBehind, "String Behind", 0.0f, 1.0f, 0.35f, ""},
        {kYarnDamping, "Yarn Damping", 0.02f, 0.4f, 0.08f, "s"},
        {kReleaseClick, "Release Click", 0.0f, 1.0f, 0.5f, ""},
        {kVelocitySensitivity, "Velocity Sensitivity", 0.0f, 1.0f, 0.7f, ""},
        {kPickupMix, "Pickup Mix", 0.0f, 1.0f, 0.5f, ""},
        {kPickupPhase, "Pickup Phase", 0.0f, 1.0f, 0.0f, ""},
        {kCutoff, "Filter Cutoff", 500.0f, 16000.0f, 9000.0f, "Hz"},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void ClavinetSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    uint64_t graine = 0x434C4156ULL;
    voiceManager_.forEachVoice([&](ClavinetVoice& voice) { voice.prepare(sampleRate_, graine++); });
    filtre_.setSampleRate(sampleRate_);
    filtre_.reset();
}

bool ClavinetSynth::handleControlEvent(const MidiControlEvent&) {
    // Une touche tient la corde contre une enclume : pas de molette. Refusé.
    return false;
}

void ClavinetSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void ClavinetSynth::process(const MidiNoteEvent* events, int numEvents,
                            float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;
    ClavinetVoice::Params p;
    p.tipHardness = params_[kTipHardness].load(std::memory_order_relaxed);
    p.decay = params_[kDecay].load(std::memory_order_relaxed);
    p.mute = params_[kMute].load(std::memory_order_relaxed);
    p.stringBehind = params_[kStringBehind].load(std::memory_order_relaxed);
    p.yarnDamping = params_[kYarnDamping].load(std::memory_order_relaxed);
    p.releaseClick = params_[kReleaseClick].load(std::memory_order_relaxed);
    p.velocitySensitivity = params_[kVelocitySensitivity].load(std::memory_order_relaxed);
    p.pickupMix = params_[kPickupMix].load(std::memory_order_relaxed);
    p.pickupDifference = params_[kPickupPhase].load(std::memory_order_relaxed) >= 0.5f;
    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    filtre_.setCutoffHz(params_[kCutoff].load(std::memory_order_relaxed));
    filtre_.setResonance(0.05f);
    // Les peignes des micros mangent le niveau : rattrapé ici, mesuré contre
    // l'e-piano (crête 0,41) — sans ce gain la crête tombait à 0,06.
    constexpr float kVoiceGain = 4.0f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);
        float somme = 0.0f;
        voiceManager_.forEachVoice([&](ClavinetVoice& voice) { somme += voice.render(p); });
        const float out = filtre_.process(somme * kVoiceGain * outputLevel);
        outputL[i] = out;
        outputR[i] = out;
    }
}

void ClavinetSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float ClavinetSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState ClavinetSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.clavinet";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void ClavinetSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.clavinet", "Clavinet (la corde qui sonne entière au relâchement)", ClavinetSynth);

} // namespace vsm::plugins::clavinet
