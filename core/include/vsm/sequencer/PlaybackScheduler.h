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
};

} // namespace vsm::sequencer
