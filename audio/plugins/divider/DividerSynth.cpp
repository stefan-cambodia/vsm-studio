#include "DividerSynth.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::divider {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace {
constexpr uint64_t kBaseSeed = 0x4449564944ULL; // "DIVID"
/// Note MIDI la plus grave que le clavier couvre. Les maîtres tournent à
/// l'octave la plus AIGUË ; tout le reste est obtenu par divisions.
constexpr int kLowestNote = 24;
/// L'octave des maîtres : la 8e, celle que les vraies machines faisaient
/// osciller (autour de 4 000 Hz pour le si).
constexpr int kMasterOctaveBase = 96;
} // namespace

DividerSynth::DividerSynth() {
    // Les niveaux de REGISTRE sont en proportion -- ce sont des tirettes, comme
    // sur un orgue, et « 16 pieds » désigne une longueur de tuyau, pas une
    // grandeur qu'on réglerait en hertz. Le reste est en unités physiques.
    parameterList_ = {
        {kLevel16, "16' Level", 0.0f, 1.0f, 0.8f, ""},
        {kLevel8, "8' Level", 0.0f, 1.0f, 0.9f, ""},
        {kEnsemble, "Ensemble", 0.0f, 1.0f, 0.7f, ""},
        {kTone, "Tone", 400.0f, 12000.0f, 4000.0f, "Hz"},
        {kAttack, "Attack", 0.005f, 2.0f, 0.25f, "s"},
        {kRelease, "Release", 0.02f, 4.0f, 0.6f, "s"},
        {kAnalogCharacter, "Analog Character", 0.0f, 1.0f, 0.35f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void DividerSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    for (int k = 0; k < kMasters; ++k) {
        masters_[static_cast<size_t>(k)].prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, static_cast<uint64_t>(k)));
        // Le maître du nom de note `k`, à l'octave la plus aiguë du clavier.
        const float hz = 440.0f * std::pow(2.0f, (static_cast<float>(kMasterOctaveBase + k) - 69.0f) / 12.0f);
        masters_[static_cast<size_t>(k)].setBaseHz(hz);
    }
    for (auto& t : touches_) {
        t.env.setSampleRate(sampleRate);
        t.tenue = false;
    }
    ensembleL_.prepare(sampleRate);
    ensembleR_.prepare(sampleRate);
    tone_.setSampleRate(sampleRate);
    tone_.setMode(StateVariableFilter::Mode::LowPass);
    tone_.setResonance(0.12f);
}

void DividerSynth::applyNoteEvent(const MidiNoteEvent& event) {
    const int index = static_cast<int>(event.note) - kLowestNote;
    if (index < 0 || index >= kMaxNotes) return;
    auto& touche = touches_[static_cast<size_t>(index)];
    touche.note = event.note;
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0) {
        touche.env.noteOn();
        touche.tenue = true;
    } else {
        touche.env.noteOff();
        touche.tenue = false;
    }
}

