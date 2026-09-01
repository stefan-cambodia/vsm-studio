#include "Juno106Synth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>

namespace vsm::plugins::juno106 {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace {
// Graine de base de la dérive/bruit. deriveSeed(base, indexDeVoix) donne à
// chaque voix un flux pseudo-aléatoire distinct MAIS déterministe : deux
// instances fraîches soumises aux mêmes événements produisent un rendu
// bit-identique (voice-to-voice variation reproductible, section 8).
constexpr uint64_t kBaseSeed = 0x006A756E6F313036ULL; // "juno106"
} // namespace

Juno106Synth::Juno106Synth() {
    parameterList_ = {
        {kDcoSawLevel, "DCO Saw Level", 0.0f, 1.0f, 1.0f, ""},
        {kDcoPulseLevel, "DCO Pulse Level", 0.0f, 1.0f, 0.0f, ""},
        {kDcoSubLevel, "DCO Sub Level", 0.0f, 1.0f, 0.0f, ""},
        {kDcoNoiseLevel, "Noise Level", 0.0f, 1.0f, 0.0f, ""},
        {kDcoPulseWidth, "Pulse Width", 0.05f, 0.95f, 0.5f, ""},
        {kPwmLfoAmount, "PWM LFO Amount", 0.0f, 1.0f, 0.0f, ""},
        {kLfoRate, "LFO Rate", 0.05f, 20.0f, 5.0f, "Hz"},
        {kLfoDelay, "LFO Delay", 0.0f, 5.0f, 0.0f, "s"},
        {kLfoPitchAmount, "LFO Pitch Amount", 0.0f, 1.0f, 0.0f, ""},
        {kHpfCutoff, "HPF Cutoff", 20.0f, 2000.0f, 20.0f, "Hz"},
        {kVcfCutoff, "VCF Cutoff", 20.0f, 20000.0f, 1200.0f, "Hz"},
        {kVcfResonance, "VCF Resonance", 0.0f, 4.2f, 0.3f, ""},
        {kVcfEnvAmount, "VCF Env Amount", -1.0f, 1.0f, 0.6f, ""},
        {kVcfLfoAmount, "VCF LFO Amount", 0.0f, 1.0f, 0.0f, ""},
        {kVcfKeyTrack, "VCF Key Track", 0.0f, 1.0f, 0.3f, ""},
        {kEnvAttack, "Env Attack", 0.001f, 4.0f, 0.01f, "s"},
        {kEnvDecay, "Env Decay", 0.001f, 8.0f, 0.4f, "s"},
        {kEnvSustain, "Env Sustain", 0.0f, 1.0f, 0.8f, ""},
        {kEnvRelease, "Env Release", 0.001f, 8.0f, 0.3f, "s"},
        {kChorusMode, "Chorus Mode", 0.0f, 3.0f, 1.0f, ""}, // 0=Off 1=I 2=II 3=I+II
        {kAnalogCharacter, "Analog Character", 0.0f, 1.0f, 0.3f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void Juno106Synth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;

    // Prépare chaque voix avec une graine distincte, dérivée de son index.
    // forEachVoice itère les slots dans l'ordre du tableau -> l'assignation
    // des graines est déterministe d'une exécution à l'autre.
    uint64_t voiceIndex = 0;
    voiceManager_.forEachVoice([&](Juno106Voice& v) {
        v.prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, voiceIndex));
        ++voiceIndex;
    });

    for (auto& filter : voiceFilters_) {
        filter.setSampleRate(sampleRate);
        filter.setPoleCount(4); // 24 dB/oct
        for (size_t lane = 0; lane < kLanes; ++lane) filter.setDrive(lane, 1.0f);
        filter.reset();
    }
    chorus_.setSampleRate(sampleRate);
    chorus_.reset();
    lastChorusMode_ = -1; // force une reconfiguration au premier bloc

    lfoPhase_ = 0.0;
    lfoDelayGain_ = 1.0f;
    lfoDelayElapsed_ = 0;
}

void Juno106Synth::applyChorusMode(int mode) {
    if (mode == lastChorusMode_) return;
    lastChorusMode_ = mode;

    switch (mode) {
        case 1: // Mode I  : modulation lente et douce
            chorus_.setRateHz(0.5f);  chorus_.setDepthMs(2.7f);
            chorus_.setBaseDelayMs(7.5f); chorus_.setMix(0.5f);
            break;
        case 2: // Mode II : plus rapide, plus profond
            chorus_.setRateHz(0.83f); chorus_.setDepthMs(3.6f);
            chorus_.setBaseDelayMs(7.5f); chorus_.setMix(0.5f);
            break;
        case 3: // I+II   : approximation du warble rapide des deux LFO combinés
            chorus_.setRateHz(1.1f);  chorus_.setDepthMs(4.2f);
            chorus_.setBaseDelayMs(8.0f); chorus_.setMix(0.6f);
            break;
        default: // 0 = Off : signal sec, dupliqué en stéréo
            chorus_.setMix(0.0f);
            break;
    }
}

void Juno106Synth::applyNoteEvent(const MidiNoteEvent& ev) {
    if (ev.kind == MidiNoteEvent::Kind::NoteOn) {
        // "Réveil" du LFO (fondu d'entrée) quand le synthé était silencieux.
        if (voiceManager_.activeVoiceCount() == 0) lfoDelayElapsed_ = 0;
        voiceManager_.noteOn(ev.channel, ev.note, ev.velocity);
    } else {
        voiceManager_.noteOff(ev.channel, ev.note, ev.velocity);
    }
}

void Juno106Synth::process(const MidiNoteEvent* events, int numEvents,
                           float* outputL, float* outputR, int numSamples) {
    // --- Paramètres lus une fois par bloc -------------------------------
    Juno106Params p;
    p.sawLevel = params_[kDcoSawLevel].load(std::memory_order_relaxed);
    p.pulseLevel = params_[kDcoPulseLevel].load(std::memory_order_relaxed);
    p.subLevel = params_[kDcoSubLevel].load(std::memory_order_relaxed);
    p.noiseLevel = params_[kDcoNoiseLevel].load(std::memory_order_relaxed);
    p.hpfCutoff = params_[kHpfCutoff].load(std::memory_order_relaxed);
    p.vcfCutoffBase = params_[kVcfCutoff].load(std::memory_order_relaxed);
    p.vcfResonance = params_[kVcfResonance].load(std::memory_order_relaxed);
    p.vcfEnvAmount = params_[kVcfEnvAmount].load(std::memory_order_relaxed);
    p.vcfLfoAmount = params_[kVcfLfoAmount].load(std::memory_order_relaxed);
    p.vcfKeyTrack = params_[kVcfKeyTrack].load(std::memory_order_relaxed);
    p.lfoPitchAmount = params_[kLfoPitchAmount].load(std::memory_order_relaxed);
    p.analogCharacter = params_[kAnalogCharacter].load(std::memory_order_relaxed);
    p.bendSemitones = bendSemitones_.load(std::memory_order_relaxed);
    p.wheelVibratoSemis = std::min(1.0f, modWheel_.load(std::memory_order_relaxed)
                                     + pressure_.load(std::memory_order_relaxed)) * kWheelVibratoSemitones;

    const float pwBase = params_[kDcoPulseWidth].load(std::memory_order_relaxed);
    const float pwmDepth = params_[kPwmLfoAmount].load(std::memory_order_relaxed);
    const float lfoRate = params_[kLfoRate].load(std::memory_order_relaxed);
    const float lfoDelaySeconds = params_[kLfoDelay].load(std::memory_order_relaxed);

    const AdsrSettings ampAdsr{
        params_[kEnvAttack].load(std::memory_order_relaxed),
        params_[kEnvDecay].load(std::memory_order_relaxed),
        params_[kEnvSustain].load(std::memory_order_relaxed),
        params_[kEnvRelease].load(std::memory_order_relaxed),
    };

    voiceManager_.forEachVoice([&](Juno106Voice& v) {
        v.setEnvSettings(ampAdsr);
        v.setDriftAmount(p.analogCharacter);
    });

    applyChorusMode(static_cast<int>(std::lround(
        params_[kChorusMode].load(std::memory_order_relaxed))));

    lfoIncrement_ = lfoRate / sampleRate_;
    lfoDelaySamples_ = static_cast<long>(lfoDelaySeconds * static_cast<float>(sampleRate_));

    // --- Boucle échantillon par échantillon (déclenchement sample-accurate)
    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i) {
            applyNoteEvent(events[eventIndex]);
            ++eventIndex;
        }

