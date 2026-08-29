#include "TB303Synth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::tb303 {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

TB303Synth::TB303Synth() {
    // Essentiel : voir le commentaire de classe -- le chevauchement de
    // notes MIDI est interprété comme un SLIDE (glissando sans retrigger
    // d'enveloppe) uniquement si le mode legato est actif en permanence.
    voiceAllocator_.setLegatoMode(true);

    parameterList_ = {
        {kWaveform, "Waveform", 0.0f, 1.0f, 0.0f, ""}, // 0 = Saw (le plus courant en acid)
        {kCutoff, "Cutoff", 40.0f, 6000.0f, 800.0f, "Hz"},
        {kResonance, "Resonance", 0.0f, 1.0f, 0.6f, ""},
        {kEnvMod, "Env Mod", 0.0f, 1.0f, 0.5f, ""},
        {kDecay, "Decay", 0.03f, 2.0f, 0.3f, "s"},
        {kAccent, "Accent", 0.0f, 1.0f, 0.6f, ""},
        {kAccentVelocityThreshold, "Accent Threshold", 1.0f, 126.0f, 100.0f, ""},
        {kGlideTime, "Glide Time", 0.0f, 0.5f, 0.06f, "s"}, // ~60 ms, proche du hardware réel
        {kAnalogCharacter, "Analog Character", 0.0f, 1.0f, 0.3f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void TB303Synth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;

    osc_.setSampleRate(sampleRate);
    filter_.setSampleRate(sampleRate);
    filter_.setPoleCount(3); // LE point de fidélité : 18 dB/oct, pas 24 (voir en-tête)

    filterEnv_.setSampleRate(sampleRate);
    ampEnv_.setSampleRate(sampleRate);
    // L'enveloppe d'amplitude ne dépend pas de l'accent dans ce modèle
    // (l'accent agit sur un gain post-enveloppe, voir process()) : ses
    // réglages sont donc constants, fixés une fois pour toutes ici plutôt
    // que recalculés à chaque note.
    ampEnv_.setSettings({kAmpAttackSeconds, kAmpDecaySeconds, 1.0f, kAmpReleaseSeconds});

    pitchGlide_.setSampleRate(sampleRate);
    pitchGlide_.reset(60.0f);

    pitchDrift_.setSampleRate(sampleRate);
    pitchDrift_.setSeed(0x7B303ULL);
    pitchDrift_.setRateHz(0.25f);
    cutoffDrift_.setSampleRate(sampleRate);
    cutoffDrift_.setSeed(0xAC1D303ULL);
    cutoffDrift_.setRateHz(0.2f);
}

Waveform TB303Synth::waveformFromParam() const {
    int idx = static_cast<int>(std::lround(params_[kWaveform].load(std::memory_order_relaxed)));
    return idx <= 0 ? Waveform::Saw : Waveform::Square;
}

void TB303Synth::applyNoteEvent(const MidiNoteEvent& ev) {
    using vsm::audio::engine::MonoVoiceAllocator;

    if (ev.kind == MidiNoteEvent::Kind::NoteOn) {
        MonoVoiceAllocator::Result r = voiceAllocator_.noteOn(ev.note, ev.velocity);
        if (!r.shouldPlay) return;

        currentVelocity_ = r.velocity;

        // Conversion vélocité -> accent (section 10, exigé explicitement) :
        // l'intensité croît linéairement du seuil jusqu'à 127, pas un
        // simple booléen tout-ou-rien.
        float threshold = params_[kAccentVelocityThreshold].load(std::memory_order_relaxed);
        float accentKnob = params_[kAccent].load(std::memory_order_relaxed);
        float range = 127.0f - threshold;
        float accentIntensity = (range > 0.5f)
            ? std::clamp((static_cast<float>(r.velocity) - threshold) / range, 0.0f, 1.0f)
            : 0.0f;
        currentEffectiveAccent_ = accentIntensity * accentKnob;

        // legatoMode_ est toujours actif (voir constructeur) : r.retrigger
        // == false signifie précisément "ce noteOn chevauche la note
        // précédente" == SLIDE.
        bool isSlide = !r.retrigger;
        pitchGlide_.setTarget(static_cast<float>(r.note));
        if (!isSlide) {
            pitchGlide_.reset(static_cast<float>(r.note)); // note normale : saute directement, pas de glide
            filterEnv_.noteOn();
            ampEnv_.noteOn();
        }
        // en slide : ni reset de pitch (il glisse) ni retrigger d'enveloppe
        // -- comportement 303 authentique.

        float decayParam = params_[kDecay].load(std::memory_order_relaxed);
        float effectiveDecay = std::max(kMinDecaySeconds,
                                         decayParam * (1.0f - currentEffectiveAccent_ * kAccentDecayShorten));
        filterEnv_.setSettings({kFilterAttackSeconds, effectiveDecay, 0.0f, effectiveDecay});
    } else {
        MonoVoiceAllocator::Result r = voiceAllocator_.noteOff(ev.note);
        if (r.shouldPlay) {
            currentVelocity_ = r.velocity;
            pitchGlide_.setTarget(static_cast<float>(r.note)); // retombe sur la note précédente -> glisse vers elle
        } else {
            filterEnv_.noteOff();
            ampEnv_.noteOff();
        }
    }
}

void TB303Synth::process(const MidiNoteEvent* events, int numEvents,
                          float* outputL, float* outputR, int numSamples) {
    float envMod = params_[kEnvMod].load(std::memory_order_relaxed);
    float cutoffBase = params_[kCutoff].load(std::memory_order_relaxed);
    float resonanceParam = params_[kResonance].load(std::memory_order_relaxed); // 0..1
    float analogCharacter = params_[kAnalogCharacter].load(std::memory_order_relaxed);

    pitchGlide_.setSmoothingTimeMs(params_[kGlideTime].load(std::memory_order_relaxed) * 1000.0f);
    pitchDrift_.setAmount(analogCharacter);
    cutoffDrift_.setAmount(analogCharacter);

    filter_.setResonance(resonanceParam * 4.2f); // mise à l'échelle vers la plage utile du LadderFilterZDF
    filter_.setDrive(kFilterDrive);

    osc_.setWaveform(waveformFromParam());

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i) {
            applyNoteEvent(events[eventIndex]); // met aussi à jour filterEnv_ (decay dépendant de l'accent)
            ++eventIndex;
        }

        float noteNumber = pitchGlide_.nextValue();
        float pitchDriftSemis = pitchDrift_.nextValue() * kMaxPitchDriftSemitones;
        float hz = 440.0f * std::exp2f((noteNumber + pitchDriftSemis + bendSemitones_.load(std::memory_order_relaxed) - 69.0f) / 12.0f);
        osc_.setFrequency(hz);

        float raw = osc_.nextSample();
        float filterEnvLevel = filterEnv_.nextSample();
        float ampEnvLevel = ampEnv_.nextSample();

        float cutoffDriftOct = cutoffDrift_.nextValue() * kMaxCutoffDriftOctaves;
        float effectiveEnvMod = envMod * (1.0f + currentEffectiveAccent_ * kAccentEnvModBoost);
        float envOctaves = effectiveEnvMod * filterEnvLevel * kEnvModRangeOctaves;
        float finalCutoff = cutoffBase * std::exp2f(envOctaves + cutoffDriftOct);
        filter_.setCutoffHz(finalCutoff);

        float filtered = filter_.process(raw);

        float velocityGain = static_cast<float>(currentVelocity_) / 127.0f;
        float accentGain = 1.0f + currentEffectiveAccent_ * kAccentAmpBoost;
        float sample = filtered * ampEnvLevel * velocityGain * accentGain * 0.3f; // 0.3 = normalisation de sortie

        outputL[i] = sample;
        outputR[i] = sample;
    }
}

bool TB303Synth::handleControlEvent(const MidiControlEvent& event) {
    if (event.kind != MidiControlEvent::Kind::PitchBend) return false;
    bendSemitones_.store(event.value, std::memory_order_relaxed);
    return true;
}

void TB303Synth::setParameter(ParamId id, float value) {
    if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}

float TB303Synth::getParameter(ParamId id) const {
    return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

const ParameterList& TB303Synth::parameterList() const { return parameterList_; }

PresetState TB303Synth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.tb303";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void TB303Synth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}

int TB303Synth::activeVoiceCount() const { return ampEnv_.isActive() ? 1 : 0; }

// Voir PluginRegistry.h : nom NON qualifié, invoqué à l'intérieur du namespace.
VSM_REGISTER_SYNTH_PLUGIN("vsm.tb303", "TB-303-style Acid Synth", TB303Synth);

} // namespace vsm::plugins::tb303
