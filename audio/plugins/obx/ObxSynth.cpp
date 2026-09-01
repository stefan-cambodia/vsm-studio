#include "ObxSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::obx {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace {
constexpr uint64_t kBaseSeed = 0x4F4258535900ULL; // "OBXSY"

Waveform shapeToWave(int shape) {
    switch (shape) {
        case 1: return Waveform::Square;
        case 2: return Waveform::Triangle;
        default: return Waveform::Saw;
    }
}
} // namespace

void ObxVoice::prepare(double sampleRate, uint64_t seed) {
    sampleRate_ = sampleRate;
    osc1_.setSampleRate(sampleRate);
    osc2_.setSampleRate(sampleRate);
    filter_.setSampleRate(sampleRate);
    filter_.setMode(StateVariableFilter::Mode::LowPass);
    filter2_.setSampleRate(sampleRate);
    filter2_.setMode(StateVariableFilter::Mode::LowPass);
    ampEnv_.setSampleRate(sampleRate);
    filterEnv_.setSampleRate(sampleRate);
    drift1_.setSampleRate(sampleRate); drift1_.setSeed(seed);              drift1_.setRateHz(0.11f);
    drift2_.setSampleRate(sampleRate); drift2_.setSeed(seed ^ 0xBEEFULL);  drift2_.setRateHz(0.09f);
}

float ObxVoice::render(const Params& p, float lfo) {
    if (!ampEnv_.isActive()) return 0.0f;

    const float filterEnvLevel = filterEnv_.nextSample();
    const float ampLevel = ampEnv_.nextSample();
    const float pwm = lfo * p.lfoToPulseWidth * 0.4f;

    const float drift1 = drift1_.nextValue() * 0.06f;
    const float drift2 = drift2_.nextValue() * 0.06f;
    // Terme de molette ADDITIF : l'expression d'origine garde son ordre
    // d'association flottant, l'empreinte ne bouge pas à molette nulle.
    const float vibrato = lfo * p.lfoToPitch * 0.5f // demi-tons
                        + lfo * p.wheelVibratoSemis;

    const float freq1 = baseHz_ * std::exp2f(
        (unisonOffset_ + drift1 + vibrato + p.bendSemitones) / 12.0f);
    const float freq2 = baseHz_ * std::exp2f(
        (unisonOffset_ + p.osc2Detune + drift2 + vibrato + p.bendSemitones) / 12.0f);

    osc1_.setFrequency(freq1);
    osc1_.setWaveform(shapeToWave(p.osc1Shape));
    if (p.osc1Shape == 1) osc1_.setPulseWidth(std::clamp(p.osc1PulseWidth + pwm, 0.05f, 0.95f));

    osc2_.setFrequency(freq2);
    osc2_.setWaveform(shapeToWave(p.osc2Shape));
    if (p.osc2Shape == 1) osc2_.setPulseWidth(std::clamp(p.osc2PulseWidth + pwm, 0.05f, 0.95f));

    // Sync : l'oscillateur 2 pilote, l'oscillateur 1 se réinitialise sur son
    // cycle. C'est la configuration de ces machines, et elle donne le
    // balayage criard caractéristique quand on module le désaccord.
    syncPhase_ += freq2 / static_cast<float>(sampleRate_);
    if (syncPhase_ >= 1.0f) { syncPhase_ -= 1.0f; if (p.sync) osc1_.reset(0.0); }

    const float mix = (osc1_.nextSample() * p.osc1Level + osc2_.nextSample() * p.osc2Level) * 0.5f;

    const float velocity = static_cast<float>(velocity_) / 127.0f;
    const float envOctaves = p.envAmount * filterEnvLevel * 5.0f;
    const float lfoOctaves = lfo * p.lfoToFilter * 3.0f;
    const float trackOctaves = p.keyTrack * (static_cast<float>(note_) - 60.0f) / 12.0f;
    const float velocityOctaves = p.velocityToFilter * (velocity - 0.5f) * 2.0f;
    const float cutoff = p.cutoff * std::exp2f(envOctaves + lfoOctaves + trackOctaves + velocityOctaves);

    filter_.setCutoffHz(cutoff);
    // Résonance : le SVF prend un Q, la façade parle en « quantité ». La borne
    // haute reste sous l'auto-oscillation franche -- ces machines chantent,
    // elles ne sifflent pas.
    filter_.setResonance(0.707f + p.resonance * 7.0f);

    float filtered = filter_.process(mix);
    if (p.fourPole) {
        // Second ÉTAGE (instance distincte) : 24 dB/oct pour qui veut le son
        // fermé. La machine d'origine n'a que 12 dB ; l'option évite d'avoir à
        // changer de machine pour un seul réglage.
        filter2_.setCutoffHz(cutoff);
        filter2_.setResonance(0.707f);
        filtered = filter2_.process(filtered);
    }
    return filtered * ampLevel;
}

