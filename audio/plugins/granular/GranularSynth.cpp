#include "GranularSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::granular {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace {
constexpr uint64_t kBaseSeed = 0x4752414E554C4Cull; // "GRANULL"
} // namespace

GranularSynth::GranularSynth() {
    parameterList_ = {
        {kGrainSize, "Grain Size", 5.0f, 400.0f, 80.0f, "ms"},
        {kDensity, "Density", 2.0f, 100.0f, 25.0f, "grains/s"},
        {kPitchSpray, "Pitch Spray", 0.0f, 12.0f, 0.0f, "st"},
        {kTimeSpray, "Time Spray", 0.0f, 1.0f, 0.0f, ""},
        {kShimmer, "Shimmer", 0.0f, 1.0f, 0.0f, ""},
        {kShape, "Grain Shape", 0.0f, 3.0f, 0.0f, ""},
        {kStereoSpread, "Stereo Spread", 0.0f, 1.0f, 0.5f, ""},
        {kCutoff, "Filter Cutoff", 40.0f, 16000.0f, 9000.0f, "Hz"},
        {kResonance, "Filter Resonance", 0.0f, 0.95f, 0.15f, ""},
        {kAmpAttack, "Amp Attack", 0.001f, 4.0f, 0.05f, "s"},
        {kAmpDecay, "Amp Decay", 0.005f, 8.0f, 0.5f, "s"},
        {kAmpSustain, "Amp Sustain", 0.0f, 1.0f, 0.8f, ""},
        {kAmpRelease, "Amp Release", 0.005f, 8.0f, 0.4f, "s"},
        {kVelocitySensitivity, "Velocity Sensitivity", 0.0f, 1.0f, 0.5f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void GranularSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t index = 0;
    voiceManager_.forEachVoice([&](GranularVoice& voice) {
        voice.prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, index));
        ++index;
    });
}

void GranularSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void GranularSynth::process(const MidiNoteEvent* events, int numEvents,
                            float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    GranularVoice::Params p;
    p.grainSizeMs = params_[kGrainSize].load(std::memory_order_relaxed);
    p.density = params_[kDensity].load(std::memory_order_relaxed);
    p.pitchSpray = params_[kPitchSpray].load(std::memory_order_relaxed);
    p.timeSpray = params_[kTimeSpray].load(std::memory_order_relaxed);
    p.shimmer = params_[kShimmer].load(std::memory_order_relaxed);
    p.shape = params_[kShape].load(std::memory_order_relaxed);
    p.stereoSpread = params_[kStereoSpread].load(std::memory_order_relaxed);
    p.cutoff = params_[kCutoff].load(std::memory_order_relaxed);
    p.resonance = params_[kResonance].load(std::memory_order_relaxed);
    p.velocityToLevel = params_[kVelocitySensitivity].load(std::memory_order_relaxed);
    p.bendSemitones = bendSemitones_.load(std::memory_order_relaxed);

    const AdsrSettings amp{
        params_[kAmpAttack].load(std::memory_order_relaxed),
        params_[kAmpDecay].load(std::memory_order_relaxed),
        params_[kAmpSustain].load(std::memory_order_relaxed),
        params_[kAmpRelease].load(std::memory_order_relaxed),
    };
    voiceManager_.forEachVoice([&](GranularVoice& voice) { voice.setEnvelope(amp); });

    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    // Le recouvrement nominal (densité × taille de grain, 2 aux défauts) fixe
    // le niveau : la somme de N fenêtres de Hann décalées vaut N/2.
    constexpr float kVoiceGain = 0.35f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        float sumL = 0.0f, sumR = 0.0f;
        voiceManager_.forEachVoice([&](GranularVoice& voice) { voice.render(p, sumL, sumR); });
        outputL[i] = sumL * kVoiceGain * outputLevel;
        outputR[i] = sumR * kVoiceGain * outputLevel;
    }
}

void GranularSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float GranularSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState GranularSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.granular";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void GranularSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.granular", "Granular (le nuage de grains)", GranularSynth);

} // namespace vsm::plugins::granular
