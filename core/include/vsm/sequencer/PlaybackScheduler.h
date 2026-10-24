#pragma once
#include "vsm/midi/MidiEvent.h"
#include "vsm/sequencer/Project.h"
#include <cstddef>
#include <vector>

namespace vsm::sequencer {

struct ScheduledEvent {
    double timeSeconds = 0.0;
    size_t trackIndex = 0;
    midi::MidiEventData data;
};

/// Construit la liste plate, triée par temps, de tous les événements
/// "jouables" d'un projet entre [startTick, endTick).
///
/// C'est une fonction PURE et déterministe : mêmes entrées -> mêmes
/// sorties, toujours. C'est un choix d'architecture volontaire : elle sert
/// à la fois de base au Transport temps réel (RealtimeTransport), aux tests
/// automatisés (pas de flakiness liée au threading), et servira en Phase 2
/// au rendu audio offline (export WAV) — un seul et même calcul de timing
/// pour la lecture live et l'export, donc pas de divergence possible entre
/// "ce qu'on entend" et "ce qu'on exporte".
///
/// Respecte mute/solo : si au moins une piste est soloée, les autres sont
/// traitées comme muettes pour la lecture.
class PlaybackScheduler {
public:
    static std::vector<ScheduledEvent> build(const Project& project,
                                               midi::Tick startTick,
                                               midi::Tick endTick);

    /// LA CHASSE AUX CONTRÔLEURS (D16.2) — « Chase Events » de Cubase.
    ///
    /// Les valeurs en vigueur à `startTick` — la dernière valeur STRICTEMENT
    /// avant lui de chaque (canal, contrôleur), du bend, de la pression de
    /// canal et du programme —, horodatées à `startTick` et prêtes à être
    /// jouées. Démarrer la lecture au refrain perdait sans cela la pédale
    /// posée au couplet, le balayage de filtre en cours et le programme
    /// choisi à la première mesure : le morceau ne sonnait pas comme
    /// lui-même, et rien ne le disait.
    ///
    /// PUBLIQUE, et pas seulement un détail de `build` : le moteur ne
    /// construit son planning qu'UNE fois, du début à la fin du morceau
    /// (`ProcessGraph::setProject`), et se déplace ensuite dedans par
    /// dichotomie. C'est donc au DÉPLACEMENT DE LA TÊTE qu'il faut chasser,
    /// pas à la construction — et c'est ce que fait `ProcessGraph::seekSeconds`
    /// avec cette fonction. Le rendre privé aurait laissé la chasse vraie
    /// dans les tests et absente à l'oreille.
    ///
    /// Respecte mute et solo, comme `build`.
    static std::vector<ScheduledEvent> chaseAt(const Project& project, midi::Tick startTick);

    /// D585.3 : LA CHASSE AUX NOTES (*Chase Events ▸ Note*) — un `NoteOnEvent` daté de `startTick` pour chaque note
    /// dont le début de SORTIE est strictement avant `startTick` et la fin de sortie (coupée à la fin du clip)
    /// strictement après : la note qu'un départ prend en route. Le calcul est celui de `build` — mêmes passages, même
    /// chaîne d'effets MIDI, mêmes transpositions, mêmes pistes jouées —, une hauteur au plus par piste. À PART de
    /// `chaseAt` : `build` s'en sert pour tout planning qui ne part pas de zéro, et des NoteOn chassés y entreraient dans
    /// des chemins qui ne les attendent pas. La fin de la note n'est pas rendue : elle est dans le planning.
    static std::vector<ScheduledEvent> chaseNotesAt(const Project& project, midi::Tick startTick);

    /// D17.5 : combien de notes la transposition des pistes ferait sortir de
    /// 0..127, et qui ne sonneront donc pas. Zéro quand aucune piste n'est
    /// transposée. À DIRE, jamais à taire : une note qu'on a écrite et qui ne
    /// sonne pas est exactement le genre de silence qu'on cherche des heures.
    static size_t transposeDroppedNotes(const Project& project);
};

} // namespace vsm::sequencer
