#pragma once
#include "vsm/sequencer/DrumKit.h"
#include <cstdint>
#include <string>

namespace vsm::app::ui {

/// LE NOM DE LA PIÈCE QU'UNE NOTE DÉCLENCHE sur une piste de batterie (D109) — la table vit au cœur depuis D610
/// (`vsm/sequencer/DrumKit.h`), où la règle qui adapte une partie General MIDI à une machine se teste. Rend une chaîne
/// vide quand la note ne déclenche rien de nommé : le piano roll retombe alors sur le nom de la hauteur.
inline std::string drumVoiceName(const std::string& instrumentId, uint8_t note) {
    return vsm::sequencer::drumVoiceName(instrumentId, note);
}

} // namespace vsm::app::ui
