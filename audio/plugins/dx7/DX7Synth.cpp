#include "DX7Synth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace vsm::plugins::dx7 {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace { constexpr uint64_t kBaseSeed = 0x4458372D464D2121ULL; } // "DX7-FM!!"

DX7Synth::DX7Synth() {
    parameterList_ = {
        {kAlgorithm, "Algorithm", 1.0f, 8.0f, 1.0f, ""},
        {kFeedback, "Feedback", 0.0f, 1.0f, 0.0f, ""},
        {kLfoRate, "LFO Rate", 0.05f, 20.0f, 5.0f, "Hz"},
        {kLfoToPitch, "LFO to Pitch", 0.0f, 1.0f, 0.0f, ""},
        {kVelocitySens, "Velocity Sens", 0.0f, 1.0f, 0.4f, ""},
        {kPitchEnvAmount, "Pitch Env Amount", -24.0f, 24.0f, 0.0f, "st"},
        {kPitchEnvTime, "Pitch Env Time", 0.002f, 4.0f, 0.3f, "s"},
        {kKeyLevelScaling, "Key Level Scaling", 0.0f, 1.0f, 0.0f, ""},
        {kAnalogCharacter, "Analog Character", 0.0f, 1.0f, 0.15f, ""},
    };
    // Réglages par défaut par opérateur : op1 (0) porteuse pleine, les autres
    // modulateurs à niveau modéré, ratios harmoniques croissants.
    static const float defaultLevels[kNumOperators] = {1.0f, 0.6f, 0.5f, 0.7f, 0.4f, 0.5f};
    static const float defaultRatios[kNumOperators] = {1.0f, 1.0f, 2.0f, 1.0f, 3.0f, 1.0f};
    for (int op = 0; op < kNumOperators; ++op) {
        const std::string n = "Op" + std::to_string(op + 1) + " ";
        const size_t s = static_cast<size_t>(op);
        parameterList_.push_back({opParam(op, kOpRatio), n + "Ratio", 0.5f, 16.0f, defaultRatios[s], ""});
        parameterList_.push_back({opParam(op, kOpLevel), n + "Level", 0.0f, 1.0f, defaultLevels[s], ""});
        parameterList_.push_back({opParam(op, kOpAttack), n + "Attack", 0.001f, 4.0f, 0.005f, "s"});
        parameterList_.push_back({opParam(op, kOpDecay), n + "Decay", 0.001f, 8.0f, 0.6f, "s"});
        parameterList_.push_back({opParam(op, kOpSustain), n + "Sustain", 0.0f, 1.0f, 0.7f, ""});
        parameterList_.push_back({opParam(op, kOpRelease), n + "Release", 0.001f, 8.0f, 0.4f, "s"});
        parameterList_.push_back({opParam(op, kOpFixed), n + "Fixed", 0.0f, 1.0f, 0.0f, ""});
    }
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void DX7Synth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t idx = 0;
    voiceManager_.forEachVoice([&](DX7Voice& v) {
        v.prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, idx));
        ++idx;
    });
    lfoPhase_ = 0.0;
}

void DX7Synth::applyNoteEvent(const MidiNoteEvent& ev) {
    if (ev.kind == MidiNoteEvent::Kind::NoteOn && ev.velocity > 0)
        voiceManager_.noteOn(ev.channel, ev.note, ev.velocity);
    else
        voiceManager_.noteOff(ev.channel, ev.note, ev.velocity);
}

