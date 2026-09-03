#include "BanjoSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::banjo {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

BanjoSynth::BanjoSynth() {
    parameterList_ = {
        {kHeadTension, "Head Tension", 150.0f, 600.0f, 300.0f, "Hz"},
        {kHeadDamping, "Head Damping", 0.0f, 1.0f, 0.35f, ""},
        {kHeadMix, "Head Mix", 0.0f, 1.0f, 0.6f, ""},
        {kPickPosition, "Pick Position", 0.02f, 0.5f, 0.15f, ""},
        {kPickHardness, "Pick Hardness", 0.0f, 1.0f, 0.8f, ""},
        {kDecay, "String Decay", 0.2f, 6.0f, 1.2f, "s"},
        {kDamping, "String Damping", 0.0f, 1.0f, 0.3f, ""},
        {kVelocitySensitivity, "Velocity Sensitivity", 0.0f, 1.0f, 0.6f, ""},
        {kCutoff, "Filter Cutoff", 500.0f, 16000.0f, 12000.0f, "Hz"},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void BanjoSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t graine = 0x42414E4AULL;
    voiceManager_.forEachVoice([&](BanjoVoice& voice) { voice.prepare(sampleRate, graine++); });
    peau_.prepare(sampleRate);
    filtre_.setSampleRate(sampleRate);
    filtre_.reset();
}

bool BanjoSynth::handleControlEvent(const MidiControlEvent& event) {
    // Une corde se tire : le banjo honore la molette, comme `vsm.string`.
    if (event.kind == MidiControlEvent::Kind::PitchBend) {
        bendSemitones_.store(event.value, std::memory_order_relaxed);
        return true;
    }
    return false;
}

void BanjoSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void BanjoSynth::process(const MidiNoteEvent* events, int numEvents,
                         float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    BanjoVoice::Params p;
    p.decay = params_[kDecay].load(std::memory_order_relaxed);
    p.damping = params_[kDamping].load(std::memory_order_relaxed);
    p.pickPosition = params_[kPickPosition].load(std::memory_order_relaxed);
    p.pickHardness = params_[kPickHardness].load(std::memory_order_relaxed);
    p.velocitySensitivity = params_[kVelocitySensitivity].load(std::memory_order_relaxed);
    p.headMix = params_[kHeadMix].load(std::memory_order_relaxed);
    p.bendSemitones = bendSemitones_.load(std::memory_order_relaxed);
    peau_.setTuning(params_[kHeadTension].load(std::memory_order_relaxed),
                    params_[kHeadDamping].load(std::memory_order_relaxed));

    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    filtre_.setCutoffHz(params_[kCutoff].load(std::memory_order_relaxed));
    filtre_.setResonance(0.05f);
    constexpr float kVoiceGain = 0.5f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        float chevalet = 0.0f;
        voiceManager_.forEachVoice([&](BanjoVoice& voice) { chevalet += voice.render(p); });
        // LA PEAU RAYONNE ce que le chevalet lui livre ; le reste est ce que
        // la corde rayonne d'elle-même (peu, sur un vrai banjo).
        const float peau = peau_.process(chevalet);
        const float mixte = chevalet * (1.0f - p.headMix) + peau * p.headMix * 2.2f;
        const float out = filtre_.process(mixte * kVoiceGain * outputLevel);
        outputL[i] = out;
        outputR[i] = out;
    }
}

void BanjoSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float BanjoSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState BanjoSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.banjo";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void BanjoSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.banjo", "Banjo (la corde sur la peau)", BanjoSynth);

} // namespace vsm::plugins::banjo