ObxSynth::ObxSynth() {
    parameterList_ = {
        {kOsc1Level, "Osc1 Level", 0.0f, 1.0f, 0.8f, ""},
        {kOsc1Shape, "Osc1 Shape", 0.0f, 2.0f, 0.0f, ""},
        {kOsc1PulseWidth, "Osc1 Pulse Width", 0.05f, 0.95f, 0.5f, ""},
        {kOsc2Level, "Osc2 Level", 0.0f, 1.0f, 0.5f, ""},
        {kOsc2Shape, "Osc2 Shape", 0.0f, 2.0f, 0.0f, ""},
        {kOsc2PulseWidth, "Osc2 Pulse Width", 0.05f, 0.95f, 0.5f, ""},
        {kOsc2Detune, "Osc2 Detune", -12.0f, 12.0f, 0.08f, "st"},
        {kSync, "Sync", 0.0f, 1.0f, 0.0f, ""},
        {kFilterCutoff, "Filter Cutoff", 20.0f, 18000.0f, 2400.0f, "Hz"},
        {kFilterResonance, "Filter Resonance", 0.0f, 1.0f, 0.2f, ""},
        {kFilterEnvAmount, "Filter Env Amount", -1.0f, 1.0f, 0.5f, ""},
        {kFilterKeyTrack, "Filter Key Track", 0.0f, 1.0f, 0.3f, ""},
        {kFilterSlope, "Filter Slope", 0.0f, 1.0f, 0.0f, ""},
        {kFilterAttack, "Filter Attack", 0.001f, 4.0f, 0.01f, "s"},
        {kFilterDecay, "Filter Decay", 0.001f, 8.0f, 0.5f, "s"},
        {kFilterSustain, "Filter Sustain", 0.0f, 1.0f, 0.3f, ""},
        {kFilterRelease, "Filter Release", 0.001f, 8.0f, 0.4f, "s"},
        {kAmpAttack, "Amp Attack", 0.001f, 4.0f, 0.01f, "s"},
        {kAmpDecay, "Amp Decay", 0.001f, 8.0f, 0.4f, "s"},
        {kAmpSustain, "Amp Sustain", 0.0f, 1.0f, 0.8f, ""},
        {kAmpRelease, "Amp Release", 0.001f, 8.0f, 0.35f, "s"},
        {kLfoRate, "LFO Rate", 0.05f, 20.0f, 4.5f, "Hz"},
        {kLfoWaveform, "LFO Waveform", 0.0f, 2.0f, 0.0f, ""},
        {kLfoToPitch, "LFO to Pitch", 0.0f, 1.0f, 0.0f, ""},
        {kLfoToPulseWidth, "LFO to PWM", 0.0f, 1.0f, 0.0f, ""},
        {kLfoToFilter, "LFO to Filter", 0.0f, 1.0f, 0.0f, ""},
        {kUnison, "Unison", 0.0f, 1.0f, 0.0f, ""},
        {kUnisonDetune, "Unison Detune", 0.0f, 1.0f, 0.25f, ""},
        {kVelocityToFilter, "Velocity to Filter", 0.0f, 1.0f, 0.3f, ""},
        {kAnalogCharacter, "Analog Character", 0.0f, 1.0f, 0.4f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void ObxSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t index = 0;
    voiceManager_.forEachVoice([&](ObxVoice& voice) {
        voice.prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, index));
        ++index;
    });
    lfoPhase_ = 0.0;
}

bool ObxSynth::handleControlEvent(const MidiControlEvent& event) {
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

void ObxSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float ObxSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState ObxSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.obx";
    for (const auto& info : parameterList_) state.parameterValues[info.id] = getParameter(info.id);
    return state;
}

void ObxSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues) setParameter(id, value);
}

void ObxSynth::updateUnisonOffsets(float detune, bool unison) {
    // Les voix sont réparties SYMÉTRIQUEMENT autour de la hauteur jouée : sans
    // cela, l'unisson ferait monter la note perçue quand on ouvre le désaccord.
    size_t index = 0;
    const float span = detune * 0.5f; // demi-tons de part et d'autre
    voiceManager_.forEachVoice([&](ObxVoice& voice) {
        if (!unison) { voice.setUnisonOffset(0.0f); ++index; return; }
        const float position = (static_cast<float>(index) / static_cast<float>(kMaxVoices - 1)) - 0.5f;
        voice.setUnisonOffset(position * 2.0f * span);
        ++index;
    });
}

void ObxSynth::applyNoteEvent(const MidiNoteEvent& event) {
    const bool unison = params_[kUnison].load(std::memory_order_relaxed) >= 0.5f;
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0) {
        if (unison) {
            // Mode unisson : TOUTES les voix jouent la même note. C'est le son
            // de référence de ces machines, pas un effet ajouté.
            voiceManager_.forEachVoice([&](ObxVoice& voice) {
                voice.noteOn(event.channel, event.note, event.velocity);
            });
        } else {
            voiceManager_.noteOn(event.channel, event.note, event.velocity);
        }
    } else {
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
    }
}

