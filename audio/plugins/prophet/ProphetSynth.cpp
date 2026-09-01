#include "ProphetSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>

namespace vsm::plugins::prophet {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace { constexpr uint64_t kBaseSeed = 0x50726F7068657435ULL; } // "Prophet5"

ProphetSynth::ProphetSynth() {
    parameterList_ = {
        {kOscALevel, "Osc A Level", 0.0f, 1.0f, 0.8f, ""},
        {kOscAShape, "Osc A Shape", 0.0f, 1.0f, 0.0f, ""},
        {kOscAPulseWidth, "Osc A Pulse Width", 0.05f, 0.95f, 0.5f, ""},
        {kOscBLevel, "Osc B Level", 0.0f, 1.0f, 0.5f, ""},
        {kOscBShape, "Osc B Shape", 0.0f, 2.0f, 0.0f, ""},
        {kOscBPulseWidth, "Osc B Pulse Width", 0.05f, 0.95f, 0.5f, ""},
        {kOscBDetune, "Osc B Detune", -12.0f, 12.0f, 0.1f, "st"},
        {kSync, "Sync", 0.0f, 1.0f, 0.0f, ""},
        {kFilterCutoff, "Filter Cutoff", 20.0f, 20000.0f, 1400.0f, "Hz"},
        {kFilterResonance, "Filter Resonance", 0.0f, 4.2f, 0.25f, ""},
        {kFilterEnvAmount, "Filter Env Amount", -1.0f, 1.0f, 0.5f, ""},
        {kFilterKeyTrack, "Filter Key Track", 0.0f, 1.0f, 0.3f, ""},
        {kFilterAttack, "Filter Attack", 0.001f, 4.0f, 0.02f, "s"},
        {kFilterDecay, "Filter Decay", 0.001f, 8.0f, 0.5f, "s"},
        {kFilterSustain, "Filter Sustain", 0.0f, 1.0f, 0.4f, ""},
        {kFilterRelease, "Filter Release", 0.001f, 8.0f, 0.4f, "s"},
        {kAmpAttack, "Amp Attack", 0.001f, 4.0f, 0.01f, "s"},
        {kAmpDecay, "Amp Decay", 0.001f, 8.0f, 0.6f, "s"},
        {kAmpSustain, "Amp Sustain", 0.0f, 1.0f, 0.8f, ""},
        {kAmpRelease, "Amp Release", 0.001f, 8.0f, 0.4f, "s"},
        {kLfoRate, "LFO Rate", 0.05f, 20.0f, 4.0f, "Hz"},
        {kLfoToPitch, "LFO to Pitch", 0.0f, 1.0f, 0.0f, ""},
        {kLfoToFilter, "LFO to Filter", 0.0f, 1.0f, 0.0f, ""},
        {kPolyModFiltEnvAmount, "PolyMod Filt Env", 0.0f, 1.0f, 0.0f, ""},
        {kPolyModOscBAmount, "PolyMod Osc B", 0.0f, 1.0f, 0.0f, ""},
        {kPolyModToFreqA, "PolyMod to Freq A", 0.0f, 1.0f, 0.0f, ""},
        {kPolyModToPwA, "PolyMod to PW A", 0.0f, 1.0f, 0.0f, ""},
        {kPolyModToFilter, "PolyMod to Filter", 0.0f, 1.0f, 0.0f, ""},
        {kAnalogCharacter, "Analog Character", 0.0f, 1.0f, 0.3f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void ProphetSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t idx = 0;
    voiceManager_.forEachVoice([&](ProphetVoice& v) {
        v.prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, idx));
        ++idx;
    });
    for (auto& filter : voiceFilters_) {
        filter.setSampleRate(sampleRate);
        filter.setPoleCount(4);
        for (size_t lane = 0; lane < kLanes; ++lane) filter.setDrive(lane, 1.0f);
        filter.reset();
    }
    lfoPhase_ = 0.0;
}

void ProphetSynth::applyNoteEvent(const MidiNoteEvent& ev) {
    if (ev.kind == MidiNoteEvent::Kind::NoteOn && ev.velocity > 0)
        voiceManager_.noteOn(ev.channel, ev.note, ev.velocity);
    else
        voiceManager_.noteOff(ev.channel, ev.note, ev.velocity);
}

void ProphetSynth::process(const MidiNoteEvent* events, int numEvents,
                           float* outputL, float* outputR, int numSamples) {
    ProphetParams p;
    p.oscALevel = params_[kOscALevel].load(std::memory_order_relaxed);
    p.oscAShape = static_cast<int>(std::lround(params_[kOscAShape].load(std::memory_order_relaxed)));
    p.oscAPw = params_[kOscAPulseWidth].load(std::memory_order_relaxed);
    p.oscBLevel = params_[kOscBLevel].load(std::memory_order_relaxed);
    p.oscBShape = static_cast<int>(std::lround(params_[kOscBShape].load(std::memory_order_relaxed)));
    p.oscBPw = params_[kOscBPulseWidth].load(std::memory_order_relaxed);
    p.oscBDetune = params_[kOscBDetune].load(std::memory_order_relaxed);
    p.sync = params_[kSync].load(std::memory_order_relaxed) >= 0.5f;
    p.cutoffBase = params_[kFilterCutoff].load(std::memory_order_relaxed);
    p.resonance = params_[kFilterResonance].load(std::memory_order_relaxed);
    p.filterEnvAmount = params_[kFilterEnvAmount].load(std::memory_order_relaxed);
    p.keyTrack = params_[kFilterKeyTrack].load(std::memory_order_relaxed);
    p.lfoToPitch = params_[kLfoToPitch].load(std::memory_order_relaxed);
    p.lfoToFilter = params_[kLfoToFilter].load(std::memory_order_relaxed);
    p.polyFiltEnvAmt = params_[kPolyModFiltEnvAmount].load(std::memory_order_relaxed);
    p.polyOscBAmt = params_[kPolyModOscBAmount].load(std::memory_order_relaxed);
    p.polyToFreqA = params_[kPolyModToFreqA].load(std::memory_order_relaxed) >= 0.5f;
    p.polyToPwA = params_[kPolyModToPwA].load(std::memory_order_relaxed) >= 0.5f;
    p.polyToFilter = params_[kPolyModToFilter].load(std::memory_order_relaxed) >= 0.5f;
    p.analogCharacter = params_[kAnalogCharacter].load(std::memory_order_relaxed);
    p.bendOctaves = bendSemitones_.load(std::memory_order_relaxed) / 12.0f;
    p.wheelVibratoOct = std::min(1.0f, modWheel_.load(std::memory_order_relaxed)
                                     + pressure_.load(std::memory_order_relaxed))
                      * (kWheelVibratoSemitones / 12.0f);

    const AdsrSettings ampAdsr{
        params_[kAmpAttack].load(std::memory_order_relaxed),
        params_[kAmpDecay].load(std::memory_order_relaxed),
        params_[kAmpSustain].load(std::memory_order_relaxed),
        params_[kAmpRelease].load(std::memory_order_relaxed),
    };
    const AdsrSettings filterAdsr{
        params_[kFilterAttack].load(std::memory_order_relaxed),
        params_[kFilterDecay].load(std::memory_order_relaxed),
        params_[kFilterSustain].load(std::memory_order_relaxed),
        params_[kFilterRelease].load(std::memory_order_relaxed),
    };
    voiceManager_.forEachVoice([&](ProphetVoice& v) {
        v.setSettings(ampAdsr, filterAdsr);
        v.setDriftAmount(p.analogCharacter);
    });

    const float lfoRate = params_[kLfoRate].load(std::memory_order_relaxed);
    lfoIncrement_ = lfoRate / sampleRate_;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i) {
            applyNoteEvent(events[eventIndex]);
            ++eventIndex;
        }

