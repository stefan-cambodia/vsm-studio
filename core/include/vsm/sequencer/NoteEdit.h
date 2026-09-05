#pragma once
#include "vsm/sequencer/Track.h"
#include <cstdint>
#include <set>
#include <string>
#include <vector>

// Toutes les opérations d'édition du piano roll, en fonctions PURES sur un
// vecteur de notes -- aucune dépendance à JUCE ni à quoi que ce soit d'UI.
//
// POURQUOI ICI ET PAS DANS LE COMPOSANT : c'est la règle n°1 du projet (« le
// moteur ne dépend jamais de l'UI », ARCHITECTURE.md § 1). Un « legato » ou un
// « arpégier » sont de la logique musicale, pas du dessin : les mettre dans le
// composant JUCE les rendrait intestables (il faudrait un serveur graphique
// pour vérifier une transposition) et inutilisables ailleurs -- alors qu'ils
// serviront tels quels à un futur éditeur de motifs, à un script Python de la
// Phase 7 ou à une action de menu.
//
// CONVENTION COMMUNE : la sélection est un ensemble d'identifiants de notes
// (`Note::id`, stable), jamais d'indices -- une opération peut réordonner ou
// insérer des notes sans invalider la sélection de l'appelant. Une sélection
// vide signifie « ne rien faire » plutôt que « tout traiter », pour qu'un
// raccourci clavier déclenché par erreur sans sélection soit sans effet.