void ObxSynth::process(const MidiNoteEvent* events, int numEvents,
                        float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    ObxVoice::Params p;
    p.osc1Level = params_[kOsc1Level].load(std::memory_order_relaxed);
    p.osc1Shape = static_cast<int>(std::lround(params_[kOsc1Shape].load(std::memory_order_relaxed)));
    p.osc1PulseWidth = params_[kOsc1PulseWidth].load(std::memory_order_relaxed);
    p.osc2Level = params_[kOsc2Level].load(std::memory_order_relaxed);
    p.osc2Shape = static_cast<int>(std::lround(params_[kOsc2Shape].load(std::memory_order_relaxed)));
    p.osc2PulseWidth = params_[kOsc2PulseWidth].load(std::memory_order_relaxed);
    p.osc2Detune = params_[kOsc2Detune].load(std::memory_order_relaxed);
    p.sync = params_[kSync].load(std::memory_order_relaxed) >= 0.5f;
    p.cutoff = params_[kFilterCutoff].load(std::memory_order_relaxed);
    p.resonance = params_[kFilterResonance].load(std::memory_order_relaxed);
    p.envAmount = params_[kFilterEnvAmount].load(std::memory_order_relaxed);
    p.keyTrack = params_[kFilterKeyTrack].load(std::memory_order_relaxed);
    p.fourPole = params_[kFilterSlope].load(std::memory_order_relaxed) >= 0.5f;
    p.lfoToPitch = params_[kLfoToPitch].load(std::memory_order_relaxed);
    p.lfoToPulseWidth = params_[kLfoToPulseWidth].load(std::memory_order_relaxed);
    p.lfoToFilter = params_[kLfoToFilter].load(std::memory_order_relaxed);
    p.velocityToFilter = params_[kVelocityToFilter].load(std::memory_order_relaxed);
    p.bendSemitones = bendSemitones_.load(std::memory_order_relaxed);
    p.wheelVibratoSemis = std::min(1.0f, modWheel_.load(std::memory_order_relaxed)
                                     + pressure_.load(std::memory_order_relaxed)) * kWheelVibratoSemitones;

    const AdsrSettings amp{
        params_[kAmpAttack].load(std::memory_order_relaxed),
        params_[kAmpDecay].load(std::memory_order_relaxed),
        params_[kAmpSustain].load(std::memory_order_relaxed),
        params_[kAmpRelease].load(std::memory_order_relaxed),
    };
    const AdsrSettings filter{
        params_[kFilterAttack].load(std::memory_order_relaxed),
        params_[kFilterDecay].load(std::memory_order_relaxed),
        params_[kFilterSustain].load(std::memory_order_relaxed),
        params_[kFilterRelease].load(std::memory_order_relaxed),
    };
    const float drift = params_[kAnalogCharacter].load(std::memory_order_relaxed);
    voiceManager_.forEachVoice([&](ObxVoice& voice) {
        voice.setSettings(amp, filter);
        voice.setDriftAmount(drift);
    });

    const bool unison = params_[kUnison].load(std::memory_order_relaxed) >= 0.5f;
    updateUnisonOffsets(params_[kUnisonDetune].load(std::memory_order_relaxed), unison);

    const float lfoRate = params_[kLfoRate].load(std::memory_order_relaxed);
    const int lfoWaveform = static_cast<int>(std::lround(params_[kLfoWaveform].load(std::memory_order_relaxed)));
    const double lfoIncrement = static_cast<double>(lfoRate) / sampleRate_;
    // L'unisson empile huit voix : sans compensation, il serait huit fois plus
    // fort que le mode polyphonique et ferait sursauter l'utilisateur.
    // 0.23 et non 0.28 : à 0.28, un accord de huit notes à vélocité 110
    // crêtait à 1.17, donc écrêtait. Mesuré, pas estimé.
    const float voiceGain = unison ? 0.23f / 2.6f : 0.23f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        float lfo = 0.0f;
        switch (lfoWaveform) {
            case 1: lfo = lfoPhase_ < 0.5 ? 1.0f : -1.0f; break;                  // carré
            case 2: lfo = lfoRandom_; break;                                       // échantillon-bloqueur
            default: lfo = 4.0f * std::abs(static_cast<float>(lfoPhase_) - 0.5f) - 1.0f; break; // triangle
        }

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](ObxVoice& voice) { sum += voice.render(p, lfo); });
        sum *= voiceGain;
        outputL[i] = sum;
        outputR[i] = sum;

        lfoPhase_ += lfoIncrement;
        if (lfoPhase_ >= 1.0) { lfoPhase_ -= 1.0; lfoRandom_ = lfoRng_.nextBipolar(); }
    }
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.obx", "OB-style Polysynth", ObxSynth);

} // namespace vsm::plugins::obx
