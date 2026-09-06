#pragma once
#include "vsm/sequencer/Track.h"
#include <cstddef>
#include <vector>

// LES EFFETS MIDI DE PISTE (D31) -- les *MIDI inserts* de Cubase, le *MIDI
// Effects rack* de Live, en fonctions PURES.
//
// POURQUOI ICI, ET NON DANS LE PLANIFICATEUR. La même règle que `NoteEdit.h`
// et `ClipEdit.h` : ce qui décide de la hauteur, de la force et de la place
// d'une note est de la logique musicale. Rangée dans `PlaybackScheduler`, elle
// ne se testerait qu'à travers un planning complet -- il faudrait un projet,
// des clips et une carte de tempo pour vérifier qu'un arpège compte seize
// attaques.
//
// CE QUI LES DISTINGUE DE `NoteEdit.h`, qui sait déjà arpéger : `NoteEdit`
// ÉCRIT dans les notes, ces fonctions RENDENT une liste neuve. Le matériau ne
// bouge pas, et c'est tout ce qui sépare un effet d'une édition.

namespace vsm::sequencer {

/// Ce qu'une passe d'effets a fait, pour le dire plutôt que le taire.
struct MidiEffectReport {
    /// Notes ÉCARTÉES parce qu'une transposition les a poussées hors de
    /// 0..127. Jamais repliées à l'octave : replier ferait sonner une note que
    /// personne n'a demandée. Même règle que la transposition de piste (D17.5).
    size_t droppedOutOfRange = 0;
    /// Effets dont l'identité n'est pas connue. Ils ne font rien, et on le dit.
    size_t unknownEffects = 0;
};

/// Applique la chaîne à `notes` et rend le résultat. `notes` n'est pas touché.
///
/// L'ORDRE DE LA CHAÎNE COMPTE, comme pour les inserts audio : transposer puis
/// arpéger n'est pas arpéger puis transposer (le second transpose des notes
/// que le premier n'avait pas encore créées -- ce qui donne le même son ici,
/// mais ne le donnerait plus avec un effet qui lit les hauteurs).
///
/// `ppq` sert à l'arpégiateur, dont le pas est en fractions de noire.
/// `report`, s'il est fourni, reçoit ce qui a été écarté.
std::vector<Note> applyMidiEffects(const std::vector<MidiEffect>& effects,
                                    const std::vector<Note>& notes, uint16_t ppq,
                                    MidiEffectReport* report = nullptr);

/// Les paramètres d'un type d'effet, avec leurs bornes et leur défaut -- la
/// source unique dont se servent la fabrique, le volet et le fichier.
struct MidiEffectParam {
    const char* name;
    float minValue;
    float maxValue;
    float defaultValue;
};
/// Vide si le type est inconnu.
std::vector<MidiEffectParam> midiEffectParameters(const std::string& type);

/// Les types connus, dans l'ordre où on les propose.
std::vector<std::string> midiEffectTypes();

/// Le nom lisible d'un type (« Transposition »...), ou le type lui-même s'il
/// est inconnu -- afficher « (inconnu) » cacherait ce qu'on a chargé.
std::string midiEffectDisplayName(const std::string& type);

} // namespace vsm::sequencer
