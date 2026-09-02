#include "MusicBoxSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::musicbox {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

MusicBoxSynth::MusicBoxSynth() {
    // LE TEMPS DE RETOUR EST UN RÉGLAGE, mais sa borne basse n'est pas zéro :
    // une lame qui reviendrait instantanément ne serait plus une lame, et la
    // machine perdrait ce qui la distingue de toutes les autres.
    parameterList_ = {
        {kReturnTime, "Return Time", 0.02f, 1.0f, 0.18f, "s"},
        {kDecay, "Decay", 0.2f, 12.0f, 3.5f, "s"},
        {kDecayTilt, "Decay Tilt", 0.0f, 3.0f, 1.1f, ""},
        {kBrightness, "Brightness", 0.0f, 1.0f, 0.35f, ""},
        {kVelocitySensitivity, "Velocity Sensitivity", 0.0f, 1.0f, 0.6f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void MusicBoxSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    uint64_t graine = 0x4D425858ULL;
    voiceManager_.forEachVoice([&](MusicBoxVoice& voice) { voice.prepare(sampleRate, graine++); });
    // Très loin dans le passé : la première note de chaque lame passe toujours.
    dernierPincement_.fill(-1.0e9);
    horloge_ = 0.0;
    refusees_.store(0, std::memory_order_relaxed);
}

void MusicBoxSynth::applyNoteEvent(const MidiNoteEvent& event, int position) {
    if (event.kind != MidiNoteEvent::Kind::NoteOn || event.velocity == 0) {
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
        return;
    }
    const auto touche = static_cast<size_t>(event.note & 0x7F);
    const double maintenant = horloge_ + static_cast<double>(position);
    const double retour = static_cast<double>(params_[kReturnTime].load(std::memory_order_relaxed))
                        * sampleRate_;

    // LA LAME N'EST PAS REVENUE : IL N'Y A RIEN À PINCER. C'est la ligne qui
    // porte le trait de la machine, et elle REFUSE la note au lieu de la
    // jouer plus doucement — une goupille qui passe sous une lame déjà levée
    // ne produit pas un son faible, elle ne produit rien.
    if (maintenant - dernierPincement_[touche] < retour) {
        refusees_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    dernierPincement_[touche] = maintenant;
    voiceManager_.noteOn(event.channel, event.note, event.velocity);
}

void MusicBoxSynth::process(const MidiNoteEvent* events, int numEvents,
                            float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    MusicBoxVoice::Params p;
    p.decay = params_[kDecay].load(std::memory_order_relaxed);
    p.decayTilt = params_[kDecayTilt].load(std::memory_order_relaxed);
    p.brightness = params_[kBrightness].load(std::memory_order_relaxed);
    p.velocitySensitivity = params_[kVelocitySensitivity].load(std::memory_order_relaxed);

    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    constexpr float kVoiceGain = 0.6f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++], i);

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](MusicBoxVoice& voice) { sum += voice.render(p); });
        sum *= kVoiceGain * outputLevel;
        outputL[i] = sum;
        outputR[i] = sum;
    }
    horloge_ += static_cast<double>(numSamples);
}

void MusicBoxSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float MusicBoxSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState MusicBoxSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.musicbox";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void MusicBoxSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.musicbox", "Music Box (la lame qui doit revenir)", MusicBoxSynth);

} // namespace vsm::plugins::musicbox
