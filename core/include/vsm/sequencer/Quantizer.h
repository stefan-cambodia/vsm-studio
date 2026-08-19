#pragma once
#include "vsm/sequencer/Track.h"
#include <cstdint>
#include <vector>

namespace vsm::sequencer {

/// Valeurs de note de 1/1 (ronde) à 1/128, conformément à la section 3
/// du cahier des charges.
enum class NoteValue {
    Whole, Half, Quarter, Eighth, Sixteenth,
    ThirtySecond, SixtyFourth, HundredTwentyEighth
};

struct GridResolution {
    NoteValue value = NoteValue::Sixteenth;
    bool triplet = false; ///< division par 3 au lieu de 2 (ex: triolet de croches)
    bool dotted = false;  ///< x1.5 (incompatible avec triplet=true)
};

/// Durée en ticks d'une case de grille pour la résolution donnée.
midi::Tick gridResolutionToTicks(GridResolution res, uint16_t ppq);

struct QuantizeSettings {
    GridResolution grid;
    float strength = 1.0f; ///< 0 = aucun effet, 1 = alignement total sur la grille
    /// Swing 0..1 : décale une case de grille sur deux (le "off-beat") d'une
    /// fraction du pas de grille. 0 = droit, ~0.33 = swing "triolet" classique.
    float swing = 0.0f;
    bool quantizeNoteStart = true;
    bool quantizeNoteEnd = false; ///< quantifie aussi la fin (donc la durée)
};

/// Calcule le tick quantifié pour une position donnée (fonction pure,
/// réutilisée par quantizeNotes et par le piano roll pour le snapping visuel).
midi::Tick quantizeTick(midi::Tick tick, const QuantizeSettings& settings, uint16_t ppq);

/// Applique la quantification à un ensemble de notes, en place.
void quantizeNotes(std::vector<Note>& notes, const QuantizeSettings& settings, uint16_t ppq);

struct HumanizeSettings {
    uint64_t seed = 0;              ///< graine de session : mêmes notes + même seed = même résultat
    float timingAmountTicks = 0.0f; ///< amplitude max du jitter de timing (ticks, +/-)
    float velocityAmount = 0.0f;    ///< amplitude max du jitter de vélocité (+/-)
};

/// Ajoute un jitter contrôlé et REPRODUCTIBLE (voir DeterministicRng) au
/// timing et à la vélocité. Deux appels avec les mêmes settings.seed et les
/// mêmes note.id produisent toujours le même résultat.
void humanizeNotes(std::vector<Note>& notes, const HumanizeSettings& settings);

} // namespace vsm::sequencer
