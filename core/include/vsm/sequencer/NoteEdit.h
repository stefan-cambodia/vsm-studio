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
/// D495 : L'IDENTIFIANT STABLE d'un type de gamme, pour un FICHIER (« dorian »).
/// Le nom affiché (`scaleTypeName`) est traduit et peut changer ; un identifiant
/// écrit dans un projet ne le doit pas.
const char* scaleTypeId(ScaleType type);
/// Rend faux, sans toucher `out`, pour un identifiant inconnu.
bool scaleTypeFromId(const std::string& id, ScaleType& out);
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
/// D536 : TOUT OU RIEN. Rend le nombre de notes choisies que `semitones` pousserait hors de
/// 0..127 ; s'il n'est pas nul, AUCUNE note ne bouge — ni bornée (une hauteur que personne n'a
/// demandée), ni laissée seule (un accord cassé en deux octaves). À dire par l'appelant.
size_t transposeNotes(std::vector<Note>& notes, const NoteSelection& selection, int semitones);
/// D536 : le compte seul, pour décider AVANT d'ouvrir un pas d'historique.
size_t notesLeavingMidiRange(const std::vector<Note>& notes, const NoteSelection& selection, int semitones);

/// Décalage temporel. Rien ne passe avant le tick 0 (bornage, pas de perte).
void nudgeNotes(std::vector<Note>& notes, const NoteSelection& selection, int64_t deltaTicks);

void setNoteLengths(std::vector<Note>& notes, const NoteSelection& selection, Tick lengthTicks);
void scaleNoteLengths(std::vector<Note>& notes, const NoteSelection& selection, float factor);

/// D22.3 : deux fois plus lent (×2) ou plus vite (÷2) -- les DÉPARTS et les
/// durées sont mis à l'échelle depuis le premier départ de la sélection,
/// qui ne bouge pas. C'est ce qui distingue « la même phrase à moitié de
/// vitesse » de « chaque note deux fois plus longue » (`scaleNoteLengths`),
/// où les notes finissent par se chevaucher. Durée d'au moins un tick ; les
/// notes non choisies sont immobiles.
void scaleNoteTimes(std::vector<Note>& notes, const NoteSelection& selection, double factor);

/// Legato : chaque note sélectionnée est étendue jusqu'au début de la note
/// suivante de la piste (toutes hauteurs confondues, comme le font les DAW).
/// La dernière note de la piste garde sa durée.
void applyLegato(std::vector<Note>& notes, const NoteSelection& selection);

/// Raccourcit les notes qui en chevauchent une autre de MÊME hauteur, pour
/// éliminer les superpositions qui produisent des notes « collées » à la
/// lecture (un NoteOff coupant la note suivante).
void removeOverlaps(std::vector<Note>& notes, const NoteSelection& selection);

/// D546.1 : SUPPRIMER LES DOUBLONS. Parmi les notes choisies, celles de même HAUTEUR et même CANAL qui
/// commencent à moins de `toleranceTicks` de la PREMIÈRE d'un groupe n'en gardent qu'une : la plus forte,
/// puis la plus longue ; une note muette ne bat jamais une note qui sonne. Une tolérance mesurée depuis la
/// première note du groupe, et non de proche en proche : une suite de notes rapprochées ne se fond pas en
/// une. Les notes hors de la sélection ne sont ni retirées ni ne font retirer. Rend le nombre de notes
/// retirées.
size_t deleteDoubleNotes(std::vector<Note>& notes, const NoteSelection& selection, Tick toleranceTicks);

/// D552.2 — LES CHEVAUCHEMENTS EN MONO (*Delete Overlaps (mono)* de Cubase) : les notes CHOISIES deviennent une ligne qui
/// ne joue jamais deux notes à la fois -- ce que demande une basse confiée à une machine monophonique. Dans l'ordre
/// des départs : deux notes qui commencent ENSEMBLE n'en gardent qu'une, la plus aiguë (les autres sont retirées,
/// `removed`) ; une note qui déborde sur le départ de la SUIVANTE, quelle que soit sa hauteur, finit où celle-ci
/// commence (`shortened`). Les notes non choisies ne bougent pas et ne comptent pas.
struct MonoOverlaps {
    size_t shortened = 0;
    size_t removed = 0;
};
MonoOverlaps removeOverlapsMono(std::vector<Note>& notes, const NoteSelection& selection);

