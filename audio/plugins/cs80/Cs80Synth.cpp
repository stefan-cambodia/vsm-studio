#include "Cs80Synth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::cs80 {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace {
constexpr uint64_t kBaseSeed = 0x4353383000ULL; // "CS80"
} // namespace

Cs80Synth::Cs80Synth() {
    // DÉFAUTS QUI FONT ENTENDRE LA MACHINE : couche I large et sombre,
    // couche II plus brillante et légèrement désaccordée, mélange au milieu.
    // C'est la nappe qui a fait la réputation de l'original, et elle rend
    // la double couche audible dès la première note.
    parameterList_ = {
        {kLayerMix, "Layer Mix", 0.0f, 1.0f, 0.5f, ""},
        {kShapeI, "I Shape", 0.0f, 3.0f, 2.0f, ""},
        {kDetuneI, "I Detune", -24.0f, 24.0f, 0.0f, "st"},
        {kPulseWidthI, "I Pulse Width", 0.05f, 0.95f, 0.5f, ""},
        {kHighPassI, "I High Pass", 20.0f, 2000.0f, 20.0f, "Hz"},
        {kCutoffI, "I Cutoff", 40.0f, 16000.0f, 2200.0f, "Hz"},
        {kResonanceI, "I Resonance", 0.0f, 0.95f, 0.25f, ""},
        {kEnvAmountI, "I Env Amount", 0.0f, 1.0f, 0.35f, ""},
        {kLevelI, "I Level", 0.0f, 1.0f, 1.0f, ""},
        {kShapeII, "II Shape", 0.0f, 3.0f, 3.0f, ""},
        {kDetuneII, "II Detune", -24.0f, 24.0f, 0.07f, "st"},
        {kPulseWidthII, "II Pulse Width", 0.05f, 0.95f, 0.35f, ""},
        {kHighPassII, "II High Pass", 20.0f, 2000.0f, 120.0f, "Hz"},
        {kCutoffII, "II Cutoff", 40.0f, 16000.0f, 6000.0f, "Hz"},
        {kResonanceII, "II Resonance", 0.0f, 0.95f, 0.15f, ""},
        {kEnvAmountII, "II Env Amount", 0.0f, 1.0f, 0.5f, ""},
        {kLevelII, "II Level", 0.0f, 1.0f, 0.8f, ""},
        {kAmpAttackI, "I Amp Attack", 0.001f, 6.0f, 0.05f, "s"},
        {kAmpDecayI, "I Amp Decay", 0.005f, 8.0f, 0.8f, "s"},
        {kAmpSustainI, "I Amp Sustain", 0.0f, 1.0f, 0.85f, ""},
        {kAmpReleaseI, "I Amp Release", 0.005f, 8.0f, 0.6f, "s"},
        {kAmpAttackII, "II Amp Attack", 0.001f, 6.0f, 0.12f, "s"},
        {kAmpDecayII, "II Amp Decay", 0.005f, 8.0f, 1.2f, "s"},
        {kAmpSustainII, "II Amp Sustain", 0.0f, 1.0f, 0.7f, ""},
        {kAmpReleaseII, "II Amp Release", 0.005f, 8.0f, 0.9f, "s"},
        {kFilterAttack, "Filter Attack", 0.001f, 6.0f, 0.03f, "s"},
        {kFilterDecay, "Filter Decay", 0.005f, 8.0f, 1.0f, "s"},
        {kFilterSustain, "Filter Sustain", 0.0f, 1.0f, 0.45f, ""},
        {kFilterRelease, "Filter Release", 0.005f, 8.0f, 0.7f, "s"},
        {kPressureToCutoff, "Pressure to Cutoff", 0.0f, 3.0f, 1.2f, "oct"},
        {kPressureToLevel, "Pressure to Level", 0.0f, 1.0f, 0.3f, ""},
        {kVelocityToCutoff, "Velocity to Cutoff", 0.0f, 3.0f, 0.6f, "oct"},
        {kVelocityToLevel, "Velocity to Level", 0.0f, 1.0f, 0.4f, ""},
        {kAnalogCharacter, "Analog Character", 0.0f, 1.0f, 0.3f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void Cs80Synth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t index = 0;
    voiceManager_.forEachVoice([&](Cs80Voice& voice) {
        voice.prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, index));
        ++index;
    });
}

void Cs80Synth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

