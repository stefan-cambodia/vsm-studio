#include "WavetableSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::wavetable {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace {
constexpr uint64_t kBaseSeed = 0x5741564554ULL; // "WAVET"
} // namespace

void WavetableVoice::prepare(double sampleRate, uint64_t seed) {
    sampleRate_ = sampleRate;
    rng_ = vsm::util::DeterministicRng{seed};
    oscA_.setSampleRate(sampleRate);
    oscB_.setSampleRate(sampleRate);
    filter_.setSampleRate(sampleRate);
    filter_.setMode(StateVariableFilter::Mode::LowPass);
    ampEnv_.setSampleRate(sampleRate);
    filterEnv_.setSampleRate(sampleRate);
    waveEnv_.setSampleRate(sampleRate);
    driftA_.setSampleRate(sampleRate); driftA_.setSeed(seed);                driftA_.setRateHz(0.10f);
    driftB_.setSampleRate(sampleRate); driftB_.setSeed(seed ^ 0xC0FFEEULL);  driftB_.setRateHz(0.07f);
}

void WavetableVoice::noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    channel_ = channel; note_ = note; velocity_ = velocity;
    ampEnv_.noteOn(); filterEnv_.noteOn(); waveEnv_.noteOn();
    baseHz_ = 440.0f * std::exp2f((static_cast<float>(note_) - 69.0f) / 12.0f);
    // Phases décorrélées entre les deux oscillateurs : démarrer ensemble
    // annulerait le battement de timbre qu'on cherche précisément à obtenir.
    oscA_.reset(static_cast<double>(rng_.nextUnipolar()));
    oscB_.reset(static_cast<double>(rng_.nextUnipolar()));
}

float WavetableVoice::render(const WaveTableBank& bank, const Params& p, float lfo) {
    if (!ampEnv_.isActive()) return 0.0f;

    const float waveEnvLevel = waveEnv_.nextSample();
    const float filterEnvLevel = filterEnv_.nextSample();
    const float ampLevel = ampEnv_.nextSample();

    // POSITION DANS LA TABLE : c'est la commande centrale de l'instrument.
    // Trois sources s'y ajoutent -- le réglage de façade, l'enveloppe dédiée
    // et le LFO -- et le résultat est borné, jamais replié : arriver au bout
    // de la table doit s'entendre comme une butée, pas comme un retour au
    // début (qui produirait un saut de timbre en plein mouvement).
    const float positionA = std::clamp(
        p.position + p.waveEnvAmount * waveEnvLevel + lfo * p.lfoToPosition * 0.5f, 0.0f, 1.0f);
    const float positionB = std::clamp(positionA + p.oscBPosition, 0.0f, 1.0f);

    const float driftA = driftA_.nextValue() * 0.05f;
    const float driftB = driftB_.nextValue() * 0.05f;
    // Terme de molette ADDITIF : l'expression d'origine garde son ordre
    // d'association flottant, l'empreinte ne bouge pas à molette nulle.
    const float vibrato = lfo * p.lfoToPitch * 0.5f + lfo * p.wheelVibratoSemis;

    const auto table = static_cast<size_t>(std::max(0, p.table));
    oscA_.setFrequency(baseHz_ * std::exp2f((driftA + vibrato + p.bendSemitones) / 12.0f));
    float sample = oscA_.nextSample(bank, table, positionA);

    if (p.oscBLevel > 0.0001f) {
        oscB_.setFrequency(baseHz_ * std::exp2f(
            (p.oscBDetune + driftB + vibrato + p.bendSemitones) / 12.0f));
        sample += oscB_.nextSample(bank, table, positionB) * p.oscBLevel;
        sample *= 1.0f / (1.0f + p.oscBLevel); // niveau stable quel que soit le dosage
    }
    if (p.noiseLevel > 0.0001f) sample += rng_.nextBipolar() * p.noiseLevel * 0.3f;

    const float velocity = static_cast<float>(velocity_) / 127.0f;
    const float envOctaves = p.envAmount * filterEnvLevel * 5.0f;
    const float lfoOctaves = lfo * p.lfoToFilter * 3.0f;
    const float trackOctaves = p.keyTrack * (static_cast<float>(note_) - 60.0f) / 12.0f;
    const float velocityOctaves = p.velocityToFilter * (velocity - 0.5f) * 2.0f;
    const float cutoff = std::clamp(
        p.cutoff * std::exp2f(envOctaves + lfoOctaves + trackOctaves + velocityOctaves), 20.0f, 18000.0f);

    filter_.setCutoffHz(cutoff);
    filter_.setResonance(0.707f + p.resonance * 7.0f);
    return filter_.process(sample) * ampLevel;
}

