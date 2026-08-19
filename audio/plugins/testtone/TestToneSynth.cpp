#include "TestToneSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::testtone {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

TestToneSynth::TestToneSynth() {
    parameterList_ = {
        {kWaveform, "Waveform", 0.0f, 3.0f, 1.0f, ""},
        {kFilterCutoff, "Filter Cutoff", 20.0f, 20000.0f, 2000.0f, "Hz"},
        {kFilterResonance, "Resonance", 0.5f, 10.0f, 0.707f, "Q"},
        {kAttack, "Attack", 0.001f, 4.0f, 0.01f, "s"},
        {kDecay, "Decay", 0.001f, 4.0f, 0.1f, "s"},
        {kSustain, "Sustain", 0.0f, 1.0f, 0.7f, ""},
        {kRelease, "Release", 0.001f, 4.0f, 0.2f, "s"},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void TestToneSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    voices_.forEachVoice([sampleRate](Voice& v) { v.prepare(sampleRate); });
}

Waveform TestToneSynth::waveformFromParam() const {
    int idx = static_cast<int>(std::lround(params_[kWaveform].load(std::memory_order_relaxed)));
    switch (std::clamp(idx, 0, 3)) {
        case 0: return Waveform::Sine;
        case 1: return Waveform::Saw;
        case 2: return Waveform::Square;
        default: return Waveform::Triangle;
    }
}

void TestToneSynth::process(const MidiNoteEvent* events, int numEvents,
                             float* outputL, float* outputR, int numSamples) {
    AdsrSettings adsr{
        params_[kAttack].load(std::memory_order_relaxed),
        params_[kDecay].load(std::memory_order_relaxed),
        params_[kSustain].load(std::memory_order_relaxed),
        params_[kRelease].load(std::memory_order_relaxed),
    };
    voices_.forEachVoice([&adsr](Voice& v) { v.setAdsr(adsr); });

    Waveform waveform = waveformFromParam();
    float cutoff = params_[kFilterCutoff].load(std::memory_order_relaxed);
    float resonance = params_[kFilterResonance].load(std::memory_order_relaxed);

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i) {
            const auto& ev = events[eventIndex];
            if (ev.kind == MidiNoteEvent::Kind::NoteOn)
                voices_.noteOn(ev.channel, ev.note, ev.velocity);
            else
                voices_.noteOff(ev.channel, ev.note, ev.velocity);
            ++eventIndex;
        }

        float sample = 0.0f;
        voices_.forEachVoice([&](Voice& v) {
            if (v.isActive()) sample += v.nextSample(waveform, cutoff, resonance);
        });

        // Atténuation simple pour éviter la saturation en sommant plusieurs
        // voix. Une vraie compensation de gain polyphonique (racine carrée
        // du nombre de voix actives, limiteur de sortie...) arrive avec le
        // Mixer/Master complet (Phase 2 UI / Phase 6).
        outputL[i] = sample * 0.25f;
        outputR[i] = sample * 0.25f;
    }
}

void TestToneSynth::setParameter(ParamId id, float value) {
    if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}

float TestToneSynth::getParameter(ParamId id) const {
    return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

const ParameterList& TestToneSynth::parameterList() const { return parameterList_; }

PresetState TestToneSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.testtone";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void TestToneSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}

int TestToneSynth::activeVoiceCount() const { return voices_.activeVoiceCount(); }

// L'enregistrement doit se faire ICI, à l'intérieur du namespace, avec un
// nom de classe NON qualifié : la macro fait du token-pasting
// (ClassName##Registrar), impossible avec un nom qualifié type
// vsm::plugins::testtone::TestToneSynth (## ne peut coller qu'un seul
// token). C'est la convention à suivre pour chaque futur plugin (Phase 3+).
VSM_REGISTER_SYNTH_PLUGIN("vsm.testtone", "Test Tone (reference)", TestToneSynth);

} // namespace vsm::plugins::testtone
