#include "PipeOrganSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::pipeorgan {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

PipeOrganSynth::PipeOrganSynth() {
    parameterList_ = {
        {kPrincipal, "Principal 8'", 0.0f, 1.0f, 1.0f, ""},
        {kFlute4, "Flute 4'", 0.0f, 1.0f, 0.4f, ""},
        {kMixture, "Mixture", 0.0f, 1.0f, 0.0f, ""},
        {kChiff, "Chiff", 0.0f, 1.0f, 0.6f, ""},
        {kWindSag, "Wind Sag", 0.0f, 1.0f, 0.3f, ""},
        {kTremulantRate, "Tremulant Rate", 0.0f, 10.0f, 0.0f, "Hz"},
        {kTremulantDepth, "Tremulant Depth", 0.0f, 1.0f, 0.5f, ""},
        {kAttack, "Attack", 5.0f, 200.0f, 25.0f, "ms"},
        {kRelease, "Release", 10.0f, 500.0f, 60.0f, "ms"},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void PipeOrganSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    uint64_t graine = 0x4F524755ULL;
    voiceManager_.forEachVoice([&](PipeOrganVoice& voice) { voice.prepare(sampleRate_, graine++); });
    tremulantPhase_ = 0.0;
    pressionLisse_ = 1.0f;
}

bool PipeOrganSynth::handleControlEvent(const MidiControlEvent&) {
    // Une soupape n'a ni molette ni pédale d'expression ici : refusé en
    // connaissance de cause, le moteur compte le refus.
    return false;
}

void PipeOrganSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void PipeOrganSynth::process(const MidiNoteEvent* events, int numEvents,
                             float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;
    PipeOrganVoice::Params p;
    p.principal = params_[kPrincipal].load(std::memory_order_relaxed);
    p.flute4 = params_[kFlute4].load(std::memory_order_relaxed);
    p.mixture = params_[kMixture].load(std::memory_order_relaxed);
    p.chiff = params_[kChiff].load(std::memory_order_relaxed);
    p.attackMs = params_[kAttack].load(std::memory_order_relaxed);
    p.releaseMs = params_[kRelease].load(std::memory_order_relaxed);
    const float sag = params_[kWindSag].load(std::memory_order_relaxed);
    const double tremRate = params_[kTremulantRate].load(std::memory_order_relaxed);
    const float tremDepth = params_[kTremulantDepth].load(std::memory_order_relaxed);
    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    constexpr float kVoiceGain = 0.5f;
    // LA SOUFFLERIE suit les notes avec l'inertie d'un réservoir : 40 ms.
    const float inertie = 1.0f - std::exp(-1.0f / (0.04f * static_cast<float>(sampleRate_)));

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);
        // LE VENT S'AFFAISSE avec le nombre de tuyaux ouverts : à sag 1, huit
        // notes prennent 30 % de pression à chacune.
        int ouverts = 0;
        voiceManager_.forEachVoice([&](PipeOrganVoice& voice) { if (voice.isHeld()) ++ouverts; });
        const float cible = 1.0f - sag * 0.3f * static_cast<float>(std::max(0, ouverts - 1)) / 7.0f;
        pressionLisse_ += inertie * (cible - pressionLisse_);
        // LE TREMBLANT : la pression ondule, donc la hauteur ET le niveau.
        float pression = pressionLisse_;
        if (tremRate > 0.05) {
            pression *= 1.0f - 0.5f * tremDepth * (1.0f - static_cast<float>(std::sin(tremulantPhase_)));
            tremulantPhase_ += 2.0 * M_PI * tremRate / sampleRate_;
            if (tremulantPhase_ > 2.0 * M_PI) tremulantPhase_ -= 2.0 * M_PI;
        }
        p.pressure = std::clamp(pression, 0.2f, 1.0f);
        float somme = 0.0f;
        voiceManager_.forEachVoice([&](PipeOrganVoice& voice) { somme += voice.render(p); });
        const float out = somme * kVoiceGain * outputLevel;
        outputL[i] = out;
        outputR[i] = out;
    }
}

void PipeOrganSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float PipeOrganSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState PipeOrganSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.pipeorgan";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void PipeOrganSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.pipeorgan", "Pipe Organ (une soufflerie commune)", PipeOrganSynth);

} // namespace vsm::plugins::pipeorgan
