#include "TR808Synth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>

namespace vsm::plugins::tr808 {

using namespace vsm::audio::plugin;

TR808Synth::TR808Synth() {
    parameterList_ = {
        {kKickLevel, "Kick Level", 0.0f, 1.0f, 0.9f, ""},
        {kKickTune, "Kick Tune", 30.0f, 90.0f, 52.0f, "Hz"},
        {kKickDecay, "Kick Decay", 0.05f, 1.5f, 0.45f, "s"},
        {kSnareLevel, "Snare Level", 0.0f, 1.0f, 0.8f, ""},
        {kSnareTune, "Snare Tune", 120.0f, 300.0f, 180.0f, "Hz"},
        {kSnareDecay, "Snare Decay", 0.05f, 0.6f, 0.2f, "s"},
        {kSnareSnappy, "Snare Snappy", 0.0f, 1.0f, 0.6f, ""},
        {kClosedHatLevel, "Closed Hat Level", 0.0f, 1.0f, 0.7f, ""},
        {kClosedHatDecay, "Closed Hat Decay", 0.02f, 0.2f, 0.05f, "s"},
        {kOpenHatLevel, "Open Hat Level", 0.0f, 1.0f, 0.7f, ""},
        {kOpenHatDecay, "Open Hat Decay", 0.1f, 1.0f, 0.4f, "s"},
        {kClapLevel, "Clap Level", 0.0f, 1.0f, 0.8f, ""},
        {kClapDecay, "Clap Decay", 0.05f, 0.5f, 0.2f, "s"},
        {kCowbellLevel, "Cowbell Level", 0.0f, 1.0f, 0.6f, ""},
        {kCowbellTune, "Cowbell Tune", 400.0f, 800.0f, 540.0f, "Hz"},
        {kAccent, "Accent", 0.0f, 1.0f, 0.5f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void TR808Synth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    kick_.setSampleRate(sampleRate);
    snare_.setSampleRate(sampleRate);
    closedHat_.setSampleRate(sampleRate);
    openHat_.setSampleRate(sampleRate);
    clap_.setSampleRate(sampleRate);
    cowbell_.setSampleRate(sampleRate);
    applyConfig();
}

void TR808Synth::applyConfig() {
    kick_.configure(params_[kKickTune].load(std::memory_order_relaxed),
                    params_[kKickDecay].load(std::memory_order_relaxed),
                    params_[kKickLevel].load(std::memory_order_relaxed));
    snare_.configure(params_[kSnareTune].load(std::memory_order_relaxed),
                     params_[kSnareDecay].load(std::memory_order_relaxed),
                     params_[kSnareLevel].load(std::memory_order_relaxed),
                     params_[kSnareSnappy].load(std::memory_order_relaxed));
    closedHat_.configure(params_[kClosedHatDecay].load(std::memory_order_relaxed),
                         params_[kClosedHatLevel].load(std::memory_order_relaxed), 7000.0f);
    openHat_.configure(params_[kOpenHatDecay].load(std::memory_order_relaxed),
                       params_[kOpenHatLevel].load(std::memory_order_relaxed), 5000.0f);
    clap_.configure(params_[kClapDecay].load(std::memory_order_relaxed),
                    params_[kClapLevel].load(std::memory_order_relaxed));
    cowbell_.configure(params_[kCowbellTune].load(std::memory_order_relaxed), 0.4f,
                       params_[kCowbellLevel].load(std::memory_order_relaxed));
}

void TR808Synth::triggerNote(uint8_t note, uint8_t velocity) {
    // Vélocité -> gain, plus une touche d'accent global (comportement 808).
    const float accent = params_[kAccent].load(std::memory_order_relaxed);
    const float velNorm = static_cast<float>(velocity) / 127.0f;
    const float velGain = (0.35f + 0.65f * velNorm) * (1.0f + accent * 0.5f * velNorm);

    switch (note) {
        case kNoteKick: kick_.trigger(velGain); break;
        case kNoteSnare: snare_.trigger(velGain); break;
        case kNoteClap: clap_.trigger(velGain); break;
        case kNoteClosedHat:
            closedHat_.trigger(velGain);
            openHat_.choke(); // le charleston fermé coupe l'ouvert (choke group)
            break;
        case kNoteOpenHat: openHat_.trigger(velGain); break;
        case kNoteCowbell: cowbell_.trigger(velGain); break;
        default: break; // note non mappée -> ignorée
    }
}

void TR808Synth::process(const MidiNoteEvent* events, int numEvents,
                         float* outputL, float* outputR, int numSamples) {
    applyConfig(); // relit les paramètres une fois par bloc

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
                  + clap_.render() + cowbell_.render();
        sum *= 0.5f; // marge avant clipping quand plusieurs pièces sonnent

        outputL[i] = sum;
        outputR[i] = sum;
    }
}

int TR808Synth::activeVoiceCount() const {
    return (kick_.isActive() ? 1 : 0) + (snare_.isActive() ? 1 : 0)
         + (closedHat_.isActive() ? 1 : 0) + (openHat_.isActive() ? 1 : 0)
         + (clap_.isActive() ? 1 : 0) + (cowbell_.isActive() ? 1 : 0);
}

void TR808Synth::setParameter(ParamId id, float value) {
    if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}

float TR808Synth::getParameter(ParamId id) const {
    return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

const ParameterList& TR808Synth::parameterList() const { return parameterList_; }

PresetState TR808Synth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.tr808";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void TR808Synth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.tr808", "TR-808-style Drum Machine", TR808Synth);

} // namespace vsm::plugins::tr808
