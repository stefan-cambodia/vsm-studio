#pragma once
#include "vsm/audio/dsp/AnalogDrift.h"
#include "vsm/audio/dsp/Biquad.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/StringWaveguide.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::piano {

/// Piano acoustique — cordes frappées, modélisées, pas échantillonnées.
///
/// POURQUOI CETTE MACHINE. Le § 1 de docs/CDC-machines-manquantes.md donnait
/// le piano acoustique pour « hors de portée sans bibliothèque
/// d'échantillons », et le sampler servait de repli. Cette porte de sortie est
/// fermée : le sampler est désormais réservé à la voix. Il fallait donc un
/// piano MODÉLISÉ — et le guide d'ondes de `vsm.string` a rendu la chose
/// abordable, puisqu'un piano n'est rien d'autre qu'un jeu de cordes frappées
/// couplées à une table d'harmonie.
///
/// CE QUI FAIT UN PIANO, ET QU'UN SIMPLE GUIDE D'ONDES NE DONNE PAS
///
///  1. **Le marteau, et sa loi d'expressivité.** Un marteau de feutre reste en
///     contact avec la corde d'autant MOINS longtemps qu'il frappe fort. Or la
///     durée de contact fixe la coupure du spectre injecté : frapper fort ne
///     monte pas seulement le volume, cela ouvre le timbre. C'est LA loi
///     expressive du piano, et elle sort ici gratuitement de la physique au
///     lieu d'être imitée par une enveloppe de filtre. L'excitation est donc
///     une IMPULSION DE FORCE lisse (demi-cosinus), pas une salve de bruit
///     comme le pincement : un marteau est un choc déterministe.
///  2. **Le choeur de cordes.** Chaque note porte deux cordes légèrement
///     désaccordées. C'est ce qui donne le battement, et surtout la
///     DÉCROISSANCE EN DEUX TEMPS caractéristique — une chute rapide, puis une
///     longue traîne bien plus faible. Une seule corde donne une décroissance
///     exponentielle unique, qui s'entend immédiatement comme « pas un piano ».
///  3. **Le point de frappe au huitième.** Les marteaux frappent vers 1/8 de
///     la corde, ce qui place un noeud sur le 8e harmonique et le supprime.
///     C'est la raison pour laquelle un piano ne sonne pas dur. Le peigne du
///     guide d'ondes le produit tout seul dès qu'on lui donne la position.
///  4. **L'inharmonicité.** Les cordes de piano sont raides ; leurs partiels
///     montent. C'est si constitutif que l'accord d'un piano est ÉTIRÉ pour en
///     tenir compte. Le guide d'ondes la fournit, et son réglage est linéaire
///     dans l'effet (voir `dsp/StringWaveguide.h`).
///  5. **La pédale forte.** Elle ne « rallonge » pas le son : elle empêche les
///     étouffoirs de retomber. Modélisée comme telle — la pédale enfoncée, un
///     relâchement de touche ne change plus rien à la corde.
///
/// APPROXIMATIONS ASSUMÉES (§ 8 de CDC-nouvelle-machine.md, § 27
/// d'ARCHITECTURE.md) — aucune mesure n'a été faite sur un instrument réel, le
/// statut honnête est « dérivé » :
///
///  - **Deux cordes par note, pas trois.** Un piano réel en a une dans
///    l'extrême grave, deux dans le médium grave, trois ailleurs. Deux suffit
///    à produire le battement et la décroissance en deux temps ; une
///    troisième coûterait 50 % de calcul pour un raffinement du battement.
///  - **La décroissance en deux temps est OBTENUE PAR UN T60 DIFFÉRENT sur la
///    seconde corde**, et non par un vrai couplage au chevalet. Le couplage
///    réel échange de l'énergie entre les cordes dans les deux sens ; on en
///    garde l'effet audible (traîne longue) sans le mécanisme.
///  - **Aucune résonance sympathique** entre notes : pédale enfoncée, un vrai
///    piano fait chanter les cordes voisines. Il faudrait faire dialoguer les
///    voix entre elles, ce que l'architecture de voix indépendantes ne permet
///    pas sans couplage explicite. La pédale agit donc sur l'étouffoir, pas
///    sur les voisines — et c'est écrit plutôt que découvert à l'oreille.
///  - **Table d'harmonie en résonances série** : trois cloches de Biquad qui
///    colorent, pas une plaque qui rayonne.
///  - **Pas de bruit de mécanique** (retombée de touche, étouffoir).
class PianoVoice {
public:
    struct Params {
        float hammerHardness = 0.5f;
        float hammerPosition = 0.125f;
        float unisonDetune = 0.35f;
        float inharmonicity = 0.45f;
        float decaySeconds = 12.0f;
        float damping = 0.30f;
        float releaseSeconds = 0.18f;
        bool sustainPedal = false;
        float velocitySensitivity = 0.85f;
    };

    void prepare(double sampleRate, uint64_t seed);

    bool isActive() const { return active_; }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    void noteOff(uint8_t) { released_ = true; }
    void setDriftAmount(float amount) { drift_.setAmount(amount); }

    void updateTuning(const Params& p);
    float render(const Params& p);   ///< `p` n'est plus lu ici : voir `updateTuning`

    /// Panoramique de la voix : un piano se joue et s'enregistre étalé, grave
    /// à gauche et aigu à droite. C'est une propriété de la NOTE, pas un
    /// réglage de mixage, d'où sa place ici.
    float pan() const { return pan_; }

private:
    static constexpr int kStringsPerNote = 2;

    double sampleRate_ = 48000.0;
    std::array<vsm::audio::dsp::StringWaveguide, kStringsPerNote> strings_{};
    std::array<size_t, kStringsPerNote> contact_{};

    int hammerRemaining_ = 0;
    int hammerLength_ = 1;
    bool pendingStrike_ = false;
    float hammerGain_ = 1.0f;
    float feltNoise_ = 0.0f;

    float dcX1_ = 0.0f, dcY1_ = 0.0f;
    float level_ = 0.0f;
    float pan_ = 0.0f;
    bool active_ = false;
    bool released_ = false;

    vsm::audio::dsp::AnalogDrift drift_;
    vsm::util::DeterministicRng rng_{0x5049414E4FULL}; // "PIANO"
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class PianoSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kHammerHardness = 1, kHammerPosition, kVelocitySensitivity,
        kUnisonDetune, kInharmonicity, kStringDecay, kStringDamping,
        kRelease, kSustainPedal,
        kSoundboardLevel, kSoundboardSize,
        kToneBass, kToneTreble, kStereoSpread,
        kAnalogCharacter, kOutputLevel,
    };

    PianoSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Piano (cordes frappées)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<PianoVoice, kMaxVoices> voiceManager_;
    std::array<vsm::audio::dsp::Biquad, 3> soundboard_{};
    vsm::audio::dsp::Biquad bassShelf_, trebleShelf_;
};

} // namespace vsm::plugins::piano
