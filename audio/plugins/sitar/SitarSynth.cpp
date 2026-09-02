#include "SitarSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::sitar {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

SitarSynth::SitarSynth() {
    parameterList_ = {
        {kSympatheticLevel, "Sympathetic Level", 0.0f, 1.0f, 0.45f, ""},
        {kSympatheticCount, "Sympathetic Strings", 1.0f, 13.0f, 11.0f, ""},
        {kSympatheticDecay, "Sympathetic Decay", 0.5f, 20.0f, 7.0f, "s"},
        {kSympatheticRoot, "Sympathetic Root", 24.0f, 72.0f, 45.0f, ""},
        {kSympatheticDamping, "Sympathetic Damping", 0.0f, 1.0f, 0.12f, ""},
        {kJawari, "Jawari", 0.0f, 1.0f, 0.6f, ""},
        {kDecay, "String Decay", 0.2f, 20.0f, 6.0f, "s"},
        {kDamping, "String Damping", 0.0f, 1.0f, 0.22f, ""},
        {kPickPosition, "Pick Position", 0.02f, 0.5f, 0.22f, ""},
        {kCutoff, "Filter Cutoff", 200.0f, 16000.0f, 11000.0f, "Hz"},
        {kResonance, "Filter Resonance", 0.0f, 0.95f, 0.05f, ""},
        {kVelocitySensitivity, "Velocity Sensitivity", 0.0f, 1.0f, 0.5f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void SitarSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t graine = 0x5117A5ULL;
    voiceManager_.forEachVoice([&](SitarVoice& voice) { voice.prepare(sampleRate, graine++); });
    // `prepare` alloue les lignes à retard : c'est ICI que cela doit se faire,
    // et nulle part ailleurs -- `process` n'a pas le droit d'allouer.
    sympathiques_.prepare(sampleRate);
    filtre_.setSampleRate(sampleRate);
    filtre_.reset();
}

void SitarSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void SitarSynth::process(const MidiNoteEvent* events, int numEvents,
                         float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    SitarVoice::Params p;
    p.decay = params_[kDecay].load(std::memory_order_relaxed);
    p.damping = params_[kDamping].load(std::memory_order_relaxed);
    p.pickPosition = params_[kPickPosition].load(std::memory_order_relaxed);
    p.jawari = params_[kJawari].load(std::memory_order_relaxed);
    p.velocitySensitivity = params_[kVelocitySensitivity].load(std::memory_order_relaxed);
    p.bendSemitones = bendSemitones_.load(std::memory_order_relaxed);

    // Les sympathiques se réaccordent UNE FOIS PAR BLOC, pas par échantillon :
    // douze `setTuning` toutes les 256 trames, soit quelques milliers d'appels
    // par seconde, ce qui ne se voit dans aucun profil.
    const float racine = params_[kSympatheticRoot].load(std::memory_order_relaxed);
    const int nombre = static_cast<int>(params_[kSympatheticCount].load(std::memory_order_relaxed) + 0.5f);
    const float t60 = params_[kSympatheticDecay].load(std::memory_order_relaxed);
    const float amortissement = params_[kSympatheticDamping].load(std::memory_order_relaxed);
    sympathiques_.accorder(racine, nombre, t60, amortissement);

    const float niveauSympathiques = params_[kSympatheticLevel].load(std::memory_order_relaxed);
    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    filtre_.setCutoffHz(params_[kCutoff].load(std::memory_order_relaxed));
    filtre_.setResonance(params_[kResonance].load(std::memory_order_relaxed));

    constexpr float kVoiceGain = 0.5f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        float cordes = 0.0f;
        voiceManager_.forEachVoice([&](SitarVoice& voice) { cordes += voice.render(p); });

        // LES SYMPATHIQUES TOURNENT TOUJOURS, même quand plus aucune voix ne
        // joue : c'est exactement ce qui fait que l'instrument continue de
        // sonner après le silence des notes. Les couper quand `activeVoiceCount`
        // tombe à zéro supprimerait le trait de la machine.
        const float sympa = sympathiques_.process(cordes);

        float sum = (cordes + niveauSympathiques * sympa) * kVoiceGain * outputLevel;
        sum = filtre_.process(sum);
        outputL[i] = sum;
        outputR[i] = sum;
    }
}

void SitarSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float SitarSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState SitarSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.sitar";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void SitarSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.sitar", "Sitar (les cordes qu'on ne joue pas)", SitarSynth);

} // namespace vsm::plugins::sitar
