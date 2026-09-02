#include "ReedSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::reed {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

ReedSynth::ReedSynth() {
    // LA PRESSION PAR DÉFAUT EST AU-DESSUS DU SEUIL D'AMORÇAGE, et c'est une
    // contrainte, pas un goût : une machine qui sort muette à ses réglages
    // d'usine est une machine qu'on croit cassée. Mesuré, le seuil vaut
    // 0,12 + 0,5·raideur, soit 0,32 à la raideur par défaut.
    parameterList_ = {
        {kPressure, "Bellows Pressure", 0.0f, 1.0f, 0.6f, ""},
        {kStiffness, "Reed Stiffness", 0.0f, 1.0f, 0.4f, ""},
        {kAirLoading, "Air Loading", 0.0f, 1.0f, 0.6f, ""},
        {kCutoff, "Filter Cutoff", 200.0f, 16000.0f, 6500.0f, "Hz"},
        {kResonance, "Filter Resonance", 0.0f, 0.95f, 0.1f, ""},
        {kAttack, "Attack", 0.001f, 4.0f, 0.05f, "s"},
        {kDecay, "Decay", 0.005f, 8.0f, 0.3f, "s"},
        {kSustain, "Sustain", 0.0f, 1.0f, 0.95f, ""},
        {kRelease, "Release", 0.005f, 8.0f, 0.15f, "s"},
        {kVelocityToPressure, "Velocity to Pressure", 0.0f, 1.0f, 0.35f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void ReedSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t graine = 0x52454544ULL;
    voiceManager_.forEachVoice([&](ReedVoice& voice) { voice.prepare(sampleRate, graine++); });
}

void ReedSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void ReedSynth::process(const MidiNoteEvent* events, int numEvents,
                        float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    ReedVoice::Params p;
    // Le soufflet reçu au canal REMPLACE le potentiomètre tant qu'il arrive :
    // c'est la main gauche de l'accordéoniste, et elle a le dernier mot.
    const float canal = pressionDeCanal_.load(std::memory_order_relaxed);
    p.pressure = canal >= 0.0f ? canal : params_[kPressure].load(std::memory_order_relaxed);
    p.stiffness = params_[kStiffness].load(std::memory_order_relaxed);
    p.airLoading = params_[kAirLoading].load(std::memory_order_relaxed);
    p.cutoff = params_[kCutoff].load(std::memory_order_relaxed);
    p.resonance = params_[kResonance].load(std::memory_order_relaxed);
    p.velocityToPressure = params_[kVelocityToPressure].load(std::memory_order_relaxed);
    p.bendSemitones = bendSemitones_.load(std::memory_order_relaxed);

    const AdsrSettings env{
        params_[kAttack].load(std::memory_order_relaxed),
        params_[kDecay].load(std::memory_order_relaxed),
        params_[kSustain].load(std::memory_order_relaxed),
        params_[kRelease].load(std::memory_order_relaxed)};
    voiceManager_.forEachVoice([&](ReedVoice& voice) { voice.setEnvelope(env); });

    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    constexpr float kVoiceGain = 0.6f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](ReedVoice& voice) { sum += voice.render(p); });
        sum *= kVoiceGain * outputLevel;
        outputL[i] = sum;
        outputR[i] = sum;
    }
}

void ReedSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float ReedSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState ReedSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.reed";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void ReedSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.reed", "Reed (l'anche libre)", ReedSynth);

} // namespace vsm::plugins::reed
