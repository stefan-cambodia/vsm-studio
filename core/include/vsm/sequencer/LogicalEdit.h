#pragma once
#include "vsm/sequencer/NoteEdit.h"
#include "vsm/sequencer/TimeSignatureMap.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// D535.1 de docs/ROADMAP-daw.md — L'ÉDITEUR LOGIQUE.
//
// Choisir ou transformer les notes qui répondent à des CONDITIONS (« vélocité < 30 et durée <
// 1/32 ») : le nettoyage d'une reconstruction — fantômes brefs et faibles, notes douteuses —
// que les cinq sélections fixes du piano roll ne couvrent qu'en partie. Des conditions liées
// par « et », sept champs, huit opérateurs, cinq actions ; la règle s'écrit et se relit en
// texte, une seule paire écrire/lire.

namespace vsm::sequencer {

enum class NoteField { Pitch, Velocity, Length, BarPosition, Channel, Confidence, Muted };
enum class CompareOp { Equal, NotEqual, Less, LessOrEqual, Greater, GreaterOrEqual, Between, Outside };

/// Une condition : `champ op a` — ou `champ entre a b` / `champ hors a b` (bornes COMPRISES dans
/// « entre », exclues de « hors »). `b` ne sert qu'à ces deux-là, et vaut 0 sinon.
struct NoteCondition {
    NoteField field = NoteField::Pitch;
    CompareOp op = CompareOp::Equal;
    double a = 0.0;
    double b = 0.0;
    bool operator==(const NoteCondition& o) const { return field == o.field && op == o.op && a == o.a && b == o.b; }
};

/// Des conditions liées par « et ». Sans condition, la règle répond pour TOUTES les notes.
struct LogicalRule {
    std::vector<NoteCondition> conditions;
    bool operator==(const LogicalRule& o) const { return conditions == o.conditions; }
};

/// La valeur d'un champ pour une note. La POSITION est celle que le piano roll montre : le tick
/// de la note dans le matériau, rapporté au début de SA mesure (la carte des signatures compte
/// un 3/4 en trois temps). Le CANAL va de 1 à 16, comme il s'affiche ; MUETTE vaut 0 ou 1.
double noteFieldValue(const Note& note, NoteField field, const TimeSignatureMap& signatures, uint16_t ppq);

bool noteMatches(const Note& note, const LogicalRule& rule, const TimeSignatureMap& signatures, uint16_t ppq);

/// Les notes qui répondent — parmi `within` s'il est donné (le champ d'application est
/// EXPLICITE, jamais déduit de l'existence d'une sélection).
NoteSelection selectNotesWhere(const std::vector<Note>& notes, const LogicalRule& rule,
                               const TimeSignatureMap& signatures, uint16_t ppq,
                               const NoteSelection* within = nullptr);

/// L'ÉCRITURE CANONIQUE : champs sans accents, nombres en ticks et en locale C
/// (« velocite < 30 et duree < 60 ») — ce que le banc et les préréglages comparent.
std::string logicalRuleText(const LogicalRule& rule);

/// D535.1 bis : L'ÉCRITURE LISIBLE — ce qu'on RELIT dans la fenêtre, et ce que les préférences
/// retiennent. Les mots accentués, la hauteur par son nom (60 → C4, comme le clavier du piano
/// roll), une durée ou une position en fraction de RONDE quand elle tombe juste (la plus petite
/// puissance de deux jusqu'à 128 : 60 ticks à 480 ppq → 1/32), en ticks sinon ; en français
/// (virgule décimale, « oui », « entre », « et ») ou en anglais (point, « yes », « between »,
/// « and »). Elle se relit en la même règle. Et, en fraction, elle garde son SENS d'une
/// résolution à l'autre, ce que la canonique ne fait pas : 60 ticks font 1/32 à 480 ppq et
/// 1/64 à 960.
enum class RuleLanguage { French, English };
std::string logicalRuleReadableText(const LogicalRule& rule, uint16_t ppq, RuleLanguage language);

/// CE QUI N'A PAS PU ÊTRE LU, et le MORCEAU tel qu'il a été tapé : l'interface traduit le
/// modèle de phrase (`logicalParseErrorTemplate`, en français, « %1 » pour le morceau) — une
/// phrase assemblée ici ne se traduirait pas.
enum class LogicalParseErrorKind {
    NotANumber, NotANote, NotTicksOrFraction, NotAField, MissingField, NotAnOperator,
    MissingValue, NeedsTwoValues, ExtraValue, EmptyCondition,
};
struct LogicalParseError {
    LogicalParseErrorKind kind = LogicalParseErrorKind::NotANumber;
    std::string piece;
};
/// Le modèle de phrase français d'une erreur — une SEULE source pour `erreur` et pour la table
/// de traduction de l'interface.
const char* logicalParseErrorTemplate(LogicalParseErrorKind kind);

/// LA LECTURE : accents facultatifs (« vélocité » = « velocite »), casse indifférente pour les
/// mots, et les mots ANGLAIS acceptés aussi (pitch, velocity, length, position, channel,
/// confidence, muted ; between, outside ; and ; yes, no) — l'interface est bilingue ; une hauteur
/// en nombre ou en nom (C4 = 60, F#3, Bb2) ; une durée ou une position en ticks ou en fraction
/// de ronde (1/16) ; une confiance avec un point ou une virgule ; « muette » prend 0/1 ou non/oui.
/// Faux, sans toucher `sortie`, pour tout morceau illisible — et `erreur` le NOMME tel qu'il a
/// été tapé, jamais deviné.
bool parseLogicalRule(const std::string& texte, uint16_t ppq, LogicalRule& sortie, std::string& erreur,
                      LogicalParseError* detail = nullptr);

enum class LogicalAction { Select, Delete, Mute, Transpose, SetVelocity };

struct LogicalResult {
    size_t changed = 0;        ///< notes modifiées (ou choisies, pour Select)
    size_t refused = 0;        ///< notes qui répondaient et que l'action a REFUSÉES — dites
    NoteSelection selection;   ///< Select : la nouvelle sélection
};

/// Applique l'action aux notes `matched` (ce que `selectNotesWhere` a rendu), et à elles seules.
/// TRANSPOSER suit la règle de D536 — une seule note qui sortirait de 0..127 et RIEN ne bouge,
/// `refused` dit combien ; FIXER LA VÉLOCITÉ refuse une valeur hors de 1..127 plutôt que de la
/// borner. `value` : les demi-tons, ou la vélocité.
LogicalResult applyLogicalAction(std::vector<Note>& notes, const NoteSelection& matched, LogicalAction action,
                                 int value);

} // namespace vsm::sequencer
