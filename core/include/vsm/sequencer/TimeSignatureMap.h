#pragma once
#include "vsm/midi/MidiEvent.h"
#include <cstdint>
#include <vector>

namespace vsm::sequencer {

using vsm::midi::Tick;

struct TimeSignatureChange {
    Tick tick;
    uint8_t numerator;        // ex: 4 pour 4/4
    uint8_t denominatorPow2;  // dénominateur = 2^denominatorPow2 (2 => /4)
};

struct BarBeat {
    int64_t bar;    // 0-indexé
    int64_t beat;   // 0-indexé, dans la mesure
    Tick tickInBeat;
};

/// Modélise les changements de signature rythmique et fournit les
/// conversions nécessaires à l'affichage (règle de mesures) et au piano
/// roll (grille alignée sur les mesures, notamment pour le swing et la
/// boucle). Indépendant de TempoMap : la position "musicale" (mesures/temps)
/// ne dépend que des ticks, jamais du tempo.
class TimeSignatureMap {
public:
    TimeSignatureMap(); // 4/4 par défaut au tick 0

    void addChange(Tick tick, uint8_t numerator, uint8_t denominatorPow2);
    void clear();
    /// D21.4 : retire le changement posé EXACTEMENT à `tick`. Celui du tick
    /// zéro ne se retire pas -- un morceau a toujours une signature -- : il se
    /// remplace par `addChange`. Rend vrai si quelque chose a été retiré.
    bool removeChangeAt(Tick tick);

    uint32_t denominatorAt(Tick tick) const; // valeur réelle (4, 8, 16...)
    uint8_t numeratorAt(Tick tick) const;

    /// Nombre de ticks pour un temps ("beat") à la position donnée.
    Tick ticksPerBeat(Tick tick, uint16_t ppq) const;
    /// Nombre de ticks pour une mesure complète à la position donnée.
    Tick ticksPerBar(Tick tick, uint16_t ppq) const;

    BarBeat barBeatAt(Tick tick, uint16_t ppq) const;

    const std::vector<TimeSignatureChange>& changes() const { return changes_; }

private:
    std::vector<TimeSignatureChange> changes_;
};

} // namespace vsm::sequencer
