#include "SH101Synth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>

namespace vsm::plugins::sh101 {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

SH101Synth::SH101Synth() {
    parameterList_ = {
        {kSawLevel, "Saw Level", 0.0f, 1.0f, 1.0f, ""},
        {kPulseLevel, "Pulse Level", 0.0f, 1.0f, 0.0f, ""},
        {kSubLevel, "Sub Level", 0.0f, 1.0f, 0.5f, ""},
        {kNoiseLevel, "Noise Level", 0.0f, 1.0f, 0.0f, ""},
        {kPulseWidth, "Pulse Width", 0.05f, 0.95f, 0.5f, ""},
        {kPwmLfoAmount, "PWM LFO Amount", 0.0f, 1.0f, 0.0f, ""},
        {kSubType, "Sub Type", 0.0f, 1.0f, 0.0f, ""}, // 0=-1oct 1=-2oct
        {kLfoRate, "LFO Rate", 0.05f, 30.0f, 5.0f, "Hz"},
        {kLfoWaveform, "LFO Waveform", 0.0f, 2.0f, 0.0f, ""}, // 0 tri 1 sq 2 rnd
        {kLfoPitchAmount, "LFO Pitch Amount", 0.0f, 1.0f, 0.0f, ""},
        {kLfoFilterAmount, "LFO Filter Amount", 0.0f, 1.0f, 0.0f, ""},
        {kFilterCutoff, "Filter Cutoff", 20.0f, 20000.0f, 1000.0f, "Hz"},
        {kFilterResonance, "Filter Resonance", 0.0f, 4.2f, 0.2f, ""},
        {kFilterEnvAmount, "Filter Env Amount", -1.0f, 1.0f, 0.5f, ""},
        {kFilterKeyTrack, "Filter Key Track", 0.0f, 1.0f, 0.3f, ""},
        {kEnvAttack, "Env Attack", 0.001f, 4.0f, 0.01f, "s"},
        {kEnvDecay, "Env Decay", 0.001f, 8.0f, 0.3f, "s"},
        {kEnvSustain, "Env Sustain", 0.0f, 1.0f, 0.7f, ""},
        {kEnvRelease, "Env Release", 0.001f, 8.0f, 0.2f, "s"},
        {kVcaMode, "VCA Mode", 0.0f, 1.0f, 0.0f, ""}, // 0 env 1 gate
        {kGlideTime, "Glide Time", 0.0f, 3.0f, 0.0f, "s"},
        {kAnalogCharacter, "Analog Character", 0.0f, 1.0f, 0.3f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void SH101Synth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    saw_.setSampleRate(sampleRate);   saw_.setWaveform(Waveform::Saw);
    pulse_.setSampleRate(sampleRate); pulse_.setWaveform(Waveform::Square);
    sub_.setSampleRate(sampleRate);   sub_.setWaveform(Waveform::Square);
    filter_.setSampleRate(sampleRate);
    filter_.setPoleCount(4);
    filter_.setDrive(1.0f);
    env_.setSampleRate(sampleRate);
    pitchGlide_.setSampleRate(sampleRate);
    pitchGlide_.reset(60.0f);
    pitchDrift_.setSampleRate(sampleRate); pitchDrift_.setSeed(0x5101000000000001ULL);
    pitchDrift_.setRateHz(0.15f);
    cutoffDrift_.setSampleRate(sampleRate); cutoffDrift_.setSeed(777);
    cutoffDrift_.setRateHz(0.1f);
    lfoPhase_ = 0.0;
    gateGain_ = 0.0f;
    gateHeld_ = false;
    voiceAllocator_.reset();
}

void SH101Synth::applyNoteEvent(const MidiNoteEvent& ev) {
    using vsm::audio::engine::MonoVoiceAllocator;
    if (ev.kind == MidiNoteEvent::Kind::NoteOn && ev.velocity > 0) {
        const bool wasIdle = !voiceAllocator_.hasHeldNotes();
        MonoVoiceAllocator::Result r = voiceAllocator_.noteOn(ev.note, ev.velocity);
        pitchGlide_.setTarget(static_cast<float>(r.note));
        if (wasIdle) pitchGlide_.reset(static_cast<float>(r.note)); // pas de glide depuis rien
        if (r.retrigger) env_.noteOn();
    } else {
        MonoVoiceAllocator::Result r = voiceAllocator_.noteOff(ev.note);
        if (r.shouldPlay) {
            pitchGlide_.setTarget(static_cast<float>(r.note));
            if (r.retrigger) env_.noteOn();
        } else {
            env_.noteOff();
        }
    }
    gateHeld_ = voiceAllocator_.hasHeldNotes();
}

float SH101Synth::renderLfo(int waveform) {
    const float phase = static_cast<float>(lfoPhase_);
    switch (waveform) {
        case 1: return phase < 0.5f ? 1.0f : -1.0f;         // carré
        case 2: return lfoRandom_;                          // aléatoire (S&H)
        default: return 4.0f * std::abs(phase - 0.5f) - 1.0f; // triangle
    }
}

void SH101Synth::process(const MidiNoteEvent* events, int numEvents,
                         float* outputL, float* outputR, int numSamples) {
    const float sawLevel = params_[kSawLevel].load(std::memory_order_relaxed);
    const float pulseLevel = params_[kPulseLevel].load(std::memory_order_relaxed);
    const float subLevel = params_[kSubLevel].load(std::memory_order_relaxed);
    const float noiseLevel = params_[kNoiseLevel].load(std::memory_order_relaxed);
    const float pwBase = params_[kPulseWidth].load(std::memory_order_relaxed);
    const float pwmAmount = params_[kPwmLfoAmount].load(std::memory_order_relaxed);
    const bool sub2Oct = params_[kSubType].load(std::memory_order_relaxed) >= 0.5f;

    const float lfoRate = params_[kLfoRate].load(std::memory_order_relaxed);
    const int lfoWave = static_cast<int>(std::lround(params_[kLfoWaveform].load(std::memory_order_relaxed)));
    const float lfoPitchAmt = params_[kLfoPitchAmount].load(std::memory_order_relaxed);
    const float lfoFilterAmt = params_[kLfoFilterAmount].load(std::memory_order_relaxed);

    const float cutoffBase = params_[kFilterCutoff].load(std::memory_order_relaxed);
    const float resonance = params_[kFilterResonance].load(std::memory_order_relaxed);
    const float envAmount = params_[kFilterEnvAmount].load(std::memory_order_relaxed);
    const float keyTrack = params_[kFilterKeyTrack].load(std::memory_order_relaxed);
    const bool vcaGate = params_[kVcaMode].load(std::memory_order_relaxed) >= 0.5f;
    const float analogCharacter = params_[kAnalogCharacter].load(std::memory_order_relaxed);

    env_.setSettings(AdsrSettings{
        params_[kEnvAttack].load(std::memory_order_relaxed),
        params_[kEnvDecay].load(std::memory_order_relaxed),
        params_[kEnvSustain].load(std::memory_order_relaxed),
        params_[kEnvRelease].load(std::memory_order_relaxed),
    });
    pitchGlide_.setSmoothingTimeMs(params_[kGlideTime].load(std::memory_order_relaxed) * 1000.0f);
    pitchDrift_.setAmount(analogCharacter);
    cutoffDrift_.setAmount(analogCharacter);

    lfoIncrement_ = lfoRate / sampleRate_;
    const float gateCoeff = 1.0f - std::exp(-1.0f / (0.005f * static_cast<float>(sampleRate_)));

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i) {
            applyNoteEvent(events[eventIndex]);
            ++eventIndex;
        }

        // LFO : avance de phase + resample aléatoire au passage de cycle.
        lfoPhase_ += lfoIncrement_;
        if (lfoPhase_ >= 1.0) { lfoPhase_ -= 1.0; lfoRandom_ = lfoRng_.nextBipolar(); }
        const float lfo = renderLfo(lfoWave);

        const float noteNumber = pitchGlide_.nextValue();
        const float pitchDriftSemis = pitchDrift_.nextValue() * kMaxPitchDriftSemitones;
        // Le terme de la molette est ADDITIF et l'expression d'origine reste
        // telle quelle : refactoriser le produit changerait son ordre
        // d'association flottant, donc l'empreinte, même à molette nulle.
        const float vibratoSemis = lfo * lfoPitchAmt * kLfoPitchRangeSemitones
                                 + lfo * (std::min(1.0f, modWheel_.load(std::memory_order_relaxed)
                                     + pressure_.load(std::memory_order_relaxed))
                                          * kWheelVibratoSemitones);
        const float baseHz = 440.0f * std::exp2f((noteNumber + pitchDriftSemis + vibratoSemis + bendSemitones_.load(std::memory_order_relaxed) - 69.0f) / 12.0f);

        const float pw = std::clamp(pwBase + lfo * pwmAmount * 0.45f, 0.05f, 0.95f);

        saw_.setFrequency(baseHz);
        pulse_.setFrequency(baseHz);
        pulse_.setPulseWidth(pw);
        sub_.setFrequency(baseHz * (sub2Oct ? 0.25f : 0.5f));

        const float noise = noiseRng_.nextBipolar();
        float mixed = saw_.nextSample() * sawLevel
                    + pulse_.nextSample() * pulseLevel
                    + sub_.nextSample() * subLevel
                    + noise * noiseLevel;
        mixed *= 0.4f;

        const float envLevel = env_.nextSample();

        const float cutoffDriftOct = cutoffDrift_.nextValue() * kMaxCutoffDriftOctaves;
        const float envOct = envAmount * envLevel * kFilterEnvRangeOctaves;
        const float lfoOct = lfo * lfoFilterAmt * kLfoFilterRangeOctaves;
        const float trackOct = keyTrack * (noteNumber - 60.0f) / 12.0f;
        const float cutoff = cutoffBase * std::exp2f(envOct + lfoOct + trackOct + cutoffDriftOct);

        filter_.setCutoffHz(cutoff);
        filter_.setResonance(resonance);
        const float filtered = filter_.process(mixed);

        // VCA : enveloppe ou gate (pas de dépendance à la vélocité -> le
        // clavier SH-101 n'est pas vélocité-sensible).
        const float gateTarget = gateHeld_ ? 1.0f : 0.0f;
        gateGain_ += (gateTarget - gateGain_) * gateCoeff;
        const float vca = vcaGate ? gateGain_ : envLevel;

        const float sample = filtered * vca;
        outputL[i] = sample;
        outputR[i] = sample;
    }
}

bool SH101Synth::handleControlEvent(const MidiControlEvent& event) {
    if (event.kind == MidiControlEvent::Kind::PitchBend) {
        bendSemitones_.store(event.value, std::memory_order_relaxed);
        return true;
    }
    // CC 1, la molette de modulation : elle dose le vibrato au LFO, comme le
    // levier du panneau. Les autres contrôleurs sont refusés en le disant.
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

void SH101Synth::setParameter(ParamId id, float value) {
    if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}

float SH101Synth::getParameter(ParamId id) const {
    return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

const ParameterList& SH101Synth::parameterList() const { return parameterList_; }

PresetState SH101Synth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.sh101";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void SH101Synth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.sh101", "SH-101-style Monosynth", SH101Synth);

} // namespace vsm::plugins::sh101
