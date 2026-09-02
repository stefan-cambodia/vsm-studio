#include "ScannedSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::scanned {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

ScannedSynth::ScannedSynth() {
    // DÉFAUTS MESURÉS, pas choisis au jugé. Le premier jeu (tension 0,35,
    // amortissement 0,08) laissait la chaîne s'éteindre en 2,5 secondes : la
    // note tenue mourait toute seule, ce qu'un synthétiseur n'a pas le droit
    // de faire. Sondé sur quatre secondes, le jeu retenu tient son niveau
    // (rms 0,33 constant) pendant que son contenu harmonique voyage
    // franchement -- h2/h1 mesuré de 0,07 à 1,18 selon l'instant. C'est le
    // régime où cette famille dit ce qu'elle est.
    parameterList_ = {
        {kTension, "Tension", 0.0f, 1.0f, 0.18f, ""},
        {kDamping, "Damping", 0.0f, 1.0f, 0.012f, ""},
        {kCentering, "Centering", 0.0f, 1.0f, 0.05f, ""},
        {kPluckPosition, "Pluck Position", 0.0f, 1.0f, 0.3f, ""},
        {kPluckHardness, "Pluck Hardness", 0.0f, 1.0f, 0.5f, ""},
        {kPluckForce, "Pluck Force", 0.0f, 2.0f, 1.0f, ""},
        {kCutoff, "Filter Cutoff", 40.0f, 16000.0f, 9000.0f, "Hz"},
        {kResonance, "Filter Resonance", 0.0f, 0.95f, 0.1f, ""},
        {kAttack, "Attack", 0.001f, 4.0f, 0.02f, "s"},
        {kDecay, "Decay", 0.005f, 8.0f, 0.5f, "s"},
        {kSustain, "Sustain", 0.0f, 1.0f, 0.85f, ""},
        {kRelease, "Release", 0.005f, 8.0f, 0.4f, "s"},
        {kVelocitySensitivity, "Velocity Sensitivity", 0.0f, 1.0f, 0.4f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void ScannedSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    voiceManager_.forEachVoice([&](ScannedVoice& voice) { voice.prepare(sampleRate); });
    chaine_.prepare(sampleRate);
}

void ScannedSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0) {
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
        // CHAQUE NOTE PINCE LA CHAÎNE, et la force suit la vélocité : c'est
        // le seul geste d'excitation de la machine. Jouer fort ne rend pas
        // seulement plus fort, cela remet la forme en mouvement.
        const float force = params_[kPluckForce].load(std::memory_order_relaxed)
                          * (0.3f + 0.7f * static_cast<float>(event.velocity) / 127.0f);
        chaine_.pincer(params_[kPluckPosition].load(std::memory_order_relaxed),
                       params_[kPluckHardness].load(std::memory_order_relaxed),
                       force);
    } else {
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
    }
}

void ScannedSynth::process(const MidiNoteEvent* events, int numEvents,
                           float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    ScannedVoice::Params p;
    p.cutoff = params_[kCutoff].load(std::memory_order_relaxed);
    p.resonance = params_[kResonance].load(std::memory_order_relaxed);
    p.velocityToLevel = params_[kVelocitySensitivity].load(std::memory_order_relaxed);
    p.bendSemitones = bendSemitones_.load(std::memory_order_relaxed);

    const float tension = params_[kTension].load(std::memory_order_relaxed);
    const float amortissement = params_[kDamping].load(std::memory_order_relaxed);
    const float rappel = params_[kCentering].load(std::memory_order_relaxed);

    const AdsrSettings env{
        params_[kAttack].load(std::memory_order_relaxed),
        params_[kDecay].load(std::memory_order_relaxed),
        params_[kSustain].load(std::memory_order_relaxed),
        params_[kRelease].load(std::memory_order_relaxed)};
    voiceManager_.forEachVoice([&](ScannedVoice& voice) { voice.setEnvelope(env); });

    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    constexpr float kVoiceGain = 0.35f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        // LA CHAÎNE VIT EN TEMPS RÉEL, indépendamment des notes : c'est le
        // trait de la famille, et c'est cette ligne qui le porte. Elle avance
        // qu'on joue ou non, à sa vitesse propre.
        chaine_.avancer(tension, amortissement, rappel);

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](ScannedVoice& voice) { sum += voice.render(p, chaine_); });
        sum *= kVoiceGain * outputLevel;
        outputL[i] = sum;
        outputR[i] = sum;
    }
}

void ScannedSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float ScannedSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState ScannedSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.scanned";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void ScannedSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.scanned", "Scanned (la forme qui vit)", ScannedSynth);

} // namespace vsm::plugins::scanned
