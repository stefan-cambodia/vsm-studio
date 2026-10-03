#pragma once
#include "vsm/sequencer/ClipEdit.h"
#include "vsm/sequencer/NoteEdit.h"
#include "vsm/sequencer/TimeSignatureMap.h"
#include <cstddef>
#include <optional>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// D532.3 de docs/ROADMAP-daw.md — LA LIGNE D'ACCORDS.
//
// Une ligne par projet, comme les repères et le tempo : un accord ne joue rien, il
// GUIDE l'écriture, et il vaut jusqu'au suivant. Le TYPE est celui que le piano roll
// connaît déjà (`ChordType`, le bouton « Accord ») — pas une seconde liste qui finirait
// par ne plus dire la même chose.

namespace vsm::sequencer {

struct ChordEvent {
    Tick tick = 0;
    uint8_t root = 0;                      ///< 0 = Do (C), 1 = Do# … 11 = Si (B)
    ChordType type = ChordType::Major;
    int bass = -1;                         ///< classe de hauteur de la basse, -1 : la fondamentale

    bool operator==(const ChordEvent& autre) const {
        return tick == autre.tick && root == autre.root && type == autre.type && bass == autre.bass;
    }
};

/// LE SYMBOLE, qui est aussi le FORMAT : « Am7/G », « F#m7b5 », « C5 ». Les dièses à
/// l'écriture, une seule paire écrire/lire — un symbole se lit dans le fichier et ne
/// dépend d'aucune langue.
std::string chordSymbol(const ChordEvent& accord);

/// Lit un symbole : la fondamentale (A à G, puis `#` ou `b`), le suffixe EXACT d'un des
/// treize types, puis « /basse » facultatif. Les bémols sont acceptés (« Bb7 » se lit
/// « A#7 »). Faux, sans toucher `sortie`, pour tout ce qui n'est pas exactement cela :
/// un symbole inconnu est refusé, jamais deviné. Le tick de `sortie` n'est jamais touché.
bool parseChordSymbol(const std::string& symbole, ChordEvent& sortie);

/// L'accord en vigueur à `tick` — le dernier qui commence à `tick` ou avant —, ou
/// nullptr avant le premier. `accords` doit être trié (ce que `setChordAt` garantit).
const ChordEvent* chordAt(const std::vector<ChordEvent>& accords, Tick tick);

/// Pose un accord : il REMPLACE celui qui commencerait au même tick, et la liste reste
/// triée.
void setChordAt(std::vector<ChordEvent>& accords, const ChordEvent& accord);

/// Retire l'accord qui commence exactement à `tick`. Faux s'il n'y en a pas.
bool removeChordAt(std::vector<ChordEvent>& accords, Tick tick);

/// Masque des 12 classes de hauteur de l'accord (bit i = la classe i y est), basse comprise.
uint16_t chordMask(const ChordEvent& accord);

/// Ce que « caler sur les accords » a fait — et ce qu'il n'a pas pu faire, compté.
struct ChordSnapReport {
    size_t moved = 0;          ///< notes déplacées vers une note de l'accord
    size_t alreadyInChord = 0; ///< notes déjà dans l'accord, laissées
    size_t withoutChord = 0;   ///< notes qui commencent avant le premier accord : laissées, et DITES
    size_t ambiguous = 0;      ///< D532.3 bis : entendues sous plusieurs harmonies — laissées, DITES
    size_t unheard = 0;        ///< D532.3 bis : entendues nulle part (hors clip, clip muet) — laissées, DITES
};

/// « CALER LES NOTES SUR LES ACCORDS » : chaque note CHOISIE va à la note de l'accord en
/// vigueur LÀ OÙ ELLE SONNE la plus proche, l'égalité tranchée vers le grave — la règle de
/// `snapNoteToScale`. Une sélection vide ne fait rien.
///
/// D532.3 bis : UNE NOTE EST DU MATÉRIAU. Ses débuts entendus sont ceux que rendent les
/// `passages` de sa piste (`clipPassages`, le calcul de la lecture et de l'export) ; une
/// piste sans clip a le passage identité. Entendue sous deux harmonies (deux masques de
/// classes de hauteur différents, ou un accord et « avant le premier »), elle reste et se
/// compte ambiguë ; entendue nulle part, elle reste et se compte muette.
ChordSnapReport snapNotesToChords(std::vector<Note>& notes, const NoteSelection& selection,
                                  const std::vector<ChordEvent>& accords,
                                  const std::vector<ClipPassage>& passages);

/// D543.1 : RECONNAÎTRE UN ACCORD d'après le POIDS de chaque classe de hauteur (la durée qu'elle
/// sonne) et la classe de la BASSE (-1 si inconnue). Le meilleur couple (fondamentale, type) parmi
/// 12 × 12 types (la quinte à vide exclue) couvre le plus de poids, moins le poids hors de l'accord,
/// moins une pénalité par note de l'accord absente ; à égalité, le premier des types, qui vont du
/// plus simple au plus riche. Une classe « sonne » à 5 % du poids au moins. Rien quand
/// moins de trois classes sonnent, ou quand le meilleur couvre moins des deux tiers du poids.
std::optional<ChordEvent> detectChord(const std::array<double, 12>& poids, int basse);

struct ChordDetectionReport {
    std::vector<ChordEvent> chords;   ///< un par mesure reconnue ; deux pareils de suite n'en font qu'un
    size_t bars = 0;                  ///< mesures examinées
    size_t barsWithoutChord = 0;      ///< mesures où rien de net ne sonnait
};

/// La ligne d'accords d'après ce que la piste fait ENTENDRE (par ses fenêtres, `passages`) dans
/// [from, to) : un accord par mesure, les notes muettes écartées.
ChordDetectionReport chordsFromNotes(const std::vector<Note>& notes, const std::vector<ClipPassage>& passages,
                                     const TimeSignatureMap& signatures, uint16_t ppq, Tick from, Tick to);

} // namespace vsm::sequencer
