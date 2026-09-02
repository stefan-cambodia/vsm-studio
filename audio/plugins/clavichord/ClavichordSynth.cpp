#include "ClavichordSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::clavichord {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

ClavichordSynth::ClavichordSynth() {
    parameterList_ = {
        {kPressureToTension, "Pressure to Tension", 0.0f, 1.0f, 1.0f, ""},
        {kDecay, "String Decay", 0.2f, 12.0f, 3.0f, "s"},
        {kDamping, "String Damping", 0.0f, 1.0f, 0.35f, ""},
        {kTangentPosition, "Tangent Position", 0.02f, 0.5f, 0.13f, ""},
        {kCutoff, "Filter Cutoff", 500.0f, 16000.0f, 9000.0f, "Hz"},
        {kResonance, "Filter Resonance", 0.0f, 0.95f, 0.05f, ""},
        {kVelocitySensitivity, "Velocity Sensitivity", 0.0f, 1.0f, 0.7f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void ClavichordSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t graine = 0x434C4156ULL;
    voiceManager_.forEachVoice([&](ClavichordVoice& voice) { voice.prepare(sampleRate, graine++); });
    filtre_.setSampleRate(sampleRate);
    filtre_.reset();
}

bool ClavichordSynth::handleControlEvent(const MidiControlEvent& event) {
    using Kind = MidiControlEvent::Kind;
    if (event.kind == Kind::PitchBend) {
        bendSemitones_.store(event.value, std::memory_order_relaxed);
        return true;
    }
    // LA PRESSION EST LE GESTE DE CET INSTRUMENT, et par touche de préférence :
    // un clavicordiste fait vibrer UNE note pendant que les autres tiennent.
    // C'est le même chemin que `vsm.cs80`, la première machine du parc à avoir
    // une modulation par voix — sauf qu'ici la pression va à la TENSION et non
    // au filtre.
    if (event.kind == Kind::PolyPressure) {
        bool touchee = false;
        voiceManager_.forEachVoice([&](ClavichordVoice& voice) {
            if (voice.isActive() && voice.note() == event.index) {
                voice.setPressure(event.value);
                touchee = true;
            }
        });
        return touchee;
    }
    // Le repli : un clavier sans capteur par touche envoie une pression de
    // canal, et le Bebung doit rester jouable depuis un tel clavier.
    if (event.kind == Kind::ChannelPressure) {
        voiceManager_.forEachVoice([&](ClavichordVoice& voice) { voice.setPressure(event.value); });
        return true;
    }
    return false;
}

void ClavichordSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void ClavichordSynth::process(const MidiNoteEvent* events, int numEvents,
                              float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    ClavichordVoice::Params p;
    p.decay = params_[kDecay].load(std::memory_order_relaxed);
    p.damping = params_[kDamping].load(std::memory_order_relaxed);
    p.tangentPosition = params_[kTangentPosition].load(std::memory_order_relaxed);
    p.pressureToTension = params_[kPressureToTension].load(std::memory_order_relaxed);
    p.velocitySensitivity = params_[kVelocitySensitivity].load(std::memory_order_relaxed);
    p.bendSemitones = bendSemitones_.load(std::memory_order_relaxed);

    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    filtre_.setCutoffHz(params_[kCutoff].load(std::memory_order_relaxed));
    filtre_.setResonance(params_[kResonance].load(std::memory_order_relaxed));
    // UN CLAVICORDE EST L'INSTRUMENT LE PLUS DOUX QUI SOIT, et le gain le dit
    // plutôt que de le corriger : on ne remonte pas artificiellement une
    // machine dont la faiblesse de niveau est un trait.
    constexpr float kVoiceGain = 0.4f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](ClavichordVoice& voice) { sum += voice.render(p); });
        sum = filtre_.process(sum * kVoiceGain * outputLevel);
        outputL[i] = sum;
        outputR[i] = sum;
    }
}

void ClavichordSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float ClavichordSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState ClavichordSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.clavichord";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void ClavichordSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.clavichord", "Clavichord (le clavier qui vibre)", ClavichordSynth);

} // namespace vsm::plugins::clavichord
