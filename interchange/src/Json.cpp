#include "vsm/interchange/Json.h"
#include "vsm/interchange/Utf8.h"
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>

namespace vsm::interchange {

namespace {
const JsonValue& nullValue() {
    static const JsonValue instance;
    return instance;
}
} // namespace

JsonValue JsonValue::makeBoolean(bool value) {
    JsonValue v;
    v.type_ = Type::Boolean;
    v.boolean_ = value;
    return v;
}

JsonValue JsonValue::makeNumber(double value) {
    JsonValue v;
    v.type_ = Type::Number;
    v.number_ = value;
    return v;
}

JsonValue JsonValue::makeFloat(float value) {
    if (!std::isfinite(value)) return JsonValue();
    // `to_chars`/`from_chars` plutôt que `snprintf`/`strtod` : la même règle
    // qu'à `writeNumber` ci-dessous, qui dit pourquoi. Ici le couple était
    // COHÉRENT sous n'importe quelle locale -- il écrit et relit avec la même
    // -- et ne produisait donc pas de valeur fausse ; il est repris quand même
    // pour qu'il n'y ait dans ce fichier qu'UNE façon de passer d'un nombre à
    // son texte, et qu'aucune ne consulte la locale du processus.
    char buffer[32];
    char* end = buffer;
    for (int precision = 1; precision <= 9; ++precision) {
        const auto written = std::to_chars(buffer, buffer + sizeof(buffer),
                                           static_cast<double>(value),
                                           std::chars_format::general, precision);
        if (written.ec != std::errc()) continue;
        end = written.ptr;
        float roundTrip = 0.0f;
        std::from_chars(buffer, end, roundTrip);
        if (!(roundTrip < value) && !(value < roundTrip)) break; // identité exacte en float
    }
    double exact = 0.0;
    std::from_chars(buffer, end, exact);
    return makeNumber(exact);
}

JsonValue JsonValue::makeString(std::string value) {
    JsonValue v;
    v.type_ = Type::String;
    v.string_ = std::move(value);
    return v;
}

JsonValue JsonValue::makeArray() {
    JsonValue v;
    v.type_ = Type::Array;
    return v;
}

JsonValue JsonValue::makeObject() {
    JsonValue v;
    v.type_ = Type::Object;
    return v;
}

bool JsonValue::asBoolean(bool fallback) const { return type_ == Type::Boolean ? boolean_ : fallback; }
double JsonValue::asNumber(double fallback) const { return type_ == Type::Number ? number_ : fallback; }
std::string JsonValue::asString(const std::string& fallback) const {
    return type_ == Type::String ? string_ : fallback;
}

bool JsonValue::has(const std::string& key) const { return members_.count(key) > 0; }

const JsonValue& JsonValue::operator[](const std::string& key) const {
    auto it = members_.find(key);
    return it == members_.end() ? nullValue() : it->second;
}

void JsonValue::set(const std::string& key, JsonValue value) {
    type_ = Type::Object;
    members_[key] = std::move(value);
}

const JsonValue& JsonValue::at(size_t index) const {
    return index < elements_.size() ? elements_[index] : nullValue();
}

void JsonValue::append(JsonValue value) {
    type_ = Type::Array;
    elements_.push_back(std::move(value));
}

// ---------------------------------------------------------------------------
// Écriture
// ---------------------------------------------------------------------------

namespace {

void writeEscapedString(std::string& out, const std::string& text) {
    out += '"';
    for (char rawChar : text) {
        const unsigned char c = static_cast<unsigned char>(rawChar);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                } else {
                    out += static_cast<char>(c); // l'UTF-8 passe tel quel
                }
        }
    }
    out += '"';
}

/// Écrit un nombre en préservant sa valeur à la relecture. `%.17g` garantit
/// l'aller-retour exact d'un double, mais produit "0.10000000000000001" ;
/// on essaie donc les précisions courtes d'abord et on garde la première qui
/// se relit à l'identique -- fichiers lisibles ET valeurs intactes.
///
/// `std::to_chars` ET PAS `snprintf`, ET C'EST LA CORRECTION D'UN FICHIER
/// INVALIDE ÉCRIT EN VRAI. `%g` place le séparateur décimal de la LOCALE du
/// processus. Le programme n'en installe aucune -- mais JUCE le fait pour lui :
/// `juce_SystemStats_linux.cpp` appelle `setlocale(LC_ALL, "")` puis
/// « restaure » ce que cet appel vient de RENDRE, c'est-à-dire la nouvelle
/// locale, si bien que le processus reste dans celle de l'environnement pour
/// de bon. Sur une machine réglée en français, l'application écrivait donc
/// `"EQ Mid Q": 0,8` -- pas du JSON, et son propre lecteur le refuse. Les
/// trois sauvegardes automatiques trouvées sur le disque de développement
/// étaient toutes dans cet état : la récupération après plantage (D10.4)
/// promettait de rendre un projet qu'elle n'était pas en mesure de relire.
///
/// `std::to_chars` est défini en locale C, toujours, quelle que soit celle du
/// processus : le problème ne se répare pas, il cesse d'exister. La forme
/// produite est celle de `printf("%.*g")` en locale C, donc les fichiers déjà
/// écrits par une chaîne saine ne changent pas d'un octet.
void writeNumber(std::string& out, double value) {
    if (!std::isfinite(value)) { out += "null"; return; } // NaN/inf n'existent pas en JSON
    char buffer[40];
    char* end = buffer;
    for (int precision : {6, 9, 12, 17}) {
        const auto written = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                           std::chars_format::general, precision);
        if (written.ec != std::errc()) continue; // ne peut pas arriver à 40 octets
        end = written.ptr;
        // Comparaison d'identité EXACTE voulue : on cherche la précision la
        // plus courte qui se relit au bit près. Écrite sans `==` pour rester
        // propre sous -Wfloat-equal, où l'avertissement viserait juste.
        double roundTrip = 0.0;
        std::from_chars(buffer, end, roundTrip);
        if (!(roundTrip < value) && !(value < roundTrip)) break;
    }
    out.append(buffer, static_cast<size_t>(end - buffer));
}

} // namespace

