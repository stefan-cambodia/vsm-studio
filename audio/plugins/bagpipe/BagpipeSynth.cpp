#include "BagpipeSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::bagpipe {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace {
float noteToHz(float note) { return 440.0f * std::exp2f((note - 69.0f) / 12.0f); }
/// LE SAC QUI SE VIDE DÉTEND LES ANCHES : 4 % de hauteur à pression nulle,
/// rien à pleine pression. Toutes les anches ensemble, c'est le même sac.
float sag(float bagPressure) { return 1.0f + 0.04f * (std::clamp(bagPressure, 0.0f, 1.0f) - 1.0f); }
/// Le chalumeau demande plus de pression que les bourdons : il parle après
/// eux à la frappe, se tait avant eux à la coupure.
float chanterPressure(float bag) { return std::clamp((bag - 0.25f) / 0.75f, 0.0f, 1.0f); }
float dronePressure(float bag) { return std::clamp((bag - 0.12f) / 0.88f, 0.0f, 1.0f); }
} // namespace

BagpipeSynth::BagpipeSynth() {
    parameterList_ = {
        {kDroneNote, "Drone Note", 40.0f, 72.0f, 57.0f, ""},
        {kDrones, "Drones", 0.0f, 1.0f, 0.7f, ""},
        {kBagReserve, "Bag Reserve", 0.0f, 3.0f, 0.6f, "s"},
        {kStrikeIn, "Strike-in", 0.05f, 1.5f, 0.35f, "s"},
        {kCutOff, "Cut-off", 0.05f, 1.5f, 0.3f, "s"},
        {kGraceLength, "Grace Length", 10.0f, 120.0f, 35.0f, "ms"},
        {kReedStiffness, "Reed Stiffness", 0.0f, 1.0f, 0.5f, ""},
        {kBrassiness, "Brassiness", 0.0f, 1.0f, 0.2f, ""},
        {kBreathNoise, "Breath Noise", 0.0f, 1.0f, 0.2f, ""},
        {kBellDamping, "Bell Damping", 0.0f, 1.0f, 0.35f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void BagpipeSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    chanter_.prepare(sampleRate_, Pipe::Shape::Conical, 0x4241475031ULL);
    tenor1_.prepare(sampleRate_, Pipe::Shape::Cylindrical, 0x4241475032ULL);
    tenor2_.prepare(sampleRate_, Pipe::Shape::Cylindrical, 0x4241475033ULL);
    bass_.prepare(sampleRate_, Pipe::Shape::Cylindrical, 0x4241475034ULL);
    bag_ = 0.0f;
    sinceRelease_ = 0;
    anyHeld_ = sounding_ = false;
    heldCount_ = 0;
    graceRemaining_ = 0;
    tunedBag_ = -1.0f;
    tunedNote_ = tunedDrone_ = 0;
    tunedBell_ = -1.0f;
    tunedGrace_ = false;
}

bool BagpipeSynth::handleControlEvent(const MidiControlEvent&) {
    // Les doigts bouchent des trous, le bras presse un sac : ni molette, ni
    // contrôleur. Refusé en connaissance de cause, le moteur compte.
    return false;
}

void BagpipeSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0) {
        // LA NOTE RÉPÉTÉE : si la note demandée est celle qui sonne déjà, le
        // son ne peut pas la séparer de lui-même. Le sonneur y met une note
        // de grâce ; la machine aussi.
        if (sounding_ && event.note == chanterNote_)
            graceRemaining_ = static_cast<int>(params_[kGraceLength].load(std::memory_order_relaxed) * 0.001f
                                               * static_cast<float>(sampleRate_));
        // La pile des doigts : la dernière touche commande.
        for (int i = 0; i < heldCount_; ++i)
            if (held_[static_cast<size_t>(i)] == event.note) {
                for (int j = i; j + 1 < heldCount_; ++j) held_[static_cast<size_t>(j)] = held_[static_cast<size_t>(j + 1)];
                --heldCount_;
                break;
            }
        if (heldCount_ == kHeldMax) {
            for (int j = 0; j + 1 < heldCount_; ++j) held_[static_cast<size_t>(j)] = held_[static_cast<size_t>(j + 1)];
            --heldCount_;
        }
        held_[static_cast<size_t>(heldCount_++)] = event.note;
        chanterNote_ = event.note;
        anyHeld_ = true;
        sounding_ = true;
        return;
    }
    for (int i = 0; i < heldCount_; ++i)
        if (held_[static_cast<size_t>(i)] == event.note) {
            for (int j = i; j + 1 < heldCount_; ++j) held_[static_cast<size_t>(j)] = held_[static_cast<size_t>(j + 1)];
            --heldCount_;
            break;
        }
    if (heldCount_ > 0) {
        chanterNote_ = held_[static_cast<size_t>(heldCount_ - 1)];
    } else if (anyHeld_) {
        // Plus aucun doigt : le chalumeau GARDE la dernière note. C'est le
        // sac qui décidera de la fin, pas la touche.
        anyHeld_ = false;
        sinceRelease_ = 0;
    }
}

