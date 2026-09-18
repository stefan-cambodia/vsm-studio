#pragma once
#include "vsm/sequencer/Track.h"
#include <string>
#include <vector>

// LA LISTE DES ÉVÉNEMENTS D'UNE PISTE (D32.2) -- l'éditeur de liste de Cubase.
//
// POURQUOI ELLE MANQUAIT, ET POURQUOI C'EST GRAVE ICI. Les notes se voient au
// piano roll, les contrôleurs dans leur onglet ; les CHANGEMENTS DE PROGRAMME,
// les PLIS DE HAUTEUR, la PRESSION DE CANAL et la PRESSION POLYPHONIQUE
// n'avaient AUCUNE vue. Le modèle les porte, le planificateur les joue,
// l'import les conserve et l'export les réécrit : seule la lecture manquait.
// Un morceau reconstruit ou importé pouvait donc porter des milliers
// d'événements que personne ne pouvait ni compter ni relire.
//
// EN FONCTION PURE, dans `core/`, comme `NoteEdit` et `ClipEdit` : ce qui
// décide de ce qu'un événement EST relève du modèle, pas du dessin. C'est
// aussi ce qui rend vérifiable l'attendu de la phase -- « autant de lignes que
// le modèle porte d'événements » -- sans ouvrir de fenêtre.

namespace vsm::sequencer {

enum class EventKind {
    Note = 0,
    ControlChange,
    PitchBend,
    PolyPressure,
    ChannelPressure,
    ProgramChange,
};

/// Une ligne de la liste. Les nombres sont BRUTS -- c'est tout l'intérêt d'une
/// liste : voir 8192 et non « au centre ».
struct EventRow {
    Tick tick = 0;
    EventKind kind = EventKind::Note;
    uint8_t channel = 0;
    /// Ce qui identifie l'événement dans sa famille : le numéro de note, le
    /// numéro de contrôleur, le programme. Zéro quand la famille n'en a pas.
    int first = 0;
    /// Sa valeur : vélocité, valeur de CC, pli (-8192..8191), pression.
    int second = 0;
    /// La durée, en ticks, pour une NOTE seulement. Zéro ailleurs.
    Tick length = 0;
    /// L'identifiant de la note, pour la retrouver et la supprimer. Zéro pour
    /// les autres familles, qui n'en ont pas -- elles se désignent par leur
    /// rang dans leur propre vecteur (voir `indexInKind`).
    uint64_t noteId = 0;
    /// Le rang dans le vecteur de sa famille. C'est ce qui permet de supprimer
    /// un CC ou un pli, qui ne portent pas d'identifiant.
    size_t indexInKind = 0;
};

/// Tous les événements de la piste, TRIÉS par tick puis par famille.
///
/// Les notes MUETTES sont incluses et le disent (`second` garde leur vélocité,
/// c'est la vue qui les grise) : une liste qui les cacherait ferait chercher
/// une note qu'on voit pourtant au piano roll.
std::vector<EventRow> listTrackEvents(const Track& track);

/// Le nom court d'une famille, pour la colonne « nature ».
std::string eventKindLabel(EventKind kind);

/// Retire de la piste l'événement décrit par `row`. Rend faux si la ligne ne
/// désigne plus rien -- ce qui arrive si la piste a changé entre l'affichage
/// et le clic, et qui doit être dit plutôt que de supprimer le voisin.
bool removeTrackEvent(Track& track, const EventRow& row);

/// D348 : CE QU'UNE LIGNE DE LA LISTE PEUT CHANGER.
///
/// La liste d'événements savait REGARDER et SUPPRIMER ; elle ne savait pas
/// MODIFIER, alors que c'est la raison d'être d'un éditeur en liste (Cubase,
/// Logic et Reaper y changent hauteur, vélocité, position et durée au clavier).
/// Corriger une vélocité demandait de retrouver la note dans le piano roll.
enum class EventField {
    Position,   ///< le tick de l'événement
    Number,     ///< numéro de note, de contrôleur ou de programme (0-127)
    Value,      ///< vélocité, valeur de contrôleur, pli, pression
    Length,     ///< la durée, pour une NOTE seulement
};

/// Change un champ de l'événement décrit par `row`.
///
/// Rend faux — et ne touche à rien — si la ligne ne désigne plus rien (la piste
/// a changé entre l'affichage et la saisie), si le champ n'a pas de sens pour
/// cette famille (une durée sur un contrôleur), ou si la valeur sort des bornes
/// de son domaine. **On ne borne pas en silence** : une vélocité à 300 est un
/// refus, pas un 127 — l'utilisateur doit savoir que ce qu'il a tapé n'a pas été
/// pris. Seule exception, dite ici : une position négative devient 0, parce
/// qu'un tick négatif n'existe pas et que le geste (« ramener au début ») est
/// sans ambiguïté.
bool setTrackEventField(Track& track, const EventRow& row, EventField field, long long value);

/// D352 : CRÉER UN ÉVÉNEMENT DEPUIS LA LISTE.
///
/// La liste savait regarder, supprimer (D32.2) et modifier (D348) ; elle ne
/// savait pas CRÉER, et c'est la troisième moitié d'un éditeur en liste. Le
/// manque n'était pas théorique : un **changement de programme** ou une
/// **pression polyphonique** ne se posent nulle part ailleurs dans le logiciel
/// — le piano roll fait des notes, la lane MIDI CC fait des contrôleurs, des
/// plis et des pressions de canal, et personne ne fait les deux autres.
///
/// `channel` est celui de la piste ; `first` et `second` ont le sens qu'ils ont
/// dans `EventRow` (numéro et valeur), `length` ne sert qu'aux notes.
/// `idCounter` n'est consommé que si une note est créée.
///
/// Rend faux — et ne touche à rien — quand la demande n'a pas de sens : un tick
/// négatif, une durée nulle pour une note, une valeur hors du domaine de sa
/// famille. **On ne borne pas en silence**, pour la même raison qu'à D348.
bool addTrackEvent(Track& track, EventKind kind, Tick tick, int channel,
                    int first, int second, Tick length, uint64_t& idCounter);

} // namespace vsm::sequencer