WavetableSynth::WavetableSynth() {
    parameterList_ = {
        {kTable, "Wavetable", 0.0f, 3.0f, 0.0f, ""},
        {kPosition, "Position", 0.0f, 1.0f, 0.15f, ""},
        {kWaveEnvAmount, "Wave Env Amount", -1.0f, 1.0f, 0.45f, ""},
        {kLfoToPosition, "LFO to Position", 0.0f, 1.0f, 0.0f, ""},
        {kOscBLevel, "Osc B Level", 0.0f, 1.0f, 0.6f, ""},
        {kOscBDetune, "Osc B Detune", -12.0f, 12.0f, 0.07f, "st"},
        {kOscBPosition, "Osc B Position", -1.0f, 1.0f, 0.12f, ""},
        {kNoiseLevel, "Noise Level", 0.0f, 1.0f, 0.0f, ""},
        {kFilterCutoff, "Filter Cutoff", 20.0f, 18000.0f, 5000.0f, "Hz"},
        {kFilterResonance, "Filter Resonance", 0.0f, 1.0f, 0.15f, ""},
        {kFilterEnvAmount, "Filter Env Amount", -1.0f, 1.0f, 0.25f, ""},
        {kFilterKeyTrack, "Filter Key Track", 0.0f, 1.0f, 0.3f, ""},
        {kFilterAttack, "Filter Attack", 0.001f, 4.0f, 0.01f, "s"},
        {kFilterDecay, "Filter Decay", 0.001f, 8.0f, 0.7f, "s"},
        {kFilterSustain, "Filter Sustain", 0.0f, 1.0f, 0.5f, ""},
        {kFilterRelease, "Filter Release", 0.001f, 8.0f, 0.5f, "s"},
        {kAmpAttack, "Amp Attack", 0.001f, 4.0f, 0.01f, "s"},
        {kAmpDecay, "Amp Decay", 0.001f, 8.0f, 0.6f, "s"},
        {kAmpSustain, "Amp Sustain", 0.0f, 1.0f, 0.8f, ""},
        {kAmpRelease, "Amp Release", 0.001f, 8.0f, 0.4f, "s"},
        {kWaveAttack, "Wave Attack", 0.001f, 8.0f, 0.4f, "s"},
        {kWaveDecay, "Wave Decay", 0.001f, 12.0f, 1.6f, "s"},
        {kWaveSustain, "Wave Sustain", 0.0f, 1.0f, 0.35f, ""},
        {kWaveRelease, "Wave Release", 0.001f, 12.0f, 0.8f, "s"},
        {kLfoRate, "LFO Rate", 0.05f, 20.0f, 0.8f, "Hz"},
        {kLfoToFilter, "LFO to Filter", 0.0f, 1.0f, 0.0f, ""},
        {kLfoToPitch, "LFO to Pitch", 0.0f, 1.0f, 0.0f, ""},
        {kVelocityToFilter, "Velocity to Filter", 0.0f, 1.0f, 0.25f, ""},
        {kAnalogCharacter, "Analog Character", 0.0f, 1.0f, 0.2f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void WavetableSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    // Construction de la banque ICI, hors du fil audio : elle alloue et
    // calcule quelques millions de sinus. La demander depuis `process()`
    // violerait la règle « ni allocation ni calcul non borné dans le fil
    // audio » sur le tout premier bloc.
    bank_ = &WaveTableBank::shared();
    uint64_t index = 0;
    voiceManager_.forEachVoice([&](WavetableVoice& voice) {
        voice.prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, index));
        ++index;
    });
    lfoPhase_ = 0.0;
}

