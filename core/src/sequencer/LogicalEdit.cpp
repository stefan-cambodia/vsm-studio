#include "vsm/sequencer/LogicalEdit.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>

namespace vsm::sequencer {

namespace {

struct NomDeChamp { NoteField champ; const char* canonique; };
constexpr std::array<NomDeChamp, 7> kChamps = {{
    {NoteField::Pitch, "hauteur"},     {NoteField::Velocity, "velocite"}, {NoteField::Length, "duree"},
    {NoteField::BarPosition, "position"}, {NoteField::Channel, "canal"}, {NoteField::Confidence, "confiance"},
    {NoteField::Muted, "muette"},
}};

struct NomDOperateur { CompareOp op; const char* texte; };
constexpr std::array<NomDOperateur, 8> kOperateurs = {{
    {CompareOp::LessOrEqual, "<="}, {CompareOp::GreaterOrEqual, ">="}, {CompareOp::NotEqual, "!="},
    {CompareOp::Equal, "="},         {CompareOp::Less, "<"},             {CompareOp::Greater, ">"},
    {CompareOp::Between, "entre"},   {CompareOp::Outside, "hors"},
}};

/// Les mots se comparent en minuscules, accents ôtés (é, è, ê : les seuls des champs).
std::string normaliser(const std::string& mot) {
    std::string sortie;
    for (size_t i = 0; i < mot.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(mot[i]);
        if (c == 0xC3 && i + 1 < mot.size()) {
            const unsigned char d = static_cast<unsigned char>(mot[i + 1]);
            if (d == 0xA9 || d == 0xA8 || d == 0xAA || d == 0x89 || d == 0x88 || d == 0x8A) { sortie += 'e'; ++i; continue; }
        }
        sortie += static_cast<char>(std::tolower(c));
    }
    return sortie;
}

bool estBlanc(char c) { return c == ' ' || c == '\t'; }
bool estLettre(char c) { return std::isalpha(static_cast<unsigned char>(c)) || static_cast<unsigned char>(c) >= 0x80; }

std::string guillemets(const std::string& morceau) { return "« " + morceau + " »"; }

/// Un nombre en locale C, le point OU la virgule pour séparateur décimal.
bool lireNombre(const std::string& texte, double& sortie) {
    std::string t = texte;
    std::replace(t.begin(), t.end(), ',', '.');
    if (t.empty()) return false;
    const auto [fin, ec] = std::from_chars(t.data(), t.data() + t.size(), sortie);
    return ec == std::errc() && fin == t.data() + t.size();
}

/// Une note par son nom : C4 = 60, F#3 = 54, Bb2 = 46, C-1 = 0.
bool lireNomDeNote(const std::string& texte, double& sortie) {
    if (texte.size() < 2) return false;
    static const int degres[7] = {9, 11, 0, 2, 4, 5, 7};   // A B C D E F G
    const char lettre = static_cast<char>(std::toupper(static_cast<unsigned char>(texte[0])));
    if (lettre < 'A' || lettre > 'G') return false;
    int classe = degres[lettre - 'A'];
    size_t i = 1;
    if (texte[i] == '#') { ++classe; ++i; }
    else if (texte[i] == 'b' && i + 1 < texte.size() && (std::isdigit(static_cast<unsigned char>(texte[i + 1])) || texte[i + 1] == '-')) { --classe; ++i; }
    double octave = 0.0;
    if (i >= texte.size() || !lireNombre(texte.substr(i), octave) || octave != std::floor(octave)) return false;
    const double numero = (octave + 1.0) * 12.0 + classe;
    if (numero < 0.0 || numero > 127.0) return false;
    sortie = numero;
    return true;
}

/// Une valeur selon son champ ; faux avec un message qui nomme le morceau tapé.
bool lireValeur(NoteField champ, const std::string& texte, uint16_t ppq, double& sortie, std::string& erreur) {
    if (lireNombre(texte, sortie)) return true;
    if (champ == NoteField::Pitch) {
        if (lireNomDeNote(texte, sortie)) return true;
        erreur = guillemets(texte) + " n'est ni un nombre ni une note (C4 = 60, F#3, Bb2)";
        return false;
    }
    if (champ == NoteField::Length || champ == NoteField::BarPosition) {
        const size_t barre = texte.find('/');
        double num = 0.0, den = 0.0;
        if (barre != std::string::npos && lireNombre(texte.substr(0, barre), num)
            && lireNombre(texte.substr(barre + 1), den) && den > 0.0) {
            sortie = num * 4.0 * ppq / den;   // une fraction de RONDE
            return true;
        }
        erreur = guillemets(texte) + " n'est ni un nombre de ticks ni une fraction de ronde (1/16)";
        return false;
    }
    if (champ == NoteField::Muted) {
        const std::string mot = normaliser(texte);
        if (mot == "oui") { sortie = 1.0; return true; }
        if (mot == "non") { sortie = 0.0; return true; }
    }
    erreur = guillemets(texte) + " n'est pas un nombre";
    return false;
}

/// Une condition, dans le texte tel qu'il a été tapé.
bool lireCondition(const std::string& clause, uint16_t ppq, NoteCondition& sortie, std::string& erreur) {
    size_t i = 0;
    while (i < clause.size() && estBlanc(clause[i])) ++i;
    const size_t debutChamp = i;
    while (i < clause.size() && estLettre(clause[i])) ++i;
    const std::string champTape = clause.substr(debutChamp, i - debutChamp);
    if (champTape.empty()) {
        erreur = guillemets(clause) + " : il manque le champ (hauteur, vélocité, durée, position, canal, confiance, muette)";
        return false;
    }
    const std::string champNorme = normaliser(champTape);
    bool trouve = false;
    for (const auto& [champ, nom] : kChamps)
        if (champNorme == nom || (champ == NoteField::Muted && champNorme == "muet")) { sortie.field = champ; trouve = true; }
    if (!trouve) {
        erreur = guillemets(champTape) + " n'est pas un champ (hauteur, vélocité, durée, position, canal, confiance, muette)";
        return false;
    }
    while (i < clause.size() && estBlanc(clause[i])) ++i;
    // L'opérateur : un signe (le plus long d'abord), ou un mot.
    trouve = false;
    for (const auto& [op, texte] : kOperateurs) {
        const std::string signe = texte;
        if (std::isalpha(static_cast<unsigned char>(signe[0]))) continue;
        if (clause.compare(i, signe.size(), signe) == 0) { sortie.op = op; i += signe.size(); trouve = true; break; }
    }
    if (!trouve) {
        const size_t debutMot = i;
        while (i < clause.size() && !estBlanc(clause[i])) ++i;
        const std::string motTape = clause.substr(debutMot, i - debutMot);
        const std::string mot = normaliser(motTape);
        if (mot == "entre") { sortie.op = CompareOp::Between; trouve = true; }
        else if (mot == "hors") { sortie.op = CompareOp::Outside; trouve = true; }
        if (!trouve) {
            erreur = guillemets(motTape.empty() ? std::string("(rien)") : motTape)
                     + " n'est pas un opérateur (=, !=, <, <=, >, >=, entre, hors)";
            return false;
        }
    }
    std::vector<std::string> valeurs;
    while (i < clause.size()) {
        while (i < clause.size() && estBlanc(clause[i])) ++i;
        const size_t debut = i;
        while (i < clause.size() && !estBlanc(clause[i])) ++i;
        if (i > debut) valeurs.push_back(clause.substr(debut, i - debut));
    }
    const bool deux = sortie.op == CompareOp::Between || sortie.op == CompareOp::Outside;
    const size_t attendues = deux ? 2 : 1;
    if (valeurs.size() < attendues) {
        erreur = deux ? guillemets(sortie.op == CompareOp::Between ? "entre" : "hors") + " attend deux valeurs"
                      : guillemets(champTape) + " : il manque la valeur";
        return false;
    }
    if (valeurs.size() > attendues) {
        erreur = guillemets(valeurs[attendues]) + " est en trop (une condition par « et »)";
        return false;
    }
    if (!lireValeur(sortie.field, valeurs[0], ppq, sortie.a, erreur)) return false;
    sortie.b = 0.0;
    if (deux && !lireValeur(sortie.field, valeurs[1], ppq, sortie.b, erreur)) return false;
    return true;
}

/// Coupe au mot « et » (isolé, casse indifférente) ; une clause vide est rendue telle quelle.
std::vector<std::string> clauses(const std::string& texte) {
    std::vector<std::string> morceaux;
    size_t debut = 0, i = 0;
    while (i < texte.size()) {
        const bool avantBlanc = i == 0 || estBlanc(texte[i - 1]);
        const bool mot = i + 2 <= texte.size() && std::tolower(static_cast<unsigned char>(texte[i])) == 'e'
                         && std::tolower(static_cast<unsigned char>(texte[i + 1])) == 't';
        const bool apresBlanc = i + 2 == texte.size() || (i + 2 < texte.size() && estBlanc(texte[i + 2]));
        if (avantBlanc && mot && apresBlanc) {
            morceaux.push_back(texte.substr(debut, i - debut));
            i += 2;
            debut = i;
            continue;
        }
        ++i;
    }
    morceaux.push_back(texte.substr(debut));
    return morceaux;
}

bool blanc(const std::string& t) {
    return std::all_of(t.begin(), t.end(), [](char c) { return estBlanc(c); });
}

std::string ecrireNombre(double v) {
    char tampon[64];
    const auto [fin, ec] = std::to_chars(tampon, tampon + sizeof(tampon), v);
    return ec == std::errc() ? std::string(tampon, fin) : std::string("0");
}

bool compare(double v, const NoteCondition& c) {
    switch (c.op) {
        case CompareOp::Equal: return v == c.a;
        case CompareOp::NotEqual: return v != c.a;
        case CompareOp::Less: return v < c.a;
        case CompareOp::LessOrEqual: return v <= c.a;
        case CompareOp::Greater: return v > c.a;
        case CompareOp::GreaterOrEqual: return v >= c.a;
        case CompareOp::Between: return v >= c.a && v <= c.b;
        case CompareOp::Outside: return v < c.a || v > c.b;
    }
    return false;
}

} // namespace

double noteFieldValue(const Note& note, NoteField field, const TimeSignatureMap& signatures, uint16_t ppq) {
    switch (field) {
        case NoteField::Pitch: return note.number;
        case NoteField::Velocity: return note.velocity;
        case NoteField::Length: return static_cast<double>(note.endTick - note.startTick);
        case NoteField::BarPosition: {
            const BarBeat ici = signatures.barBeatAt(note.startTick, ppq);
            return static_cast<double>(note.startTick - signatures.tickAtBarBeat(ici.bar, 0, ppq));
        }
        case NoteField::Channel: return note.channel + 1.0;
        case NoteField::Confidence: return note.confidence;
        case NoteField::Muted: return note.muted ? 1.0 : 0.0;
    }
    return 0.0;
}

bool noteMatches(const Note& note, const LogicalRule& rule, const TimeSignatureMap& signatures, uint16_t ppq) {
    for (const auto& c : rule.conditions)
        if (!compare(noteFieldValue(note, c.field, signatures, ppq), c)) return false;
    return true;
}

NoteSelection selectNotesWhere(const std::vector<Note>& notes, const LogicalRule& rule,
                               const TimeSignatureMap& signatures, uint16_t ppq, const NoteSelection* within) {
    NoteSelection repondent;
    for (const auto& note : notes) {
        if (within != nullptr && within->count(note.id) == 0) continue;
        if (noteMatches(note, rule, signatures, ppq)) repondent.insert(note.id);
    }
    return repondent;
}

std::string logicalRuleText(const LogicalRule& rule) {
    std::string texte;
    for (const auto& c : rule.conditions) {
        if (!texte.empty()) texte += " et ";
        for (const auto& [champ, nom] : kChamps)
            if (champ == c.field) texte += nom;
        for (const auto& [op, signe] : kOperateurs)
            if (op == c.op) texte += std::string(" ") + signe + " ";
        texte += ecrireNombre(c.a);
        if (c.op == CompareOp::Between || c.op == CompareOp::Outside) texte += " " + ecrireNombre(c.b);
    }
    return texte;
}

bool parseLogicalRule(const std::string& texte, uint16_t ppq, LogicalRule& sortie, std::string& erreur) {
    LogicalRule lue;
    if (!blanc(texte)) {
        for (const auto& clause : clauses(texte)) {
            if (blanc(clause)) {
                erreur = "une condition est vide (un « et » en trop ?)";
                return false;
            }
            NoteCondition c;
            if (!lireCondition(clause, ppq, c, erreur)) return false;
            lue.conditions.push_back(c);
        }
    }
    sortie = lue;
    return true;
}

LogicalResult applyLogicalAction(std::vector<Note>& notes, const NoteSelection& matched, LogicalAction action,
                                 int value) {
    LogicalResult r;
    if (matched.empty()) return r;
    switch (action) {
        case LogicalAction::Select:
            r.selection = matched;
            r.changed = matched.size();
            break;
        case LogicalAction::Delete: {
            const size_t avant = notes.size();
            notes.erase(std::remove_if(notes.begin(), notes.end(),
                                       [&](const Note& n) { return matched.count(n.id) > 0; }),
                        notes.end());
            r.changed = avant - notes.size();
            break;
        }
        case LogicalAction::Mute:
            for (const auto& n : notes)
                if (matched.count(n.id) > 0 && !n.muted) ++r.changed;
            setNotesMuted(notes, matched, true);
            break;
        case LogicalAction::Transpose:
            r.refused = transposeNotes(notes, matched, value);   // D536 : tout ou rien
            if (r.refused == 0)
                for (const auto& n : notes) if (matched.count(n.id) > 0) ++r.changed;
            break;
        case LogicalAction::SetVelocity:
            if (value < 1 || value > 127) {
                for (const auto& n : notes) if (matched.count(n.id) > 0) ++r.refused;
                break;
            }
            for (const auto& n : notes)
                if (matched.count(n.id) > 0 && n.velocity != value) ++r.changed;
            setVelocity(notes, matched, static_cast<uint8_t>(value));
            break;
    }
    return r;
}

} // namespace vsm::sequencer