namespace vsm::sequencer {

using NoteSelection = std::set<uint64_t>;

// ---------------------------------------------------------------------------
// Gammes
// ---------------------------------------------------------------------------

enum class ScaleType {
    Chromatic, Major, NaturalMinor, HarmonicMinor, MelodicMinor,
    Dorian, Phrygian, Lydian, Mixolydian, Locrian,
    PentatonicMajor, PentatonicMinor, Blues, WholeTone
};

struct Scale {
    uint8_t root = 0;                        ///< 0 = Do, 1 = Do#, ... 11 = Si
    ScaleType type = ScaleType::Chromatic;
};

/// Masque des 12 demi-tons de la gamme (bit i = le degré i est dans la gamme).
uint16_t scaleMask(ScaleType type);
bool isNoteInScale(uint8_t noteNumber, Scale scale);
/// Ramène une note sur le degré de gamme le plus proche (départage vers le bas
/// à égalité, pour que le résultat soit déterministe et reproductible).
uint8_t snapNoteToScale(uint8_t noteNumber, Scale scale);
const char* scaleTypeName(ScaleType type);
std::vector<ScaleType> allScaleTypes();
/// Nom de note avec octave (« C4 », « F#3 »), convention note 60 = C4.
std::string noteNumberToName(uint8_t noteNumber);

// ---------------------------------------------------------------------------
// Accords et arpèges
// ---------------------------------------------------------------------------

enum class ChordType { Major, Minor, Diminished, Augmented, Sus2, Sus4, Power,
                        Major7, Minor7, Dominant7, Minor7Flat5, Major9, Minor9 };

const char* chordTypeName(ChordType type);
std::vector<ChordType> allChordTypes();
/// Intervalles en demi-tons depuis la fondamentale (toujours triés croissant).
std::vector<int> chordIntervals(ChordType type);

enum class ArpeggioMode { Up, Down, UpDown, Random };

// ---------------------------------------------------------------------------
// Opérations d'édition (en place, sur la sélection)
// ---------------------------------------------------------------------------

/// Transposition. Les notes qui sortiraient de 0..127 sont bornées, jamais
/// perdues -- une transposition suivie de son inverse peut donc écraser des
/// extrêmes, c'est le comportement attendu partout ailleurs (et l'undo est là
/// pour ça).
void transposeNotes(std::vector<Note>& notes, const NoteSelection& selection, int semitones);

/// Décalage temporel. Rien ne passe avant le tick 0 (bornage, pas de perte).
void nudgeNotes(std::vector<Note>& notes, const NoteSelection& selection, int64_t deltaTicks);

void setNoteLengths(std::vector<Note>& notes, const NoteSelection& selection, Tick lengthTicks);
void scaleNoteLengths(std::vector<Note>& notes, const NoteSelection& selection, float factor);

/// Legato : chaque note sélectionnée est étendue jusqu'au début de la note
/// suivante de la piste (toutes hauteurs confondues, comme le font les DAW).
/// La dernière note de la piste garde sa durée.
void applyLegato(std::vector<Note>& notes, const NoteSelection& selection);

/// Raccourcit les notes qui en chevauchent une autre de MÊME hauteur, pour
/// éliminer les superpositions qui produisent des notes « collées » à la
/// lecture (un NoteOff coupant la note suivante).
void removeOverlaps(std::vector<Note>& notes, const NoteSelection& selection);

/// Coupe en deux, au tick donné, chaque note sélectionnée qui le traverse.
/// Renvoie le nombre de notes créées ; les nouvelles moitiés sont ajoutées à
/// `newIds` si le pointeur est fourni (pour que l'appelant les sélectionne).
size_t splitNotes(std::vector<Note>& notes, const NoteSelection& selection, Tick atTick,
                   uint64_t& idCounter, NoteSelection* newIds = nullptr);

/// Fusionne les notes sélectionnées de même hauteur en une seule (de la
/// première attaque à la dernière fin). Renvoie le nombre de notes supprimées.
/// `selection` est mise à jour pour ne contenir que les notes survivantes.
size_t joinNotes(std::vector<Note>& notes, NoteSelection& selection);

/// Rétrograde : renverse l'ordre temporel de la sélection dans sa propre
/// fenêtre (la première note devient la dernière), durées conservées.
void reverseNotesInTime(std::vector<Note>& notes, const NoteSelection& selection);

/// Miroir des hauteurs autour du centre de la sélection (une mélodie montante
/// devient descendante).
void mirrorNotesPitch(std::vector<Note>& notes, const NoteSelection& selection);

void setVelocity(std::vector<Note>& notes, const NoteSelection& selection, uint8_t velocity);
void scaleVelocity(std::vector<Note>& notes, const NoteSelection& selection, float factor);
/// Dégradé de vélocité dans le temps sur la sélection (crescendo/decrescendo).
void rampVelocity(std::vector<Note>& notes, const NoteSelection& selection, uint8_t fromVelocity, uint8_t toVelocity);
/// Jitter de vélocité REPRODUCTIBLE (même seed + mêmes id = même résultat),
/// comme humanizeNotes() du Quantizer.
void randomizeVelocity(std::vector<Note>& notes, const NoteSelection& selection, int amount, uint64_t seed);

/// D19.1 — RESSERRE LES VÉLOCITÉS VERS LEUR MOYENNE.
///
/// `amount` va de 0 à 1 : **1 ne change rien**, 0 rend toutes les notes égales
/// à la moyenne arrondie de la sélection, et les valeurs intermédiaires
/// interpolent. Le sens est celui d'un compresseur — c'est un RAPPORT de
/// conservation, pas une quantité de compression — et il est choisi ainsi
/// pour que la valeur neutre soit celle qui ne touche à rien.
///
/// POURQUOI CETTE FONCTION EXISTE, ET CE N'EST PAS UN OUTIL MUSICAL. Sur une
/// TRANSCRIPTION, les vélocités relevées sont bruitées : le même coup de
/// caisse claire ressort à 71, 96 et 58 parce que l'estimation dépend de ce
/// qui sonnait en même temps. Les resserrer rend à l'instrument une frappe
/// régulière que le jeu avait, et que l'analyse a perdue. `scaleVelocity`
/// multiplie et ne peut donc pas faire cela : elle écarte les valeurs autant
/// qu'elle les monte.
void compressVelocity(std::vector<Note>& notes, const NoteSelection& selection, float amount);

/// D19.1 — CONTIENT LES VÉLOCITÉS DANS UN INTERVALLE, en les y RAMENANT plutôt
/// qu'en les y remettant à l'échelle : une note déjà comprise ne bouge pas.
///
/// C'est ce qui rend l'opération IDEMPOTENTE — l'appliquer deux fois donne le
/// même résultat qu'une fois — et c'est la propriété qu'on attend d'une
/// limite. Une mise à l'échelle vers l'intervalle, elle, déplacerait des notes
/// qui n'avaient rien demandé.
///
/// `minVelocity > maxVelocity` est traité comme l'intervalle inversé plutôt
/// que refusé en silence : deux bornes saisies à l'envers sont une faute de
/// frappe, pas une demande d'ignorer le geste.
void limitVelocity(std::vector<Note>& notes, const NoteSelection& selection,
                    uint8_t minVelocity, uint8_t maxVelocity);

void constrainNotesToScale(std::vector<Note>& notes, const NoteSelection& selection, Scale scale);

void setNotesMuted(std::vector<Note>& notes, const NoteSelection& selection, bool muted);
void toggleNotesMuted(std::vector<Note>& notes, const NoteSelection& selection);

/// Copie la sélection décalée de `offsetTicks`. Renvoie les identifiants des
/// copies (pour les sélectionner à la place des originales, comportement
/// habituel d'un « dupliquer »).
NoteSelection duplicateNotes(std::vector<Note>& notes, const NoteSelection& selection,
                              Tick offsetTicks, uint64_t& idCounter);

/// Transforme les accords de la sélection en arpèges : les notes qui démarrent
/// au même tick sont réparties dans le temps par pas de `stepTicks`.
/// Renvoie le nombre de notes déplacées.
size_t arpeggiateNotes(std::vector<Note>& notes, const NoteSelection& selection,
                        Tick stepTicks, ArpeggioMode mode, uint64_t seed = 1);

/// Insère un accord complet. Renvoie les identifiants des notes créées.
NoteSelection insertChord(std::vector<Note>& notes, Tick startTick, Tick lengthTicks,
                           uint8_t rootNote, ChordType type, uint8_t channel,
                           uint8_t velocity, uint64_t& idCounter);

// ---------------------------------------------------------------------------
// Sélection
// ---------------------------------------------------------------------------

NoteSelection selectAllNotes(const std::vector<Note>& notes);
NoteSelection invertNoteSelection(const std::vector<Note>& notes, const NoteSelection& selection);
/// Toutes les notes ayant la même hauteur qu'une note déjà sélectionnée.
NoteSelection selectNotesWithSamePitch(const std::vector<Note>& notes, const NoteSelection& selection);
/// Toutes les notes qui commencent dans [fromTick, toTick).
NoteSelection selectNotesInTimeRange(const std::vector<Note>& notes, Tick fromTick, Tick toTick);

/// D21.1 : LES NOTES FANTÔMES D'UNE TRANSCRIPTION se reconnaissent à leur
/// vélocité et à leur durée. Bornes STRICTES : « plus faibles que 32 » ne prend
/// pas 32, « plus courtes que la grille » ne prend pas une note d'une grille.
NoteSelection selectNotesBelowVelocity(const std::vector<Note>& notes, uint8_t belowVelocity);
NoteSelection selectNotesShorterThan(const std::vector<Note>& notes, Tick shorterThanTicks);

/// Résumé d'une sélection, pour la barre d'information du piano roll.
struct SelectionStats {
    size_t count = 0;
    Tick startTick = 0;
    Tick endTick = 0;
    uint8_t lowestNote = 0;
    uint8_t highestNote = 0;
    float averageVelocity = 0.0f;
};
SelectionStats computeSelectionStats(const std::vector<Note>& notes, const NoteSelection& selection);

// ---------------------------------------------------------------------------
// Notes douteuses (étape 11.3 de ROADMAP-fusion.md)
//
// La transcription rend une confiance par note ; le piano roll marque celles
// qui passent sous le seuil. Sur un morceau de quelques milliers de notes,
// les MARQUER ne suffit pas : il faut pouvoir y ALLER sans les chercher à
// l'œil, une par une, dans l'ordre du morceau. La règle de parcours vit ici,
// pas dans le composant : c'est une question d'ordre musical (quelle est la
// note douteuse « suivante » ?), et elle se teste sans écran.
// ---------------------------------------------------------------------------

/// En dessous de cette confiance, une note est douteuse. 0,55 : au-dessus, la
/// transcription est franche dans les cas mesurés ; en dessous, elle a hésité.
/// Le seuil est ici, en un seul endroit, pour qu'il se règle sans chercher --
/// le piano roll, la barre d'état et le rapport d'ouverture lisent tous
/// celui-ci.
inline constexpr float kDoubtfulNoteThreshold = 0.55f;

bool isNoteDoubtful(const Note& note, float threshold = kDoubtfulNoteThreshold);
size_t countDoubtfulNotes(const std::vector<Note>& notes, float threshold = kDoubtfulNoteThreshold);
/// Toutes les notes douteuses de la piste (pour les écouter, les rendre
/// muettes ou les supprimer d'un seul geste).
NoteSelection selectDoubtfulNotes(const std::vector<Note>& notes, float threshold = kDoubtfulNoteThreshold);

/// La note douteuse suivante (ou précédente) dans l'ordre du morceau -- tick
/// de début, puis hauteur, puis identifiant, pour que l'ordre soit total.
///
/// Point de départ : la sélection, si elle en a une (la plus tardive vers
/// l'avant, la plus précoce vers l'arrière) ; sinon la tête de lecture, et la
/// première douteuse qui commence à la tête ou après est la « suivante ».
/// Arrivé au bout, on repart de l'autre extrémité : appuyer encore fait le
/// tour du morceau au lieu de ne rien faire. Rend 0 s'il n'y a aucune note
/// douteuse (0 n'est jamais un identifiant de note).
uint64_t nextDoubtfulNote(const std::vector<Note>& notes, const NoteSelection& selection,
                          Tick playheadTick, bool forward, float threshold = kDoubtfulNoteThreshold);

} // namespace vsm::sequencer
