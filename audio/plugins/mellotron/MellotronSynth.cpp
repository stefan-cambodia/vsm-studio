#include "MellotronSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::mellotron {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

MellotronSynth::MellotronSynth() {
    // DÉFAUTS PRIS SUR L'INSTRUMENT. Les huit secondes ne sont pas un chiffre
    // rond choisi pour faire joli : c'est la longueur de bande d'un Mellotron
    // M400 sous chaque touche, et c'est elle qui a écrit la manière d'en
    // jouer. Le pleurage à 0,6 Hz et une douzaine de cents est le régime où
    // l'instrument « respire » sans sonner cassé.
    parameterList_ = {
        {kTapeLength, "Tape Length", 1.0f, 20.0f, 8.0f, "s"},
        {kRewindTime, "Rewind Time", 0.05f, 4.0f, 0.9f, "s"},
        {kWowDepth, "Wow Depth", 0.0f, 60.0f, 12.0f, "cents"},
        {kWowRate, "Wow Rate", 0.05f, 4.0f, 0.6f, "Hz"},
        {kFlutterDepth, "Flutter Depth", 0.0f, 30.0f, 4.0f, "cents"},
        {kTone, "Tone", 0.0f, 1.0f, 0.45f, ""},
        {kHiss, "Tape Hiss", 0.0f, 1.0f, 0.15f, ""},
        {kCutoff, "Filter Cutoff", 40.0f, 16000.0f, 4500.0f, "Hz"},
        {kResonance, "Filter Resonance", 0.0f, 0.95f, 0.1f, ""},
        {kAttack, "Attack", 0.001f, 4.0f, 0.06f, "s"},
        {kDecay, "Decay", 0.005f, 8.0f, 1.0f, "s"},
        {kSustain, "Sustain", 0.0f, 1.0f, 0.9f, ""},
        {kRelease, "Release", 0.005f, 8.0f, 0.25f, "s"},
        {kVelocitySensitivity, "Velocity Sensitivity", 0.0f, 1.0f, 0.4f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void MellotronSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t graine = 0x7A9E5ULL;
    voiceManager_.forEachVoice([&](MellotronVoice& voice) { voice.prepare(sampleRate, graine++); });
    positionBande_.fill(0.0f);
    toucheEnfoncee_.fill(false);
}

void MellotronSynth::applyNoteEvent(const MidiNoteEvent& event) {
    const auto touche = static_cast<size_t>(event.note & 0x7F);
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0) {
        MellotronVoice* voix = voiceManager_.noteOn(event.channel, event.note, event.velocity);
        // La voix reprend la bande LÀ OÙ LA TOUCHE l'avait laissée. Rejouer
        // avant la fin du rembobinage donne donc une note plus courte -- le
        // trait n° 4, et la raison pour laquelle un mellotroniste apprend à
        // espacer ses reprises.
        if (voix != nullptr) voix->placerBande(positionBande_[touche]);
        toucheEnfoncee_[touche] = true;
    } else {
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
        toucheEnfoncee_[touche] = false;
    }
}

void MellotronSynth::rembobiner(int numSamples) {
    // Le rembobinage est mis à jour UNE FOIS PAR BLOC, et c'est assez : il
    // n'entre dans aucun calcul d'échantillon, il ne fait que dire d'où la
    // prochaine note repartira. Le faire par échantillon coûterait 128
    // soustractions pour un résultat identique à l'oreille.
    const float vitesse = 1.0f / std::max(0.05f, params_[kRewindTime].load(std::memory_order_relaxed));
    const float dt = static_cast<float>(numSamples) / static_cast<float>(sampleRate_);
    const float recul = vitesse * dt * params_[kTapeLength].load(std::memory_order_relaxed);

    // Une touche TENUE avance : sa bande est celle que joue la voix, et il
    // faut que la touche la suive, sinon relâcher une note longue la ferait
    // repartir de zéro.
    voiceManager_.forEachVoice([&](MellotronVoice& voice) {
        if (voice.isActive())
            positionBande_[static_cast<size_t>(voice.note() & 0x7F)] = voice.positionBande();
    });
    for (size_t k = 0; k < positionBande_.size(); ++k) {
        if (toucheEnfoncee_[k] || positionBande_[k] <= 0.0f) continue;
        positionBande_[k] = std::max(0.0f, positionBande_[k] - recul);
    }
}

void MellotronSynth::process(const MidiNoteEvent* events, int numEvents,
                        float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    MellotronVoice::Params p;
    p.tone = params_[kTone].load(std::memory_order_relaxed);
    p.hiss = params_[kHiss].load(std::memory_order_relaxed);
    p.wowDepth = params_[kWowDepth].load(std::memory_order_relaxed);
    p.wowRate = params_[kWowRate].load(std::memory_order_relaxed);
    p.flutterDepth = params_[kFlutterDepth].load(std::memory_order_relaxed);
    p.cutoff = params_[kCutoff].load(std::memory_order_relaxed);
    p.resonance = params_[kResonance].load(std::memory_order_relaxed);
    p.velocityToLevel = params_[kVelocitySensitivity].load(std::memory_order_relaxed);
    p.bendSemitones = bendSemitones_.load(std::memory_order_relaxed);
    p.tapeLength = params_[kTapeLength].load(std::memory_order_relaxed);

    const AdsrSettings env{
        params_[kAttack].load(std::memory_order_relaxed),
        params_[kDecay].load(std::memory_order_relaxed),
        params_[kSustain].load(std::memory_order_relaxed),
        params_[kRelease].load(std::memory_order_relaxed)};
    voiceManager_.forEachVoice([&](MellotronVoice& voice) { voice.setEnvelope(env); });

    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    constexpr float kVoiceGain = 0.45f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](MellotronVoice& voice) { sum += voice.render(p); });
        sum *= kVoiceGain * outputLevel;
        outputL[i] = sum;
        outputR[i] = sum;
    }

    rembobiner(numSamples);
}

void MellotronSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float MellotronSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState MellotronSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.mellotron";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void MellotronSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.mellotron", "Tape (la bande qui finit)", MellotronSynth);

} // namespace vsm::plugins::mellotron
