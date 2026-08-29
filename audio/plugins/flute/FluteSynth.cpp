#include "FluteSynth.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::flute {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace {
constexpr uint64_t kBaseSeed = 0x464C555445ULL; // "FLUTE"
} // namespace

FluteSynth::FluteSynth() {
    parameterList_ = {
        {kBreath, "Breath Pressure", 0.0f, 1.0f, 0.6f, ""},
        {kNoise, "Breath Noise", 0.0f, 1.0f, 0.04f, ""},
        {kJetRatio, "Jet Delay", 0.1f, 0.9f, 0.5f, ""},
        {kJetGain, "Jet Feedback", 0.0f, 2.0f, 0.5f, ""},
        {kDamping, "Bell Damping", 0.02f, 0.8f, 0.55f, ""},
        {kVibratoRate, "Vibrato Rate", 0.5f, 9.0f, 5.0f, "Hz"},
        {kVibratoDepth, "Vibrato Depth", 0.0f, 1.0f, 0.2f, ""},
        {kVibratoDelay, "Vibrato Delay", 0.0f, 2.0f, 0.4f, "s"},
        {kAttack, "Attack", 0.005f, 1.0f, 0.08f, "s"},
        {kRelease, "Release", 0.01f, 2.0f, 0.15f, "s"},
        {kVelocityToBreath, "Velocity Sensitivity", 0.0f, 1.0f, 0.4f, ""},
        {kAnalogCharacter, "Analog Character", 0.0f, 1.0f, 0.2f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void FluteSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t index = 0;
    voiceManager_.forEachVoice([&](FluteVoice& voice) {
        voice.prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, index));
        ++index;
    });
}

void FluteSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void FluteSynth::process(const MidiNoteEvent* events, int numEvents,
                         float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    FluteVoice::Params p;
    p.breath = params_[kBreath].load(std::memory_order_relaxed);
    p.noise = params_[kNoise].load(std::memory_order_relaxed);
    p.jetRatio = params_[kJetRatio].load(std::memory_order_relaxed);
    p.jetGain = params_[kJetGain].load(std::memory_order_relaxed);
    p.damping = params_[kDamping].load(std::memory_order_relaxed);
    p.vibratoRate = params_[kVibratoRate].load(std::memory_order_relaxed);
    p.vibratoDepth = params_[kVibratoDepth].load(std::memory_order_relaxed);
    p.vibratoDelay = params_[kVibratoDelay].load(std::memory_order_relaxed);
    p.velocityToBreath = params_[kVelocityToBreath].load(std::memory_order_relaxed);

    const AdsrSettings env{
        params_[kAttack].load(std::memory_order_relaxed),
        0.02f, 1.0f,
        params_[kRelease].load(std::memory_order_relaxed),
    };
    const float drift = params_[kAnalogCharacter].load(std::memory_order_relaxed);
    voiceManager_.forEachVoice([&](FluteVoice& voice) {
        voice.setSettings(env);
        voice.setDriftAmount(drift);
    });

    const float sortie = params_[kOutputLevel].load(std::memory_order_relaxed);
    constexpr float kVoiceGain = 0.5f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](FluteVoice& voice) { sum += voice.render(p); });
        sum = std::tanh(sum * kVoiceGain) * sortie;
        outputL[i] = sum;
        outputR[i] = sum;
    }
}

void FluteSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float FluteSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState FluteSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.flute";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void FluteSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.flute", "Flute (jet d'air sur biseau)", FluteSynth);

} // namespace vsm::plugins::flute
