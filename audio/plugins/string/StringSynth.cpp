#include "StringSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::string_machine {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace {
constexpr uint64_t kBaseSeed = 0x53545249ULL;

/// Hauteur la plus grave qu'on accepte de tenir dans la ligne à retard. Le
/// do -1 du MIDI est à 8,18 Hz : en dessous, une corde n'est plus une corde,
/// et la mémoire serait réservée pour rien.
constexpr float kLowestHz = 8.0f;

float noteToHz(uint8_t note, float driftSemis) {
    return 440.0f * std::exp2f((static_cast<float>(note) + driftSemis - 69.0f) / 12.0f);
}
} // namespace

// ---------------------------------------------------------------------------
// Voix
// ---------------------------------------------------------------------------

void StringVoice::prepare(double sampleRate, uint64_t seed) {
    sampleRate_ = sampleRate;
    // La ligne est réservée ICI, pour la note la plus grave qu'on accepte de
    // tenir. Le do -1 du MIDI est à 8,18 Hz : en dessous, une corde n'est plus
    // une corde, et la mémoire serait réservée pour rien.
    string_.prepare(sampleRate, kLowestHz);
    rng_ = vsm::util::DeterministicRng(seed);
    drift_.setSampleRate(sampleRate);
    drift_.setSeed(seed ^ 0x51DEULL);
    drift_.setRateHz(0.05f);
    pickLpState_ = pickLpState2_ = dcX1_ = dcY1_ = level_ = 0.0f;
    active_ = released_ = false;
}

void StringVoice::noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    channel_ = channel;
    note_ = note;
    velocity_ = velocity;
    // La corde repart au repos : effacer coûte un memset, jamais une
    // allocation. Garder l'ancien contenu ferait claquer une voix volée.
    string_.reset();
    pickLpState_ = pickLpState2_ = dcX1_ = dcY1_ = 0.0f;
    level_ = 1.0f;
    active_ = true;
    released_ = false;
    // La longueur de la salve dépend de la hauteur, qui n'est connue qu'à
    // l'accord de la boucle : on la DEMANDE ici, `updateTuning` l'arme.
    pluckRemaining_ = 0;
    pendingPluck_ = true;
}

void StringVoice::updateTuning(const Params& p) {
    const float driftSemis = drift_.nextValue() * 0.06f;
    const float hz = std::clamp(noteToHz(note_, driftSemis + p.bendSemitones), kLowestHz,
                                static_cast<float>(sampleRate_) * 0.25f);

    // Lever le doigt étouffe la corde : ce n'est pas une enveloppe qui se
    // ferme, c'est la boucle qui perd davantage à chaque tour.
    const float t60 = std::max(0.02f, released_ ? p.releaseSeconds : p.decaySeconds);
    string_.setTuning(hz, p.damping, p.stiffness, t60);

    // Point de pincement. Le peigne « 1 - z^-pD » annule les harmoniques
    // multiples de 1/p : pincer au milieu (p = 0,5) supprime les harmoniques
    // paires et creuse le son, pincer près du chevalet (p petit) les garde
    // toutes et donne le son nasillard d'un médiator au bord.
    contactOffset_ = string_.contactOffset(p.pickPosition);

    // Longueur de la salve : UNE PÉRIODE, c'est-à-dire la corde entière.
    //
    // Ce n'était pas le premier choix, et la mesure a tranché. Une salve
    // courte à durée fixe (5 ms) paraissait plus « médiator » ; elle est en
    // réalité incapable d'exciter une corde grave, parce qu'une fenêtre de
    // 5 ms n'a presque pas d'énergie en dessous de 200 Hz. Sur un violoncelle
    // pizzicato à 73 Hz, le fondamental de la machine ne sortait même pas dans
    // les huit plus fortes raies du spectre. Le pincement d'une corde met en
    // mouvement TOUTE sa longueur : la salve dure un aller-retour.
    pluckLength_ = std::max(8, static_cast<int>(string_.loopDelay()));
    // Coupure de la salve, en échelle géométrique : de 45 Hz (le pouce, sous
    // le fondamental de toute corde grave, donc pente en 1/n² sur toute
    // l'étendue) à 5 kHz (le médiator dur). Deux pôles, pour la pente de
    // -12 dB par octave qu'impose la forme triangulaire du pincement.
    const float hardnessHz = 45.0f * std::exp2f(6.8f * std::clamp(p.pickHardness, 0.0f, 1.0f));
    pickLpCoef_ = 1.0f - std::exp(-static_cast<float>(kTwoPi) * hardnessHz / static_cast<float>(sampleRate_));
    pickLpCoef_ = std::clamp(pickLpCoef_, 0.0005f, 1.0f);
    // Deux passe-bas en cascade retirent presque toute l'énergie d'un bruit
    // blanc : sans compensation, « Pick Hardness » réglerait le VOLUME avant
    // de régler le timbre. Pour H(z) = (c / (1 - a z^-1))^2 avec a = 1 - c, la
    // variance de sortie vaut c^4 (1 + a^2) / (1 - a^2)^3 fois celle de
    // l'entrée ; on rend donc le gain inverse.
    {
        const float pole = 1.0f - pickLpCoef_;
        const float c2 = pickLpCoef_ * pickLpCoef_;
        const float oneMinusA2 = std::max(1.0e-9f, 1.0f - pole * pole);
        const float variance = c2 * c2 * (1.0f + pole * pole) / (oneMinusA2 * oneMinusA2 * oneMinusA2);
        pickNoiseGain_ = std::min(80.0f, 1.0f / std::sqrt(std::max(1.0e-12f, variance)));
    }
    if (pendingPluck_) {
        pluckRemaining_ = pluckLength_;
        pendingPluck_ = false;
    }

    const float velocity = static_cast<float>(velocity_) / 127.0f;
    const float velocityGain = 1.0f - std::clamp(p.velocitySensitivity, 0.0f, 1.0f) * (1.0f - velocity);
    pluckGain_ = (1.0f - std::clamp(p.excitation, 0.0f, 1.0f)) * velocityGain;
    bowGain_ = std::clamp(p.excitation, 0.0f, 1.0f) * velocityGain;
}