        const float lfo = 4.0f * std::abs(static_cast<float>(lfoPhase_) - 0.5f) - 1.0f; // triangle
        float sum = 0.0f;
        // Voix rendues par groupes de quatre pour partager un filtre vectorisé
        // (voir ProphetVoice::renderPreFilter et SimdFloat4.h).
        for (size_t group = 0; group < kVoiceGroups; ++group) {
            float preFilter[kLanes] = {0.0f, 0.0f, 0.0f, 0.0f};
            float cutoffs[kLanes];
            for (size_t lane = 0; lane < kLanes; ++lane) {
                const size_t voiceIndex = group * kLanes + lane;
                if (voiceIndex >= kMaxVoices) { cutoffs[lane] = p.cutoffBase; continue; }
                ProphetVoice& voice = voiceManager_.voiceAt(voiceIndex);
                preFilter[lane] = voice.renderPreFilter(p, lfo);
                cutoffs[lane] = voice.pendingCutoffHz();
            }

            auto& filter = voiceFilters_[group];
            filter.setCutoffsHz(cutoffs);
            for (size_t lane = 0; lane < kLanes; ++lane) filter.setResonance(lane, p.resonance);

            const vsm::audio::dsp::SimdFloat4 filtered =
                filter.process(vsm::audio::dsp::SimdFloat4::load(preFilter));
            for (size_t lane = 0; lane < kLanes; ++lane) {
                const size_t voiceIndex = group * kLanes + lane;
                if (voiceIndex >= kMaxVoices) continue;
                sum += voiceManager_.voiceAt(voiceIndex).applyFilterOutput(filtered.lane(lane));
            }
        }
        sum *= 0.35f;

        outputL[i] = sum;
        outputR[i] = sum;

        lfoPhase_ += lfoIncrement_;
        if (lfoPhase_ >= 1.0) lfoPhase_ -= 1.0;
    }
}

bool ProphetSynth::handleControlEvent(const MidiControlEvent& event) {
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

void ProphetSynth::setParameter(ParamId id, float value) {
    if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}
float ProphetSynth::getParameter(ParamId id) const {
    return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}
const ParameterList& ProphetSynth::parameterList() const { return parameterList_; }

PresetState ProphetSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.prophet";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}
void ProphetSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.prophet", "Prophet-style Polysynth", ProphetSynth);

} // namespace vsm::plugins::prophet
