#include "Jupiter8Synth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>

namespace vsm::plugins::jupiter8 {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace { constexpr uint64_t kBaseSeed = 0x4A757069746572ULL; } // "Jupiter"

Jupiter8Synth::Jupiter8Synth() {
    parameterList_ = {
        {kVco1Level, "VCO-1 Level", 0.0f, 1.0f, 0.8f, ""},
        {kVco1Shape, "VCO-1 Shape", 0.0f, 2.0f, 0.0f, ""},
        {kVco1PulseWidth, "VCO-1 Pulse Width", 0.05f, 0.95f, 0.5f, ""},
        {kVco2Level, "VCO-2 Level", 0.0f, 1.0f, 0.6f, ""},
        {kVco2Shape, "VCO-2 Shape", 0.0f, 2.0f, 0.0f, ""},
        {kVco2PulseWidth, "VCO-2 Pulse Width", 0.05f, 0.95f, 0.5f, ""},
        {kVco2Detune, "VCO-2 Detune", -12.0f, 12.0f, 0.1f, "st"},
        {kCrossMod, "Cross Mod", 0.0f, 1.0f, 0.0f, ""},
        {kSync, "Sync", 0.0f, 1.0f, 0.0f, ""},
        {kHpfCutoff, "HPF Cutoff", 20.0f, 2000.0f, 20.0f, "Hz"},
        {kFilterCutoff, "Filter Cutoff", 20.0f, 20000.0f, 1600.0f, "Hz"},
        {kFilterResonance, "Filter Resonance", 0.0f, 4.2f, 0.3f, ""},
        {kFilterEnvAmount, "Filter Env Amount", -1.0f, 1.0f, 0.5f, ""},
        {kFilterKeyTrack, "Filter Key Track", 0.0f, 1.0f, 0.3f, ""},
        {kEnv1Attack, "Env 1 Attack", 0.001f, 4.0f, 0.02f, "s"},
        {kEnv1Decay, "Env 1 Decay", 0.001f, 8.0f, 0.5f, "s"},
        {kEnv1Sustain, "Env 1 Sustain", 0.0f, 1.0f, 0.4f, ""},
        {kEnv1Release, "Env 1 Release", 0.001f, 8.0f, 0.4f, "s"},
        {kEnv2Attack, "Env 2 Attack", 0.001f, 4.0f, 0.01f, "s"},
        {kEnv2Decay, "Env 2 Decay", 0.001f, 8.0f, 0.6f, "s"},
        {kEnv2Sustain, "Env 2 Sustain", 0.0f, 1.0f, 0.8f, ""},
        {kEnv2Release, "Env 2 Release", 0.001f, 8.0f, 0.5f, "s"},
        {kLfoRate, "LFO Rate", 0.05f, 20.0f, 4.0f, "Hz"},
        {kLfoToPitch, "LFO to Pitch", 0.0f, 1.0f, 0.0f, ""},
        {kLfoToFilter, "LFO to Filter", 0.0f, 1.0f, 0.0f, ""},
        {kLfoToPwm, "LFO to PWM", 0.0f, 1.0f, 0.0f, ""},
        {kChorusMode, "Chorus Mode", 0.0f, 2.0f, 1.0f, ""},
        {kAnalogCharacter, "Analog Character", 0.0f, 1.0f, 0.3f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void Jupiter8Synth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t idx = 0;
    voiceManager_.forEachVoice([&](Jupiter8Voice& v) {
        v.prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, idx));
        ++idx;
    });
    for (auto& filter : voiceFilters_) {
        filter.setSampleRate(sampleRate);
        filter.setPoleCount(4);
        for (size_t lane = 0; lane < vsm::audio::dsp::LadderFilterZDFx4::kLanes; ++lane)
            filter.setDrive(lane, 1.0f);
        filter.reset();
    }
    chorus_.setSampleRate(sampleRate);
    chorus_.setBaseDelayMs(7.5f);
    chorus_.setMix(0.5f);
    lfoPhase_ = 0.0;
}

void Jupiter8Synth::applyNoteEvent(const MidiNoteEvent& ev) {
    if (ev.kind == MidiNoteEvent::Kind::NoteOn && ev.velocity > 0)
        voiceManager_.noteOn(ev.channel, ev.note, ev.velocity);
    else
        voiceManager_.noteOff(ev.channel, ev.note, ev.velocity);
}

void Jupiter8Synth::process(const MidiNoteEvent* events, int numEvents,
                            float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    Jupiter8Params p;
    p.vco1Level = params_[kVco1Level].load(std::memory_order_relaxed);
    p.vco1Shape = static_cast<int>(std::lround(params_[kVco1Shape].load(std::memory_order_relaxed)));
    p.vco1Pw = params_[kVco1PulseWidth].load(std::memory_order_relaxed);
    p.vco2Level = params_[kVco2Level].load(std::memory_order_relaxed);
    p.vco2Shape = static_cast<int>(std::lround(params_[kVco2Shape].load(std::memory_order_relaxed)));
    p.vco2Pw = params_[kVco2PulseWidth].load(std::memory_order_relaxed);
    p.vco2Detune = params_[kVco2Detune].load(std::memory_order_relaxed);
    p.crossMod = params_[kCrossMod].load(std::memory_order_relaxed);
    p.sync = params_[kSync].load(std::memory_order_relaxed) >= 0.5f;
    p.hpfCutoff = params_[kHpfCutoff].load(std::memory_order_relaxed);
    p.cutoffBase = params_[kFilterCutoff].load(std::memory_order_relaxed);
    p.resonance = params_[kFilterResonance].load(std::memory_order_relaxed);
    p.filterEnvAmount = params_[kFilterEnvAmount].load(std::memory_order_relaxed);
    p.keyTrack = params_[kFilterKeyTrack].load(std::memory_order_relaxed);
    p.lfoToPitch = params_[kLfoToPitch].load(std::memory_order_relaxed);
    p.lfoToFilter = params_[kLfoToFilter].load(std::memory_order_relaxed);
    p.lfoToPwm = params_[kLfoToPwm].load(std::memory_order_relaxed);
    p.analogCharacter = params_[kAnalogCharacter].load(std::memory_order_relaxed);

    const AdsrSettings ampAdsr{
        params_[kEnv2Attack].load(std::memory_order_relaxed),
        params_[kEnv2Decay].load(std::memory_order_relaxed),
        params_[kEnv2Sustain].load(std::memory_order_relaxed),
        params_[kEnv2Release].load(std::memory_order_relaxed),
    };
    const AdsrSettings filterAdsr{
        params_[kEnv1Attack].load(std::memory_order_relaxed),
        params_[kEnv1Decay].load(std::memory_order_relaxed),
        params_[kEnv1Sustain].load(std::memory_order_relaxed),
        params_[kEnv1Release].load(std::memory_order_relaxed),
    };
    voiceManager_.forEachVoice([&](Jupiter8Voice& v) {
        v.setSettings(ampAdsr, filterAdsr);
        v.setDriftAmount(p.analogCharacter);
    });

    chorusMode_ = static_cast<int>(std::lround(params_[kChorusMode].load(std::memory_order_relaxed)));
    // Mode I : doux et lent ; mode II : plus profond et rapide (approximation
    // continue des modes commutés du hardware, voir Chorus.h §27).
    chorus_.setRateHz(chorusMode_ >= 2 ? 0.83f : 0.5f);
    chorus_.setDepthMs(chorusMode_ >= 2 ? 3.6f : 2.7f);

    const float lfoRate = params_[kLfoRate].load(std::memory_order_relaxed);
    lfoIncrement_ = lfoRate / sampleRate_;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i) {
            applyNoteEvent(events[eventIndex]);
            ++eventIndex;
        }