void JsonValue::writeTo(std::string& out, int indent, int depth) const {
    const bool pretty = indent >= 0;
    const std::string pad = pretty ? std::string(static_cast<size_t>(indent * (depth + 1)), ' ') : std::string();
    const std::string padClose = pretty ? std::string(static_cast<size_t>(indent * depth), ' ') : std::string();
    const char* newline = pretty ? "\n" : "";

    switch (type_) {
        case Type::Null:    out += "null"; break;
        case Type::Boolean: out += boolean_ ? "true" : "false"; break;
        case Type::Number:  writeNumber(out, number_); break;
        case Type::String:  writeEscapedString(out, string_); break;
        case Type::Array: {
            if (elements_.empty()) { out += "[]"; break; }
            out += '[';
            out += newline;
            for (size_t i = 0; i < elements_.size(); ++i) {
                out += pad;
                elements_[i].writeTo(out, indent, depth + 1);
                if (i + 1 < elements_.size()) out += ',';
                out += newline;
            }
            out += padClose;
            out += ']';
            break;
        }
        case Type::Object: {
            if (members_.empty()) { out += "{}"; break; }
            out += '{';
            out += newline;
            size_t index = 0;
            for (const auto& [key, value] : members_) {
                out += pad;
                writeEscapedString(out, key);
                out += pretty ? ": " : ":";
                value.writeTo(out, indent, depth + 1);
                if (++index < members_.size()) out += ',';
                out += newline;
            }
            out += padClose;
            out += '}';
            break;
        }
    }
}

std::string JsonValue::toString(int indent) const {
    std::string out;
    writeTo(out, indent, 0);
    return out;
}

// ---------------------------------------------------------------------------
// Lecture
// ---------------------------------------------------------------------------

namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    JsonParseResult run() {
        JsonParseResult result;
        skipWhitespace();
        JsonValue value;
        if (!parseValue(value)) {
            result.error = error_.empty() ? "valeur JSON attendue" : error_;
            result.errorOffset = pos_;
            return result;
        }
        skipWhitespace();
        if (pos_ != text_.size()) {
            result.error = "données superflues après la valeur JSON";
            result.errorOffset = pos_;
            return result;
        }
        result.success = true;
        result.value = std::move(value);
        return result;
    }

