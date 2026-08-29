#include "WestCoastSynth.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::westcoast {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace {
constexpr uint64_t kBaseSeed = 0x574553544300ULL; // "WESTC"
} // namespace

WestCoastSynth::WestCoastSynth() {
    // Unités physiques là où la grandeur en a une : des demi-tons pour la
    // profondeur de modulation, des hertz pour la coupure, des secondes pour
    // les temps. Le pliage et la symétrie sont des PROPORTIONS -- il n'existe
    // pas d'unité du « combien on plie » -- et 0..1 est alors leur mesure
    // naturelle, pas un normalisé qui cacherait autre chose.
    parameterList_ = {
        {kFold, "Fold", 0.0f, 1.0f, 0.35f, ""},
        {kSymmetry, "Fold Symmetry", 0.0f, 1.0f, 0.5f, ""},
        {kRatio, "Mod Ratio", 0.25f, 8.0f, 2.0f, ""},
        {kFmIndex, "Mod Depth", 0.0f, 24.0f, 0.0f, "demi-tons"},
        {kGateCutoff, "Gate Cutoff", 200.0f, 16000.0f, 6000.0f, "Hz"},
        {kGateLag, "Gate Lag", 0.005f, 1.5f, 0.25f, "s"},
        {kAmpAttack, "Amp Attack", 0.001f, 2.0f, 0.005f, "s"},
        {kAmpDecay, "Amp Decay", 0.005f, 4.0f, 0.4f, "s"},
        {kAmpSustain, "Amp Sustain", 0.0f, 1.0f, 0.0f, ""},
        {kAmpRelease, "Amp Release", 0.005f, 4.0f, 0.2f, "s"},
        {kVelocityToFold, "Velocity to Fold", 0.0f, 1.0f, 0.5f, ""},
        {kAnalogCharacter, "Analog Character", 0.0f, 1.0f, 0.15f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void WestCoastSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t index = 0;
    voiceManager_.forEachVoice([&](WestCoastVoice& voice) {
        voice.prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, index));
        ++index;
    });
}

void WestCoastSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void WestCoastSynth::process(const MidiNoteEvent* events, int numEvents,
                             float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    WestCoastVoice::Params p;
    p.fold = params_[kFold].load(std::memory_order_relaxed);
    p.symmetry = params_[kSymmetry].load(std::memory_order_relaxed);
    p.ratio = params_[kRatio].load(std::memory_order_relaxed);
    p.fmIndex = params_[kFmIndex].load(std::memory_order_relaxed);
    p.gateCutoff = params_[kGateCutoff].load(std::memory_order_relaxed);
    p.gateLag = params_[kGateLag].load(std::memory_order_relaxed);
    p.velocityToFold = params_[kVelocityToFold].load(std::memory_order_relaxed);

    const AdsrSettings amp{
        params_[kAmpAttack].load(std::memory_order_relaxed),
        params_[kAmpDecay].load(std::memory_order_relaxed),
        params_[kAmpSustain].load(std::memory_order_relaxed),
        params_[kAmpRelease].load(std::memory_order_relaxed),
    };
    const float drift = params_[kAnalogCharacter].load(std::memory_order_relaxed);
    voiceManager_.forEachVoice([&](WestCoastVoice& voice) {
        voice.setSettings(amp);
        voice.setDriftAmount(drift);
    });

    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    // NIVEAU CALIBRÉ SUR LA NORME DU PARC, relevée plutôt que supposée. Un
    // accord de huit notes à vélocité 110 crête, sur les autres polyphoniques :
    // Juno-106 0,944, Prophet 0,766, additif 0,736, Jupiter-8 0,567. À 0,30,
    // cette machine montait à 1,664 -- elle aurait écrêté là où les autres ont
    // de la marge. Le facteur est donc ramené dans la même fenêtre, ce qui met
    // une note seule à 0,154 de crête, exactement le Jupiter-8. Deux machines
    // mises en concurrence sur un stem ne doivent pas se départager au volume.
    constexpr float kVoiceGain = 0.17f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](WestCoastVoice& voice) { sum += voice.render(p); });
        sum *= kVoiceGain * outputLevel;
        outputL[i] = sum;
        outputR[i] = sum;
    }
}

void WestCoastSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float WestCoastSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState WestCoastSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.westcoast";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void WestCoastSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.westcoast", "West Coast (pliage et porte passe-bas)", WestCoastSynth);

} // namespace vsm::plugins::westcoast
