#include "ConeSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::cone {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace {
constexpr uint64_t kBaseSeed = 0x434F4E45ULL; // "CONE"

/// Le si bémol grave d'un saxophone baryton est vers 35 Hz ; 25 Hz laisse la
/// marge d'un basson sans réserver de mémoire pour rien. Le trajet étant
/// COMPLET et non de moitié, la ligne est deux fois plus longue que celle de
/// `vsm.wind` pour la même note.
constexpr float kLowestHz = 25.0f;

float noteToHz(uint8_t note, float semitones) {
    return 440.0f * std::exp2f((static_cast<float>(note) + semitones - 69.0f) / 12.0f);
}
} // namespace

// ---------------------------------------------------------------------------
// Voix
// ---------------------------------------------------------------------------

void ConeVoice::prepare(double sampleRate, uint64_t seed) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    bore_.prepare(sampleRate_, kLowestHz);
    rng_ = vsm::util::DeterministicRng(seed);
    drift_.setSampleRate(sampleRate_);
    drift_.setSeed(seed ^ 0xC04EULL);
    drift_.setRateHz(0.09f);
    // ÉTAT REMIS À ZÉRO À CHAQUE PRÉPARATION. `ProcessGraph::prepare()` rappelle
    // `initialize()` sur une machine DÉJÀ en marche quand la fréquence ou la
    // taille de bloc changent ; laisser l'état des résonateurs le ferait passer
    // dans des coefficients recalculés, ce qui s'entend en salve sur les
    // premiers échantillons.
    bore_.reset();
    breath_ = target_ = 0.0f;
    dcX1_ = dcY1_ = noiseLp_ = 0.0f;
    active_ = released_ = false;
}

void ConeVoice::noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    channel_ = channel;
    note_ = note;
    velocity_ = velocity;
    bore_.reset();
    breath_ = 0.0f;
    dcX1_ = dcY1_ = noiseLp_ = 0.0f;
    vibratoPhase_ = 0.0;
    vibratoRamp_ = 0.0f;
    active_ = true;
    released_ = false;
}

void ConeVoice::updateTuning(const Params& p) {
    const float driftSemis = drift_.nextValue() * 0.05f;

    vibratoIncrement_ = static_cast<double>(std::max(0.1f, p.vibratoRate)) / sampleRate_;
    const float rampSeconds = std::max(0.01f, p.vibratoDelay);
    vibratoRampCoeff_ = 1.0f - std::exp(-1.0f / (rampSeconds * static_cast<float>(sampleRate_)));
    const float vibratoSemis = std::sin(static_cast<float>(vibratoPhase_ * kTwoPi))
                             * p.vibratoDepth * vibratoRamp_ * 0.5f;

    const float hz = noteToHz(note_, driftSemis + vibratoSemis);
    bore_.setTuning(hz, p.bellDamping);

    attackCoeff_ = 1.0f - std::exp(-1.0f / (std::max(0.002f, p.attackSeconds) * static_cast<float>(sampleRate_)));
    releaseCoeff_ = 1.0f - std::exp(-1.0f / (std::max(0.002f, p.releaseSeconds) * static_cast<float>(sampleRate_)));

    const float velocity = static_cast<float>(velocity_) / 127.0f;
    velocityGain_ = 1.0f - std::clamp(p.velocitySensitivity, 0.0f, 1.0f) * (1.0f - velocity);
    target_ = released_ ? 0.0f : std::clamp(p.breathPressure, 0.0f, 1.0f) * velocityGain_;
}

float ConeVoice::render(const Params& p) {
    if (!active_) return 0.0f;

    const float coeff = (target_ > breath_) ? attackCoeff_ : releaseCoeff_;
    breath_ += coeff * (target_ - breath_);
    vibratoRamp_ += vibratoRampCoeff_ * (1.0f - vibratoRamp_);
    vibratoPhase_ += vibratoIncrement_;
    if (vibratoPhase_ >= 1.0) vibratoPhase_ -= 1.0;

    // Turbulence, proportionnelle au souffle : sinon elle sifflerait dans le
    // silence.
    noiseLp_ += 0.35f * (rng_.nextBipolar() - noiseLp_);
    const float breath = breath_ * (1.0f + p.breathNoise * noiseLp_ * 0.6f);

    // LA VALVE, EN FORMULATION PAR DIFFUSION. `anche` est un coefficient de
    // RÉFLEXION, pas un débit : c'est ce qui donne à la boucle un gain proche
    // de 0,7 au repos, tendant vers 1 quand la valve arrive en butée. La
    // première version injectait un débit et n'atteignait que 0,22 : elle ne
    // s'amorçait pas (voir l'en-tête).
    const float returning = bore_.returning();
    const float difference = returning - breath;
    const float reed = reedTable(difference, std::clamp(p.reedStiffness, 0.0f, 1.0f));
    float pressure = breath + difference * reed;

    // Plus on pousse, plus l'onde se raidit et plus ça claque : c'est ce qui
    // sépare un saxophone tenu piano d'un ténor poussé.
    if (p.brassiness > 0.0f) {
        const float drive = 1.0f + 6.0f * p.brassiness * breath_;
        pressure = std::tanh(pressure * drive) / std::sqrt(drive);
    }

    bore_.inject(pressure);

    const float out = pressure - dcX1_ + 0.9995f * dcY1_;
    dcX1_ = pressure;
    dcY1_ = out;

    // Fin de note : le souffle est retombé ET le tuyau s'est vidé. Couper au
    // relâchement supprimerait l'extinction, qui est ce qui s'entend le plus.
    if (released_ && breath_ < 1.0e-4f && std::fabs(pressure) < 2.0e-5f) active_ = false;

    return out;
}

