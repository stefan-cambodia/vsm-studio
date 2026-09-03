#include "KalimbaSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::kalimba {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

KalimbaSynth::KalimbaSynth() {
    parameterList_ = {
        {kTineDecay, "Tine Decay", 0.3f, 8.0f, 2.5f, "s"},
        {kDecayTilt, "Decay Tilt", 0.0f, 3.0f, 1.2f, ""},
        {kHardness, "Thumb Hardness", 0.0f, 1.0f, 0.6f, ""},
        {kBuzz, "Buzz", 0.0f, 1.0f, 0.35f, ""},
        {kBodyResonance, "Body Resonance", 80.0f, 600.0f, 230.0f, "Hz"},
        {kBodyLevel, "Body Level", 0.0f, 1.0f, 0.5f, ""},
        {kHoleCover, "Hole Cover", 0.0f, 1.0f, 0.0f, ""},
        {kVelocitySensitivity, "Velocity Sensitivity", 0.0f, 1.0f, 0.7f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void KalimbaSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    uint64_t graine = 0x4B414C49ULL;
    voiceManager_.forEachVoice([&](KalimbaVoice& voice) { voice.prepare(sampleRate_, graine++); });
    helmholtz_.reset();
    table_.reset();
    couvertLisse_ = params_[kHoleCover].load(std::memory_order_relaxed);
}

bool KalimbaSynth::handleControlEvent(const MidiControlEvent& event) {
    // LES DOIGTS SUR LES TROUS : la molette de modulation et la pression de
    // canal bouchent la caisse — c'est le geste du kalimba, le « wah ».
    if (event.kind == MidiControlEvent::Kind::ControlChange && event.index == 1) {
        molette_.store(std::clamp(event.value, 0.0f, 1.0f), std::memory_order_relaxed);
        return true;
    }
    if (event.kind == MidiControlEvent::Kind::ChannelPressure) {
        pression_.store(std::clamp(event.value, 0.0f, 1.0f), std::memory_order_relaxed);
        return true;
    }
    // Une lame frappée n'a pas de molette de hauteur : refusé en connaissance
    // de cause, le moteur compte le refus.
    return false;
}

void KalimbaSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void KalimbaSynth::process(const MidiNoteEvent* events, int numEvents,
                           float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;
    KalimbaVoice::Params p;
    p.tineDecay = params_[kTineDecay].load(std::memory_order_relaxed);
    p.decayTilt = params_[kDecayTilt].load(std::memory_order_relaxed);
    p.hardness = params_[kHardness].load(std::memory_order_relaxed);
    p.buzz = params_[kBuzz].load(std::memory_order_relaxed);
    p.velocitySensitivity = params_[kVelocitySensitivity].load(std::memory_order_relaxed);
    const float resonance = params_[kBodyResonance].load(std::memory_order_relaxed);
    const float bodyLevel = std::clamp(params_[kBodyLevel].load(std::memory_order_relaxed), 0.0f, 1.0f);
    const float couvertVise = std::clamp(params_[kHoleCover].load(std::memory_order_relaxed)
                                         + molette_.load(std::memory_order_relaxed)
                                         + pression_.load(std::memory_order_relaxed), 0.0f, 1.0f);
    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    constexpr float kVoiceGain = 0.45f;
    // Les doigts bougent en quelques dizaines de millisecondes : la caisse
    // les suit sans à-coup, et ses coefficients se refont à chaque échantillon
    // (un seul filtre, deux modes : ce n'est pas cher).
    const float lissage = 1.0f - std::exp(-1.0f / (0.02f * static_cast<float>(sampleRate_)));

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);
        couvertLisse_ += lissage * (couvertVise - couvertLisse_);
        // BOUCHER DESCEND LA RÉSONANCE (jusqu'à 0,6·f, trous fermés) et la
        // resserre (moins de rayonnement par les trous) ; la table, elle, ne
        // bouge pas.
        const double hz = static_cast<double>(resonance) * (1.0 - 0.4 * static_cast<double>(couvertLisse_));
        helmholtz_.regler(hz, 5.0 + 5.0 * static_cast<double>(couvertLisse_), sampleRate_);
        table_.regler(static_cast<double>(resonance) * 2.4, 4.0, sampleRate_);
        float somme = 0.0f;
        voiceManager_.forEachVoice([&](KalimbaVoice& voice) { somme += voice.render(p); });
        const float caisse = helmholtz_.traiter(somme) + 0.35f * table_.traiter(somme);
        const float out = (somme * (1.0f - 0.5f * bodyLevel) + caisse * bodyLevel * 1.6f) * kVoiceGain * outputLevel;
        outputL[i] = out;
        outputR[i] = out;
    }
}

void KalimbaSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float KalimbaSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState KalimbaSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.kalimba";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void KalimbaSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.kalimba", "Kalimba (la lame encastrée et la caisse qu’on bouche)", KalimbaSynth);

} // namespace vsm::plugins::kalimba