void BagpipeSynth::retune(float bagPressure) {
    const uint8_t drone = static_cast<uint8_t>(std::clamp(params_[kDroneNote].load(std::memory_order_relaxed), 40.0f, 72.0f));
    const float bell = params_[kBellDamping].load(std::memory_order_relaxed);
    const bool grace = graceRemaining_ > 0;
    // Retoucher les lignes à retard coûte ; on ne le fait que si quelque
    // chose a changé — la note, la grâce, le bourdon, ou la pression
    // (par pas de 1 %).
    const float bagStep = std::floor(bagPressure * 100.0f) / 100.0f;
    if (bagStep == tunedBag_ && chanterNote_ == tunedNote_ && drone == tunedDrone_
        && bell == tunedBell_ && grace == tunedGrace_) return;
    tunedBag_ = bagStep; tunedNote_ = chanterNote_; tunedDrone_ = drone; tunedBell_ = bell; tunedGrace_ = grace;

    const float s = sag(bagStep);
    // LA NOTE DE GRÂCE est le la aigu du chalumeau : l'octave de la tonique.
    const float noteChanter = grace ? static_cast<float>(drone) + 12.0f : static_cast<float>(chanterNote_);
    chanter_.setTuning(noteToHz(noteChanter) * s, bell);
    tenor1_.setTuning(noteToHz(static_cast<float>(drone) - 12.0f) * s, bell);
    tenor2_.setTuning(noteToHz(static_cast<float>(drone) - 12.0f) * s * 1.0015f, bell);
    bass_.setTuning(noteToHz(static_cast<float>(drone) - 24.0f) * s, bell);
}

void BagpipeSynth::process(const MidiNoteEvent* events, int numEvents,
                           float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    const float drones = params_[kDrones].load(std::memory_order_relaxed);
    const float reserve = params_[kBagReserve].load(std::memory_order_relaxed);
    const float strikeIn = std::max(0.01f, params_[kStrikeIn].load(std::memory_order_relaxed));
    const float cutOff = std::max(0.01f, params_[kCutOff].load(std::memory_order_relaxed));
    const float stiffness = params_[kReedStiffness].load(std::memory_order_relaxed);
    const float brass = params_[kBrassiness].load(std::memory_order_relaxed);
    const float noise = params_[kBreathNoise].load(std::memory_order_relaxed);
    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    const float montee = 1.0f - std::exp(-1.0f / (strikeIn * static_cast<float>(sampleRate_)));
    const float chute = 1.0f - std::exp(-1.0f / (cutOff * static_cast<float>(sampleRate_)));
    const int reserveSamples = static_cast<int>(reserve * static_cast<float>(sampleRate_));
    constexpr float kGain = 0.6f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        if (!sounding_) { outputL[i] = outputR[i] = 0.0f; continue; }

        // LE SAC : monte tant qu'un doigt joue, tient sa réserve, puis
        // tombe. Il ne dépend d'aucune vélocité.
        if (anyHeld_) {
            bag_ += montee * (1.0f - bag_);
        } else if (sinceRelease_ < reserveSamples) {
            ++sinceRelease_;
        } else {
            bag_ += chute * (0.0f - bag_);
        }
        if (graceRemaining_ > 0) --graceRemaining_;
        retune(bag_);

        const float chanter = chanter_.render(chanterPressure(bag_), stiffness, brass, noise);
        const float pd = dronePressure(bag_);
        const float bourdons = (tenor1_.render(pd, stiffness, brass * 0.5f, noise)
                                + tenor2_.render(pd, stiffness, brass * 0.5f, noise)
                                + bass_.render(pd, stiffness, brass * 0.5f, noise) * 1.2f) * drones * 0.5f;
        // Le chalumeau devant, les bourdons sur l'épaule gauche.
        outputL[i] = (chanter * 0.75f + bourdons * 0.85f) * kGain * outputLevel;
        outputR[i] = (chanter * 0.75f + bourdons * 0.55f) * kGain * outputLevel;

        // Sac vide et anches muettes : la machine se tait, et se le dit.
        if (!anyHeld_ && bag_ < 1e-4f && std::abs(chanter) < 1e-5f && std::abs(bourdons) < 1e-5f) {
            sounding_ = false;
            chanter_.reset(); tenor1_.reset(); tenor2_.reset(); bass_.reset();
            tunedBag_ = -1.0f;
        }
    }
}

void BagpipeSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float BagpipeSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState BagpipeSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.bagpipe";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void BagpipeSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.bagpipe", "Bagpipe (la réserve d'air)", BagpipeSynth);

} // namespace vsm::plugins::bagpipe
