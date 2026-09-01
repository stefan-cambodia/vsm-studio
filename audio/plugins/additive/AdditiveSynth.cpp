#include "AdditiveSynth.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::additive {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace {
constexpr uint64_t kBaseSeed = 0x414444495456ULL; // "ADDITV"
} // namespace

AdditiveSynth::AdditiveSynth() {
    // UNITÉS PHYSIQUES (§ 1 du CDC) partout où la grandeur en a une : des
    // décibels par octave pour la pente, des secondes pour l'enveloppe. Les
    // trois réglages sans unité physique -- balance, raideur, étalement -- sont
    // des PROPORTIONS, et 0..1 est alors leur unité naturelle, pas un
    // normalisé qui cacherait des hertz.
    parameterList_ = {
        {kPartialCount, "Partials", 1.0f, 32.0f, 16.0f, ""},
        {kSpectralTilt, "Spectral Tilt", -18.0f, 0.0f, -6.0f, "dB/oct"},
        {kOddEven, "Odd/Even Balance", 0.0f, 1.0f, 0.5f, ""},
        {kInharmonicity, "Inharmonicity", 0.0f, 1.0f, 0.0f, ""},
        {kDecayTilt, "Decay Tilt", 0.0f, 1.0f, 0.25f, ""},
        {kAttackSpread, "Attack Spread", 0.0f, 1.0f, 0.0f, ""},
        {kAmpAttack, "Amp Attack", 0.001f, 2.0f, 0.01f, "s"},
        {kAmpDecay, "Amp Decay", 0.005f, 4.0f, 0.5f, "s"},
        {kAmpSustain, "Amp Sustain", 0.0f, 1.0f, 0.7f, ""},
        {kAmpRelease, "Amp Release", 0.005f, 4.0f, 0.3f, "s"},
        {kVelocityToTilt, "Velocity to Tilt", 0.0f, 1.0f, 0.4f, ""},
        {kAnalogCharacter, "Analog Character", 0.0f, 1.0f, 0.15f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void AdditiveSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t index = 0;
    voiceManager_.forEachVoice([&](AdditiveVoice& voice) {
        voice.prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, index));
        ++index;
    });
}

void AdditiveSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void AdditiveSynth::process(const MidiNoteEvent* events, int numEvents,
                            float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    AdditiveVoice::Params p;
    p.partialCount = params_[kPartialCount].load(std::memory_order_relaxed);
    p.tiltDbPerOct = params_[kSpectralTilt].load(std::memory_order_relaxed);
    p.oddEven = params_[kOddEven].load(std::memory_order_relaxed);
    p.inharmonicity = params_[kInharmonicity].load(std::memory_order_relaxed);
    p.decayTilt = params_[kDecayTilt].load(std::memory_order_relaxed);
    p.attackSpread = params_[kAttackSpread].load(std::memory_order_relaxed);
    p.velocityToTilt = params_[kVelocityToTilt].load(std::memory_order_relaxed);
    p.bendSemitones = bendSemitones_.load(std::memory_order_relaxed);

    const AdsrSettings amp{
        params_[kAmpAttack].load(std::memory_order_relaxed),
        params_[kAmpDecay].load(std::memory_order_relaxed),
        params_[kAmpSustain].load(std::memory_order_relaxed),
        params_[kAmpRelease].load(std::memory_order_relaxed),
    };
    const float drift = params_[kAnalogCharacter].load(std::memory_order_relaxed);
    voiceManager_.forEachVoice([&](AdditiveVoice& voice) {
        voice.setSettings(amp);
        voice.setDriftAmount(drift);
    });

    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    // Niveau calibré par MESURE, comme pour les autres polyphoniques du parc :
    // un accord de huit notes à vélocité 110 doit rester sous 0 dBFS avec de la
    // marge, et une note seule sortir dans la même plage que le Juno ou le
    // Prophet -- deux machines mises en concurrence sur un stem ne doivent pas
    // se départager au volume.
    constexpr float kVoiceGain = 0.34f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](AdditiveVoice& voice) { sum += voice.render(p); });
        sum *= kVoiceGain * outputLevel;
        outputL[i] = sum;
        outputR[i] = sum;
    }
}

void AdditiveSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float AdditiveSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState AdditiveSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.additive";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void AdditiveSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.additive", "Additive (le spectre rang par rang)", AdditiveSynth);

} // namespace vsm::plugins::additive
