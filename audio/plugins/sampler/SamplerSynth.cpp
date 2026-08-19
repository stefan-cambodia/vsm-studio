#include "SamplerSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cstdio>

namespace vsm::plugins::sampler {

using namespace vsm::audio::plugin;
using vsm::audio::io::SampleBuffer;
using vsm::audio::io::SampleBufferPtr;
using vsm::audio::io::WavFileReader;

namespace {

/// Interpolation cubique (Catmull-Rom) : quatre points au lieu de deux.
/// L'interpolation linéaire suffirait à faire "du son", mais elle ajoute un
/// repliement audible dès qu'on transpose vers le haut -- exactement ce qu'un
/// outil de reconstruction ne doit pas ajouter, puisqu'il serait ensuite
/// mesuré comme un écart avec l'original.
float interpolate(const std::vector<float>& data, double position) {
    const long long index = static_cast<long long>(position);
    const double fraction = position - static_cast<double>(index);
    const long long size = static_cast<long long>(data.size());
    if (size == 0) return 0.0f;

    auto at = [&data, size](long long i) -> double {
        if (i < 0) i = 0;
        if (i >= size) i = size - 1;
        return static_cast<double>(data[static_cast<size_t>(i)]);
    };

    const double p0 = at(index - 1), p1 = at(index), p2 = at(index + 1), p3 = at(index + 2);
    const double a = -0.5 * p0 + 1.5 * p1 - 1.5 * p2 + 0.5 * p3;
    const double b = p0 - 2.5 * p1 + 2.0 * p2 - 0.5 * p3;
    const double c = -0.5 * p0 + 0.5 * p2;
    return static_cast<float>(((a * fraction + b) * fraction + c) * fraction + p1);
}

} // namespace

SamplerSynth::SamplerSynth() {
    parameterList_.push_back({kMasterLevel, "Master Level", 0.0f, 2.0f, 1.0f, ""});

    for (int slot = 0; slot < kSlotCount; ++slot) {
        char name[64];
        const int display = slot + 1;

        // Notes par défaut : la convention General MIDI de la batterie, pour
        // qu'un fichier MIDI de batterie transcrit par l'analyse tombe
        // directement sur les bons emplacements, sans réglage préalable.
        // Notes par défaut : la convention General MIDI pour les huit
        // premières pièces (grosse caisse, caisse claire, charlestons...),
        // puis les toms, cymbales et percussions qui la prolongent. Un kit
        // exporté se relit ainsi dans n'importe quel séquenceur avec les bons
        // noms de pièces.
        static const float defaultNotes[kSlotCount] = {
            36, 38, 42, 46, 39, 45, 49, 51,   // GM : kick, snare, hats, clap, toms, cymbales
            41, 43, 47, 50, 37, 40, 53, 56,   // toms graves/aigus, rimshot, cloche
        };

        std::snprintf(name, sizeof(name), "Slot %d Note", display);
        parameterList_.push_back({slotParam(slot, kSlotNote), name, 0.0f, 127.0f, defaultNotes[slot], ""});
        std::snprintf(name, sizeof(name), "Slot %d Tune", display);
        parameterList_.push_back({slotParam(slot, kSlotTune), name, -24.0f, 24.0f, 0.0f, "st"});
        std::snprintf(name, sizeof(name), "Slot %d Level", display);
        parameterList_.push_back({slotParam(slot, kSlotLevel), name, 0.0f, 2.0f, 1.0f, ""});
        std::snprintf(name, sizeof(name), "Slot %d Pan", display);
        parameterList_.push_back({slotParam(slot, kSlotPan), name, -1.0f, 1.0f, 0.0f, ""});
        std::snprintf(name, sizeof(name), "Slot %d Decay", display);
        parameterList_.push_back({slotParam(slot, kSlotDecay), name, 0.0f, 4.0f, 0.0f, "s"});
        std::snprintf(name, sizeof(name), "Slot %d Start", display);
        parameterList_.push_back({slotParam(slot, kSlotStart), name, 0.0f, 1.0f, 0.0f, ""});
        std::snprintf(name, sizeof(name), "Slot %d Choke", display);
        parameterList_.push_back({slotParam(slot, kSlotChoke), name, 0.0f, 4.0f, 0.0f, ""});
    }

    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void SamplerSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    // Les voix repartent de zéro : deux rendus d'une même requête doivent
    // donner le même son (condition du service de rendu et des empreintes).
    for (auto& voice : voices_) voice = Voice{};
    nextVoice_ = 0;
}

void SamplerSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float SamplerSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState SamplerSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.sampler";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = getParameter(info.id);
    return state;
}

void SamplerSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues) setParameter(id, value);
}

int SamplerSynth::activeVoiceCount() const {
    int count = 0;
    for (const auto& voice : voices_) if (voice.active) ++count;
    return count;
}

bool SamplerSynth::loadSample(int slot, const std::string& path, std::string& outError) {
    if (slot < 0 || slot >= kSlotCount) { outError = "emplacement hors bornes"; return false; }

    auto result = WavFileReader::readFile(path);
    if (!result.success) {
        // Échec SIGNALÉ, emplacement laissé tel quel : substituer un autre son
        // donnerait un rendu faux que personne ne rattacherait au fichier
        // manquant.
        outError = result.error;
        return false;
    }
    if (result.buffer.empty()) { outError = "fichier sans échantillon : " + path; return false; }

    setSample(slot, std::make_shared<const SampleBuffer>(std::move(result.buffer)));
    slotPaths_[static_cast<size_t>(slot)] = path;
    return true;
}