private:
    bool fail(std::string message) { error_ = std::move(message); return false; }
    bool atEnd() const { return pos_ >= text_.size(); }
    char peek() const { return text_[pos_]; }

    void skipWhitespace() {
        while (!atEnd()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
    }

    bool expect(char c) {
        if (atEnd() || text_[pos_] != c) return fail(std::string("'") + c + "' attendu");
        ++pos_;
        return true;
    }

    bool literal(const char* word, JsonValue produced, JsonValue& out) {
        const size_t length = std::char_traits<char>::length(word);
        if (text_.compare(pos_, length, word) != 0) return false;
        pos_ += length;
        out = std::move(produced);
        return true;
    }

    bool parseValue(JsonValue& out) {
        skipWhitespace();
        if (atEnd()) return fail("fin de document inattendue");
        switch (peek()) {
            case '{': return parseObject(out);
            case '[': return parseArray(out);
            case '"': {
                std::string s;
                if (!parseString(s)) return false;
                out = JsonValue::makeString(std::move(s));
                return true;
            }
            case 't': if (literal("true", JsonValue::makeBoolean(true), out)) return true; return fail("littéral invalide");
            case 'f': if (literal("false", JsonValue::makeBoolean(false), out)) return true; return fail("littéral invalide");
            case 'n': if (literal("null", JsonValue::makeNull(), out)) return true; return fail("littéral invalide");
            default:  return parseNumber(out);
        }
    }

    bool parseObject(JsonValue& out) {
        if (!expect('{')) return false;
        JsonValue object = JsonValue::makeObject();
        skipWhitespace();
        if (!atEnd() && peek() == '}') { ++pos_; out = std::move(object); return true; }
        while (true) {
            skipWhitespace();
            std::string key;
            if (!parseString(key)) return false;
            skipWhitespace();
            if (!expect(':')) return false;
            JsonValue value;
            if (!parseValue(value)) return false;
            object.set(key, std::move(value));
            skipWhitespace();
            if (atEnd()) return fail("'}' attendu");
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == '}') { ++pos_; break; }
            return fail("',' ou '}' attendu");
        }
        out = std::move(object);
        return true;
    }

    bool parseArray(JsonValue& out) {
        if (!expect('[')) return false;
        JsonValue array = JsonValue::makeArray();
        skipWhitespace();
        if (!atEnd() && peek() == ']') { ++pos_; out = std::move(array); return true; }
        while (true) {
            JsonValue value;
            if (!parseValue(value)) return false;
            array.append(std::move(value));
            skipWhitespace();
            if (atEnd()) return fail("']' attendu");
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == ']') { ++pos_; break; }
            return fail("',' ou ']' attendu");
        }
        out = std::move(array);
        return true;
    }

    /// Encode un point de code en UTF-8 (les échappements \uXXXX du JSON sont
    /// en UTF-16 ; les paires de substitution sont recombinées). Délègue à
    /// l'encodeur partagé — c'était la première des quatre copies du module,
    /// voir interchange/Utf8.h.
    static void appendUtf8(std::string& out, uint32_t codepoint) {
        vsm::interchange::appendUtf8(out, codepoint);
    }

    bool parseHex4(uint32_t& out) {
        if (pos_ + 4 > text_.size()) return fail("échappement \\u incomplet");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<uint32_t>(c - 'A' + 10);
            else return fail("chiffre hexadécimal attendu");
        }
        return true;
    }

    bool parseString(std::string& out) {
        if (!expect('"')) return false;
        out.clear();
        while (true) {
            if (atEnd()) return fail("chaîne non terminée");
            const char c = text_[pos_++];
            if (c == '"') return true;
            if (c != '\\') {
                if (static_cast<unsigned char>(c) < 0x20) return fail("caractère de contrôle non échappé");
                out += c;
                continue;
            }
            if (atEnd()) return fail("échappement incomplet");
            const char escape = text_[pos_++];
            switch (escape) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    uint32_t codepoint = 0;
                    if (!parseHex4(codepoint)) return false;
                    if (codepoint >= 0xD800 && codepoint <= 0xDBFF) { // paire de substitution
                        if (pos_ + 1 < text_.size() && text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                            pos_ += 2;
                            uint32_t low = 0;
                            if (!parseHex4(low)) return false;
                            if (low >= 0xDC00 && low <= 0xDFFF)
                                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                            else
                                return fail("paire de substitution UTF-16 invalide");
                        }
                    }
                    appendUtf8(out, codepoint);
                    break;
                }
                default: return fail("séquence d'échappement inconnue");
            }
        }
    }

    bool parseNumber(JsonValue& out) {
        const size_t start = pos_;
        if (!atEnd() && (peek() == '-' || peek() == '+')) ++pos_;
        bool anyDigit = false;
        while (!atEnd() && peek() >= '0' && peek() <= '9') { ++pos_; anyDigit = true; }
        if (!atEnd() && peek() == '.') {
            ++pos_;
            while (!atEnd() && peek() >= '0' && peek() <= '9') { ++pos_; anyDigit = true; }
        }
        if (anyDigit && !atEnd() && (peek() == 'e' || peek() == 'E')) {
            ++pos_;
            if (!atEnd() && (peek() == '-' || peek() == '+')) ++pos_;
            while (!atEnd() && peek() >= '0' && peek() <= '9') ++pos_;
        }
        if (!anyDigit) { pos_ = start; return fail("nombre attendu"); }
        // `std::from_chars` ET PAS `strtod`, POUR LA RAISON SYMÉTRIQUE DE
        // `writeNumber`, et c'est le côté SILENCIEUX de la panne : sous une
        // locale à virgule, `strtod("0.8")` s'arrête sur le point et rend 0.
        // Un `project.json` parfaitement valide -- ceux qu'écrit la chaîne
        // d'analyse en Python -- se chargeait donc avec TOUS ses paramètres
        // fractionnaires à zéro, sans un mot. `from_chars` lit en locale C,
        // toujours. Il refuse le `+` de tête, que le balayage ci-dessus
        // accepte ; on le saute pour ne rien changer à ce qui passait déjà.
        const char* premier = text_.data() + start;
        const char* dernier = text_.data() + pos_;
        if (premier != dernier && *premier == '+') ++premier;
        double valeur = 0.0;
        std::from_chars(premier, dernier, valeur); // échec => 0, comme strtod
        out = JsonValue::makeNumber(valeur);
        return true;
    }

    const std::string& text_;
    size_t pos_ = 0;
    std::string error_;
};

} // namespace

JsonParseResult parseJson(const std::string& text) { return Parser(text).run(); }

} // namespace vsm::interchange
