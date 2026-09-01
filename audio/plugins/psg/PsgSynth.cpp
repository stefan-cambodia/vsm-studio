#include "PsgSynth.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::psg {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace {
constexpr uint64_t kBaseSeed = 0x5053473830ULL; // "PSG80"
} // namespace

PsgSynth::PsgSynth() {
    // L'HORLOGE EST EN HERTZ, et ce n'est pas une coquetterie : c'est la
    // grandeur physique qui décide de tout le reste sur cette machine. Le défaut
    // est l'horloge du NES et des puces AY de cette époque (1,79 MHz) ; la
    // plage va d'une horloge lente, qui désaccorde franchement, à une horloge
    // rapide, qui rend la quantification inaudible. Les BITS sont en bits.
    parameterList_ = {
        {kClock, "Clock", 100000.0f, 8000000.0f, 1789773.0f, "Hz"},
        {kPulseWidth, "Pulse Width", 0.05f, 0.5f, 0.5f, ""},
        {kVoices, "Square Voices", 1.0f, 3.0f, 1.0f, ""},
        {kDetune, "Detune", 0.0f, 50.0f, 0.0f, "cents"},
        {kNoiseLevel, "Noise Level", 0.0f, 1.0f, 0.0f, ""},
        {kNoisePeriod, "Noise Period", 1.0f, 256.0f, 32.0f, "échantillons"},
        {kBits, "Volume Bits", 1.0f, 8.0f, 4.0f, "bits"},
        {kAttack, "Attack", 0.001f, 1.0f, 0.002f, "s"},
        {kDecay, "Decay", 0.005f, 2.0f, 0.15f, "s"},
        {kSustain, "Sustain", 0.0f, 1.0f, 0.7f, ""},
        {kRelease, "Release", 0.005f, 2.0f, 0.08f, "s"},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void PsgSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t index = 0;
    voiceManager_.forEachVoice([&](PsgVoice& voice) {
        voice.prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, index));
        ++index;
    });
}

void PsgSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void PsgSynth::process(const MidiNoteEvent* events, int numEvents,
                       float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    PsgVoice::Params p;
    p.clockHz = params_[kClock].load(std::memory_order_relaxed);
    p.pulseWidth = params_[kPulseWidth].load(std::memory_order_relaxed);
    p.voices = static_cast<int>(std::lround(params_[kVoices].load(std::memory_order_relaxed)));
    p.detune = params_[kDetune].load(std::memory_order_relaxed);
    p.noiseLevel = params_[kNoiseLevel].load(std::memory_order_relaxed);
    p.noisePeriod = params_[kNoisePeriod].load(std::memory_order_relaxed);
    p.bits = params_[kBits].load(std::memory_order_relaxed);
    p.bendSemitones = bendSemitones_.load(std::memory_order_relaxed);

    const AdsrSettings env{
        params_[kAttack].load(std::memory_order_relaxed),
        params_[kDecay].load(std::memory_order_relaxed),
        params_[kSustain].load(std::memory_order_relaxed),
        params_[kRelease].load(std::memory_order_relaxed),
    };
    voiceManager_.forEachVoice([&](PsgVoice& voice) { voice.setSettings(env); });

    const float sortie = params_[kOutputLevel].load(std::memory_order_relaxed);
    // Niveau calibré sur la norme du parc. Une onde carrée est le signal le plus
    // fort à amplitude crête donnée -- elle passe tout son temps à la valeur
    // maximale --, d'où un facteur plus bas que celui des machines à dents de
    // scie filtrées.
    constexpr float kVoiceGain = 0.085f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](PsgVoice& voice) { sum += voice.render(p); });
        sum *= kVoiceGain * sortie;
        outputL[i] = sum;
        outputR[i] = sum;
    }
}

void PsgSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float PsgSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState PsgSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.psg";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void PsgSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.psg", "PSG (puce 8 bits)", PsgSynth);

} // namespace vsm::plugins::psg
