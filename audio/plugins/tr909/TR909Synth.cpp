#include "TR909Synth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>

namespace vsm::plugins::tr909 {

using namespace vsm::audio::plugin;

TR909Synth::TR909Synth() {
    parameterList_ = {
        {kKickLevel, "Kick Level", 0.0f, 1.0f, 0.9f, ""},
        {kKickTune, "Kick Tune", 30.0f, 90.0f, 55.0f, "Hz"},
        {kKickDecay, "Kick Decay", 0.05f, 1.2f, 0.32f, "s"},
        {kKickAttack, "Kick Attack", 0.0f, 1.0f, 0.5f, ""},
        {kSnareLevel, "Snare Level", 0.0f, 1.0f, 0.8f, ""},
        {kSnareTune, "Snare Tune", 120.0f, 320.0f, 190.0f, "Hz"},
        {kSnareDecay, "Snare Decay", 0.05f, 0.6f, 0.18f, "s"},
        {kSnareSnappy, "Snare Snappy", 0.0f, 1.0f, 0.78f, ""},
        {kClosedHatLevel, "Closed Hat Level", 0.0f, 1.0f, 0.7f, ""},
        {kClosedHatDecay, "Closed Hat Decay", 0.02f, 0.2f, 0.06f, "s"},
        {kOpenHatLevel, "Open Hat Level", 0.0f, 1.0f, 0.7f, ""},
        {kOpenHatDecay, "Open Hat Decay", 0.1f, 1.0f, 0.4f, "s"},
        {kClapLevel, "Clap Level", 0.0f, 1.0f, 0.8f, ""},
        {kClapDecay, "Clap Decay", 0.05f, 0.5f, 0.2f, "s"},
        {kCrashLevel, "Crash Level", 0.0f, 1.0f, 0.6f, ""},
        {kCrashDecay, "Crash Decay", 0.3f, 3.0f, 1.4f, "s"},
        {kTomLevel, "Tom Level", 0.0f, 1.0f, 0.8f, ""},
        {kTomTune, "Tom Tune", 60.0f, 200.0f, 100.0f, "Hz"},
        {kTomDecay, "Tom Decay", 0.1f, 1.0f, 0.4f, "s"},
        {kAccent, "Accent", 0.0f, 1.0f, 0.5f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void TR909Synth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    kick_.setSampleRate(sampleRate);
    snare_.setSampleRate(sampleRate);
    closedHat_.setSampleRate(sampleRate);
    openHat_.setSampleRate(sampleRate);
    clap_.setSampleRate(sampleRate);
    crash_.setSampleRate(sampleRate);
    lowTom_.setSampleRate(sampleRate);
    midTom_.setSampleRate(sampleRate);
    hiTom_.setSampleRate(sampleRate);
    applyConfig();
}

void TR909Synth::applyConfig() {
    kick_.configure(params_[kKickTune].load(std::memory_order_relaxed),
                    params_[kKickDecay].load(std::memory_order_relaxed),
                    params_[kKickAttack].load(std::memory_order_relaxed),
                    params_[kKickLevel].load(std::memory_order_relaxed));
    snare_.configure(params_[kSnareTune].load(std::memory_order_relaxed),
                     params_[kSnareDecay].load(std::memory_order_relaxed),
                     params_[kSnareLevel].load(std::memory_order_relaxed),
                     params_[kSnareSnappy].load(std::memory_order_relaxed));
    closedHat_.configure(params_[kClosedHatDecay].load(std::memory_order_relaxed),
                         params_[kClosedHatLevel].load(std::memory_order_relaxed), 8500.0f);
    openHat_.configure(params_[kOpenHatDecay].load(std::memory_order_relaxed),
                       params_[kOpenHatLevel].load(std::memory_order_relaxed), 7000.0f);
    clap_.configure(params_[kClapDecay].load(std::memory_order_relaxed),
                    params_[kClapLevel].load(std::memory_order_relaxed));
    crash_.configure(params_[kCrashDecay].load(std::memory_order_relaxed),
                     params_[kCrashLevel].load(std::memory_order_relaxed));

    const float tomTune = params_[kTomTune].load(std::memory_order_relaxed);
    const float tomDecay = params_[kTomDecay].load(std::memory_order_relaxed);
    const float tomLevel = params_[kTomLevel].load(std::memory_order_relaxed);
    lowTom_.configure(tomTune * 0.75f, tomDecay, tomLevel);
    midTom_.configure(tomTune, tomDecay, tomLevel);
    hiTom_.configure(tomTune * 1.4f, tomDecay, tomLevel);
}

void TR909Synth::triggerNote(uint8_t note, uint8_t velocity) {
    const float accent = params_[kAccent].load(std::memory_order_relaxed);
    const float velNorm = static_cast<float>(velocity) / 127.0f;
    const float velGain = (0.35f + 0.65f * velNorm) * (1.0f + accent * 0.5f * velNorm);

    switch (note) {
        case kNoteKick: kick_.trigger(velGain); break;
        case kNoteSnare: snare_.trigger(velGain); break;
        case kNoteClap: clap_.trigger(velGain); break;
        case kNoteClosedHat: closedHat_.trigger(velGain); openHat_.choke(); break;
        case kNoteOpenHat: openHat_.trigger(velGain); break;
        case kNoteCrash: crash_.trigger(velGain); break;
        case kNoteLowTom: lowTom_.trigger(velGain); break;
        case kNoteMidTom: midTom_.trigger(velGain); break;
        case kNoteHiTom: hiTom_.trigger(velGain); break;
        default: break;
    }
}

void TR909Synth::process(const MidiNoteEvent* events, int numEvents,
                         float* outputL, float* outputR, int numSamples) {
    applyConfig();

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i) {
            const auto& ev = events[eventIndex];
            if (ev.kind == MidiNoteEvent::Kind::NoteOn && ev.velocity > 0)
                triggerNote(ev.note, ev.velocity);
            ++eventIndex;
        }

        float sum = kick_.render() + snare_.render()
                  + closedHat_.render() + openHat_.render()
                  + clap_.render() + crash_.render()
                  + lowTom_.render() + midTom_.render() + hiTom_.render();
        sum *= 0.45f;

        outputL[i] = sum;
        outputR[i] = sum;
    }
}

int TR909Synth::activeVoiceCount() const {
    return (kick_.isActive() ? 1 : 0) + (snare_.isActive() ? 1 : 0)
         + (closedHat_.isActive() ? 1 : 0) + (openHat_.isActive() ? 1 : 0)
         + (clap_.isActive() ? 1 : 0) + (crash_.isActive() ? 1 : 0)
         + (lowTom_.isActive() ? 1 : 0) + (midTom_.isActive() ? 1 : 0)
         + (hiTom_.isActive() ? 1 : 0);
}

void TR909Synth::setParameter(ParamId id, float value) {
    if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}

float TR909Synth::getParameter(ParamId id) const {
    return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

const ParameterList& TR909Synth::parameterList() const { return parameterList_; }

PresetState TR909Synth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.tr909";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void TR909Synth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.tr909", "TR-909-style Drum Machine", TR909Synth);

} // namespace vsm::plugins::tr909
