#include "VectorSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::vector {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace {
constexpr uint64_t kBaseSeed = 0x56454354455552ULL; // "VECTEUR"
} // namespace

VectorSynth::VectorSynth() {
    // Les coins portent des DÉFAUTS qui font entendre la machine dès la
    // première note : A sinus, B scie, C triangle à l'octave, D carré à la
    // quinte -- une orbite au repos, mais un carré déjà contrasté.
    parameterList_ = {
        {kVectorX, "Vector X", 0.0f, 1.0f, 0.5f, ""},
        {kVectorY, "Vector Y", 0.0f, 1.0f, 0.5f, ""},
        {kOrbitRate, "Orbit Rate", 0.02f, 8.0f, 0.6f, "Hz"},
        {kOrbitDepth, "Orbit Depth", 0.0f, 1.0f, 0.0f, ""},
        {kShapeA, "A Shape", 0.0f, 3.0f, 0.0f, ""},
        {kDetuneA, "A Detune", -24.0f, 24.0f, 0.0f, "st"},
        {kShapeB, "B Shape", 0.0f, 3.0f, 2.0f, ""},
        {kDetuneB, "B Detune", -24.0f, 24.0f, 0.0f, "st"},
        {kShapeC, "C Shape", 0.0f, 3.0f, 1.0f, ""},
        {kDetuneC, "C Detune", -24.0f, 24.0f, 12.0f, "st"},
        {kShapeD, "D Shape", 0.0f, 3.0f, 3.0f, ""},
        {kDetuneD, "D Detune", -24.0f, 24.0f, 7.0f, "st"},
        {kCutoff, "Filter Cutoff", 40.0f, 16000.0f, 6000.0f, "Hz"},
        {kResonance, "Filter Resonance", 0.0f, 0.95f, 0.2f, ""},
        {kEnvAmount, "Filter Env Amount", 0.0f, 1.0f, 0.3f, ""},
        {kKeyTrack, "Filter Key Track", 0.0f, 1.0f, 0.3f, ""},
        {kAmpAttack, "Amp Attack", 0.001f, 4.0f, 0.01f, "s"},
        {kAmpDecay, "Amp Decay", 0.005f, 8.0f, 0.4f, "s"},
        {kAmpSustain, "Amp Sustain", 0.0f, 1.0f, 0.8f, ""},
        {kAmpRelease, "Amp Release", 0.005f, 8.0f, 0.3f, "s"},
        {kFilterAttack, "Filter Attack", 0.001f, 4.0f, 0.01f, "s"},
        {kFilterDecay, "Filter Decay", 0.005f, 8.0f, 0.5f, "s"},
        {kFilterSustain, "Filter Sustain", 0.0f, 1.0f, 0.4f, ""},
        {kFilterRelease, "Filter Release", 0.005f, 8.0f, 0.3f, "s"},
        {kVelocitySensitivity, "Velocity Sensitivity", 0.0f, 1.0f, 0.5f, ""},
        {kAnalogCharacter, "Analog Character", 0.0f, 1.0f, 0.15f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void VectorSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t index = 0;
    voiceManager_.forEachVoice([&](VectorVoice& voice) {
        voice.prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, index));
        ++index;
    });
}

void VectorSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void VectorSynth::process(const MidiNoteEvent* events, int numEvents,
                          float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    VectorVoice::Params p;
    p.vectorX = params_[kVectorX].load(std::memory_order_relaxed);
    p.vectorY = params_[kVectorY].load(std::memory_order_relaxed);
    p.orbitRate = params_[kOrbitRate].load(std::memory_order_relaxed);
    p.orbitDepth = params_[kOrbitDepth].load(std::memory_order_relaxed);
    p.shape = {params_[kShapeA].load(std::memory_order_relaxed),
               params_[kShapeB].load(std::memory_order_relaxed),
               params_[kShapeC].load(std::memory_order_relaxed),
               params_[kShapeD].load(std::memory_order_relaxed)};
    p.detune = {params_[kDetuneA].load(std::memory_order_relaxed),
                params_[kDetuneB].load(std::memory_order_relaxed),
                params_[kDetuneC].load(std::memory_order_relaxed),
                params_[kDetuneD].load(std::memory_order_relaxed)};
    p.cutoff = params_[kCutoff].load(std::memory_order_relaxed);
    p.resonance = params_[kResonance].load(std::memory_order_relaxed);
    p.envAmount = params_[kEnvAmount].load(std::memory_order_relaxed);
    p.keyTrack = params_[kKeyTrack].load(std::memory_order_relaxed);
    p.velocityToLevel = params_[kVelocitySensitivity].load(std::memory_order_relaxed);
    p.bendSemitones = bendSemitones_.load(std::memory_order_relaxed);

    const AdsrSettings amp{
        params_[kAmpAttack].load(std::memory_order_relaxed),
        params_[kAmpDecay].load(std::memory_order_relaxed),
        params_[kAmpSustain].load(std::memory_order_relaxed),
        params_[kAmpRelease].load(std::memory_order_relaxed),
    };
    const AdsrSettings filtre{
        params_[kFilterAttack].load(std::memory_order_relaxed),
        params_[kFilterDecay].load(std::memory_order_relaxed),
        params_[kFilterSustain].load(std::memory_order_relaxed),
        params_[kFilterRelease].load(std::memory_order_relaxed),
    };
    const float drift = params_[kAnalogCharacter].load(std::memory_order_relaxed);
    voiceManager_.forEachVoice([&](VectorVoice& voice) {
        voice.setEnvelopes(amp, filtre);
        voice.setDriftAmount(drift);
    });

    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    // Huit voix bilinéaires : au centre, chaque source pèse un quart, la somme
    // reste dans la fenêtre des polyphoniques du parc.
    constexpr float kVoiceGain = 0.5f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](VectorVoice& voice) { sum += voice.render(p); });
        sum *= kVoiceGain * outputLevel;
        outputL[i] = sum;
        outputR[i] = sum;
    }
}

void VectorSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float VectorSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState VectorSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.vector";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void VectorSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.vector", "Vector (quatre coins, un trajet)", VectorSynth);

} // namespace vsm::plugins::vector
