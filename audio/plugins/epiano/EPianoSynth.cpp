#include "EPianoSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::epiano {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace {
constexpr uint64_t kBaseSeed = 0x4550494E4F00ULL;

float noteToHz(uint8_t note, float driftSemis) {
    return 440.0f * std::exp2f((static_cast<float>(note) + driftSemis - 69.0f) / 12.0f);
}
} // namespace

float EPianoVoice::render(const Params& p) {
    if (!amp_.isActive()) return 0.0f;

    const float driftSemis = drift_.nextValue() * 0.03f; // très faible : ce n'est pas un VCO
    const float baseHz = noteToHz(note_, driftSemis);
    const float envelope = amp_.nextSample();

    // Vélocité : elle agit surtout sur le TIMBRE, pas seulement sur le volume.
    // Frapper fort une lame ne fait pas qu'augmenter le niveau, cela réveille
    // la cloche et le choc du marteau -- c'est ce qui rend l'instrument
    // expressif, et le rater donnerait un piano « en plastique ».
    const float velocity = static_cast<float>(velocity_) / 127.0f;
    const float velocityGain = 1.0f - p.velocitySensitivity * (1.0f - velocity);
    const float brightness = velocity * velocity;

    // Partiels INHARMONIQUES : une lame n'est pas une corde, ses modes ne
    // tombent pas sur des multiples entiers. `character` resserre ces rapports
    // vers l'harmonique en allant du timbre de lame vers celui d'anche.
    const float inharmonicity = 1.0f - p.character;
    const float ratios[kPartialCount] = {
        1.0f,
        4.0f + 0.75f * inharmonicity,   // la « cloche » : c'est elle qu'on entend en attaque
        9.0f + 1.60f * inharmonicity,
    };
    const float levels[kPartialCount] = {
        1.0f,
        p.bellLevel * (0.35f + 0.65f * brightness),
        p.bellLevel * 0.25f * brightness,
    };

    float tine = 0.0f;
    for (int i = 0; i < kPartialCount; ++i) {
        partialPhase_[static_cast<size_t>(i)] +=
            static_cast<double>(baseHz * ratios[i]) / sampleRate_;
        if (partialPhase_[static_cast<size_t>(i)] >= 1.0) partialPhase_[static_cast<size_t>(i)] -= 1.0;
        // Les partiels hauts s'éteignent plus vite que le fondamental : c'est
        // ce qui fait qu'une note brillante à l'attaque devient douce en tenue.
        const float partialDecay = (i == 0) ? 1.0f : std::pow(envelope, 1.0f + 1.8f * static_cast<float>(i));
        tine += static_cast<float>(std::sin(kTwoPi * partialPhase_[static_cast<size_t>(i)])) *
                levels[i] * partialDecay;
    }

    // Choc du marteau : bruit filtré, très bref, plus présent en frappe forte.
    float knock = 0.0f;
    if (knockLevel_ > 1.0e-4f) {
        const float hardnessHz = 800.0f + 5200.0f * p.hammerHardness;
        knockFilter_.set(Biquad::Type::LowPass, hardnessHz, 0.9f, 0.0f);
        knock = knockFilter_.process(rng_.nextBipolar()) * knockLevel_ * p.hammerNoise * (0.3f + brightness);
        // Décroissance en quelques millisecondes : un choc, pas un souffle.
        knockLevel_ *= std::exp(-1.0f / (0.004f * static_cast<float>(sampleRate_)));
    }

    float signal = (tine + knock) * envelope * velocityGain;

    // Micro : saturation douce et asymétrique. L'asymétrie compte -- une
    // saturation symétrique ne produirait que des harmoniques impaires et
    // sonnerait « fuzz », alors que le micro d'un piano électrique ajoute
    // surtout de la deuxième harmonique.
    if (p.pickupDrive > 0.0f) {
        const float drive = 1.0f + 4.0f * p.pickupDrive;
        signal = std::tanh(signal * drive + 0.12f * p.pickupDrive * signal * signal) / drive;
        signal *= 1.0f + 2.0f * p.pickupDrive;
    }
    return signal;
}

