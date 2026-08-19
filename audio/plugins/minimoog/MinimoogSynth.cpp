#include "MinimoogSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>

namespace vsm::plugins::minimoog {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

MinimoogSynth::MinimoogSynth() {
    parameterList_ = {
        {kOsc1Waveform, "Osc1 Waveform", 0.0f, 3.0f, 1.0f, ""},         // Saw
        {kOsc2Waveform, "Osc2 Waveform", 0.0f, 3.0f, 1.0f, ""},         // Saw
        {kOsc3Waveform, "Osc3 Waveform", 0.0f, 3.0f, 2.0f, ""},         // Square
        {kOsc1Level, "Osc1 Level", 0.0f, 1.0f, 0.8f, ""},
        {kOsc2Level, "Osc2 Level", 0.0f, 1.0f, 0.5f, ""},
        {kOsc3Level, "Osc3 Level", 0.0f, 1.0f, 0.0f, ""},
        {kNoiseLevel, "Noise Level", 0.0f, 1.0f, 0.0f, ""},
        {kOsc2DetuneSemitones, "Osc2 Detune", -12.0f, 12.0f, -0.08f, "st"},
        {kOsc3DetuneSemitones, "Osc3 Detune", -12.0f, 12.0f, 7.0f, "st"},
        {kFilterCutoff, "Filter Cutoff", 20.0f, 20000.0f, 1200.0f, "Hz"},
        {kFilterResonance, "Filter Resonance", 0.0f, 4.2f, 0.8f, ""},
        {kFilterDrive, "Filter Drive", 0.1f, 8.0f, 1.2f, ""},
        {kFilterEnvAmount, "Filter Env Amount", -1.0f, 1.0f, 0.5f, ""},
        {kFilterKeyTrack, "Filter Key Track", 0.0f, 1.0f, 0.3f, ""},
        {kFilterAttack, "Filter Attack", 0.001f, 4.0f, 0.005f, "s"},
        {kFilterDecay, "Filter Decay", 0.001f, 8.0f, 0.4f, "s"},
        {kFilterSustain, "Filter Sustain", 0.0f, 1.0f, 0.3f, ""},
        {kAmpAttack, "Amp Attack", 0.001f, 4.0f, 0.003f, "s"},
        {kAmpDecay, "Amp Decay", 0.001f, 8.0f, 0.6f, "s"},
        {kAmpSustain, "Amp Sustain", 0.0f, 1.0f, 0.8f, ""},
        {kGlideTimeSeconds, "Glide Time", 0.0f, 3.0f, 0.0f, "s"},
        {kAnalogCharacter, "Analog Character", 0.0f, 1.0f, 0.3f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void MinimoogSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;

    osc1_.setSampleRate(sampleRate);
    osc2_.setSampleRate(sampleRate);
    osc3_.setSampleRate(sampleRate);
    filter_.setSampleRate(sampleRate);
    filterEnv_.setSampleRate(sampleRate);
    ampEnv_.setSampleRate(sampleRate);

    pitchGlide_.setSampleRate(sampleRate);
    pitchGlide_.reset(60.0f);

    // Graines fixes : deux instances fraîches soumises aux mêmes événements
    // produisent un rendu bit-identique (vérifié par test) -- déterminisme
    // de session, section 8. Un futur paramètre "seed" explicite (Phase 6)
    // pourra varier ceci par piste si besoin de dérives non corrélées entre
    // plusieurs instances du même patch.
    pitchDrift_.setSampleRate(sampleRate);
    pitchDrift_.setSeed(0xA17C5EEDULL);
    pitchDrift_.setRateHz(0.2f);
    cutoffDrift_.setSampleRate(sampleRate);
    cutoffDrift_.setSeed(0xC0FFEE42ULL);
    cutoffDrift_.setRateHz(0.15f);
}

Waveform MinimoogSynth::waveformFromParam(ParamId id) const {
    int idx = static_cast<int>(std::lround(params_[id].load(std::memory_order_relaxed)));
    switch (std::clamp(idx, 0, 3)) {
        case 0: return Waveform::Sine;
        case 1: return Waveform::Saw;
        case 2: return Waveform::Square;
        default: return Waveform::Triangle;
    }
}

void MinimoogSynth::applyNoteEvent(const MidiNoteEvent& ev) {
    using vsm::audio::engine::MonoVoiceAllocator;

    if (ev.kind == MidiNoteEvent::Kind::NoteOn) {
        MonoVoiceAllocator::Result r = voiceAllocator_.noteOn(ev.note, ev.velocity);
        if (!r.shouldPlay) return;

        currentVelocity_ = r.velocity;
        bool wasIdle = !ampEnv_.isActive();
        pitchGlide_.setTarget(static_cast<float>(r.note));
        if (wasIdle) pitchGlide_.reset(static_cast<float>(r.note)); // pas de glide "depuis rien"

        if (r.retrigger) {
            filterEnv_.noteOn();
            ampEnv_.noteOn();
        }
    } else {
        MonoVoiceAllocator::Result r = voiceAllocator_.noteOff(ev.note);
        if (r.shouldPlay) {
            currentVelocity_ = r.velocity;
            pitchGlide_.setTarget(static_cast<float>(r.note)); // retombe sur la note précédente, glide vers elle
            if (r.retrigger) {
                filterEnv_.noteOn();
                ampEnv_.noteOn();
            }
        } else {
            filterEnv_.noteOff();
            ampEnv_.noteOff();
        }
    }
}

void MinimoogSynth::process(const MidiNoteEvent* events, int numEvents,
                             float* outputL, float* outputR, int numSamples) {
    // Paramètres lus une fois par bloc (comme TestToneSynth) -- suffisant
    // pour la Phase 3 ; un lissage per-sample de tous les paramètres
    // continus (pas seulement pitch/cutoff) reste un raffinement Phase 6.
    float osc1Level = params_[kOsc1Level].load(std::memory_order_relaxed);
    float osc2Level = params_[kOsc2Level].load(std::memory_order_relaxed);
    float osc3Level = params_[kOsc3Level].load(std::memory_order_relaxed);
    float noiseLevel = params_[kNoiseLevel].load(std::memory_order_relaxed);
    float osc2Detune = params_[kOsc2DetuneSemitones].load(std::memory_order_relaxed);
    float osc3Detune = params_[kOsc3DetuneSemitones].load(std::memory_order_relaxed);

    float filterCutoffBase = params_[kFilterCutoff].load(std::memory_order_relaxed);
    float filterResonance = params_[kFilterResonance].load(std::memory_order_relaxed);
    float filterDrive = params_[kFilterDrive].load(std::memory_order_relaxed);
    float filterEnvAmount = params_[kFilterEnvAmount].load(std::memory_order_relaxed);
    float filterKeyTrack = params_[kFilterKeyTrack].load(std::memory_order_relaxed);

    float analogCharacter = params_[kAnalogCharacter].load(std::memory_order_relaxed);

    AdsrSettings filterAdsr{
        params_[kFilterAttack].load(std::memory_order_relaxed),
        params_[kFilterDecay].load(std::memory_order_relaxed),
        params_[kFilterSustain].load(std::memory_order_relaxed),
        params_[kFilterDecay].load(std::memory_order_relaxed), // release = decay (quirk Model D, voir en-tête)
    };
    AdsrSettings ampAdsr{
        params_[kAmpAttack].load(std::memory_order_relaxed),
        params_[kAmpDecay].load(std::memory_order_relaxed),
        params_[kAmpSustain].load(std::memory_order_relaxed),
        params_[kAmpDecay].load(std::memory_order_relaxed),
    };
    filterEnv_.setSettings(filterAdsr);
    ampEnv_.setSettings(ampAdsr);

    pitchGlide_.setSmoothingTimeMs(params_[kGlideTimeSeconds].load(std::memory_order_relaxed) * 1000.0f);
    pitchDrift_.setAmount(analogCharacter);
    cutoffDrift_.setAmount(analogCharacter);

    Waveform w1 = waveformFromParam(kOsc1Waveform);
    Waveform w2 = waveformFromParam(kOsc2Waveform);
    Waveform w3 = waveformFromParam(kOsc3Waveform);
    osc1_.setWaveform(w1);
    osc2_.setWaveform(w2);
    osc3_.setWaveform(w3);

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i) {
            applyNoteEvent(events[eventIndex]);
            ++eventIndex;
        }

        float noteNumber = pitchGlide_.nextValue();
        float pitchDriftSemis = pitchDrift_.nextValue() * kMaxPitchDriftSemitones;
        float baseHz = 440.0f * std::exp2f((noteNumber + pitchDriftSemis - 69.0f) / 12.0f);

        osc1_.setFrequency(baseHz);
        osc2_.setFrequency(baseHz * std::exp2f(osc2Detune / 12.0f));
        osc3_.setFrequency(baseHz * std::exp2f(osc3Detune / 12.0f));

        float noiseSample = noiseRng_.nextBipolar();
        float mixed = osc1_.nextSample() * osc1Level + osc2_.nextSample() * osc2Level +
                      osc3_.nextSample() * osc3Level + noiseSample * noiseLevel;
        mixed *= 0.35f; // normalisation grossière : évite la saturation systématique à pleine charge

        float filterEnvLevel = filterEnv_.nextSample();
        float ampEnvLevel = ampEnv_.nextSample();

        float cutoffDriftOct = cutoffDrift_.nextValue() * kMaxCutoffDriftOctaves;
        float envOctaves = filterEnvAmount * filterEnvLevel * kFilterEnvRangeOctaves;
        float trackOctaves = filterKeyTrack * (noteNumber - 60.0f) / 12.0f;
        float finalCutoff = filterCutoffBase * std::exp2f(envOctaves + trackOctaves + cutoffDriftOct);

        filter_.setCutoffHz(finalCutoff);
        filter_.setResonance(filterResonance);
        filter_.setDrive(filterDrive);

        float filtered = filter_.process(mixed);
        float velocityGain = static_cast<float>(currentVelocity_) / 127.0f;
        float sample = filtered * ampEnvLevel * velocityGain;

        outputL[i] = sample;
        outputR[i] = sample;
    }
}

void MinimoogSynth::setParameter(ParamId id, float value) {
    if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}

float MinimoogSynth::getParameter(ParamId id) const {
    return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

const ParameterList& MinimoogSynth::parameterList() const { return parameterList_; }

PresetState MinimoogSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.minimoog";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void MinimoogSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}

int MinimoogSynth::activeVoiceCount() const {
    // Lecture non atomique de l'état de l'enveloppe (même limitation que
    // TestToneSynth::activeVoiceCount() -- usage diagnostic/affichage
    // uniquement, jamais dans le chemin de synthèse lui-même).
    return ampEnv_.isActive() ? 1 : 0;
}

// Voir PluginRegistry.h : la macro fait du token-pasting, elle doit être
// invoquée ICI (nom non qualifié, à l'intérieur du namespace du plugin).
VSM_REGISTER_SYNTH_PLUGIN("vsm.minimoog", "Minimoog-style Monosynth", MinimoogSynth);

} // namespace vsm::plugins::minimoog