bool WavetableSynth::handleControlEvent(const MidiControlEvent& event) {
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
    return false;
}

void WavetableSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float WavetableSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState WavetableSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.wavetable";
    for (const auto& info : parameterList_) state.parameterValues[info.id] = getParameter(info.id);
    return state;
}

void WavetableSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues) setParameter(id, value);
}

void WavetableSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void WavetableSynth::process(const MidiNoteEvent* events, int numEvents,
                              float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    if (bank_ == nullptr) {
        // `initialize()` n'a pas été appelé : on sort du silence plutôt que
        // de construire la banque depuis le fil audio.
        std::fill(outputL, outputL + numSamples, 0.0f);
        std::fill(outputR, outputR + numSamples, 0.0f);
        return;
    }

    WavetableVoice::Params p;
    p.table = static_cast<int>(std::lround(params_[kTable].load(std::memory_order_relaxed)));
    p.position = params_[kPosition].load(std::memory_order_relaxed);
    p.waveEnvAmount = params_[kWaveEnvAmount].load(std::memory_order_relaxed);
    p.lfoToPosition = params_[kLfoToPosition].load(std::memory_order_relaxed);
    p.oscBLevel = params_[kOscBLevel].load(std::memory_order_relaxed);
    p.oscBDetune = params_[kOscBDetune].load(std::memory_order_relaxed);
    p.oscBPosition = params_[kOscBPosition].load(std::memory_order_relaxed);
    p.noiseLevel = params_[kNoiseLevel].load(std::memory_order_relaxed);
    p.cutoff = params_[kFilterCutoff].load(std::memory_order_relaxed);
    p.resonance = params_[kFilterResonance].load(std::memory_order_relaxed);
    p.envAmount = params_[kFilterEnvAmount].load(std::memory_order_relaxed);
    p.keyTrack = params_[kFilterKeyTrack].load(std::memory_order_relaxed);
    p.lfoToFilter = params_[kLfoToFilter].load(std::memory_order_relaxed);
    p.lfoToPitch = params_[kLfoToPitch].load(std::memory_order_relaxed);
    p.velocityToFilter = params_[kVelocityToFilter].load(std::memory_order_relaxed);
    p.bendSemitones = bendSemitones_.load(std::memory_order_relaxed);
    p.wheelVibratoSemis = modWheel_.load(std::memory_order_relaxed) * kWheelVibratoSemitones;

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
    const AdsrSettings wave{
        params_[kWaveAttack].load(std::memory_order_relaxed),
        params_[kWaveDecay].load(std::memory_order_relaxed),
        params_[kWaveSustain].load(std::memory_order_relaxed),
        params_[kWaveRelease].load(std::memory_order_relaxed),
    };
    const float drift = params_[kAnalogCharacter].load(std::memory_order_relaxed);
    voiceManager_.forEachVoice([&](WavetableVoice& voice) {
        voice.setSettings(amp, filter, wave);
        voice.setDriftAmount(drift);
    });

    const float lfoRate = params_[kLfoRate].load(std::memory_order_relaxed);
    const double lfoIncrement = static_cast<double>(lfoRate) / sampleRate_;
    // Niveau calibré par MESURE, pas au jugé : un accord de huit notes à
    // vélocité 110 doit rester sous 0 dBFS avec de la marge, tout en restant
    // dans la même plage de niveau perçu que les autres polyphoniques du parc
    // (Juno, Prophet, Jupiter). À 0.45 l'accord crêtait à 1.02 -- écrêtage.
    constexpr float kVoiceGain = 0.34f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        const float lfo = static_cast<float>(std::sin(kTwoPi * lfoPhase_));

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](WavetableVoice& voice) { sum += voice.render(*bank_, p, lfo); });
        sum *= kVoiceGain;
        outputL[i] = sum;
        outputR[i] = sum;

        lfoPhase_ += lfoIncrement;
        if (lfoPhase_ >= 1.0) lfoPhase_ -= 1.0;
    }
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.wavetable", "Wavetable Synth", WavetableSynth);

} // namespace vsm::plugins::wavetable
