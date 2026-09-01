#include "ModalSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::modal {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace {
constexpr uint64_t kBaseSeed = 0x4D4F44414C00ULL; // "MODAL"
} // namespace

ModalSynth::ModalSynth() {
    // DÉFAUTS : une barre de métal frappée à 28 % de sa longueur -- un
    // vibraphone, l'objet le plus reconnaissable de cette famille, et celui
    // qui rend les rapports non harmoniques audibles dès la première note.
    parameterList_ = {
        {kMaterial, "Material", 0.0f, 1.0f, 0.6f, ""},
        {kModeCount, "Modes", 1.0f, 24.0f, 12.0f, ""},
        {kDecay, "Decay", 0.05f, 12.0f, 2.5f, "s"},
        {kDecayTilt, "Decay Tilt", 0.0f, 3.0f, 1.0f, ""},
        {kStrikePosition, "Strike Position", 0.02f, 0.98f, 0.28f, ""},
        {kHardness, "Mallet Hardness", 0.0f, 1.0f, 0.55f, ""},
        {kSpread, "Spread", 0.5f, 2.0f, 1.0f, ""},
        {kVelocityToHardness, "Velocity to Hardness", 0.0f, 1.0f, 0.5f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void ModalSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t index = 0;
    voiceManager_.forEachVoice([&](ModalVoice& voice) {
        voice.prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, index));
        ++index;
    });
}

void ModalSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void ModalSynth::process(const MidiNoteEvent* events, int numEvents,
                         float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    ModalVoice::Params p;
    p.material = params_[kMaterial].load(std::memory_order_relaxed);
    p.modes = params_[kModeCount].load(std::memory_order_relaxed);
    p.decay = params_[kDecay].load(std::memory_order_relaxed);
    p.decayTilt = params_[kDecayTilt].load(std::memory_order_relaxed);
    p.strikePosition = params_[kStrikePosition].load(std::memory_order_relaxed);
    p.hardness = params_[kHardness].load(std::memory_order_relaxed);
    p.spread = params_[kSpread].load(std::memory_order_relaxed);
    p.velocityToHardness = params_[kVelocityToHardness].load(std::memory_order_relaxed);
    p.bendSemitones = bendSemitones_.load(std::memory_order_relaxed);

    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    // Douze modes par voix, huit voix : la somme des modes est déjà bornée
    // par leur injection en n^-pente, d'où un facteur voisin des autres
    // polyphoniques du parc.
    constexpr float kVoiceGain = 0.6f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](ModalVoice& voice) { sum += voice.render(p); });
        sum *= kVoiceGain * outputLevel;
        outputL[i] = sum;
        outputR[i] = sum;
    }
}

void ModalSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float ModalSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState ModalSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.modal";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void ModalSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.modal", "Modal (l'objet frappé)", ModalSynth);

} // namespace vsm::plugins::modal