        // Fondu d'entrée du LFO ("LFO delay" du panneau).
        if (lfoDelaySamples_ <= 0) {
            lfoDelayGain_ = 1.0f;
        } else {
            lfoDelayGain_ = std::min(1.0f, static_cast<float>(lfoDelayElapsed_) /
                                             static_cast<float>(lfoDelaySamples_));
            ++lfoDelayElapsed_;
        }

        // LFO triangulaire global [-1,1], atténué par le fondu.
        const float lfoTri = 4.0f * std::abs(static_cast<float>(lfoPhase_) - 0.5f) - 1.0f;
        const float lfo = lfoTri * lfoDelayGain_;

        const float pulseWidthNow =
            std::clamp(pwBase + lfo * pwmDepth * 0.45f, 0.05f, 0.95f);

        // Voix rendues par groupes de quatre, pour partager un VCF vectorisé.
        // Les lignes au-delà de la sixième voix (le Juno en a six, la largeur
        // SIMD est de quatre) reçoivent du silence et une coupure valide.
        float sum = 0.0f;
        for (size_t group = 0; group < kVoiceGroups; ++group) {
            float preFilter[kLanes] = {0.0f, 0.0f, 0.0f, 0.0f};
            float cutoffs[kLanes];
            for (size_t lane = 0; lane < kLanes; ++lane) {
                const size_t voiceIndex = group * kLanes + lane;
                if (voiceIndex >= kMaxVoices) { cutoffs[lane] = p.vcfCutoffBase; continue; }
                Juno106Voice& voice = voiceManager_.voiceAt(voiceIndex);
                preFilter[lane] = voice.renderPreFilter(p, lfo, pulseWidthNow);
                cutoffs[lane] = voice.pendingCutoffHz();
            }

            auto& filter = voiceFilters_[group];
            filter.setCutoffsHz(cutoffs);
            for (size_t lane = 0; lane < kLanes; ++lane) filter.setResonance(lane, p.vcfResonance);

            const vsm::audio::dsp::SimdFloat4 filtered =
                filter.process(vsm::audio::dsp::SimdFloat4::load(preFilter));
            for (size_t lane = 0; lane < kLanes; ++lane) {
                const size_t voiceIndex = group * kLanes + lane;
                if (voiceIndex >= kMaxVoices) continue;
                sum += voiceManager_.voiceAt(voiceIndex).applyFilterOutput(filtered.lane(lane));
            }
        }

        float outL = 0.0f, outR = 0.0f;
        chorus_.process(sum, outL, outR);
        outputL[i] = outL;
        outputR[i] = outR;

        lfoPhase_ += lfoIncrement_;
        if (lfoPhase_ >= 1.0) lfoPhase_ -= 1.0;
    }
}

bool Juno106Synth::handleControlEvent(const MidiControlEvent& event) {
    // La molette de hauteur et la molette de modulation (CC 1), comme sur les
    // monophoniques (D0.5) ; le reste est refusé EN LE DISANT -- le moteur
    // compte ce refus, l'interface peut l'expliquer.
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

void Juno106Synth::setParameter(ParamId id, float value) {
    if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}

float Juno106Synth::getParameter(ParamId id) const {
    return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

const ParameterList& Juno106Synth::parameterList() const { return parameterList_; }

PresetState Juno106Synth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.juno106";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void Juno106Synth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.juno106", "Juno-106-style Polysynth", Juno106Synth);

} // namespace vsm::plugins::juno106
