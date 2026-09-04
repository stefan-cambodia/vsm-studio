#pragma once
#include "vsm/midi/MidiEvent.h"
#include <cstdint>
#include <vector>

namespace vsm::sequencer {

using vsm::midi::Tick;

struct TempoChange {
    Tick tick;
    uint32_t microsecondsPerQuarterNote;
    /// D15.5 : LE TEMPO GLISSE JUSQU'AU CHANGEMENT SUIVANT, linéairement en
    /// BPM contre la position musicale (la courbe de tempo de Cubase), au
    /// lieu de tenir en palier. Sans changement suivant, le drapeau est sans
    /// effet. Faux par défaut : les cartes existantes ne changent pas.
    bool rampToNext = false;
};

/// Convertit entre position "musicale" (ticks) et position "horloge"
/// (secondes), en tenant compte de tous les changements de tempo.
///
/// C'est le composant central pour la précision temporelle exigée par le
/// cahier des charges : le Transport, le PlaybackScheduler et (en Phase 2)
/// le rendu audio offline s'appuient tous sur cette même conversion, ce qui
/// garantit un comportement identique en lecture temps réel et en export.
///
/// Peut aussi fonctionner en mode SMPTE (débit de ticks constant, pas de
/// tempo map) via le constructeur dédié.
class TempoMap {
public:
    /// Mode métrique par défaut : 120 BPM (500 000 µs/noire) au tick 0.
    TempoMap();

    /// Mode SMPTE : débit de ticks constant, indépendant du tempo musical.
    static TempoMap smpte(double framesPerSecond, uint16_t ticksPerFrame);

    /// Ajoute (ou remplace, si déjà présent à ce tick) un changement de
    /// tempo. Ignoré silencieusement en mode SMPTE.
    void addTempoChange(Tick tick, uint32_t microsecondsPerQuarterNote, bool rampToNext = false);
    /// D15.5 : pose ou retire la rampe du changement d'index `index`.
    void setRampToNext(size_t index, bool rampToNext);
    bool hasRamps() const;
    /// D15.5 : LA CARTE EN PALIERS, pour ce qui ne connaît pas les rampes (le
    /// fichier MIDI) : chaque rampe devient des paliers de `stepTicks`, et
    /// chaque palier reçoit le tempo qui lui fait durer EXACTEMENT ce que la
    /// rampe lui donne -- la durée totale est conservée à l'arrondi de la
    /// microseconde près, la courbe est approchée.
    std::vector<TempoChange> flattened(uint16_t ticksPerQuarterNote, Tick stepTicks) const;

    void clearTempoChanges(); // réinitialise à 120 BPM au tick 0

    double ticksToSeconds(Tick tick, uint16_t ticksPerQuarterNote) const;
    Tick secondsToTicks(double seconds, uint16_t ticksPerQuarterNote) const;

    double bpmAt(Tick tick) const;

    const std::vector<TempoChange>& changes() const { return changes_; }
    bool isSmpte() const { return isSmpte_; }

private:
    TempoMap(bool smpte, double fps, uint16_t tpf);

    std::vector<TempoChange> changes_; // trié par tick croissant, entrée au tick 0 garantie

    bool isSmpte_ = false;
    double smpteFramesPerSecond_ = 30.0;
    uint16_t smpteTicksPerFrame_ = 80;
};

} // namespace vsm::sequencer
