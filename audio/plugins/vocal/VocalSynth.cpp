#include "VocalSynth.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::vocal {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace {
constexpr uint64_t kBaseSeed = 0x564F43414C00ULL; // "VOCAL"
} // namespace

VocalSynth::VocalSynth() {
    // UNITÉS PHYSIQUES : des demi-tons pour le décalage du conduit -- une gorge
    // plus courte transpose ses formants, et le demi-ton est la façon dont un
    // musicien pense une transposition --, des hertz pour le vibrato, des
    // secondes pour les temps. La VOYELLE est un index continu de 0 à 4 : ce
    // n'est pas un normalisé déguisé, c'est une position sur le trapèze
    // vocalique, et les valeurs entières y sont des voyelles nommées.
    parameterList_ = {
        {kVowel, "Vowel", 0.0f, 4.0f, 0.0f, "a e i o u"},
        {kFormantShift, "Formant Shift", -12.0f, 12.0f, 0.0f, "demi-tons"},
        {kBreath, "Breath", 0.0f, 1.0f, 0.2f, ""},
        {kTension, "Tension", 0.0f, 1.0f, 0.5f, ""},
        {kVibratoRate, "Vibrato Rate", 0.5f, 9.0f, 5.2f, "Hz"},
        {kVibratoDepth, "Vibrato Depth", 0.0f, 1.0f, 0.25f, ""},
        {kVibratoDelay, "Vibrato Delay", 0.0f, 2.0f, 0.35f, "s"},
        {kAmpAttack, "Amp Attack", 0.001f, 2.0f, 0.06f, "s"},
        {kAmpDecay, "Amp Decay", 0.005f, 4.0f, 0.3f, "s"},
        {kAmpSustain, "Amp Sustain", 0.0f, 1.0f, 0.85f, ""},
        {kAmpRelease, "Amp Release", 0.005f, 4.0f, 0.25f, "s"},
        {kVelocityToBreath, "Velocity to Breath", 0.0f, 1.0f, 0.3f, ""},
        {kAnalogCharacter, "Analog Character", 0.0f, 1.0f, 0.2f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void VocalSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t index = 0;
    voiceManager_.forEachVoice([&](VocalVoice& voice) {
        voice.prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, index));
        ++index;
    });
}

void VocalSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void VocalSynth::process(const MidiNoteEvent* events, int numEvents,
                         float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    VocalVoice::Params p;
    p.vowel = std::clamp(params_[kVowel].load(std::memory_order_relaxed), 0.0f, 4.0f);
    p.formantShift = params_[kFormantShift].load(std::memory_order_relaxed);
    p.breath = params_[kBreath].load(std::memory_order_relaxed);
    p.tension = params_[kTension].load(std::memory_order_relaxed);
    p.vibratoRate = params_[kVibratoRate].load(std::memory_order_relaxed);
    p.vibratoDepth = params_[kVibratoDepth].load(std::memory_order_relaxed);
    p.vibratoDelay = params_[kVibratoDelay].load(std::memory_order_relaxed);
    p.velocityToBreath = params_[kVelocityToBreath].load(std::memory_order_relaxed);

    const AdsrSettings amp{
        params_[kAmpAttack].load(std::memory_order_relaxed),
        params_[kAmpDecay].load(std::memory_order_relaxed),
        params_[kAmpSustain].load(std::memory_order_relaxed),
        params_[kAmpRelease].load(std::memory_order_relaxed),
    };
    const float drift = params_[kAnalogCharacter].load(std::memory_order_relaxed);
    voiceManager_.forEachVoice([&](VocalVoice& voice) {
        voice.setSettings(amp);
        voice.setDriftAmount(drift);
    });

    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    // NIVEAU CALIBRÉ SUR LA NORME DU PARC : un accord de huit notes doit crêter
    // dans la fenêtre des autres polyphoniques (0,57 au Jupiter-8, 0,94 au
    // Juno-106), et une note seule sortir dans leur plage. Un chœur de huit voix
    // sur la même voyelle s'additionne fort -- les trois formants sont aux mêmes
    // fréquences pour toutes les voix, donc leurs sorties sont corrélées, ce qui
    // n'est le cas d'aucune autre machine du parc.
    constexpr float kVoiceGain = 0.16f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](VocalVoice& voice) { sum += voice.render(p); });
        sum *= kVoiceGain * outputLevel;
        outputL[i] = sum;
        outputR[i] = sum;
    }
}

void VocalSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float VocalSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState VocalSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.vocal";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void VocalSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.vocal", "Vocal (conduit vocal, voyelles)", VocalSynth);

} // namespace vsm::plugins::vocal
