#include "ThereminSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::theremin {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

ThereminSynth::ThereminSynth() {
    // LA BORNE BASSE DU GLISSANDO EST À VINGT MILLISECONDES, et ce n'est pas
    // une précaution : c'est la définition de l'instrument. Un thérémine à
    // portamento nul serait un oscillateur ordinaire, et la machine cesserait
    // d'imiter quoi que ce soit.
    parameterList_ = {
        {kGlide, "Glide", 0.02f, 1.5f, 0.12f, "s"},
        {kWarmth, "Warmth", 0.0f, 1.0f, 0.25f, ""},
        {kVibratoDepth, "Vibrato Depth", 0.0f, 60.0f, 18.0f, "cents"},
        {kVibratoRate, "Vibrato Rate", 0.5f, 9.0f, 5.2f, "Hz"},
        {kVolumeResponse, "Volume Response", 0.005f, 0.5f, 0.05f, "s"},
        {kCutoff, "Filter Cutoff", 500.0f, 16000.0f, 7000.0f, "Hz"},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void ThereminSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    filtre_.setSampleRate(sampleRate_);
    filtre_.reset();
    phase_ = 0.0;
    hzCourant_ = hzVise_ = 0.0f;
    niveau_ = 0.0f;
    phaseVibrato_ = 0.0f;
    noteTenue_ = -1;
}

void ThereminSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0) {
        // UNE NOTE NE PREND PAS UNE VOIX, ELLE DÉPLACE LA MAIN. La vélocité
        // n'est pas lue : elle ne veut rien dire pour un instrument qu'on ne
        // touche pas.
        noteTenue_ = event.note;
        hzVise_ = 440.0f * std::exp2f((static_cast<float>(event.note) - 69.0f) / 12.0f);
        if (hzCourant_ <= 0.0f) hzCourant_ = hzVise_;   // la première note ne glisse de nulle part
    } else if (event.note == noteTenue_) {
        noteTenue_ = -1;
    }
}

void ThereminSynth::process(const MidiNoteEvent* events, int numEvents,
                            float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    const float glide = std::max(0.02f, params_[kGlide].load(std::memory_order_relaxed));
    const float warmth = params_[kWarmth].load(std::memory_order_relaxed);
    const float vibDepth = params_[kVibratoDepth].load(std::memory_order_relaxed);
    const float vibRate = params_[kVibratoRate].load(std::memory_order_relaxed);
    const float reponse = std::max(0.005f, params_[kVolumeResponse].load(std::memory_order_relaxed));
    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    const float bend = bendSemitones_.load(std::memory_order_relaxed);
    filtre_.setCutoffHz(params_[kCutoff].load(std::memory_order_relaxed));
    filtre_.setResonance(0.05f);

    // LA MAIN GAUCHE, ou son remplaçant. Aucune pression reçue : on prend la
    // note tenue comme présence — sinon la machine serait muette sur un
    // clavier sans aftertouch, ce qui la rendrait injouable pour la moitié des
    // gens. Le geste garde la priorité dès qu'il arrive.
    const float gauche = mainGauche_.load(std::memory_order_relaxed);
    const float cible = gauche >= 0.0f ? gauche : (noteTenue_ >= 0 ? 1.0f : 0.0f);

    const float coefGlide = 1.0f - std::exp(-1.0f / (glide * static_cast<float>(sampleRate_)));
    const float coefNiveau = 1.0f - std::exp(-1.0f / (reponse * static_cast<float>(sampleRate_)));

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        // LA MAIN GLISSE, TOUJOURS. C'est la ligne qui interdit les sauts, et
        // le réglage ne peut pas descendre assez bas pour la contourner.
        if (hzVise_ > 0.0f)
            hzCourant_ += (hzVise_ - hzCourant_) * coefGlide;
        niveau_ += (cible - niveau_) * coefNiveau;

        if (hzCourant_ <= 0.0f || niveau_ < 1e-5f) {
            outputL[i] = 0.0f;
            outputR[i] = 0.0f;
            continue;
        }

        phaseVibrato_ += vibRate / static_cast<float>(sampleRate_);
        if (phaseVibrato_ >= 1.0f) phaseVibrato_ -= 1.0f;
        const float cents = vibDepth * std::sin(phaseVibrato_ * vsm::audio::dsp::kTwoPi);
        const float hz = hzCourant_ * std::exp2f((cents / 1200.0f) + bend / 12.0f);

        phase_ += static_cast<double>(hz) / sampleRate_;
        if (phase_ >= 1.0) phase_ -= 1.0;

        // Le battement de deux oscillateurs radio donne une onde presque pure ;
        // `Warmth` ajoute les deux rangs qui l'éloignent de la sinusoïde.
        const auto angle = phase_ * vsm::audio::dsp::kTwoPi;
        float son = static_cast<float>(std::sin(angle));
        son += warmth * 0.25f * static_cast<float>(std::sin(2.0 * angle));
        son += warmth * 0.10f * static_cast<float>(std::sin(3.0 * angle));

        const float sortie = filtre_.process(son * niveau_ * 0.5f * outputLevel);
        outputL[i] = sortie;
        outputR[i] = sortie;
    }
}

void ThereminSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float ThereminSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState ThereminSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.theremin";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void ThereminSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.theremin", "Theremin (la main dans l'air)", ThereminSynth);

} // namespace vsm::plugins::theremin
