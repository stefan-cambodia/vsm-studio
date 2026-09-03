#include "HurdyGurdySynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::hurdygurdy {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

HurdyGurdySynth::HurdyGurdySynth() {
    parameterList_ = {
        {kDrones, "Drones", 0.0f, 1.0f, 0.6f, ""},
        {kDroneNote, "Drone Note", 24.0f, 60.0f, 36.0f, "note"},
        {kWheelSpeed, "Wheel Speed", 0.0f, 1.0f, 0.5f, ""},
        {kWheelPressure, "Wheel Pressure", 0.0f, 1.0f, 0.55f, ""},
        {kWheelInertia, "Wheel Inertia", 0.05f, 2.0f, 0.4f, "s"},
        {kChien, "Chien", 0.0f, 1.0f, 0.6f, ""},
        {kChienBuzz, "Chien Buzz", 30.0f, 120.0f, 60.0f, "Hz"},
        {kDamping, "String Damping", 0.0f, 1.0f, 0.2f, ""},
        {kCutoff, "Filter Cutoff", 500.0f, 16000.0f, 7000.0f, "Hz"},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void HurdyGurdySynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    voiceManager_.forEachVoice([&](HurdyGurdyVoice& voice) { voice.prepare(sampleRate); });
    bourdonGrave_.prepare(sampleRate, 30.0f);
    mouche_.prepare(sampleRate, 40.0f);
    trompette_.prepare(sampleRate, 40.0f);
    filtre_.setSampleRate(sampleRate);
    filtre_.reset();
    roue_ = 0.0f;
    touchesEnfoncees_ = 0;
    chienRestant_ = 0;
}

bool HurdyGurdySynth::handleControlEvent(const MidiControlEvent& /*event*/) {
    // Ni molette ni pression : une vielle se joue à la manivelle et aux
    // touches, et rien d'autre n'y change la hauteur. Refus en connaissance
    // de cause, compté par le moteur.
    return false;
}

void HurdyGurdySynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0) {
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
        ++touchesEnfoncees_;
        // LE COUP DE POIGNET : la vélocité ne pousse pas la roue plus fort,
        // elle la fait ACCÉLÉRER un instant, et le chien claque d'autant.
        const float coup = static_cast<float>(event.velocity) / 127.0f;
        if (coup > chienForce_ * 0.5f) {
            chienForce_ = coup;
            chienRestant_ = static_cast<int>((0.05 + 0.10 * coup) * sampleRate_);
            chienPhase_ = 0.0f;
        }
    } else {
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
        touchesEnfoncees_ = std::max(0, touchesEnfoncees_ - 1);
    }
}

void HurdyGurdySynth::process(const MidiNoteEvent* events, int numEvents,
                              float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    HurdyGurdyVoice::Params p;
    p.wheelSpeed = params_[kWheelSpeed].load(std::memory_order_relaxed);
    p.wheelPressure = params_[kWheelPressure].load(std::memory_order_relaxed);
    p.damping = params_[kDamping].load(std::memory_order_relaxed);
    const float drones = params_[kDrones].load(std::memory_order_relaxed);
    const float droneNote = params_[kDroneNote].load(std::memory_order_relaxed);
    const float inertie = params_[kWheelInertia].load(std::memory_order_relaxed);
    const float chien = params_[kChien].load(std::memory_order_relaxed);
    const float buzzHz = params_[kChienBuzz].load(std::memory_order_relaxed);
    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    filtre_.setCutoffHz(params_[kCutoff].load(std::memory_order_relaxed));
    filtre_.setResonance(0.1f);

    const float tonique = 440.0f * std::exp2f((droneNote - 69.0f) / 12.0f);
    // Tonique, quinte, octave : l'accord des vielles en sol/do. Le premier
    // jet mettait le gros bourdon une octave plus bas, à 32 Hz -- et la roue
    // n'y trouvait pas la fondamentale (mesuré : 3·f0 dominait de 70 fois).
    bourdonGrave_.setTuning(tonique, p.damping, 6.0f);
    mouche_.setTuning(tonique * 1.5f, p.damping, 5.0f);
    trompette_.setTuning(tonique * 2.0f, p.damping, 4.0f);

    // La roue prend sa vitesse en ~80 ms et la perd avec son inertie.
    const float montee = 1.0f - std::exp(-1.0f / (0.08f * static_cast<float>(sampleRate_)));
    const float descente = 1.0f - std::exp(-1.0f / (std::max(0.05f, inertie) * static_cast<float>(sampleRate_)));
    constexpr float kVoiceGain = 0.35f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        const float cible = touchesEnfoncees_ > 0 ? 1.0f : 0.0f;
        roue_ += (cible - roue_) * (cible > roue_ ? montee : descente);
        const float roue = roue_ < 1e-4f ? 0.0f : roue_;

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](HurdyGurdyVoice& voice) { sum += voice.render(p, roue); });

        // LES BOURDONS SUIVENT LA ROUE, pas les touches : ils sonnent tant
        // qu'elle tourne, et s'éteignent avec elle.
        // La force de la roue sur les bourdons est CHOISIE AU BANC, pas
        // devinée : la friction ne s'auto-entretient franchement qu'au-dessus
        // d'un seuil -- à 0,3 la fondamentale du gros bourdon vaut 0,00002, à
        // 0,45 0,00004, à 0,6 0,0022, à 0,8 0,0106 (magnitude à f0, note
        // tenue). Un peu moins que la chanterelle, qui est plus près de l'axe.
        const float forceBourdon = roue * drones * 0.8f;
        float bourdons = 0.0f;
        if (forceBourdon > 0.0f || roue > 0.0f) {
            bourdons += 0.9f * bourdonGrave_.render(forceBourdon, p.wheelSpeed, p.wheelPressure);
            bourdons += 0.6f * mouche_.render(forceBourdon, p.wheelSpeed, p.wheelPressure);
            float tromp = trompette_.render(forceBourdon * 0.7f, p.wheelSpeed, p.wheelPressure);
            // LE CHIEN : le chevalet claque contre la table au rythme de la
            // corde -- une modulation d'amplitude brutale et brève, dont la
            // force est le coup de poignet et la durée quelques dizaines de ms.
            if (chienRestant_ > 0 && chien > 0.0f) {
                chienPhase_ += buzzHz / static_cast<float>(sampleRate_);
                if (chienPhase_ >= 1.0f) chienPhase_ -= 1.0f;
                const float claquement = chienPhase_ < 0.5f ? 1.0f : -0.6f;
                const float bruit = 0.25f * rng_.nextBipolar();
                tromp *= 1.0f + chien * chienForce_ * 2.5f * (claquement + bruit);
                --chienRestant_;
            }
            bourdons += 0.7f * tromp;
        }

        const float out = filtre_.process((sum + bourdons) * kVoiceGain * outputLevel);
        outputL[i] = out;
        outputR[i] = out;
    }
}

void HurdyGurdySynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float HurdyGurdySynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState HurdyGurdySynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.hurdygurdy";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void HurdyGurdySynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.hurdygurdy", "Hurdy-Gurdy (la vielle à roue)", HurdyGurdySynth);

} // namespace vsm::plugins::hurdygurdy