float StringVoice::render(const Params& p) {
    if (!active_) return 0.0f;

    const float looped = string_.advance();

    // --- excitation --------------------------------------------------------
    float drive = 0.0f;

    if (pluckRemaining_ > 0 && pluckGain_ > 0.0f) {
        // Fenêtre ASYMÉTRIQUE : montée très brève, puis décroissance linéaire
        // jusqu'à zéro. Le pincement libère la corde d'un coup — une fenêtre
        // symétrique (demi-cosinus) mettrait la moitié de la salve à monter et
        // effacerait l'attaque. La montée existe tout de même, faute de quoi
        // le premier échantillon serait un clic sans rapport avec la corde.
        //
        // La longueur est recalculée à chaque bloc (la dérive fait bouger la
        // période) : borner la phase évite qu'un raccourcissement en cours de
        // salve la fasse sortir de [0, 1] et retourne le signe de la fenêtre.
        const float phase = std::clamp(
            1.0f - static_cast<float>(pluckRemaining_) / static_cast<float>(pluckLength_), 0.0f, 1.0f);
        const float window = std::min(1.0f, phase * 12.0f) * (1.0f - phase);
        pickLpState_ += pickLpCoef_ * (rng_.nextBipolar() - pickLpState_);
        pickLpState2_ += pickLpCoef_ * (pickLpState_ - pickLpState2_);
        drive += pluckGain_ * window * pickLpState2_ * pickNoiseGain_ * 0.55f;
    }
    if (pluckRemaining_ > 0) --pluckRemaining_;

    if (bowGain_ > 0.0f && !released_) {
        // Archet : la force dépend de la vitesse RELATIVE entre le crin et la
        // corde. Tant que l'adhérence tient, l'archet entraîne la corde ; au
        // décrochement elle repart en arrière. Ce cycle est le son du violon.
        const float bowVelocity = 0.03f + 0.42f * std::clamp(p.bowSpeed, 0.0f, 1.0f);
        const float slope = 5.0f - 4.4f * std::clamp(p.bowPressure, 0.0f, 1.0f);
        const float relative = bowVelocity - looped;
        drive += bowGain_ * bowFriction(relative, slope) * relative;
    }

    const float value = string_.inject(looped, drive, contactOffset_);

    // Bloqueur de continu : la table de friction n'est pas symétrique et
    // laisserait une composante continue qui mangerait la dynamique.
    const float out = value - dcX1_ + 0.9995f * dcY1_;
    dcX1_ = value;
    dcY1_ = out;

    // Fin de note : une corde s'éteint d'elle-même, sans qu'on lève le doigt.
    // C'est le suiveur de crête qui décide, pas une enveloppe.
    const float magnitude = std::abs(out);
    level_ = magnitude > level_ ? magnitude : level_ * 0.99995f;
    if (level_ < 2.0e-5f && pluckRemaining_ == 0) active_ = false;

    return out;
}

// ---------------------------------------------------------------------------
// Machine
// ---------------------------------------------------------------------------