        const float lfo = 4.0f * std::abs(static_cast<float>(lfoPhase_) - 0.5f) - 1.0f; // triangle

        // Les voix sont rendues par GROUPES de quatre, pour que leurs quatre
        // VCF tiennent dans un seul filtre vectorisé (voir Jupiter8Voice :
        // renderPreFilter / applyFilterOutput, et SimdFloat4.h pour le
        // pourquoi). Le résultat est le même qu'en scalaire, à l'arrondi près.
        constexpr size_t kLanes = LadderFilterZDFx4::kLanes;
        float sum = 0.0f;
        for (size_t group = 0; group < kVoiceGroups; ++group) {
            float preFilter[kLanes];
            float cutoffs[kLanes];
            for (size_t lane = 0; lane < kLanes; ++lane) {
                Jupiter8Voice& voice = voiceManager_.voiceAt(group * kLanes + lane);
                preFilter[lane] = voice.renderPreFilter(p, lfo);
                cutoffs[lane] = voice.pendingCutoffHz();
            }

            auto& filter = voiceFilters_[group];
            filter.setCutoffsHz(cutoffs);
            for (size_t lane = 0; lane < kLanes; ++lane) filter.setResonance(lane, p.resonance);

            const SimdFloat4 filtered = filter.process(SimdFloat4::load(preFilter));
            for (size_t lane = 0; lane < kLanes; ++lane)
                sum += voiceManager_.voiceAt(group * kLanes + lane)
                           .applyFilterOutput(filtered.lane(lane));
        }
        sum *= 0.28f;

        if (chorusMode_ <= 0) {
            outputL[i] = sum;
            outputR[i] = sum;
        } else {
            float l = sum, r = sum;
            chorus_.process(sum, l, r);
            outputL[i] = l;
            outputR[i] = r;
        }

        lfoPhase_ += lfoIncrement_;
        if (lfoPhase_ >= 1.0) lfoPhase_ -= 1.0;
    }
}

void Jupiter8Synth::setParameter(ParamId id, float value) {
    if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}
float Jupiter8Synth::getParameter(ParamId id) const {
    return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}
const ParameterList& Jupiter8Synth::parameterList() const { return parameterList_; }

PresetState Jupiter8Synth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.jupiter8";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}
void Jupiter8Synth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.jupiter8", "Jupiter-8-style Polysynth", Jupiter8Synth);

} // namespace vsm::plugins::jupiter8
