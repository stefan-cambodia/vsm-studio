#include "SpectralSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::spectral {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

SpectralSynth::SpectralSynth() {
    parameterList_ = {
        {kPartials, "Partials", 1.0f, 256.0f, 64.0f, ""},
        {kStretch, "Stretch", 0.7f, 1.6f, 1.0f, ""},
        {kTilt, "Spectral Tilt", 0.2f, 3.0f, 1.2f, ""},
        {kSpread, "Spread", 0.0f, 1.0f, 0.15f, ""},
        {kAttack, "Attack", 0.005f, 4.0f, 0.05f, "s"},
        {kDecay, "Decay", 0.01f, 8.0f, 1.0f, "s"},
        {kSustain, "Sustain", 0.0f, 1.0f, 0.8f, ""},
        {kRelease, "Release", 0.01f, 8.0f, 0.6f, "s"},
        {kVelocitySensitivity, "Velocity Sensitivity", 0.0f, 1.0f, 0.4f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void SpectralSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    for (auto& n : notes_) {
        n = Note{};
        n.env.setSampleRate(sampleRate_);
    }
    // FENÊTRE DE HANN À SAUT DE MOITIÉ : sa somme sur les trames décalées vaut
    // une constante (condition COLA), donc le raccord entre deux trames est
    // exactement continu et il n'y a pas de clic à recoller.
    for (size_t i = 0; i < kTrame; ++i)
        fenetre_[i] = 0.5f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI)
                                             * static_cast<float>(i) / static_cast<float>(kTrame));
    file_.fill(0.0f);
    lecture_ = 0;
    restant_ = 0;
}

int SpectralSynth::activeVoiceCount() const {
    int n = 0;
    for (const auto& note : notes_) if (note.used && note.env.isActive()) ++n;
    return n;
}

void SpectralSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0) {
        // On cherche une place libre ; sinon on prend la plus ancienne. Ce
        // n'est PAS un vol de voix au sens habituel : les notes ne coûtent
        // rien à rendre, la limite est seulement celle du tableau.
        Note* choisie = nullptr;
        for (auto& n : notes_)
            if (!n.used || !n.env.isActive()) { choisie = &n; break; }
        if (choisie == nullptr) choisie = &notes_[0];
        choisie->used = true;
        choisie->note = event.note;
        choisie->channel = event.channel;
        choisie->velocity = event.velocity;
        choisie->phase = 0.0f;
        choisie->env.noteOn();
    } else {
        for (auto& n : notes_)
            if (n.used && n.note == event.note && n.channel == event.channel) n.env.noteOff();
    }
}

void SpectralSynth::rendreUneTrame() {
    re_.fill(0.0f);
    im_.fill(0.0f);

    const int partiels = std::clamp(
        static_cast<int>(params_[kPartials].load(std::memory_order_relaxed) + 0.5f), 1, kMaxPartiels);
    const float etirement = params_[kStretch].load(std::memory_order_relaxed);
    const float pente = params_[kTilt].load(std::memory_order_relaxed);
    const float etalement = params_[kSpread].load(std::memory_order_relaxed);
    const float velSens = params_[kVelocitySensitivity].load(std::memory_order_relaxed);
    const float bend = bendSemitones_.load(std::memory_order_relaxed);

    const AdsrSettings env{
        params_[kAttack].load(std::memory_order_relaxed),
        params_[kDecay].load(std::memory_order_relaxed),
        params_[kSustain].load(std::memory_order_relaxed),
        params_[kRelease].load(std::memory_order_relaxed)};

    const auto bins = static_cast<float>(RealIfft<kTrame>::kBins);
    const float parHz = static_cast<float>(kTrame) / static_cast<float>(sampleRate_);

    for (auto& note : notes_) {
        if (!note.used) continue;
        note.env.setSettings(env);
        // L'enveloppe avance d'un SAUT par trame : c'est le pas de temps de
        // cette machine, et il vaut onze millisecondes.
        float niveau = 0.0f;
        for (size_t i = 0; i < kSaut; ++i) niveau = note.env.nextSample();
        if (!note.env.isActive()) { note.used = false; continue; }

        const float velocity = static_cast<float>(note.velocity) / 127.0f;
        const float gain = niveau * (1.0f - velSens * (1.0f - velocity));
        const float f0 = 440.0f * std::exp2f(
            (static_cast<float>(note.note) + bend - 69.0f) / 12.0f);

        for (int k = 1; k <= partiels; ++k) {
            // L'ÉTIREMENT EST CE QUI REND LE SPECTRE INHARMONIQUE : à 1,0 les
            // partiels sont des multiples entiers, ailleurs ils ne le sont
            // plus du tout — et aucune machine du parc ne sait poser des
            // centaines de raies à des fréquences quelconques.
            const float f = f0 * std::pow(static_cast<float>(k), etirement);
            const float caseF = f * parHz;
            if (caseF >= bins - 1.0f) break;
            const float a = gain * std::pow(static_cast<float>(k), -pente);
            if (a < 1e-6f) break;

            const float phase = 2.0f * static_cast<float>(M_PI) * f * note.phase
                              / static_cast<float>(sampleRate_);
            const float c = std::cos(phase), s = std::sin(phase);
            // Le partiel est déposé dans sa case, et `Spread` en verse une
            // fraction dans les deux voisines : un partiel étalé sonne moins
            // pur, ce qui est le grain d'un « bruit accordé ».
            const auto centre = static_cast<size_t>(caseF);
            re_[centre] += a * c;
            im_[centre] += a * s;
            if (etalement > 0.0f && centre >= 1 && centre + 1 < RealIfft<kTrame>::kBins) {
                const float cote = a * etalement * 0.5f;
                re_[centre - 1] += cote * c;
                im_[centre - 1] += cote * s;
                re_[centre + 1] += cote * c;
                im_[centre + 1] += cote * s;
            }
        }
        note.phase += static_cast<float>(kSaut);
    }

    ifft_.inverse(re_.data(), im_.data(), trame_.data());
    // LA TRANSFORMÉE DIVISE PAR N : une raie d'amplitude un rend une sinusoïde
    // d'amplitude 2/1024, si bien que la machine sortait à un rms de 0,0013 —
    // cent fois trop faible pour le parc. On rend l'échelle ici plutôt que de
    // demander à l'utilisateur de pousser le volume, et la valeur est mesurée
    // (rms visé : le même ordre que les autres machines).
    constexpr float kEchelleSpectrale = 130.0f;
    for (size_t i = 0; i < kTrame; ++i)
        file_[i] += trame_[i] * fenetre_[i] * kEchelleSpectrale;
}

void SpectralSynth::process(const MidiNoteEvent* events, int numEvents,
                            float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;
    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        if (restant_ == 0) {
            // On décale ce qui reste du recouvrement, puis on rend la trame
            // suivante par-dessus. Aucune allocation : tout est en place.
            for (size_t k = 0; k + kSaut < kTrame; ++k) file_[k] = file_[k + kSaut];
            for (size_t k = kTrame - kSaut; k < kTrame; ++k) file_[k] = 0.0f;
            rendreUneTrame();
            lecture_ = 0;
            restant_ = kSaut;
        }
        const float x = file_[lecture_++] * outputLevel;
        --restant_;
        outputL[i] = x;
        outputR[i] = x;
    }
}

void SpectralSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float SpectralSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState SpectralSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.spectral";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void SpectralSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.spectral", "Spectral (le spectre écrit)", SpectralSynth);

} // namespace vsm::plugins::spectral
