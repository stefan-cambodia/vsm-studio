#pragma once
#include "vsm/sequencer/Track.h"
#include <map>
#include <cstdint>
#include <string>
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
    /// D577.1 : LE QUINTOLET — cinq cases dans l'espace de quatre (× 4 / 5), exclusif des deux autres. Exact à la noire,
    /// à la croche, à la double et à la triple-croche (384, 192, 96, 48 à 480 la noire) ; le septolet ne l'est jamais.
    bool quintuplet = false;
};

/// Durée en ticks d'une case de grille pour la résolution donnée.
midi::Tick gridResolutionToTicks(GridResolution res, uint16_t ppq);

/// D622.1 — UNE GRILLE PAR SON LIBELLÉ, comme la barre l'écrit (D495) : « 1/1 » à « 1/128 », suivi de « T » (triolet),
/// « . » (pointée) ou « Q » (quintolet). Faux pour un libellé qu'on ne sait pas lire — `grille` n'est alors pas touchée.
bool gridResolutionFromLabel(const std::string& libelle, GridResolution& grille);
/// L'inverse : le libellé de `grille` (« 1/16T »). `gridResolutionFromLabel(gridResolutionLabel(g))` rend `g`.
std::string gridResolutionLabel(GridResolution grille);

struct QuantizeSettings {
    GridResolution grid;
    float strength = 1.0f; ///< 0 = aucun effet, 1 = alignement total sur la grille
    /// Swing 0..1 : décale une case de grille sur deux (le "off-beat") de
    /// swing × pas / 3. 0 = droit, 1 = TRIOLET (rapport 2:1) -- D415 : l'ancien
    /// commentaire disait « ~0.33 = triolet », ce qui ne décale que d'un neuvième.
    float swing = 0.0f;
    bool quantizeNoteStart = true;
    bool quantizeNoteEnd = false; ///< quantifie aussi la fin (donc la durée)
    /// D575.1 : LA ZONE DE NON-QUANTIFICATION (*Non Quantize* de Cubase), en PART DU PAS : un début (ou une fin) déjà
    /// à cette distance de sa cible, ou moins, ne bouge pas — le jeu humain gardé près de la grille. 0 = aucune.
    float nonQuantizeZone = 0.0f;
    /// D575.2 : LE RAYON DE CAPTURE (*Catch Range*), en part du pas : seul ce qui est à cette distance de sa cible, ou
    /// moins, est quantifié ; plus loin, la note reste. 0 = tout capturé. La distance est mesurée avant la force.
    float catchRange = 0.0f;
};

/// D575.1, D575.2 : vrai si la position `tick` reste où elle est sous la zone et le rayon de `settings`.
bool quantizeLeavesAlone(midi::Tick tick, const QuantizeSettings& settings, uint16_t ppq);

/// Calcule le tick quantifié pour une position donnée (fonction pure,
/// réutilisée par quantizeNotes et par le piano roll pour le snapping visuel).
midi::Tick quantizeTick(midi::Tick tick, const QuantizeSettings& settings, uint16_t ppq);

/// Applique la quantification à un ensemble de notes, en place.
void quantizeNotes(std::vector<Note>& notes, const QuantizeSettings& settings, uint16_t ppq);

/// D624.1 : CHAQUE NOTE À LA GRILLE DE SON RANG — `rowGrids` est la carte de grilles d'une piste (`Track::drumGrid` : note
/// écrite → libellé, « 1/16T ») ; une note dont le rang y porte un libellé LISIBLE est quantifiée à cette grille, les
/// autres à `settings.grid` ; le swing, la force, la zone et le rayon sont ceux de `settings` pour toutes. L'ordre du
/// vecteur est gardé. Une carte vide : `quantizeNotes`, note pour note. Rend le nombre de notes qui ont suivi leur rang.
/// C'est LA quantification du logiciel : « Quantifier » (le piano roll) et « Quantifier chaque prise » l'appellent.
size_t quantizeNotesByRow(std::vector<Note>& notes, const QuantizeSettings& settings, uint16_t ppq,
                          const std::map<uint8_t, std::string>& rowGrids);

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