EPianoSynth::EPianoSynth() {
    parameterList_ = {
        {kBellLevel, "Bell Level", 0.0f, 1.0f, 0.55f, ""},
        {kTineDecay, "Tine Decay", 0.3f, 12.0f, 4.5f, "s"},
        {kRelease, "Release", 0.02f, 2.0f, 0.25f, "s"},
        {kHammerHardness, "Hammer Hardness", 0.0f, 1.0f, 0.5f, ""},
        {kHammerNoise, "Hammer Noise", 0.0f, 1.0f, 0.3f, ""},
        {kPickupDrive, "Pickup Drive", 0.0f, 1.0f, 0.25f, ""},
        {kCharacter, "Character", 0.0f, 1.0f, 0.0f, ""},
        {kVelocitySensitivity, "Velocity Sensitivity", 0.0f, 1.0f, 0.8f, ""},
        {kToneBass, "Tone Bass", -12.0f, 12.0f, 0.0f, "dB"},
        {kToneTreble, "Tone Treble", -12.0f, 12.0f, 0.0f, "dB"},
        {kTremoloRate, "Tremolo Rate", 0.1f, 12.0f, 5.0f, "Hz"},
        {kTremoloDepth, "Tremolo Depth", 0.0f, 1.0f, 0.0f, ""},
        {kTremoloStereo, "Tremolo Stereo", 0.0f, 1.0f, 1.0f, ""},
        {kAnalogCharacter, "Analog Character", 0.0f, 1.0f, 0.25f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void EPianoSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t index = 0;
    voiceManager_.forEachVoice([&](EPianoVoice& voice) {
        voice.prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, index));
        ++index;
    });
    bassShelf_.setSampleRate(sampleRate);
    trebleShelf_.setSampleRate(sampleRate);
    tremoloPhase_ = 0.0;
}

void EPianoSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float EPianoSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState EPianoSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.epiano";
    for (const auto& info : parameterList_) state.parameterValues[info.id] = getParameter(info.id);
    return state;
}

void EPianoSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues) setParameter(id, value);
}

void EPianoSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void EPianoSynth::process(const MidiNoteEvent* events, int numEvents,
                           float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    EPianoVoice::Params p;
    p.bellLevel = params_[kBellLevel].load(std::memory_order_relaxed);
    p.hammerHardness = params_[kHammerHardness].load(std::memory_order_relaxed);
    p.hammerNoise = params_[kHammerNoise].load(std::memory_order_relaxed);
    p.pickupDrive = params_[kPickupDrive].load(std::memory_order_relaxed);
    p.character = params_[kCharacter].load(std::memory_order_relaxed);
    p.velocitySensitivity = params_[kVelocitySensitivity].load(std::memory_order_relaxed);

    // Le maintien d'une lame N'A PAS de palier : elle décroît continûment
    // jusqu'à l'étouffoir. On modélise donc le decay par un sustain nul et une
    // longue descente -- pas par un ADSR à palier, qui donnerait un orgue.
    AdsrSettings amp;
    amp.attackSeconds = 0.002f;
    amp.decaySeconds = params_[kTineDecay].load(std::memory_order_relaxed);
    amp.sustainLevel = 0.0f;
    amp.releaseSeconds = params_[kRelease].load(std::memory_order_relaxed);
    const float drift = params_[kAnalogCharacter].load(std::memory_order_relaxed);
    voiceManager_.forEachVoice([&](EPianoVoice& voice) {
        voice.setSettings(amp);
        voice.setDriftAmount(drift);
    });

    const float bassDb = params_[kToneBass].load(std::memory_order_relaxed);
    const float trebleDb = params_[kToneTreble].load(std::memory_order_relaxed);
    bassShelf_.set(Biquad::Type::LowShelf, 220.0f, 0.707f, bassDb);
    trebleShelf_.set(Biquad::Type::HighShelf, 3200.0f, 0.707f, trebleDb);

    const float tremoloRate = params_[kTremoloRate].load(std::memory_order_relaxed);
    const float tremoloDepth = params_[kTremoloDepth].load(std::memory_order_relaxed);
    const float tremoloStereo = params_[kTremoloStereo].load(std::memory_order_relaxed);
    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    const double tremoloIncrement = static_cast<double>(tremoloRate) / sampleRate_;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](EPianoVoice& voice) { sum += voice.render(p); });
        sum = trebleShelf_.process(bassShelf_.process(sum * 0.28f));

        // Trémolo : sur ces instruments il est STÉRÉO -- le son passe d'une
        // enceinte à l'autre plutôt que de monter et descendre en volume.
        // Réduire cela à une modulation d'amplitude perdrait ce qui le rend
        // reconnaissable.
        const float phase = static_cast<float>(tremoloPhase_ * kTwoPi);
        const float modulation = std::sin(phase) * tremoloDepth;
        const float left = 1.0f - 0.5f * modulation * (1.0f + tremoloStereo);
        const float right = 1.0f - 0.5f * (-modulation * tremoloStereo + modulation * (1.0f - tremoloStereo));

        outputL[i] = sum * left * outputLevel;
        outputR[i] = sum * right * outputLevel;

        tremoloPhase_ += tremoloIncrement;
        if (tremoloPhase_ >= 1.0) tremoloPhase_ -= 1.0;
    }
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.epiano", "Electric Piano (lames)", EPianoSynth);

} // namespace vsm::plugins::epiano
