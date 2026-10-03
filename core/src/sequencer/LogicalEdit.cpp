#include "vsm/sequencer/LogicalEdit.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <string_view>

namespace vsm::sequencer {

namespace {

struct NomDeChamp { NoteField champ; const char* canonique; };
constexpr std::array<NomDeChamp, 7> kChamps = {{
    {NoteField::Pitch, "hauteur"},     {NoteField::Velocity, "velocite"}, {NoteField::Length, "duree"},
    {NoteField::BarPosition, "position"}, {NoteField::Channel, "canal"}, {NoteField::Confidence, "confiance"},
    {NoteField::Muted, "muette"},
}};

/// Les mêmes champs en ANGLAIS, et « muet », acceptés à la lecture — jamais écrits : l'écriture
/// est canonique. L'interface est bilingue, la règle se tape dans l'une ou l'autre langue.
constexpr std::array<NomDeChamp, 8> kAutresNoms = {{
    {NoteField::Pitch, "pitch"},       {NoteField::Velocity, "velocity"}, {NoteField::Length, "length"},
    {NoteField::Length, "duration"},   {NoteField::Channel, "channel"},   {NoteField::Confidence, "confidence"},
    {NoteField::Muted, "muted"},       {NoteField::Muted, "muet"},
}};

struct NomDOperateur { CompareOp op; const char* texte; };
constexpr std::array<NomDOperateur, 8> kOperateurs = {{
    {CompareOp::LessOrEqual, "<="}, {CompareOp::GreaterOrEqual, ">="}, {CompareOp::NotEqual, "!="},
    {CompareOp::Equal, "="},         {CompareOp::Less, "<"},             {CompareOp::Greater, ">"},
    {CompareOp::Between, "entre"},   {CompareOp::Outside, "hors"},
}};

/// Les mots se comparent en minuscules, accents ôtés (é, è, ê et leurs capitales : les seuls des champs).
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

/// L'erreur : son genre, le morceau tapé, et la phrase française tirée du MODÈLE — la même
/// source que la table de traduction de l'interface.
struct Echec {
    LogicalParseError detail;
    std::string phrase;
};

void echouer(Echec& e, LogicalParseErrorKind kind, const std::string& morceau) {
    e.detail = {kind, morceau};
    e.phrase = logicalParseErrorTemplate(kind);
    const size_t ici = e.phrase.find("%1");
    if (ici != std::string::npos) e.phrase.replace(ici, 2, morceau);
}

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

/// Une valeur selon son champ ; faux avec le genre d'erreur et le morceau tapé.
bool lireValeur(NoteField champ, const std::string& texte, uint16_t ppq, double& sortie, Echec& echec) {
    if (lireNombre(texte, sortie)) return true;
    if (champ == NoteField::Pitch) {
        if (lireNomDeNote(texte, sortie)) return true;
        echouer(echec, LogicalParseErrorKind::NotANote, texte);
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
        echouer(echec, LogicalParseErrorKind::NotTicksOrFraction, texte);
        return false;
    }
    if (champ == NoteField::Muted) {
        const std::string mot = normaliser(texte);
        if (mot == "oui" || mot == "yes") { sortie = 1.0; return true; }
        if (mot == "non" || mot == "no") { sortie = 0.0; return true; }
    }
    echouer(echec, LogicalParseErrorKind::NotANumber, texte);
    return false;
}

/// Une condition, dans le texte tel qu'il a été tapé.
bool lireCondition(const std::string& clause, uint16_t ppq, NoteCondition& sortie, Echec& echec) {
    size_t i = 0;
    while (i < clause.size() && estBlanc(clause[i])) ++i;
    const size_t debutChamp = i;
    while (i < clause.size() && estLettre(clause[i])) ++i;
    const std::string champTape = clause.substr(debutChamp, i - debutChamp);
    if (champTape.empty()) {
        echouer(echec, LogicalParseErrorKind::MissingField, clause);
        return false;
    }
    const std::string champNorme = normaliser(champTape);
    bool trouve = false;
    for (const auto& [champ, nom] : kChamps)
        if (champNorme == nom) { sortie.field = champ; trouve = true; }
    for (const auto& [champ, nom] : kAutresNoms)
        if (champNorme == nom) { sortie.field = champ; trouve = true; }
    if (!trouve) {
        echouer(echec, LogicalParseErrorKind::NotAField, champTape);
        return false;
    }
    while (i < clause.size() && estBlanc(clause[i])) ++i;
    // L'opérateur : un signe (le plus long d'abord), ou un mot.
    std::string operateurTape;
    trouve = false;
    for (const auto& [op, texte] : kOperateurs) {
        const std::string signe = texte;
        if (std::isalpha(static_cast<unsigned char>(signe[0]))) continue;
        if (clause.compare(i, signe.size(), signe) == 0) {
            sortie.op = op;
            operateurTape = signe;
            i += signe.size();
            trouve = true;
            break;
        }
    }
    if (!trouve) {
        const size_t debutMot = i;
        while (i < clause.size() && !estBlanc(clause[i])) ++i;
        operateurTape = clause.substr(debutMot, i - debutMot);
        const std::string mot = normaliser(operateurTape);
        if (mot == "entre" || mot == "between") { sortie.op = CompareOp::Between; trouve = true; }
        else if (mot == "hors" || mot == "outside") { sortie.op = CompareOp::Outside; trouve = true; }
        if (!trouve) {
            echouer(echec, LogicalParseErrorKind::NotAnOperator, operateurTape.empty() ? std::string("(rien)") : operateurTape);
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
        if (deux) echouer(echec, LogicalParseErrorKind::NeedsTwoValues, operateurTape);
        else echouer(echec, LogicalParseErrorKind::MissingValue, champTape);
        return false;
    }
    if (valeurs.size() > attendues) {
        echouer(echec, LogicalParseErrorKind::ExtraValue, valeurs[attendues]);
        return false;
    }
    if (!lireValeur(sortie.field, valeurs[0], ppq, sortie.a, echec)) return false;
    sortie.b = 0.0;
    if (deux && !lireValeur(sortie.field, valeurs[1], ppq, sortie.b, echec)) return false;
    return true;
}

/// Coupe au mot « et » ou « and » (isolé, casse indifférente) ; une clause vide est rendue
/// telle quelle, pour être refusée.
std::vector<std::string> clauses(const std::string& texte) {
    std::vector<std::string> morceaux;
    size_t debut = 0, i = 0;
    while (i < texte.size()) {
        const bool avantBlanc = i == 0 || estBlanc(texte[i - 1]);
        size_t longueur = 0;
        if (avantBlanc)
            for (std::string_view conjonction : {std::string_view("et"), std::string_view("and")}) {
                const size_t n = conjonction.size();
                if (i + n > texte.size()) continue;
                bool pareil = true;
                for (size_t k = 0; k < n; ++k)
                    if (std::tolower(static_cast<unsigned char>(texte[i + k])) != conjonction[k]) pareil = false;
                if (pareil && (i + n == texte.size() || estBlanc(texte[i + n]))) longueur = n;
            }
        if (longueur > 0) {
            morceaux.push_back(texte.substr(debut, i - debut));
            i += longueur;
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

namespace {

const char* nomLisible(NoteField champ, bool anglais) {
    switch (champ) {
        case NoteField::Pitch: return anglais ? "pitch" : "hauteur";
        case NoteField::Velocity: return anglais ? "velocity" : "vélocité";
        case NoteField::Length: return anglais ? "length" : "durée";
        case NoteField::BarPosition: return "position";
        case NoteField::Channel: return anglais ? "channel" : "canal";
        case NoteField::Confidence: return anglais ? "confidence" : "confiance";
        case NoteField::Muted: return anglais ? "muted" : "muette";
    }
    return "";
}

/// Une durée ou une position en fraction de RONDE quand elle tombe juste — la plus petite
/// puissance de deux, jusqu'à 128 —, en ticks sinon. 1 920 à 480 ppq s'écrit « 1/1 » : « 1 »
/// se relirait comme UN tick.
std::string ecrireDuree(double ticks, uint16_t ppq) {
    if (ticks == 0.0) return "0";
    if (ticks > 0.0 && ticks < 1e12 && ticks == std::floor(ticks) && ppq > 0) {
        const long long t = static_cast<long long>(ticks);
        const long long ronde = 4LL * ppq;
        for (long long den = 1; den <= 128; den *= 2)
            if ((t * den) % ronde == 0) return std::to_string(t * den / ronde) + "/" + std::to_string(den);
    }
    return ecrireNombre(ticks);
}

std::string ecrireValeurLisible(NoteField champ, double v, uint16_t ppq, bool anglais) {
    std::string texte;
    switch (champ) {
        case NoteField::Pitch:
            texte = v >= 0.0 && v <= 127.0 && v == std::floor(v) ? noteNumberToName(static_cast<uint8_t>(v))
                                                                 : ecrireNombre(v);
            break;
        case NoteField::Length:
        case NoteField::BarPosition: texte = ecrireDuree(v, ppq); break;
        case NoteField::Muted:
            if (v == 1.0) return anglais ? "yes" : "oui";
            if (v == 0.0) return anglais ? "no" : "non";
            texte = ecrireNombre(v);
            break;
        default: texte = ecrireNombre(v); break;
    }
    if (!anglais) std::replace(texte.begin(), texte.end(), '.', ',');   // la virgule décimale
    return texte;
}

} // namespace

std::string logicalRuleReadableText(const LogicalRule& rule, uint16_t ppq, RuleLanguage language) {
    const bool anglais = language == RuleLanguage::English;
    std::string texte;
    for (const auto& c : rule.conditions) {
        if (!texte.empty()) texte += anglais ? " and " : " et ";
        texte += nomLisible(c.field, anglais);
        if (c.op == CompareOp::Between) texte += anglais ? " between " : " entre ";
        else if (c.op == CompareOp::Outside) texte += anglais ? " outside " : " hors ";
        else
            for (const auto& [op, signe] : kOperateurs)
                if (op == c.op) texte += std::string(" ") + signe + " ";
        texte += ecrireValeurLisible(c.field, c.a, ppq, anglais);
        if (c.op == CompareOp::Between || c.op == CompareOp::Outside)
            texte += " " + ecrireValeurLisible(c.field, c.b, ppq, anglais);
    }
    return texte;
}

const char* logicalParseErrorTemplate(LogicalParseErrorKind kind) {
    switch (kind) {
        case LogicalParseErrorKind::NotANumber: return "« %1 » n'est pas un nombre";
        case LogicalParseErrorKind::NotANote: return "« %1 » n'est ni un nombre ni une note (C4 = 60, F#3, Bb2)";
        case LogicalParseErrorKind::NotTicksOrFraction:
            return "« %1 » n'est ni un nombre de ticks ni une fraction de ronde (1/16)";
        case LogicalParseErrorKind::NotAField:
            return "« %1 » n'est pas un champ (hauteur, vélocité, durée, position, canal, confiance, muette)";
        case LogicalParseErrorKind::MissingField:
            return "« %1 » : il manque le champ (hauteur, vélocité, durée, position, canal, confiance, muette)";
        case LogicalParseErrorKind::NotAnOperator: return "« %1 » n'est pas un opérateur (=, !=, <, <=, >, >=, entre, hors)";
        case LogicalParseErrorKind::MissingValue: return "« %1 » : il manque la valeur";
        case LogicalParseErrorKind::NeedsTwoValues: return "« %1 » attend deux valeurs";
        case LogicalParseErrorKind::ExtraValue: return "« %1 » est en trop (une condition par « et »)";
        case LogicalParseErrorKind::EmptyCondition: return "une condition est vide (un « et » en trop ?)";
    }
    return "";
}

bool parseLogicalRule(const std::string& texte, uint16_t ppq, LogicalRule& sortie, std::string& erreur,
                      LogicalParseError* detail) {
    LogicalRule lue;
    Echec echec;
    const auto rater = [&]() {
        erreur = echec.phrase;
        if (detail != nullptr) *detail = echec.detail;
        return false;
    };
    if (!blanc(texte)) {
        for (const auto& clause : clauses(texte)) {
            if (blanc(clause)) {
                echouer(echec, LogicalParseErrorKind::EmptyCondition, "");
                return rater();
            }
            NoteCondition c;
            if (!lireCondition(clause, ppq, c, echec)) return rater();
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

const std::vector<LogicalPreset>& builtInLogicalPresets() {
    // CE QUE LA CHAÎNE LAISSE LE PLUS SOUVENT À NETTOYER, d'abord : les fantômes brefs et faibles,
    // les notes douteuses. Les règles sont écrites dans la forme lisible française (D535.1 bis).
    static const std::vector<LogicalPreset> prereglages = {
        {"Fantômes (brèves et faibles)", "vélocité < 30 et durée < 1/32", LogicalAction::Delete, 0},
        {"Notes douteuses", "confiance < 0,5", LogicalAction::Select, 0},
        {"Premier temps de chaque mesure", "position = 0", LogicalAction::Select, 0},
        {"Batterie (canal 10)", "canal = 10", LogicalAction::Select, 0},
        {"Notes muettes", "muette = oui", LogicalAction::Delete, 0},
        {"Attaques trop fortes", "vélocité >= 120", LogicalAction::SetVelocity, 100},
        {"Notes très longues (plus de deux mesures)", "durée > 2/1", LogicalAction::Select, 0},
    };
    return prereglages;
}

} // namespace vsm::sequencer
