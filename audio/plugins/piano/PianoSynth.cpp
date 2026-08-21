#include "PianoSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::piano {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace {
constexpr uint64_t kBaseSeed = 0x50494E4F32ULL;

/// La 0 d'un piano est à 27,5 Hz. Réserver la ligne à retard pour plus grave
/// que 20 Hz serait payer de la mémoire pour des notes qui n'existent sur
/// aucun clavier.
constexpr float kLowestHz = 20.0f;

float noteToHz(uint8_t note, float detuneSemis) {
    return 440.0f * std::exp2f((static_cast<float>(note) + detuneSemis - 69.0f) / 12.0f);
}
} // namespace

// ---------------------------------------------------------------------------
// Voix
// ---------------------------------------------------------------------------

void PianoVoice::prepare(double sampleRate, uint64_t seed) {
    sampleRate_ = sampleRate;
    for (auto& string : strings_) string.prepare(sampleRate, kLowestHz);
    rng_ = vsm::util::DeterministicRng(seed);
    drift_.setSampleRate(sampleRate);
    drift_.setSeed(seed ^ 0x7A11ULL);
    drift_.setRateHz(0.04f);
    dcX1_ = dcY1_ = level_ = 0.0f;
    active_ = released_ = false;
}

void PianoVoice::noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    channel_ = channel;
    note_ = note;
    velocity_ = velocity;
    for (auto& string : strings_) string.reset();
    dcX1_ = dcY1_ = 0.0f;
    level_ = 1.0f;
    active_ = true;
    released_ = false;
    hammerRemaining_ = 0;
    pendingStrike_ = true;
    // Le piano s'étale : grave à gauche, aigu à droite, comme on l'entend
    // assis devant et comme on l'enregistre. La 0 (21) à gauche, do 8 (108)
    // à droite ; hors de cette étendue, on sature aux bords du clavier.
    pan_ = std::clamp((static_cast<float>(note_) - 21.0f) / 87.0f, 0.0f, 1.0f) * 2.0f - 1.0f;
}

void PianoVoice::updateTuning(const Params& p) {
    const float driftSemis = drift_.nextValue() * 0.04f;

    // Le choeur : deux cordes désaccordées de part et d'autre de la hauteur
    // juste. Huit cents d'écart maximum -- au-delà on n'entend plus un piano
    // mais un honky-tonk, ce qui reste un piano, d'où la borne haute généreuse.
    const float spreadSemis = 0.08f * std::clamp(p.unisonDetune, 0.0f, 1.0f);

    // L'étouffoir : relâcher la touche fait perdre la boucle beaucoup plus
    // vite. Pédale enfoncée, l'étouffoir ne retombe pas et le relâchement ne
    // change rien -- c'est ce que fait la pédale forte, elle ne « rallonge »
    // rien par elle-même.
    const bool damped = released_ && !p.sustainPedal;
    const float t60 = std::max(0.02f, damped ? p.releaseSeconds : p.decaySeconds);

    for (int i = 0; i < kStringsPerNote; ++i) {
        const float offset = (i == 0 ? -0.5f : 0.5f) * spreadSemis;
        const float hz = noteToHz(note_, driftSemis + offset);
        // DÉCROISSANCE EN DEUX TEMPS. La seconde corde tient plus longtemps
        // que la première : leur somme chute d'abord vite, puis laisse une
        // traîne longue et faible. C'est l'effet audible du couplage au
        // chevalet, obtenu sans le mécanisme (voir l'en-tête).
        const float stringT60 = damped ? t60 : t60 * (i == 0 ? 1.0f : 1.35f);
        strings_[static_cast<size_t>(i)].setTuning(hz, p.damping, p.inharmonicity, stringT60);
        contact_[static_cast<size_t>(i)] =
            strings_[static_cast<size_t>(i)].contactOffset(p.hammerPosition);
    }

    // DURÉE DE CONTACT DU MARTEAU — la loi expressive du piano.
    //
    // Un marteau de feutre s'écrase et rebondit d'autant plus vite qu'il
    // arrive vite : le contact dure quelques millisecondes en jeu doux, moins
    // d'une en fortissimo. Or la durée de contact fixe la coupure du spectre
    // injecté (une impulsion de durée T n'a presque rien au-dessus de 1/T).
    // Frapper fort ouvre donc le timbre, et pas seulement le volume, SANS
    // qu'aucune enveloppe de filtre n'ait à le simuler.
    //
    // Un feutre dur (hardness -> 1) écourte le contact à vélocité égale : le
    // piano « brillant » d'un feutre neuf ou piqué.
    const float velocity = static_cast<float>(velocity_) / 127.0f;
    const float hardness = std::clamp(p.hammerHardness, 0.0f, 1.0f);
    const float contactMs = (3.6f - 2.4f * hardness) * (1.0f - 0.62f * velocity);
    const float contactSamples = contactMs * 1.0e-3f * static_cast<float>(sampleRate_);
    // Le contact ne peut pas durer plus que la moitié d'un aller-retour : au
    // delà, le marteau étoufferait la corde qu'il vient d'exciter.
    const float halfPeriod = 0.5f * strings_[0].loopDelay();
    hammerLength_ = std::max(3, static_cast<int>(std::min(contactSamples, halfPeriod)));

    if (pendingStrike_) {
        hammerRemaining_ = hammerLength_;
        pendingStrike_ = false;
    }

    const float velocityGain = 1.0f - std::clamp(p.velocitySensitivity, 0.0f, 1.0f) * (1.0f - velocity);
    hammerGain_ = velocityGain;
    // Bruit de feutre : très faible, et d'autant plus présent que le feutre
    // est dur. Il n'est pas décoratif -- sans lui, l'attaque d'un piano
    // modélisé sonne trop propre, « en verre ».
    feltNoise_ = 0.06f * hardness;
}

