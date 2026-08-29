#include "StochasticSynth.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::stochastic {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace {
constexpr uint64_t kBaseSeed = 0x53544F4348ULL; // "STOCH"
} // namespace

StochasticSynth::StochasticSynth() {
    // Le NOMBRE DE POINTS est un entier -- c'est un compte, pas une proportion.
    // Les deux divagations sont des PAS de marche aléatoire, sans unité
    // physique : elles disent « de combien un point bouge d'un tour au
    // suivant », rapporté à sa plage. Le verrou est une proportion lui aussi.
    parameterList_ = {
        {kPoints, "Breakpoints", 2.0f, 16.0f, 8.0f, "points"},
        {kAmpWander, "Shape Wander", 0.0f, 0.5f, 0.12f, ""},
        {kTimeWander, "Time Wander", 0.0f, 0.5f, 0.08f, ""},
        {kPitchLock, "Pitch Lock", 0.0f, 1.0f, 1.0f, ""},
        {kTone, "Tone", 500.0f, 16000.0f, 8000.0f, "Hz"},
        {kAttack, "Attack", 0.001f, 2.0f, 0.01f, "s"},
        {kDecay, "Decay", 0.005f, 4.0f, 0.4f, "s"},
        {kSustain, "Sustain", 0.0f, 1.0f, 0.8f, ""},
        {kRelease, "Release", 0.005f, 4.0f, 0.3f, "s"},
        {kVelocityToWander, "Velocity to Wander", 0.0f, 1.0f, 0.3f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void StochasticSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t index = 0;
    voiceManager_.forEachVoice([&](StochasticVoice& voice) {
        voice.prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, index));
        ++index;
    });
}

void StochasticSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void StochasticSynth::process(const MidiNoteEvent* events, int numEvents,
                              float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    StochasticVoice::Params p;
    p.points = params_[kPoints].load(std::memory_order_relaxed);
    p.ampWander = params_[kAmpWander].load(std::memory_order_relaxed);
    p.timeWander = params_[kTimeWander].load(std::memory_order_relaxed);
    p.pitchLock = params_[kPitchLock].load(std::memory_order_relaxed);
    p.velocityToWander = params_[kVelocityToWander].load(std::memory_order_relaxed);

    const AdsrSettings env{
        params_[kAttack].load(std::memory_order_relaxed),
        params_[kDecay].load(std::memory_order_relaxed),
        params_[kSustain].load(std::memory_order_relaxed),
        params_[kRelease].load(std::memory_order_relaxed),
    };
    const float tone = params_[kTone].load(std::memory_order_relaxed);
    voiceManager_.forEachVoice([&](StochasticVoice& voice) {
        voice.setSettings(env);
        voice.setToneHz(std::min(tone, static_cast<float>(sampleRate_) * 0.45f));
    });

    const float sortie = params_[kOutputLevel].load(std::memory_order_relaxed);
    // Niveau calibré sur la norme du parc : la forme d'onde est faite de
    // segments droits, donc riche et forte à amplitude crête donnée.
    constexpr float kVoiceGain = 0.16f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](StochasticVoice& voice) { sum += voice.render(p); });
        sum *= kVoiceGain * sortie;
        outputL[i] = sum;
        outputR[i] = sum;
    }
}

void StochasticSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float StochasticSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState StochasticSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.stochastic";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void StochasticSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.stochastic", "Stochastic (la forme qui divague)", StochasticSynth);

} // namespace vsm::plugins::stochastic
