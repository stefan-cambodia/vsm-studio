#include "ChebyshevSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::chebyshev {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace {
constexpr uint64_t kBaseSeed = 0x43484542ULL; // "CHEB"
/// Quatre fois : un polynôme de rang 8 occupe huit fois la bande du sinus
/// d'entrée. Au-delà de quatre, le filtre coûte plus qu'il ne rapporte sur
/// les notes réellement jouables.
constexpr int kOversampling = 4;
} // namespace

ChebyshevSynth::ChebyshevSynth() {
    // DÉFAUTS : les rangs 1, 2 et 3 décroissants -- un timbre de cuivre doux,
    // qui s'ouvre quand on joue fort. C'est le son que cette famille rend le
    // plus naturellement, et il fait entendre l'index dès la première note.
    parameterList_ = {
        {kIndex, "Index", 0.0f, 1.0f, 0.8f, ""},
        {kVelocityToIndex, "Velocity to Index", 0.0f, 1.0f, 0.6f, ""},
        {kW1, "Partial 1", 0.0f, 1.0f, 1.0f, ""},
        {kW2, "Partial 2", 0.0f, 1.0f, 0.5f, ""},
        {kW3, "Partial 3", 0.0f, 1.0f, 0.3f, ""},
        {kW4, "Partial 4", 0.0f, 1.0f, 0.0f, ""},
        {kW5, "Partial 5", 0.0f, 1.0f, 0.0f, ""},
        {kW6, "Partial 6", 0.0f, 1.0f, 0.0f, ""},
        {kW7, "Partial 7", 0.0f, 1.0f, 0.0f, ""},
        {kW8, "Partial 8", 0.0f, 1.0f, 0.0f, ""},
        {kAttack, "Attack", 0.001f, 4.0f, 0.01f, "s"},
        {kDecay, "Decay", 0.005f, 8.0f, 0.6f, "s"},
        {kSustain, "Sustain", 0.0f, 1.0f, 0.7f, ""},
        {kRelease, "Release", 0.005f, 8.0f, 0.4f, "s"},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void ChebyshevSynth::initialize(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    uint64_t index = 0;
    voiceManager_.forEachVoice([&](ChebyshevVoice& voice) {
        voice.prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, index));
        ++index;
    });
    // TOUT S'ALLOUE ICI, jamais dans process() : l'invariant n° 2 du § 6 de
    // ROADMAP-daw, désormais vérifié machine par machine.
    const int taille = std::max(maxBlockSize, 4096);
    oversampler_.prepare(kOversampling, taille);
    bloc_.assign(static_cast<size_t>(taille), 0.0f);
}

void ChebyshevSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void ChebyshevSynth::process(const MidiNoteEvent* events, int numEvents,
                             float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    ChebyshevVoice::Params p;
    p.index = params_[kIndex].load(std::memory_order_relaxed);
    p.velocityToIndex = params_[kVelocityToIndex].load(std::memory_order_relaxed);
    p.bendSemitones = bendSemitones_.load(std::memory_order_relaxed);

    std::array<float, kPartials> poids{};
    for (int n = 0; n < kPartials; ++n)
        poids[static_cast<size_t>(n)] =
            params_[static_cast<ParamId>(kW1 + n)].load(std::memory_order_relaxed);

    const AdsrSettings env{
        params_[kAttack].load(std::memory_order_relaxed),
        params_[kDecay].load(std::memory_order_relaxed),
        params_[kSustain].load(std::memory_order_relaxed),
        params_[kRelease].load(std::memory_order_relaxed)};
    voiceManager_.forEachVoice([&](ChebyshevVoice& voice) { voice.setEnvelope(env); });

    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    // La somme des poids borne la sortie du shaper : on normalise par elle
    // pour qu'ajouter un rang n'augmente pas le volume, seulement le timbre.
    float sommePoids = 0.0f;
    for (float w : poids) sommePoids += std::abs(w);
    const float normalisation = 1.0f / std::max(0.25f, sommePoids);

    const int count = std::min(numSamples, static_cast<int>(bloc_.size()));
    int eventIndex = 0;
    for (int i = 0; i < count; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        // LES VOIX RENDENT LEUR SINUS ; le shaper vient après, une seule fois
        // -- c'est le choix d'un waveshaper analogique, et l'intermodulation
        // qui en résulte fait partie du son (voir l'en-tête).
        float sum = 0.0f;
        voiceManager_.forEachVoice([&](ChebyshevVoice& voice) { sum += voice.render(p); });
        bloc_[static_cast<size_t>(i)] = std::clamp(sum, -1.0f, 1.0f);
    }

    // LE SHAPER TOURNE EN SUR-ÉCHANTILLONNÉ : un rang 8 occupe huit fois la
    // bande, et sans cela les rangs hauts redescendraient en sifflements
    // inharmoniques -- ruinant l'argument même de la machine, qui est un
    // spectre exact.
    oversampler_.processBlock(bloc_.data(), count,
                              [&poids](float x) { return shape(x, poids); });

    for (int i = 0; i < count; ++i) {
        const float out = bloc_[static_cast<size_t>(i)] * normalisation * outputLevel;
        outputL[i] = out;
        outputR[i] = out;
    }
    for (int i = count; i < numSamples; ++i) { outputL[i] = 0.0f; outputR[i] = 0.0f; }
}

void ChebyshevSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float ChebyshevSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState ChebyshevSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.chebyshev";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void ChebyshevSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.chebyshev", "Chebyshev (le spectre commandé)", ChebyshevSynth);

} // namespace vsm::plugins::chebyshev