float PianoVoice::render(const Params& /*p*/) {
    // Tous les réglages ont été traduits en géométrie de boucle et en durée de
    // contact par `updateTuning` : le rendu par échantillon n'a plus à les
    // relire, et c'est ce qui le garde court.
    if (!active_) return 0.0f;

    // Impulsion de force du marteau : un demi-cosinus, lisse et déterministe.
    // Ce n'est PAS une salve de bruit comme le pincement : un marteau est un
    // choc, et la douceur de sa forme est exactement ce qui donne la coupure
    // que la durée de contact commande.
    float drive = 0.0f;
    if (hammerRemaining_ > 0) {
        const float phase = std::clamp(
            1.0f - static_cast<float>(hammerRemaining_) / static_cast<float>(hammerLength_), 0.0f, 1.0f);
        const float force = 0.5f * (1.0f - std::cos(static_cast<float>(kTwoPi) * phase));
        drive = hammerGain_ * force * (0.9f + feltNoise_ * rng_.nextBipolar());
        --hammerRemaining_;
    }

    float sum = 0.0f;
    for (int i = 0; i < kStringsPerNote; ++i) {
        const size_t index = static_cast<size_t>(i);
        const float looped = strings_[index].advance();
        sum += strings_[index].inject(looped, drive, contact_[index]);
    }
    sum *= 0.5f;

    const float out = sum - dcX1_ + 0.9995f * dcY1_;
    dcX1_ = sum;
    dcY1_ = out;

    // Une corde de piano s'éteint d'elle-même : c'est le suiveur de crête qui
    // décide de la fin de la voix, jamais une enveloppe.
    const float magnitude = std::abs(out);
    level_ = magnitude > level_ ? magnitude : level_ * 0.99995f;
    if (level_ < 2.0e-5f && hammerRemaining_ == 0) active_ = false;

    return out;
}

// ---------------------------------------------------------------------------
// Machine
// ---------------------------------------------------------------------------

PianoSynth::PianoSynth() {
    parameterList_ = {
        {kHammerHardness, "Hammer Hardness", 0.0f, 1.0f, 0.45f, ""},
        {kHammerPosition, "Hammer Position", 0.04f, 0.3f, 0.125f, ""},
        {kVelocitySensitivity, "Velocity Sensitivity", 0.0f, 1.0f, 0.85f, ""},
        {kUnisonDetune, "Unison Detune", 0.0f, 1.0f, 0.35f, ""},
        {kInharmonicity, "Inharmonicity", 0.0f, 1.0f, 0.45f, ""},
        {kStringDecay, "String Decay", 0.5f, 30.0f, 12.0f, "s"},
        {kStringDamping, "String Damping", 0.0f, 1.0f, 0.3f, ""},
        {kRelease, "Release", 0.02f, 1.5f, 0.18f, "s"},
        {kSustainPedal, "Sustain Pedal", 0.0f, 1.0f, 0.0f, ""},
        {kSoundboardLevel, "Soundboard Level", 0.0f, 1.0f, 0.5f, ""},
        {kSoundboardSize, "Soundboard Size", 0.0f, 1.0f, 0.5f, ""},
        {kToneBass, "Tone Bass", -12.0f, 12.0f, 0.0f, "dB"},
        {kToneTreble, "Tone Treble", -12.0f, 12.0f, 0.0f, "dB"},
        {kStereoSpread, "Stereo Spread", 0.0f, 1.0f, 0.5f, ""},
        {kAnalogCharacter, "Analog Character", 0.0f, 1.0f, 0.2f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void PianoSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t index = 0;
    voiceManager_.forEachVoice([&](PianoVoice& voice) {
        voice.prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, index));
        ++index;
    });
    for (auto& section : soundboard_) section.setSampleRate(sampleRate);
    bassShelf_.setSampleRate(sampleRate);
    trebleShelf_.setSampleRate(sampleRate);
}

void PianoSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float PianoSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState PianoSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.piano";
    for (const auto& info : parameterList_) state.parameterValues[info.id] = getParameter(info.id);
    return state;
}

void PianoSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues) setParameter(id, value);
}

void PianoSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void PianoSynth::process(const MidiNoteEvent* events, int numEvents,
                         float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    PianoVoice::Params p;
    p.hammerHardness = params_[kHammerHardness].load(std::memory_order_relaxed);
    p.hammerPosition = params_[kHammerPosition].load(std::memory_order_relaxed);
    p.velocitySensitivity = params_[kVelocitySensitivity].load(std::memory_order_relaxed);
    p.unisonDetune = params_[kUnisonDetune].load(std::memory_order_relaxed);
    p.inharmonicity = params_[kInharmonicity].load(std::memory_order_relaxed);
    p.decaySeconds = params_[kStringDecay].load(std::memory_order_relaxed);
    p.damping = params_[kStringDamping].load(std::memory_order_relaxed);
    p.releaseSeconds = params_[kRelease].load(std::memory_order_relaxed);
    p.sustainPedal = params_[kSustainPedal].load(std::memory_order_relaxed) >= 0.5f;

    const float drift = params_[kAnalogCharacter].load(std::memory_order_relaxed);

    // Table d'harmonie. `Soundboard Size` fait glisser ses modes d'un piano
    // droit à un grand queue de concert : c'est la TAILLE de la table qui
    // sépare ces instruments bien plus que leur bois.
    const float boardLevel = params_[kSoundboardLevel].load(std::memory_order_relaxed);
    const float boardSize = params_[kSoundboardSize].load(std::memory_order_relaxed);
    const float scale = std::exp2f(0.9f - 1.9f * std::clamp(boardSize, 0.0f, 1.0f));
    const float boardHz[3] = {84.0f * scale, 176.0f * scale, 390.0f * scale};
    const float boardQ[3] = {2.2f, 2.8f, 1.8f};
    const float boardDb[3] = {7.5f, 5.5f, 3.5f};
    for (size_t i = 0; i < soundboard_.size(); ++i)
        soundboard_[i].set(Biquad::Type::Peaking, boardHz[i], boardQ[i], boardLevel * boardDb[i]);

    bassShelf_.set(Biquad::Type::LowShelf, 200.0f, 0.707f,
                   params_[kToneBass].load(std::memory_order_relaxed));
    trebleShelf_.set(Biquad::Type::HighShelf, 3000.0f, 0.707f,
                     params_[kToneTreble].load(std::memory_order_relaxed));

    const float spread = std::clamp(params_[kStereoSpread].load(std::memory_order_relaxed), 0.0f, 1.0f);
    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);

    voiceManager_.forEachVoice([&](PianoVoice& voice) {
        voice.setDriftAmount(drift);
        if (voice.isActive()) voice.updateTuning(p);
    });

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i) {
            applyNoteEvent(events[eventIndex++]);
            // Une note qui vient de démarrer n'a pas encore sa géométrie de
            // boucle ni sa durée de contact : les lui donner tout de suite
            // plutôt qu'au bloc suivant, sans quoi son attaque partirait sur
            // les réglages de la note précédente.
            voiceManager_.forEachVoice([&](PianoVoice& voice) {
                if (voice.isActive()) voice.updateTuning(p);
            });
        }

        float left = 0.0f, right = 0.0f;
        voiceManager_.forEachVoice([&](PianoVoice& voice) {
            const float value = voice.render(p);
            // Panoramique à puissance constante, pour qu'étaler le clavier ne
            // change pas le niveau d'ensemble.
            const float position = voice.pan() * spread;
            const float angle = (position + 1.0f) * 0.25f * static_cast<float>(kPi);
            left += value * std::cos(angle);
            right += value * std::sin(angle);
        });

        // NIVEAU CALIBRÉ SUR L'ACCORD, pas sur la note seule -- leçon de
        // `vsm.obx`. Mesuré : huit notes à vélocité 127, table d'harmonie au
        // maximum, l'accord culminait à 2,73 avec un gain de 0,42. Ramené à
        // 0,14, il culmine à 0,90 : de la marge sans écrêtage, et un test le
        // verrouille.
        const float gain = 0.14f * outputLevel;
        outputL[i] = left * gain;
        outputR[i] = right * gain;
    }

    // Table d'harmonie et correcteur agissent sur le MILIEU, en une seule
    // chaîne mono, et le côté passe intact.
    //
    // Ce n'est pas un raccourci, c'est la seule façon correcte avec un jeu de
    // filtres unique : appeler `process()` deux fois par échantillon, une fois
    // pour la gauche et une fois pour la droite, ferait avancer l'état du
    // filtre DEUX fois par échantillon -- il tournerait à double fréquence et
    // mélangerait les deux voies. C'est exactement le piège relevé sur la
    // cascade 24 dB/oct de l'OB-X et sur le filtre multimode du generic (§ 31).
    // Physiquement, c'est aussi le bon choix : la table rayonne le même bois
    // pour les deux oreilles, et le correcteur appartient à l'instrument.
    for (int i = 0; i < numSamples; ++i) {
        float mid = 0.5f * (outputL[i] + outputR[i]);
        const float side = 0.5f * (outputL[i] - outputR[i]);
        mid = trebleShelf_.process(bassShelf_.process(mid));
        for (auto& section : soundboard_) mid = section.process(mid);
        outputL[i] = mid + side;
        outputR[i] = mid - side;
    }
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.piano", "Piano (cordes frappées)", PianoSynth);

} // namespace vsm::plugins::piano
