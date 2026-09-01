#include "PhaseDistSynth.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::phasedist {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace {
constexpr uint64_t kBaseSeed = 0x5048415344ULL; // "PHASD"
} // namespace

PhaseDistSynth::PhaseDistSynth() {
    // Le RANG de résonance est en unités de rang harmonique -- 3 veut dire « le
    // troisième », pas « 30 % » --, et c'est une vraie unité : elle se lit sur
    // un spectre. Les autres réglages sans grandeur physique sont des
    // proportions.
    parameterList_ = {
        {kAmount, "Distortion", 0.0f, 1.0f, 0.35f, ""},
        {kEnvToAmount, "Env to Distortion", 0.0f, 1.0f, 0.5f, ""},
        {kResonance, "Resonance", 0.0f, 1.0f, 0.0f, ""},
        {kResonanceHarmonic, "Resonance Harmonic", 1.0f, 16.0f, 3.0f, "rang"},
        {kModAttack, "Mod Attack", 0.001f, 2.0f, 0.005f, "s"},
        {kModDecay, "Mod Decay", 0.005f, 4.0f, 0.5f, "s"},
        {kModSustain, "Mod Sustain", 0.0f, 1.0f, 0.3f, ""},
        {kModRelease, "Mod Release", 0.005f, 4.0f, 0.3f, "s"},
        {kAmpAttack, "Amp Attack", 0.001f, 2.0f, 0.005f, "s"},
        {kAmpDecay, "Amp Decay", 0.005f, 4.0f, 0.6f, "s"},
        {kAmpSustain, "Amp Sustain", 0.0f, 1.0f, 0.6f, ""},
        {kAmpRelease, "Amp Release", 0.005f, 4.0f, 0.3f, "s"},
        {kVelocityToAmount, "Velocity to Distortion", 0.0f, 1.0f, 0.4f, ""},
        {kAnalogCharacter, "Analog Character", 0.0f, 1.0f, 0.1f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void PhaseDistSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t index = 0;
    voiceManager_.forEachVoice([&](PhaseDistVoice& voice) {
        voice.prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, index));
        ++index;
    });
}

void PhaseDistSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void PhaseDistSynth::process(const MidiNoteEvent* events, int numEvents,
                             float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    PhaseDistVoice::Params p;
    p.amount = params_[kAmount].load(std::memory_order_relaxed);
    p.envToAmount = params_[kEnvToAmount].load(std::memory_order_relaxed);
    p.resonance = params_[kResonance].load(std::memory_order_relaxed);
    p.resonanceHarmonic = params_[kResonanceHarmonic].load(std::memory_order_relaxed);
    p.velocityToAmount = params_[kVelocityToAmount].load(std::memory_order_relaxed);
    p.bendSemitones = bendSemitones_.load(std::memory_order_relaxed);

    const AdsrSettings amp{
        params_[kAmpAttack].load(std::memory_order_relaxed),
        params_[kAmpDecay].load(std::memory_order_relaxed),
        params_[kAmpSustain].load(std::memory_order_relaxed),
        params_[kAmpRelease].load(std::memory_order_relaxed),
    };
    const AdsrSettings mod{
        params_[kModAttack].load(std::memory_order_relaxed),
        params_[kModDecay].load(std::memory_order_relaxed),
        params_[kModSustain].load(std::memory_order_relaxed),
        params_[kModRelease].load(std::memory_order_relaxed),
    };
    const float drift = params_[kAnalogCharacter].load(std::memory_order_relaxed);
    voiceManager_.forEachVoice([&](PhaseDistVoice& voice) {
        voice.setSettings(amp, mod);
        voice.setDriftAmount(drift);
    });

    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    // NIVEAU CALIBRÉ SUR LA NORME DU PARC, relevée et non supposée. Un accord de
    // huit notes crête, sur les autres polyphoniques : Juno-106 0,944, Prophet
    // 0,766, additif 0,736, Jupiter-8 0,567. À 0,26 cette machine montait à
    // 1,748 -- huit sinus déformés partent tous de la même phase et
    // s'additionnent en crête. Ramené dans la fenêtre, ce qui met une note
    // seule à 0,124, l'ordre de grandeur du Jupiter-8 (0,154). Deux machines
    // mises en concurrence sur un stem ne doivent pas se départager au volume.
    constexpr float kVoiceGain = 0.135f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](PhaseDistVoice& voice) { sum += voice.render(p); });
        sum *= kVoiceGain * outputLevel;
        outputL[i] = sum;
        outputR[i] = sum;
    }
}

void PhaseDistSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float PhaseDistSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState PhaseDistSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.phasedist";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void PhaseDistSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.phasedist", "Phase Distortion (le temps déformé)", PhaseDistSynth);

} // namespace vsm::plugins::phasedist