void SamplerSynth::setSample(int slot, SampleBufferPtr sample) {
    if (slot < 0 || slot >= kSlotCount) return;
    slots_[static_cast<size_t>(slot)].store(std::move(sample), std::memory_order_release);
}

void SamplerSynth::clearSample(int slot) {
    if (slot < 0 || slot >= kSlotCount) return;
    slots_[static_cast<size_t>(slot)].store(nullptr, std::memory_order_release);
    slotPaths_[static_cast<size_t>(slot)].clear();
}

std::string SamplerSynth::samplePath(int slot) const {
    if (slot < 0 || slot >= kSlotCount) return {};
    return slotPaths_[static_cast<size_t>(slot)];
}

void SamplerSynth::triggerSlot(int slot, uint8_t velocity) {
    auto sample = slots_[static_cast<size_t>(slot)].load(std::memory_order_acquire);
    if (!sample || sample->empty()) return; // emplacement vide : silence, pas de son de repli

    const int chokeGroup = static_cast<int>(std::lround(slotValue(slot, kSlotChoke)));
    if (chokeGroup > 0) {
        // Coupure : charleston fermé qui étouffe l'ouvert. C'est le seul
        // couplage entre voix d'une boîte à rythmes, et il est indispensable
        // -- sans lui, les deux charlestons sonnent ensemble.
        for (auto& voice : voices_)
            if (voice.active && voice.chokeGroup == chokeGroup) voice.active = false;
    }

    Voice& voice = voices_[nextVoice_];
    nextVoice_ = (nextVoice_ + 1) % voices_.size();

    voice.sample = sample;
    voice.slot = slot;
    voice.chokeGroup = chokeGroup;

    const double start = std::clamp(static_cast<double>(slotValue(slot, kSlotStart)), 0.0, 0.99);
    voice.position = start * static_cast<double>(sample->numFrames());

    // Deux facteurs : l'accord demandé, et la conversion entre la fréquence du
    // FICHIER et celle du moteur. Oublier le second ferait jouer un
    // échantillon 44,1 kHz trop grave sur un moteur à 48 kHz.
    const double tune = static_cast<double>(slotValue(slot, kSlotTune));
    voice.increment = std::pow(2.0, tune / 12.0) * (sample->sampleRate / sampleRate_);

    const float velocityGain = static_cast<float>(velocity) / 127.0f;
    voice.level = slotValue(slot, kSlotLevel) * velocityGain;

    const float pan = std::clamp(slotValue(slot, kSlotPan), -1.0f, 1.0f);
    const float angle = (pan + 1.0f) * 0.25f * 3.14159265358979f; // panoramique à puissance constante
    voice.panLeft = std::cos(angle);
    voice.panRight = std::sin(angle);

    voice.envelope = 1.0f;
    const float decay = slotValue(slot, kSlotDecay);
    voice.envelopeDecay = decay > 0.0f
        ? static_cast<float>(1.0 / (static_cast<double>(decay) * sampleRate_))
        : 0.0f;
    voice.active = true;
}

void SamplerSynth::process(const MidiNoteEvent* events, int numEvents,
                            float* outputL, float* outputR, int numSamples) {
    vsm::audio::dsp::ScopedNoDenormals noDenormals;
    std::fill(outputL, outputL + numSamples, 0.0f);
    std::fill(outputR, outputR + numSamples, 0.0f);

    const float master = params_[kMasterLevel].load(std::memory_order_relaxed);
    int eventIndex = 0;

    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset <= i) {
            const MidiNoteEvent& event = events[eventIndex];
            // Un NoteOff n'arrête rien : un coup de batterie se joue jusqu'au
            // bout. C'est la convention des boîtes à rythmes, et elle évite
            // qu'une note courte tronque une cymbale.
            if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0) {
                for (int slot = 0; slot < kSlotCount; ++slot) {
                    const int slotNote = static_cast<int>(std::lround(slotValue(slot, kSlotNote)));
                    if (slotNote == static_cast<int>(event.note)) triggerSlot(slot, event.velocity);
                }
            }
            ++eventIndex;
        }

        float left = 0.0f, right = 0.0f;
        for (auto& voice : voices_) {
            if (!voice.active) continue;
            const SampleBuffer& sample = *voice.sample;

            if (voice.position >= static_cast<double>(sample.numFrames())) { voice.active = false; continue; }

            const float mono = interpolate(sample.left, voice.position);
            const float other = sample.isStereo() ? interpolate(sample.right, voice.position) : mono;

            const float gain = voice.level * voice.envelope;
            left += mono * gain * voice.panLeft;
            right += other * gain * voice.panRight;

            voice.position += voice.increment;
            if (voice.envelopeDecay > 0.0f) {
                voice.envelope -= voice.envelopeDecay;
                if (voice.envelope <= 0.0f) { voice.envelope = 0.0f; voice.active = false; }
            }
        }

        outputL[i] = left * master;
        outputR[i] = right * master;
    }
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.sampler", "Sampler (8 emplacements)", SamplerSynth);

} // namespace vsm::plugins::sampler