// ---------------------------------------------------------------------------
// Machine
// ---------------------------------------------------------------------------

ConeSynth::ConeSynth() {
    parameterList_ = {
        {kBreathPressure, "Breath Pressure", 0.0f, 1.0f, 0.7f, ""},
        {kReedStiffness, "Reed Stiffness", 0.0f, 1.0f, 0.5f, ""},
        {kBrassiness, "Brassiness", 0.0f, 1.0f, 0.15f, ""},
        {kBreathNoise, "Breath Noise", 0.0f, 1.0f, 0.25f, ""},
        {kBellDamping, "Bell Damping", 0.0f, 1.0f, 0.35f, ""},
        {kAttack, "Attack", 0.002f, 0.6f, 0.06f, "s"},
        {kRelease, "Release", 0.01f, 1.0f, 0.12f, "s"},
        {kVibratoRate, "Vibrato Rate", 0.5f, 9.0f, 5.0f, "Hz"},
        {kVibratoDepth, "Vibrato Depth", 0.0f, 1.0f, 0.15f, ""},
        {kVibratoDelay, "Vibrato Delay", 0.01f, 2.0f, 0.35f, "s"},
        {kToneBass, "Tone Bass", -12.0f, 12.0f, 0.0f, "dB"},
        {kToneTreble, "Tone Treble", -12.0f, 12.0f, 0.0f, "dB"},
        {kVelocitySensitivity, "Velocity Sensitivity", 0.0f, 1.0f, 0.6f, ""},
        {kAnalogCharacter, "Analog Character", 0.0f, 1.0f, 0.25f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void ConeSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t index = 0;
    voiceManager_.forEachVoice([&](ConeVoice& voice) {
        voice.prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, index));
        ++index;
    });
    bassShelf_.setSampleRate(sampleRate);
    trebleShelf_.setSampleRate(sampleRate);
    // Les filtres de tonalité gardaient leur état d'une fréquence à l'autre.
    bassShelf_.reset();
    trebleShelf_.reset();
}

void ConeSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float ConeSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState ConeSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.cone";
    for (const auto& info : parameterList_) state.parameterValues[info.id] = getParameter(info.id);
    return state;
}

void ConeSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues) setParameter(id, value);
}

void ConeSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void ConeSynth::process(const MidiNoteEvent* events, int numEvents,
                        float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    ConeVoice::Params p;
    p.breathPressure = params_[kBreathPressure].load(std::memory_order_relaxed);
    p.reedStiffness = params_[kReedStiffness].load(std::memory_order_relaxed);
    p.brassiness = params_[kBrassiness].load(std::memory_order_relaxed);
    p.breathNoise = params_[kBreathNoise].load(std::memory_order_relaxed);
    p.bellDamping = params_[kBellDamping].load(std::memory_order_relaxed);
    p.attackSeconds = params_[kAttack].load(std::memory_order_relaxed);
    p.releaseSeconds = params_[kRelease].load(std::memory_order_relaxed);
    p.vibratoRate = params_[kVibratoRate].load(std::memory_order_relaxed);
    p.vibratoDepth = params_[kVibratoDepth].load(std::memory_order_relaxed);
    p.vibratoDelay = params_[kVibratoDelay].load(std::memory_order_relaxed);
    p.velocitySensitivity = params_[kVelocitySensitivity].load(std::memory_order_relaxed);

    const float drift = params_[kAnalogCharacter].load(std::memory_order_relaxed);
    bassShelf_.set(Biquad::Type::LowShelf, 250.0f, 0.707f,
                   params_[kToneBass].load(std::memory_order_relaxed));
    trebleShelf_.set(Biquad::Type::HighShelf, 2600.0f, 0.707f,
                     params_[kToneTreble].load(std::memory_order_relaxed));
    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);

    voiceManager_.forEachVoice([&](ConeVoice& voice) {
        voice.setDriftAmount(drift);
        if (voice.isActive()) voice.updateTuning(p);
    });

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i) {
            applyNoteEvent(events[eventIndex++]);
            voiceManager_.forEachVoice([&](ConeVoice& voice) {
                if (voice.isActive()) voice.updateTuning(p);
            });
        }

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](ConeVoice& voice) { sum += voice.render(p); });

        // NIVEAU CALIBRÉ SUR LE PUPITRE : quatre anches soufflées ensemble,
        // comme un pupitre de saxophones. Même facteur que `vsm.wind`, et ce
        // n'est pas une coïncidence -- c'est la même boucle, à la perce près.
        sum = trebleShelf_.process(bassShelf_.process(sum * 0.30f));
        const float out = sum * outputLevel;
        outputL[i] = out;
        outputR[i] = out;
    }
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.cone", "Cone (anche sur perce conique)", ConeSynth);

} // namespace vsm::plugins::cone