bool Cs80Synth::handleControlEvent(const MidiControlEvent& event) {
    // LA PRESSION POLYPHONIQUE VA DROIT À LA VOIX QUI PORTE LA NOTE, et c'est
    // tout l'objet de cette machine. Le § 10 du CDC nouvelle-machine notait
    // que le parc refusait ce message « parce qu'il n'a pas de modulation
    // par-voix, et l'honorer à moitié mentirait au musicien » ; ici, la
    // modulation par-voix existe, donc le message est honoré pour de vrai.
    if (event.kind == MidiControlEvent::Kind::PolyPressure) {
        bool trouvee = false;
        voiceManager_.forEachVoice([&](Cs80Voice& voice) {
            if (voice.isActive() && voice.note() == event.index) {
                voice.setPressure(event.value);
                trouvee = true;
            }
        });
        // UNE PRESSION SUR UNE NOTE QUI NE SONNE PAS N'EST PAS HONORÉE, et le
        // dire vaut mieux que l'avaler : le moteur la comptera comme ignorée,
        // ce qui est exact -- personne ne l'a entendue.
        return trouvee;
    }
    if (event.kind == MidiControlEvent::Kind::PitchBend) {
        bendSemitones_.store(event.value, std::memory_order_relaxed);
        return true;
    }
    // La pression de CANAL s'applique à toutes les voix : c'est le repli
    // d'un clavier qui n'a pas de capteur par touche, et un CS-80 joué
    // depuis un tel clavier doit répondre quand même.
    if (event.kind == MidiControlEvent::Kind::ChannelPressure) {
        channelPressure_.store(event.value, std::memory_order_relaxed);
        voiceManager_.forEachVoice([&](Cs80Voice& voice) {
            if (voice.isActive()) voice.setPressure(event.value);
        });
        return true;
    }
    return false;
}

void Cs80Synth::process(const MidiNoteEvent* events, int numEvents,
                        float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    Cs80Voice::Params p;
    p.layerMix = params_[kLayerMix].load(std::memory_order_relaxed);
    p.layer[0] = {params_[kShapeI].load(std::memory_order_relaxed),
                  params_[kDetuneI].load(std::memory_order_relaxed),
                  params_[kPulseWidthI].load(std::memory_order_relaxed),
                  params_[kHighPassI].load(std::memory_order_relaxed),
                  params_[kCutoffI].load(std::memory_order_relaxed),
                  params_[kResonanceI].load(std::memory_order_relaxed),
                  params_[kEnvAmountI].load(std::memory_order_relaxed),
                  params_[kLevelI].load(std::memory_order_relaxed)};
    p.layer[1] = {params_[kShapeII].load(std::memory_order_relaxed),
                  params_[kDetuneII].load(std::memory_order_relaxed),
                  params_[kPulseWidthII].load(std::memory_order_relaxed),
                  params_[kHighPassII].load(std::memory_order_relaxed),
                  params_[kCutoffII].load(std::memory_order_relaxed),
                  params_[kResonanceII].load(std::memory_order_relaxed),
                  params_[kEnvAmountII].load(std::memory_order_relaxed),
                  params_[kLevelII].load(std::memory_order_relaxed)};
    p.pressureToCutoff = params_[kPressureToCutoff].load(std::memory_order_relaxed);
    p.pressureToLevel = params_[kPressureToLevel].load(std::memory_order_relaxed);
    p.velocityToCutoff = params_[kVelocityToCutoff].load(std::memory_order_relaxed);
    p.velocityToLevel = params_[kVelocityToLevel].load(std::memory_order_relaxed);
    p.bendSemitones = bendSemitones_.load(std::memory_order_relaxed);

    const AdsrSettings ampI{
        params_[kAmpAttackI].load(std::memory_order_relaxed),
        params_[kAmpDecayI].load(std::memory_order_relaxed),
        params_[kAmpSustainI].load(std::memory_order_relaxed),
        params_[kAmpReleaseI].load(std::memory_order_relaxed)};
    const AdsrSettings ampII{
        params_[kAmpAttackII].load(std::memory_order_relaxed),
        params_[kAmpDecayII].load(std::memory_order_relaxed),
        params_[kAmpSustainII].load(std::memory_order_relaxed),
        params_[kAmpReleaseII].load(std::memory_order_relaxed)};
    const AdsrSettings filtre{
        params_[kFilterAttack].load(std::memory_order_relaxed),
        params_[kFilterDecay].load(std::memory_order_relaxed),
        params_[kFilterSustain].load(std::memory_order_relaxed),
        params_[kFilterRelease].load(std::memory_order_relaxed)};
    const float drift = params_[kAnalogCharacter].load(std::memory_order_relaxed);
    voiceManager_.forEachVoice([&](Cs80Voice& voice) {
        voice.setEnvelopes(0, ampI, filtre);
        voice.setEnvelopes(1, ampII, filtre);
        voice.setDriftAmount(drift);
    });

    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    // Huit voix de DEUX couches : c'est seize chaînes qui peuvent sonner
    // ensemble, d'où un facteur plus bas que les autres polyphoniques.
    constexpr float kVoiceGain = 0.22f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](Cs80Voice& voice) { sum += voice.render(p); });
        sum *= kVoiceGain * outputLevel;
        outputL[i] = sum;
        outputR[i] = sum;
    }
}

void Cs80Synth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float Cs80Synth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState Cs80Synth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.cs80";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void Cs80Synth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.cs80", "CS-80-style (deux couches, pression par note)", Cs80Synth);

} // namespace vsm::plugins::cs80
