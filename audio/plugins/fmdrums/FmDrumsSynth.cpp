#include "FmDrumsSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>

namespace vsm::plugins::fmdrums {

using namespace vsm::audio::plugin;

FmDrumsSynth::FmDrumsSynth() {
    // UNITÉS PHYSIQUES : des hertz pour les hauteurs, des secondes pour les
    // durées. Les RAPPORTS sont sans unité par nature -- c'est un rapport de
    // fréquences -- et les INDICES sont en radians de déviation de phase, ce
    // qui est la façon dont la modulation de fréquence se mesure vraiment.
    //
    // Les rapports par défaut sont NON ENTIERS, et c'est le sujet de la
    // machine : 1,414 (racine de deux) et 1,618 (nombre d'or) ne peuvent être
    // le rapport d'aucun couple d'harmoniques, donc rien de ce qu'ils
    // produisent ne tombe sur la série harmonique.
    parameterList_ = {
        {kKickLevel, "Kick Level", 0.0f, 1.0f, 0.9f, ""},
        {kKickTune, "Kick Tune", 30.0f, 120.0f, 55.0f, "Hz"},
        {kKickDecay, "Kick Decay", 0.05f, 1.2f, 0.35f, "s"},
        {kKickRatio, "Kick Ratio", 0.25f, 8.0f, 1.414f, ""},
        {kKickIndex, "Kick Clang", 0.0f, 12.0f, 3.0f, "rad"},
        {kSnareLevel, "Snare Level", 0.0f, 1.0f, 0.8f, ""},
        {kSnareTune, "Snare Tune", 120.0f, 400.0f, 210.0f, "Hz"},
        {kSnareDecay, "Snare Decay", 0.03f, 0.6f, 0.16f, "s"},
        {kSnareRatio, "Snare Ratio", 0.25f, 8.0f, 2.732f, ""},
        {kSnareIndex, "Snare Clang", 0.0f, 12.0f, 5.5f, "rad"},
        {kTomLevel, "Tom Level", 0.0f, 1.0f, 0.8f, ""},
        {kTomTune, "Tom Tune", 60.0f, 300.0f, 130.0f, "Hz"},
        {kTomDecay, "Tom Decay", 0.05f, 1.0f, 0.35f, "s"},
        {kTomRatio, "Tom Ratio", 0.25f, 8.0f, 1.618f, ""},
        {kTomIndex, "Tom Clang", 0.0f, 12.0f, 2.5f, "rad"},
        {kBellLevel, "Bell Level", 0.0f, 1.0f, 0.7f, ""},
        {kBellTune, "Bell Tune", 200.0f, 1600.0f, 560.0f, "Hz"},
        {kBellDecay, "Bell Decay", 0.1f, 3.0f, 1.1f, "s"},
        {kBellRatio, "Bell Ratio", 0.25f, 8.0f, 3.414f, ""},
        {kBellIndex, "Bell Clang", 0.0f, 12.0f, 7.0f, "rad"},
        {kHatLevel, "Hat Level", 0.0f, 1.0f, 0.7f, ""},
        {kHatTone, "Hat Tone", 3000.0f, 14000.0f, 8000.0f, "Hz"},
        {kClosedHatDecay, "Closed Hat Decay", 0.01f, 0.2f, 0.05f, "s"},
        {kOpenHatDecay, "Open Hat Decay", 0.1f, 1.2f, 0.45f, "s"},
        {kClapLevel, "Clap Level", 0.0f, 1.0f, 0.75f, ""},
        {kClapDecay, "Clap Decay", 0.03f, 0.5f, 0.14f, "s"},
        {kAccent, "Accent", 0.0f, 1.0f, 0.5f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void FmDrumsSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    kick_.setSampleRate(sampleRate);
    snare_.setSampleRate(sampleRate);
    tom_.setSampleRate(sampleRate);
    bell_.setSampleRate(sampleRate);
    clap_.setSampleRate(sampleRate);
    closedHat_.setSampleRate(sampleRate);
    openHat_.setSampleRate(sampleRate);
    applyConfig();
}

void FmDrumsSynth::applyConfig() {
    auto lire = [this](ParamId id) { return params_[id].load(std::memory_order_relaxed); };

    // Le kick garde un BALAYAGE DE HAUTEUR : c'est ce que toutes les grosses
    // caisses font, analogiques comme numériques, et s'en priver donnerait une
    // note tenue au lieu d'un coup.
    kick_.configure(lire(kKickTune), lire(kKickRatio), lire(kKickIndex),
                    lire(kKickDecay), lire(kKickLevel), 3.5f);
    snare_.configure(lire(kSnareTune), lire(kSnareRatio), lire(kSnareIndex),
                     lire(kSnareDecay), lire(kSnareLevel), 0.6f);
    tom_.configure(lire(kTomTune), lire(kTomRatio), lire(kTomIndex),
                   lire(kTomDecay), lire(kTomLevel), 1.2f);
    bell_.configure(lire(kBellTune), lire(kBellRatio), lire(kBellIndex),
                    lire(kBellDecay), lire(kBellLevel), 0.0f);
    // Le clap est la caisse claire, plus court et plus haut : sur une machine à
    // deux opérateurs, en faire une pièce séparée avec ses propres rapports
    // n'apporterait qu'un jeu de réglages de plus à chercher.
    clap_.configure(lire(kSnareTune) * 1.45f, lire(kSnareRatio) * 1.3f,
                    lire(kSnareIndex) * 1.2f, lire(kClapDecay), lire(kClapLevel), 0.3f);
    closedHat_.configure(lire(kHatTone), lire(kClosedHatDecay), lire(kHatLevel));
    openHat_.configure(lire(kHatTone) * 0.85f, lire(kOpenHatDecay), lire(kHatLevel));
}

void FmDrumsSynth::triggerNote(uint8_t note, uint8_t velocity) {
    const float accent = params_[kAccent].load(std::memory_order_relaxed);
    const float velNorm = static_cast<float>(velocity) / 127.0f;
    const float velGain = (0.35f + 0.65f * velNorm) * (1.0f + accent * 0.5f * velNorm);

    switch (note) {
        case kNoteKick: kick_.trigger(velGain); break;
        case kNoteSnare: snare_.trigger(velGain); break;
        case kNoteClap: clap_.trigger(velGain); break;
        case kNoteTom: tom_.trigger(velGain); break;
        case kNoteBell: bell_.trigger(velGain); break;
        case kNoteClosedHat: closedHat_.trigger(velGain); openHat_.choke(); break;
        case kNoteOpenHat: openHat_.trigger(velGain); break;
        default: break;
    }
}

void FmDrumsSynth::process(const MidiNoteEvent* events, int numEvents,
                           float* outputL, float* outputR, int numSamples) {
    applyConfig();

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i) {
            const auto& ev = events[eventIndex];
            if (ev.kind == MidiNoteEvent::Kind::NoteOn && ev.velocity > 0)
                triggerNote(ev.note, ev.velocity);
            ++eventIndex;
        }

        const float sum = kick_.render() + snare_.render() + tom_.render()
                        + bell_.render() + clap_.render()
                        + closedHat_.render() + openHat_.render();
        // Niveau puis saturation douce, comme `vsm.perc` et pour la même raison
        // mesurée : sept pièces frappées ensemble s'additionnent en phase --
        // ici, toutes les porteuses démarrent à zéro. La tangente hyperbolique
        // borne sans casser la dynamique utile ; le facteur 0,45 est celui des
        // boîtes du parc, pour qu'aucune ne se départage au volume.
        outputL[i] = std::tanh(sum * 0.45f);
        outputR[i] = outputL[i];
    }
}

int FmDrumsSynth::activeVoiceCount() const {
    return (kick_.isActive() ? 1 : 0) + (snare_.isActive() ? 1 : 0)
         + (tom_.isActive() ? 1 : 0) + (bell_.isActive() ? 1 : 0)
         + (clap_.isActive() ? 1 : 0)
         + (closedHat_.isActive() ? 1 : 0) + (openHat_.isActive() ? 1 : 0);
}

void FmDrumsSynth::setParameter(ParamId id, float value) {
    if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}

float FmDrumsSynth::getParameter(ParamId id) const {
    return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

const ParameterList& FmDrumsSynth::parameterList() const { return parameterList_; }

PresetState FmDrumsSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.fmdrums";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void FmDrumsSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.fmdrums", "FM Drums (percussions métalliques)", FmDrumsSynth);

} // namespace vsm::plugins::fmdrums