StringSynth::StringSynth() {
    parameterList_ = {
        {kPickPosition, "Pick Position", 0.02f, 0.5f, 0.24f, ""},
        {kPickHardness, "Pick Hardness", 0.0f, 1.0f, 0.55f, ""},
        {kExcitation, "Excitation", 0.0f, 1.0f, 0.0f, ""},
        {kBowPressure, "Bow Pressure", 0.0f, 1.0f, 0.5f, ""},
        {kBowSpeed, "Bow Speed", 0.0f, 1.0f, 0.5f, ""},
        {kStringDecay, "String Decay", 0.1f, 20.0f, 4.0f, "s"},
        {kStringDamping, "String Damping", 0.0f, 1.0f, 0.35f, ""},
        {kStiffness, "Stiffness", 0.0f, 1.0f, 0.2f, ""},
        {kRelease, "Release", 0.02f, 3.0f, 0.25f, "s"},
        {kBodyLevel, "Body Level", 0.0f, 1.0f, 0.4f, ""},
        {kBodySize, "Body Size", 0.0f, 1.0f, 0.5f, ""},
        {kVelocitySensitivity, "Velocity Sensitivity", 0.0f, 1.0f, 0.7f, ""},
        {kDrive, "Drive", 0.0f, 1.0f, 0.0f, ""},
        {kAnalogCharacter, "Analog Character", 0.0f, 1.0f, 0.2f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void StringSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t index = 0;
    voiceManager_.forEachVoice([&](StringVoice& voice) {
        voice.prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, index));
        ++index;
    });
    for (auto& section : body_) section.setSampleRate(sampleRate);
}

void StringSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float StringSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState StringSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.string";
    for (const auto& info : parameterList_) state.parameterValues[info.id] = getParameter(info.id);
    return state;
}

void StringSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues) setParameter(id, value);
}

void StringSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void StringSynth::process(const MidiNoteEvent* events, int numEvents,
                          float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    StringVoice::Params p;
    p.pickPosition = params_[kPickPosition].load(std::memory_order_relaxed);
    p.pickHardness = params_[kPickHardness].load(std::memory_order_relaxed);
    p.excitation = params_[kExcitation].load(std::memory_order_relaxed);
    p.bowPressure = params_[kBowPressure].load(std::memory_order_relaxed);
    p.bowSpeed = params_[kBowSpeed].load(std::memory_order_relaxed);
    p.decaySeconds = params_[kStringDecay].load(std::memory_order_relaxed);
    p.damping = params_[kStringDamping].load(std::memory_order_relaxed);
    p.stiffness = params_[kStiffness].load(std::memory_order_relaxed);
    p.releaseSeconds = params_[kRelease].load(std::memory_order_relaxed);
    p.velocitySensitivity = params_[kVelocitySensitivity].load(std::memory_order_relaxed);
    p.bendSemitones = bendSemitones_.load(std::memory_order_relaxed);

    const float drift = params_[kAnalogCharacter].load(std::memory_order_relaxed);

    // Corps : trois résonances en série. `Body Size` les fait glisser d'une
    // petite caisse (violon, ukulélé) à une grande (contrebasse) — c'est la
    // TAILLE qui distingue ces instruments, bien plus que leur bois.
    // À `Body Level = 0` tous les gains valent 0 dB : la machine est alors
    // exactement transparente, ce dont une basse électrique a besoin.
    const float bodyLevel = params_[kBodyLevel].load(std::memory_order_relaxed);
    const float bodySize = params_[kBodySize].load(std::memory_order_relaxed);
    const float scale = std::exp2f(1.1f - 2.3f * std::clamp(bodySize, 0.0f, 1.0f));
    const float bodyHz[3] = {104.0f * scale, 215.0f * scale, 400.0f * scale};
    const float bodyQ[3] = {2.5f, 3.0f, 2.0f};
    const float bodyDb[3] = {9.0f, 6.5f, 4.0f};
    for (size_t i = 0; i < body_.size(); ++i)
        body_[i].set(Biquad::Type::Peaking, bodyHz[i], bodyQ[i], bodyLevel * bodyDb[i]);

    const float driveAmount = params_[kDrive].load(std::memory_order_relaxed);
    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);

    voiceManager_.forEachVoice([&](StringVoice& voice) {
        voice.setDriftAmount(drift);
        if (voice.isActive()) voice.updateTuning(p);
    });

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i) {
            applyNoteEvent(events[eventIndex++]);
            // Une note qui vient de démarrer n'a pas encore de géométrie de
            // boucle : la lui donner tout de suite plutôt qu'au bloc suivant,
            // sans quoi son attaque partirait sur l'accord de la note d'avant.
            voiceManager_.forEachVoice([&](StringVoice& voice) {
                if (voice.isActive()) voice.updateTuning(p);
            });
        }

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](StringVoice& voice) { sum += voice.render(p); });
        sum *= 0.32f;

        for (auto& section : body_) sum = section.process(sum);

        if (driveAmount > 0.0f) {
            const float gain = 1.0f + 9.0f * driveAmount;
            sum = std::tanh(sum * gain) / std::sqrt(gain);
        }

        const float out = sum * outputLevel;
        outputL[i] = out;
        outputR[i] = out;
    }
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.string", "String (corde pincée / frottée)", StringSynth);

} // namespace vsm::plugins::string_machine
