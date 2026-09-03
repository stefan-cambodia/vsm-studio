#include "VibraphoneSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::vibraphone {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

VibraphoneSynth::VibraphoneSynth() {
    parameterList_ = {
        {kUndercut, "Bar Undercut", 0.0f, 1.0f, 1.0f, ""},
        {kDecay, "Bar Decay", 0.5f, 12.0f, 6.0f, "s"},
        {kDecayTilt, "Decay Tilt", 0.0f, 3.0f, 1.2f, ""},
        {kDamperDecay, "Damper Decay", 0.05f, 1.5f, 0.3f, "s"},
        {kHardness, "Mallet Hardness", 0.0f, 1.0f, 0.5f, ""},
        {kStrikeOffset, "Strike Offset", 0.0f, 1.0f, 0.15f, ""},
        {kVelocityToHardness, "Velocity to Hardness", 0.0f, 1.0f, 0.5f, ""},
        {kResonatorMix, "Resonator Mix", 0.0f, 1.0f, 0.7f, ""},
        {kMotorSpeed, "Motor Speed", 0.5f, 12.0f, 4.5f, "Hz"},
        {kMotorDepth, "Motor Depth", 0.0f, 1.0f, 0.8f, ""},
        {kStereoSpread, "Stereo Spread", 0.0f, 1.0f, 0.5f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void VibraphoneSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    voiceManager_.forEachVoice([&](VibraphoneVoice& voice) { voice.prepare(sampleRate_); });
    motorPhase_ = 0.0;
    pedal_.store(false, std::memory_order_relaxed);
}

bool VibraphoneSynth::handleControlEvent(const MidiControlEvent& event) {
    // LA PÉDALE DE SUSTAIN EST LA PÉDALE DU VIBRAPHONE : c'est le même pied
    // et le même geste (soulever le feutre). Honorée.
    if (event.kind == MidiControlEvent::Kind::ControlChange && event.index == 64) {
        pedal_.store(event.value >= 0.5f, std::memory_order_relaxed);
        return true;
    }
    // Une barre frappée n'a pas de molette : refusé en connaissance de
    // cause, le moteur compte le refus (comme le piano).
    return false;
}

void VibraphoneSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void VibraphoneSynth::process(const MidiNoteEvent* events, int numEvents,
                              float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    VibraphoneVoice::Params p;
    p.undercut = params_[kUndercut].load(std::memory_order_relaxed);
    p.decay = params_[kDecay].load(std::memory_order_relaxed);
    p.decayTilt = params_[kDecayTilt].load(std::memory_order_relaxed);
    p.damperDecay = params_[kDamperDecay].load(std::memory_order_relaxed);
    p.hardness = params_[kHardness].load(std::memory_order_relaxed);
    p.strikeOffset = params_[kStrikeOffset].load(std::memory_order_relaxed);
    p.velocityToHardness = params_[kVelocityToHardness].load(std::memory_order_relaxed);
    p.pedalDown = pedal_.load(std::memory_order_relaxed);

    const float resonatorMix = params_[kResonatorMix].load(std::memory_order_relaxed);
    const float motorSpeed = params_[kMotorSpeed].load(std::memory_order_relaxed);
    const float motorDepth = std::clamp(params_[kMotorDepth].load(std::memory_order_relaxed), 0.0f, 1.0f);
    const float spread = std::clamp(params_[kStereoSpread].load(std::memory_order_relaxed), 0.0f, 1.0f);
    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    const double motorIncrement = static_cast<double>(motorSpeed) / sampleRate_;
    constexpr float kVoiceGain = 0.45f;
    constexpr float kTubeGain = 1.6f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        // L'OUVERTURE DES TUBES : un disque qui tourne ferme le tube une
        // fois par tour. Un seul axe pour toutes les barres.
        const float ouverture = 1.0f - motorDepth * (0.5f - 0.5f * static_cast<float>(std::cos(kTwoPi * motorPhase_)));
        motorPhase_ += motorIncrement;
        if (motorPhase_ >= 1.0) motorPhase_ -= 1.0;

        float left = 0.0f, right = 0.0f;
        voiceManager_.forEachVoice([&](VibraphoneVoice& voice) {
            if (!voice.isActive()) return;
            const auto out = voice.render(p);
            const float somme = out.bar + out.tube * resonatorMix * kTubeGain * ouverture;
            // LES BARRES VONT DU GRAVE À GAUCHE À L'AIGU À DROITE, comme
            // devant l'instrument : fa3 (53) à fa6 (89).
            const float position = std::clamp((static_cast<float>(voice.note()) - 53.0f) / 36.0f, 0.0f, 1.0f);
            const float angle = (0.25f + (position - 0.5f) * 0.5f * spread) * static_cast<float>(M_PI);
            left += somme * std::cos(angle);
            right += somme * std::sin(angle);
        });
        outputL[i] = left * kVoiceGain * outputLevel;
        outputR[i] = right * kVoiceGain * outputLevel;
    }
}

void VibraphoneSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float VibraphoneSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState VibraphoneSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.vibraphone";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void VibraphoneSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.vibraphone", "Vibraphone (la barre, le tube et le moteur)", VibraphoneSynth);

} // namespace vsm::plugins::vibraphone