void DividerSynth::process(const MidiNoteEvent* events, int numEvents,
                           float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    const float niveau16 = params_[kLevel16].load(std::memory_order_relaxed);
    const float niveau8 = params_[kLevel8].load(std::memory_order_relaxed);
    const float ensemble = params_[kEnsemble].load(std::memory_order_relaxed);
    const float sortie = params_[kOutputLevel].load(std::memory_order_relaxed);
    const float derive = params_[kAnalogCharacter].load(std::memory_order_relaxed);
    tone_.setCutoffHz(std::min(params_[kTone].load(std::memory_order_relaxed),
                               static_cast<float>(sampleRate_) * 0.45f));

    // PAS D'ENVELOPPE DE TIMBRE, PAS DE SUSTAIN RÉGLABLE : ces machines n'ont
    // qu'une attaque et une extinction, et le maintien vaut toujours un. C'est
    // une fidélité, pas une simplification -- leur clavier ne fait qu'ouvrir et
    // fermer des portes.
    const AdsrSettings env{
        params_[kAttack].load(std::memory_order_relaxed),
        0.001f, 1.0f,
        params_[kRelease].load(std::memory_order_relaxed),
    };
    for (auto& t : touches_) t.env.setSettings(env);
    for (auto& m : masters_) m.setDriftAmount(derive);

    // NIVEAU CALIBRÉ SUR LE PIRE CAS DE CETTE MACHINE, qui n'est celui d'aucune
    // autre : elle est INTÉGRALEMENT polyphonique, là où les autres plafonnent
    // à huit voix. Vingt notes tenues ensemble, deux registres ouverts, sont
    // donc un cas normal ici et impossible ailleurs. Le facteur est fixé pour
    // que cet accord-là reste sous zéro dBFS, ce qui rend une note seule plus
    // discrète que sur un polyphonique du parc -- c'est le prix d'une machine
    // qui ne coupe jamais une voix, et il est assumé.
    constexpr float kGain = 0.045f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        // LES DOUZE MAÎTRES AVANCENT, QU'ON JOUE OU NON. C'est le coeur de
        // l'architecture : ils ne sont pas déclenchés par les touches, ils
        // tournent en permanence, et une note ne fait qu'ouvrir une porte sur
        // une phase qui existait déjà.
        std::array<float, kMasters> hz{};
        for (int k = 0; k < kMasters; ++k) hz[static_cast<size_t>(k)] = masters_[static_cast<size_t>(k)].advance();

        float somme = 0.0f;
        for (int n = 0; n < kMaxNotes; ++n) {
            auto& touche = touches_[static_cast<size_t>(n)];
            const int note = kLowestNote + n;
            const int classe = ((note % 12) + 12) % 12;
            // DIVISION ENTIÈRE DE LA FRÉQUENCE, et c'est tout le sujet : la
            // note vaut le maître divisé par 2^octaves. Deux notes à l'octave
            // reçoivent la MÊME fréquence de maître au même instant, donc la
            // MÊME dérive -- leur rapport reste exactement deux et elles ne
            // peuvent pas battre.
            const int divisions = std::clamp((kMasterOctaveBase + classe - note) / 12, 0, 8);
            const float f = hz[static_cast<size_t>(classe)]
                          / static_cast<float>(1 << divisions);

            // LE DIVISEUR TOURNE MÊME TOUCHE RELÂCHÉE : on avance la phase de
            // TOUTES les touches, pas seulement des actives. C'est ce que fait
            // une chaîne de bascules, et c'est ce qui fait qu'une même note
            // rejouée ne repart pas du même endroit.
            touche.phase8 += f / static_cast<float>(sampleRate_);
            if (touche.phase8 >= 1.0f) touche.phase8 -= 1.0f;
            // LE 16 PIEDS EST UNE BASCULE DE PLUS : sa propre phase, avancée à
            // la MOITIÉ de la fréquence. C'est une division, pas une mise à
            // l'échelle.
            touche.phase16 += 0.5f * f / static_cast<float>(sampleRate_);
            if (touche.phase16 >= 1.0f) touche.phase16 -= 1.0f;
            if (!touche.env.isActive()) continue;

            // Dents de scie : l'onde d'un diviseur, filtrée ensuite.
            const float onde = (2.0f * touche.phase8 - 1.0f) * niveau8
                             + (2.0f * touche.phase16 - 1.0f) * niveau16;
            somme += onde * touche.env.nextSample();
        }

        const float filtre = tone_.process(somme * kGain);
        outputL[i] = ensembleL_.process(filtre, ensemble) * sortie;
        outputR[i] = ensembleR_.process(filtre, ensemble * 0.92f) * sortie;
    }
}

int DividerSynth::activeVoiceCount() const {
    int n = 0;
    for (const auto& t : touches_) if (t.env.isActive()) ++n;
    return n;
}

void DividerSynth::setParameter(ParamId id, float value) {
    if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}

float DividerSynth::getParameter(ParamId id) const {
    return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState DividerSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.divider";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void DividerSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.divider", "Divider (cordes électroniques)", DividerSynth);

} // namespace vsm::plugins::divider
