#include "ArpOdysseySynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>

namespace vsm::plugins::arpodyssey {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

ArpOdysseySynth::ArpOdysseySynth() {
    parameterList_ = {
        {kVco1Level, "VCO-1 Level", 0.0f, 1.0f, 0.8f, ""},
        {kVco1Shape, "VCO-1 Shape", 0.0f, 1.0f, 0.0f, ""},
        {kVco1PulseWidth, "VCO-1 Pulse Width", 0.05f, 0.95f, 0.5f, ""},
        {kVco2Level, "VCO-2 Level", 0.0f, 1.0f, 0.6f, ""},
        {kVco2Shape, "VCO-2 Shape", 0.0f, 1.0f, 0.0f, ""},
        {kVco2PulseWidth, "VCO-2 Pulse Width", 0.05f, 0.95f, 0.5f, ""},
        {kVco2Detune, "VCO-2 Detune", -12.0f, 12.0f, 0.0f, "st"},
        {kRingModLevel, "Ring Mod Level", 0.0f, 1.0f, 0.0f, ""},
        {kNoiseLevel, "Noise Level", 0.0f, 1.0f, 0.0f, ""},
        {kSync, "Sync", 0.0f, 1.0f, 0.0f, ""},
        {kHpfCutoff, "HPF Cutoff", 20.0f, 2000.0f, 20.0f, "Hz"},
        {kFilterCutoff, "Filter Cutoff", 20.0f, 20000.0f, 1400.0f, "Hz"},
        {kFilterResonance, "Filter Resonance", 0.0f, 4.2f, 0.3f, ""},
        {kFilterEnvAmount, "Filter Env Amount", -1.0f, 1.0f, 0.5f, ""},
        {kFilterKeyTrack, "Filter Key Track", 0.0f, 1.0f, 0.3f, ""},
        {kLfoRate, "LFO Rate", 0.05f, 30.0f, 5.0f, "Hz"},
        {kLfoWaveform, "LFO Waveform", 0.0f, 2.0f, 0.0f, ""},
        {kLfoToPitch, "LFO to Pitch", 0.0f, 1.0f, 0.0f, ""},
        {kLfoToFilter, "LFO to Filter", 0.0f, 1.0f, 0.0f, ""},
        {kAttack, "Amp Attack", 0.001f, 4.0f, 0.005f, "s"},
        {kDecay, "Amp Decay", 0.001f, 8.0f, 0.3f, "s"},
        {kSustain, "Amp Sustain", 0.0f, 1.0f, 0.8f, ""},
        {kRelease, "Amp Release", 0.001f, 8.0f, 0.2f, "s"},
        {kGlideTime, "Glide Time", 0.0f, 3.0f, 0.0f, "s"},
        {kAnalogCharacter, "Analog Character", 0.0f, 1.0f, 0.3f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void ArpOdysseySynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    vco1_.setSampleRate(sampleRate);
    vco2_.setSampleRate(sampleRate);
    hpf_.setSampleRate(sampleRate);
    hpf_.setMode(StateVariableFilter::Mode::HighPass);
    hpf_.setResonance(0.707f);
    lpf_.setSampleRate(sampleRate);
    lpf_.setPoleCount(4);
    lpf_.setDrive(1.0f);
    env_.setSampleRate(sampleRate);
    glide1_.setSampleRate(sampleRate); glide1_.reset(60.0f);
    glide2_.setSampleRate(sampleRate); glide2_.reset(60.0f);
    pitchDrift1_.setSampleRate(sampleRate); pitchDrift1_.setSeed(0x0D95000000000001ULL); pitchDrift1_.setRateHz(0.15f);
    pitchDrift2_.setSampleRate(sampleRate); pitchDrift2_.setSeed(0x0D95000000000002ULL); pitchDrift2_.setRateHz(0.13f);
    cutoffDrift_.setSampleRate(sampleRate); cutoffDrift_.setSeed(0x0D95000000000003ULL); cutoffDrift_.setRateHz(0.1f);
    lfoPhase_ = 0.0;
    lfoRandom_ = 0.0f;
    syncPhase_ = 0.0f;
    keys_.reset();
}

void ArpOdysseySynth::applyNoteEvent(const MidiNoteEvent& ev) {
    if (ev.kind == MidiNoteEvent::Kind::NoteOn && ev.velocity > 0) {
        const bool gateRose = keys_.noteOn(ev.note);
        if (gateRose) {
            // Premier front de gate : caler le glide sur les touches actuelles
            // pour ne pas glisser depuis une note fantôme.
            glide1_.reset(static_cast<float>(keys_.lowest()));
            glide2_.reset(static_cast<float>(keys_.highest()));
            env_.noteOn();
        }
    } else {
        keys_.noteOff(ev.note);
        if (!keys_.anyHeld()) env_.noteOff();
    }
    if (keys_.anyHeld()) {
        glide1_.setTarget(static_cast<float>(keys_.lowest()));
        glide2_.setTarget(static_cast<float>(keys_.highest()));
    }
}

float ArpOdysseySynth::renderLfo(int waveform) const {
    const float phase = static_cast<float>(lfoPhase_);
    switch (waveform) {
        case 1: return phase < 0.5f ? 1.0f : -1.0f;             // carré
        case 2: return lfoRandom_;                              // sample & hold
        default: return 4.0f * std::abs(phase - 0.5f) - 1.0f;  // triangle
    }
}

void ArpOdysseySynth::process(const MidiNoteEvent* events, int numEvents,
                              float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    const float vco1Level = params_[kVco1Level].load(std::memory_order_relaxed);
    const int vco1Shape = static_cast<int>(std::lround(params_[kVco1Shape].load(std::memory_order_relaxed)));
    const float vco1Pw = params_[kVco1PulseWidth].load(std::memory_order_relaxed);
    const float vco2Level = params_[kVco2Level].load(std::memory_order_relaxed);
    const int vco2Shape = static_cast<int>(std::lround(params_[kVco2Shape].load(std::memory_order_relaxed)));
    const float vco2Pw = params_[kVco2PulseWidth].load(std::memory_order_relaxed);
    const float vco2Detune = params_[kVco2Detune].load(std::memory_order_relaxed);
    const float ringLevel = params_[kRingModLevel].load(std::memory_order_relaxed);
    const float noiseLevel = params_[kNoiseLevel].load(std::memory_order_relaxed);
    const bool sync = params_[kSync].load(std::memory_order_relaxed) >= 0.5f;
    const float hpfCutoff = params_[kHpfCutoff].load(std::memory_order_relaxed);
    const float cutoffBase = params_[kFilterCutoff].load(std::memory_order_relaxed);
    const float resonance = params_[kFilterResonance].load(std::memory_order_relaxed);
    const float envAmount = params_[kFilterEnvAmount].load(std::memory_order_relaxed);
    const float keyTrack = params_[kFilterKeyTrack].load(std::memory_order_relaxed);
    const float lfoRate = params_[kLfoRate].load(std::memory_order_relaxed);
    const int lfoWave = static_cast<int>(std::lround(params_[kLfoWaveform].load(std::memory_order_relaxed)));
    const float lfoToPitch = params_[kLfoToPitch].load(std::memory_order_relaxed);
    const float lfoToFilter = params_[kLfoToFilter].load(std::memory_order_relaxed);
    const float analogCharacter = params_[kAnalogCharacter].load(std::memory_order_relaxed);

    env_.setSettings(AdsrSettings{
        params_[kAttack].load(std::memory_order_relaxed),
        params_[kDecay].load(std::memory_order_relaxed),
        params_[kSustain].load(std::memory_order_relaxed),
        params_[kRelease].load(std::memory_order_relaxed),
    });
    const float glideMs = params_[kGlideTime].load(std::memory_order_relaxed) * 1000.0f;
    glide1_.setSmoothingTimeMs(glideMs);
    glide2_.setSmoothingTimeMs(glideMs);
    pitchDrift1_.setAmount(analogCharacter);
    pitchDrift2_.setAmount(analogCharacter);
    cutoffDrift_.setAmount(analogCharacter);

    lfoIncrement_ = lfoRate / sampleRate_;
    hpf_.setCutoffHz(hpfCutoff); // block-constant : réglé une fois hors boucle

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i) {
            applyNoteEvent(events[eventIndex]);
            ++eventIndex;
        }

        lfoPhase_ += lfoIncrement_;
        if (lfoPhase_ >= 1.0) { lfoPhase_ -= 1.0; lfoRandom_ = lfoRng_.nextBipolar(); }
        const float lfo = renderLfo(lfoWave);
        // Le terme de la molette est ADDITIF et l'expression d'origine reste
        // telle quelle : refactoriser le produit changerait son ordre
        // d'association flottant, donc l'empreinte, même à molette nulle.
        const float vibratoSemis = lfo * lfoToPitch * kLfoPitchRangeSemitones
                                 + lfo * (std::min(1.0f, modWheel_.load(std::memory_order_relaxed)
                                     + pressure_.load(std::memory_order_relaxed))
                                          * kWheelVibratoSemitones);

        const float note1 = glide1_.nextValue();
        const float note2 = glide2_.nextValue();
        const float d1 = pitchDrift1_.nextValue() * kMaxPitchDriftSemitones;
        const float d2 = pitchDrift2_.nextValue() * kMaxPitchDriftSemitones;

        const float bend = bendSemitones_.load(std::memory_order_relaxed);
        const float hz1 = 440.0f * std::exp2f((note1 + d1 + vibratoSemis + bend - 69.0f) / 12.0f);
        const float hz2 = 440.0f * std::exp2f((note2 + vco2Detune + d2 + vibratoSemis + bend - 69.0f) / 12.0f);

        // VCO-2 d'abord (source de sync).
        vco2_.setFrequency(hz2);
        vco2_.setWaveform(shapeToWave(vco2Shape));
        if (vco2Shape == 1) vco2_.setPulseWidth(vco2Pw);

        syncPhase_ += hz2 / static_cast<float>(sampleRate_);
        if (syncPhase_ >= 1.0f) { syncPhase_ -= 1.0f; if (sync) vco1_.reset(0.0); }
        const float raw2 = vco2_.nextSample();

        vco1_.setFrequency(hz1);
        vco1_.setWaveform(shapeToWave(vco1Shape));
        if (vco1Shape == 1) vco1_.setPulseWidth(vco1Pw);
        const float raw1 = vco1_.nextSample();

        const float ring = raw1 * raw2; // modulateur en anneau
        const float noise = noiseRng_.nextBipolar();

        float mixed = raw1 * vco1Level + raw2 * vco2Level
                    + ring * ringLevel + noise * noiseLevel;
        mixed *= 0.4f;

        mixed = hpf_.process(mixed);

        const float envLevel = env_.nextSample();
        const float cutoffDriftOct = cutoffDrift_.nextValue() * kMaxCutoffDriftOctaves;
        const float envOct = envAmount * envLevel * kFilterEnvRangeOctaves;
        const float lfoOct = lfo * lfoToFilter * kLfoFilterRangeOctaves;
        const float trackOct = keyTrack * (note1 - 60.0f) / 12.0f;
        const float cutoff = cutoffBase * std::exp2f(envOct + lfoOct + trackOct + cutoffDriftOct);
        lpf_.setCutoffHz(cutoff);
        lpf_.setResonance(resonance);

        const float sample = lpf_.process(mixed) * envLevel; // pas de vélocité
        outputL[i] = sample;
        outputR[i] = sample;
    }
}

bool ArpOdysseySynth::handleControlEvent(const MidiControlEvent& event) {
    if (event.kind == MidiControlEvent::Kind::PitchBend) {
        bendSemitones_.store(event.value, std::memory_order_relaxed);
        return true;
    }
    // CC 1, la molette de modulation : elle dose le vibrato au LFO, comme le
    // levier PPC du panneau. Les autres contrôleurs sont refusés en le disant.
    if (event.kind == MidiControlEvent::Kind::ControlChange && event.index == 1) {
        modWheel_.store(event.value, std::memory_order_relaxed);
        return true;
    }
    // L'AFTERTOUCH (pression de canal) dose le même vibrato que la molette :
    // c'est le geste qu'un clavier envoie quand on appuie dans la touche.
    if (event.kind == MidiControlEvent::Kind::ChannelPressure) {
        pressure_.store(event.value, std::memory_order_relaxed);
        return true;
    }
    return false;
}

void ArpOdysseySynth::setParameter(ParamId id, float value) {
    if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}
float ArpOdysseySynth::getParameter(ParamId id) const {
    return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}
const ParameterList& ArpOdysseySynth::parameterList() const { return parameterList_; }

PresetState ArpOdysseySynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.arpodyssey";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}
void ArpOdysseySynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.arpodyssey", "ARP-Odyssey-style Duophonic", ArpOdysseySynth);

} // namespace vsm::plugins::arpodyssey
