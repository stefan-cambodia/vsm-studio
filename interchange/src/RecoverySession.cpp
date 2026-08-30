#include "vsm/interchange/RecoverySession.h"
#include "vsm/interchange/Json.h"

namespace vsm::interchange {

namespace {
constexpr const char* kFormat = "vsm.recuperation.v1";
}

std::string recoveryRecordToJson(const RecoveryRecord& record) {
    JsonValue racine = JsonValue::makeObject();
    racine.set("format", JsonValue::makeString(kFormat));
    racine.set("originalFolder", JsonValue::makeString(record.originalFolder));
    racine.set("title", JsonValue::makeString(record.title));
    racine.set("savedAt", JsonValue::makeNumber(static_cast<double>(record.savedAtEpochSeconds)));
    racine.set("tracks", JsonValue::makeNumber(record.trackCount));
    racine.set("notes", JsonValue::makeNumber(record.noteCount));
    return racine.toString(2);
}

bool recoveryRecordFromJson(const std::string& text, RecoveryRecord& out) {
    if (text.empty()) return false;
    const JsonParseResult analyse = parseJson(text);
    if (!analyse.success || !analyse.value.isObject()) return false;
    const JsonValue& racine = analyse.value;
    // UN FORMAT INCONNU EST IGNORÉ, PAS INTERPRÉTÉ AU MIEUX. Proposer de
    // récupérer une session puis échouer à l'ouvrir serait la deuxième
    // mauvaise nouvelle d'affilée.
    if (racine["format"].asString() != kFormat) return false;

    out.originalFolder = racine["originalFolder"].asString();
    out.title = racine["title"].asString();
    out.savedAtEpochSeconds = static_cast<int64_t>(racine["savedAt"].asNumber(0.0));
    out.trackCount = static_cast<int>(racine["tracks"].asNumber(0.0));
    out.noteCount = static_cast<int>(racine["notes"].asNumber(0.0));
    return true;
}

} // namespace vsm::interchange
