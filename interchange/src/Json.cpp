#include "vsm/interchange/Json.h"
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
    char buffer[32];
    for (int precision = 1; precision <= 9; ++precision) {
        std::snprintf(buffer, sizeof(buffer), "%.*g", precision, static_cast<double>(value));
        const float roundTrip = std::strtof(buffer, nullptr);
        if (!(roundTrip < value) && !(value < roundTrip)) break; // identité exacte en float
    }
    return makeNumber(std::strtod(buffer, nullptr));
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
void writeNumber(std::string& out, double value) {
    if (!std::isfinite(value)) { out += "null"; return; } // NaN/inf n'existent pas en JSON
    char buffer[40];
    for (int precision : {6, 9, 12, 17}) {
        std::snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
        // Comparaison d'identité EXACTE voulue : on cherche la précision la
        // plus courte qui se relit au bit près. Écrite sans `==` pour rester
        // propre sous -Wfloat-equal, où l'avertissement viserait juste.
        const double roundTrip = std::strtod(buffer, nullptr);
        if (!(roundTrip < value) && !(value < roundTrip)) break;
    }
    out += buffer;
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
    /// en UTF-16 ; les paires de substitution sont recombinées).
    static void appendUtf8(std::string& out, uint32_t codepoint) {
        if (codepoint < 0x80) {
            out += static_cast<char>(codepoint);
        } else if (codepoint < 0x800) {
            out += static_cast<char>(0xC0 | (codepoint >> 6));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else if (codepoint < 0x10000) {
            out += static_cast<char>(0xE0 | (codepoint >> 12));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (codepoint >> 18));
            out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
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
        out = JsonValue::makeNumber(std::strtod(text_.substr(start, pos_ - start).c_str(), nullptr));
        return true;
    }

    const std::string& text_;
    size_t pos_ = 0;
    std::string error_;
};

} // namespace

JsonParseResult parseJson(const std::string& text) { return Parser(text).run(); }

} // namespace vsm::interchange