/// D554.2 — RESTREINDRE LA POLYPHONIE (*Restrict Polyphony* de Cubase) : parmi les notes CHOISIES, jamais plus de
/// `voices` à la fois. Dans l'ordre des départs, par groupe d'attaques simultanées : un groupe plus nombreux que
/// `voices` garde ses plus AIGUËS et retire les autres (`removed`, la règle de D552.2) ; puis, tant que les notes qui
/// sonnent encore et le groupe dépassent `voices`, la plus ANCIENNE (à départ égal, la plus grave) finit où le groupe
/// commence (`shortened`). À une voix, c'est `removeOverlapsMono`. Les notes non choisies ne bougent pas et ne
/// comptent pas ; `voices` < 1 ne fait rien.
MonoOverlaps restrictPolyphony(std::vector<Note>& notes, const NoteSelection& selection, int voices);


/// D554.3 — LES PÉDALES EN DURÉES DE NOTES (*Pedals to Note Length* de Cubase) : ce que la pédale forte (CC 64,
/// enfoncée à 64 et plus) fait ENTENDRE devient la durée écrite. Une note dont la fin tombe pédale ENFONCÉE -- l'état
/// laissé par le dernier CC 64 posé AVANT sa fin -- dure jusqu'au relâchement suivant, ou jusqu'à la frappe suivante de
/// la même hauteur si elle vient avant (une note ne se chevauche pas elle-même) ; puis les CC 64 sont retirés. La
/// pédale est celle de la PISTE, tous canaux confondus, comme le graphe la retient (`sustainDown_`, D25.1). Une
/// pédale jamais relâchée tient jusqu'à `materialEnd` ; ces notes-là sont comptées à part (`heldToEnd`).
struct PedalConversion {
    size_t lengthened = 0;    ///< notes allongées
    size_t pedalEvents = 0;   ///< CC 64 retirés
    size_t heldToEnd = 0;     ///< parmi les allongées, celles qu'aucun relâchement ne termine
};
PedalConversion pedalsToNoteLength(std::vector<Note>& notes, std::vector<CcPoint>& controlChanges, Tick materialEnd);
/// Le nombre d'APPUIS (passages de relâchée à enfoncée) : ce que dit le libellé du menu.
size_t countPedalPresses(const std::vector<CcPoint>& controlChanges);


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
/// D556.3 — LES NOTES DE MÊME NOM, À TOUTES LES OCTAVES (*Select Equal Pitch – all Octaves* de Cubase) : toutes
/// celles dont la hauteur modulo 12 est celle d'une note déjà sélectionnée ; la sélection s'ÉTEND.
NoteSelection selectNotesWithSamePitchClass(const std::vector<Note>& notes, const NoteSelection& selection);
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
/// D223 (A39) : les `part` × N notes de PLUS BASSE confiance, quel que soit le
/// seuil -- mesuré : « douteuse » couvre de 53 à 91 % des notes des
/// reconstructions réelles, et ne désigne donc plus par où commencer. Au moins
/// une note dès qu'il y en a ; à confiance égale, l'ordre des notes tranche, pour
/// que deux appels rendent la même sélection.
NoteSelection selectLeastConfident(const std::vector<Note>& notes, double part = 0.10);

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

// ---------------------------------------------------------------------------
// D566.1 -- annuler et figer la quantification (*Reset Quantize*, *Freeze Quantize* de Cubase)
// ---------------------------------------------------------------------------

/// Les notes choisies qui portent un déplacement de quantification (`Note::quantizeShiftStart`, `quantizeShiftEnd`).
size_t countQuantizedNotes(const std::vector<Note>& notes, const NoteSelection& selection);
/// Chaque note choisie revient du déplacement que les quantifications lui ont fait -- début ET fin --, puis l'oublie ;
/// ce qu'on lui a fait à la main depuis reste. Un début ne passe pas avant zéro ; une fin qui tomberait sur ou avant le
/// début garde la durée que la note a. Les notes se retrient par début. Rend le nombre de notes rendues.
size_t resetQuantize(std::vector<Note>& notes, const NoteSelection& selection);
/// Le déplacement oublié, les notes immobiles : la position quantifiée devient la position de la note. Rend le nombre
/// de notes qui en portaient un.
size_t freezeQuantize(std::vector<Note>& notes, const NoteSelection& selection);

} // namespace vsm::sequencer