void DX7Synth::process(const MidiNoteEvent* events, int numEvents,
                       float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    const auto& table = algorithmTable();
    const int algoIndex = std::clamp(
        static_cast<int>(std::lround(params_[kAlgorithm].load(std::memory_order_relaxed))) - 1,
        0, static_cast<int>(table.size()) - 1);
    const Algorithm& algo = table[static_cast<size_t>(algoIndex)];
    const float feedback = params_[kFeedback].load(std::memory_order_relaxed);
    const float velSens = params_[kVelocitySens].load(std::memory_order_relaxed);
    const float analogCharacter = params_[kAnalogCharacter].load(std::memory_order_relaxed);
    const float lfoToPitch = params_[kLfoToPitch].load(std::memory_order_relaxed);
    const float pitchEnvAmount = params_[kPitchEnvAmount].load(std::memory_order_relaxed);
    const float pitchEnvTime = params_[kPitchEnvTime].load(std::memory_order_relaxed);
    const float keyLevelScaling = params_[kKeyLevelScaling].load(std::memory_order_relaxed);

    std::array<float, kNumOperators> levels{}, ratios{}, fixedHz{};
    std::array<bool, kNumOperators> fixedModes{};
    std::array<AdsrSettings, kNumOperators> envs{};
    for (int op = 0; op < kNumOperators; ++op) {
        const size_t s = static_cast<size_t>(op);
        ratios[s] = params_[opParam(op, kOpRatio)].load(std::memory_order_relaxed);
        levels[s] = params_[opParam(op, kOpLevel)].load(std::memory_order_relaxed);
        fixedModes[s] = params_[opParam(op, kOpFixed)].load(std::memory_order_relaxed) >= 0.5f;
        // §11 fréquence fixe : quand activée, l'opérateur ne suit plus la note.
        // La fréquence fixe est dérivée du ratio (ratio x 100 Hz) -> plage
        // ~50 Hz..1.6 kHz, réglable au même knob, documenté (section 27).
        fixedHz[s] = ratios[s] * 100.0f;
        envs[s] = AdsrSettings{
            params_[opParam(op, kOpAttack)].load(std::memory_order_relaxed),
            params_[opParam(op, kOpDecay)].load(std::memory_order_relaxed),
            params_[opParam(op, kOpSustain)].load(std::memory_order_relaxed),
            params_[opParam(op, kOpRelease)].load(std::memory_order_relaxed),
        };
    }

    voiceManager_.forEachVoice([&](DX7Voice& v) {
        v.configure(algo, feedback, velSens, pitchEnvAmount, pitchEnvTime, keyLevelScaling,
                    levels, ratios, fixedModes, fixedHz, envs);
        v.setDriftAmount(analogCharacter);
    });

    const float lfoRate = params_[kLfoRate].load(std::memory_order_relaxed);
    lfoIncrement_ = lfoRate / sampleRate_;

    const float bendSemis = bendSemitones_.load(std::memory_order_relaxed);
    const float wheelSemis = std::min(1.0f, modWheel_.load(std::memory_order_relaxed)
                                     + pressure_.load(std::memory_order_relaxed)) * kWheelVibratoSemitones;
    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i) {
            applyNoteEvent(events[eventIndex]);
            ++eventIndex;
        }

        const float lfo = std::sin(static_cast<float>(lfoPhase_ * kTwoPi));
        // Molettes comprises : la voix somme déjà ses hauteurs en demi-tons,
        // et à molettes nulles les additions sont exactes (empreinte
        // inchangée au bit). Le CC 1 dose un vibrato au même LFO.
        const float lfoPitchSemis = lfo * lfoToPitch * 2.0f
                                  + lfo * wheelSemis + bendSemis;

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](DX7Voice& v) { sum += v.render(lfoPitchSemis); });
        sum *= 0.5f;

        outputL[i] = sum;
        outputR[i] = sum;

        lfoPhase_ += lfoIncrement_;
        if (lfoPhase_ >= 1.0) lfoPhase_ -= 1.0;
    }
}

bool DX7Synth::handleControlEvent(const MidiControlEvent& event) {
    // Molette de hauteur et molette de modulation (CC 1) ; le reste est
    // refusé en le disant -- le moteur compte le refus.
    if (event.kind == MidiControlEvent::Kind::PitchBend) {
        bendSemitones_.store(event.value, std::memory_order_relaxed);
        return true;
    }
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

void DX7Synth::setParameter(ParamId id, float value) {
    if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}
float DX7Synth::getParameter(ParamId id) const {
    return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}
const ParameterList& DX7Synth::parameterList() const { return parameterList_; }

PresetState DX7Synth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.dx7";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}
void DX7Synth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.dx7", "DX7-style FM Synthesis", DX7Synth);

} // namespace vsm::plugins::dx7
