#include "vsm/interchange/MultisampleProfile.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <set>
#include <sstream>

namespace vsm::interchange {

using vsm::audio::plugin::IMultisampleBank;
using vsm::audio::plugin::MultisampleProfileSpec;
using vsm::audio::plugin::MultisampleZoneSpec;

namespace {

/// Champs qu'une zone peut déclarer. Tout autre nom est RAPPORTÉ comme ignoré
/// -- une faute de frappe (`rootKey` au lieu de `rootNote`) passerait sinon
/// inaperçue et la zone jouerait une octave à côté sans que rien ne le dise.
const std::set<std::string>& knownZoneFields() {
    static const std::set<std::string> fields = {
        "sample", "program", "lowNote", "highNote", "lowVelocity", "highVelocity",
        "rootNote", "tuneCents", "level", "loop",
    };
    return fields;
}

const std::set<std::string>& knownRootFields() {
    static const std::set<std::string> fields = {
        "format", "version", "name", "attribution", "programs", "zones",
    };
    return fields;
}

int intOr(const JsonValue& object, const char* key, int fallback) {
    return object.has(key) ? static_cast<int>(object[key].asNumber(fallback)) : fallback;
}
float floatOr(const JsonValue& object, const char* key, float fallback) {
    return object.has(key) ? static_cast<float>(object[key].asNumber(fallback)) : fallback;
}

} // namespace

std::string MultisampleProfileApplyReport::summary() const {
    std::ostringstream out;
    if (!error.empty()) { out << "profil non chargé : " << error; return out.str(); }
    if (!applied) return "aucun profil à charger";
    out << "profil « " << profileName << " » : " << zoneCount << " zone(s), "
        << (memoryBytes / (1024u * 1024u)) << " Mo en mémoire";
    if (!ignored.empty()) {
        out << " ; champs ignorés :";
        for (const auto& field : ignored) out << ' ' << field;
    }
    return out.str();
}

MultisampleProfileLoadResult parseMultisampleProfile(const std::string& jsonText,
                                                      const std::string& baseFolder,
                                                      const std::string& sourcePath) {
    MultisampleProfileLoadResult result;

    auto parsed = parseJson(jsonText);
    if (!parsed.success) { result.error = "JSON illisible : " + parsed.error; return result; }
    const JsonValue& root = parsed.value;
    if (!root.isObject()) { result.error = "le profil doit être un objet JSON"; return result; }

    const std::string format = root["format"].asString();
    if (format != kMultisampleProfileFormat) {
        result.error = "format inattendu : « " + format + " », attendu « "
                     + kMultisampleProfileFormat + " »";
        return result;
    }
    const int version = static_cast<int>(root["version"].asNumber(0));
    if (version != kMultisampleProfileVersion) {
        result.error = "version " + std::to_string(version) + " non prise en charge (attendu "
                     + std::to_string(kMultisampleProfileVersion) + ")";
        return result;
    }

    result.spec.name = root["name"].asString("Sans titre");
    result.spec.attribution = root["attribution"].asString();
    result.spec.sourcePath = sourcePath;
    if (result.spec.attribution.empty()) {
        result.error = "champ « attribution » absent ou vide : licence inconnue, banque refusée "
                       "(§ 28 d'ARCHITECTURE.md)";
        return result;
    }

    for (const auto& [key, value] : root.members()) {
        (void)value;
        if (knownRootFields().count(key) == 0) result.ignored.push_back(key);
    }

    if (root["programs"].isArray())
        for (const auto& entry : root["programs"].elements())
            result.spec.programNames.push_back(entry.asString());

    const JsonValue& zones = root["zones"];
    if (!zones.isArray() || zones.size() == 0) {
        result.error = "le profil ne déclare aucune zone";
        return result;
    }

    const std::filesystem::path base(baseFolder);
    for (size_t i = 0; i < zones.size(); ++i) {
        const JsonValue& entry = zones.at(i);
        const std::string where = "zone " + std::to_string(i);
        if (!entry.isObject()) { result.error = where + " : ce n'est pas un objet"; return result; }

        for (const auto& [key, value] : entry.members()) {
            (void)value;
            if (knownZoneFields().count(key) == 0) result.ignored.push_back(where + "." + key);
        }

        MultisampleZoneSpec zone;
        zone.relativePath = entry["sample"].asString();
        if (zone.relativePath.empty()) {
            result.error = where + " : champ « sample » absent";
            return result;
        }
        if (std::filesystem::path(zone.relativePath).is_absolute()) {
            // Refusé, pas « converti en relatif » : un profil qui porte un
            // chemin absolu a été produit par un outil fautif, et le corriger en
            // douce laisserait l'outil fautif en place.
            result.error = where + " : chemin absolu refusé (« " + zone.relativePath
                         + " ») ; les chemins d'un profil sont relatifs à son dossier";
            return result;
        }
        zone.samplePath = (base / zone.relativePath).string();

        zone.program = intOr(entry, "program", 0);
        zone.lowNote = intOr(entry, "lowNote", 0);
        zone.highNote = intOr(entry, "highNote", 127);
        zone.lowVelocity = intOr(entry, "lowVelocity", 1);
        zone.highVelocity = intOr(entry, "highVelocity", 127);
        zone.rootNote = intOr(entry, "rootNote", 60);
        zone.tuneCents = floatOr(entry, "tuneCents", 0.0f);
        zone.level = floatOr(entry, "level", 1.0f);

        if (zone.lowNote > zone.highNote || zone.lowVelocity > zone.highVelocity) {
            result.error = where + " : étendue vide (notes " + std::to_string(zone.lowNote) + ".."
                         + std::to_string(zone.highNote) + ", vélocités "
                         + std::to_string(zone.lowVelocity) + ".." + std::to_string(zone.highVelocity)
                         + ") -- une zone que rien ne peut atteindre est une erreur, pas un choix";
            return result;
        }

        const JsonValue& loop = entry["loop"];
        if (loop.isObject()) {
            zone.loopEnabled = true;
            zone.loopStart = static_cast<uint64_t>(loop["start"].asNumber(0));
            zone.loopEnd = static_cast<uint64_t>(loop["end"].asNumber(0));
        }

        result.spec.zones.push_back(std::move(zone));
    }

    result.success = true;
    return result;
}

MultisampleProfileLoadResult loadMultisampleProfileFile(const std::string& path) {
    MultisampleProfileLoadResult result;
    std::ifstream file(path, std::ios::binary);
    if (!file) { result.error = "profil illisible : " + path; return result; }
    std::ostringstream buffer;
    buffer << file.rdbuf();

    const std::filesystem::path folder = std::filesystem::path(path).parent_path();
    return parseMultisampleProfile(buffer.str(), folder.string(), path);
}

JsonValue multisampleProfileToJson(const MultisampleProfileSpec& spec, const std::string& baseFolder) {
    JsonValue root = JsonValue::makeObject();
    root.set("format", JsonValue::makeString(kMultisampleProfileFormat));
    root.set("version", JsonValue::makeNumber(kMultisampleProfileVersion));
    root.set("name", JsonValue::makeString(spec.name));
    root.set("attribution", JsonValue::makeString(spec.attribution));

    if (!spec.programNames.empty()) {
        JsonValue programs = JsonValue::makeArray();
        for (const auto& name : spec.programNames) programs.append(JsonValue::makeString(name));
        root.set("programs", std::move(programs));
    }

    const std::filesystem::path base(baseFolder);
    JsonValue zones = JsonValue::makeArray();
    for (const auto& zone : spec.zones) {
        JsonValue entry = JsonValue::makeObject();
        std::string relative = zone.relativePath;
        if (relative.empty() && !zone.samplePath.empty() && !baseFolder.empty())
            relative = std::filesystem::relative(zone.samplePath, base).string();
        entry.set("sample", JsonValue::makeString(relative));
        if (zone.program != 0) entry.set("program", JsonValue::makeNumber(zone.program));
        entry.set("lowNote", JsonValue::makeNumber(zone.lowNote));
        entry.set("highNote", JsonValue::makeNumber(zone.highNote));
        entry.set("lowVelocity", JsonValue::makeNumber(zone.lowVelocity));
        entry.set("highVelocity", JsonValue::makeNumber(zone.highVelocity));
        entry.set("rootNote", JsonValue::makeNumber(zone.rootNote));
        if (zone.tuneCents != 0.0f) entry.set("tuneCents", JsonValue::makeFloat(zone.tuneCents));
        if (zone.level != 1.0f) entry.set("level", JsonValue::makeFloat(zone.level));
        if (zone.loopEnabled) {
            JsonValue loop = JsonValue::makeObject();
            loop.set("start", JsonValue::makeNumber(static_cast<double>(zone.loopStart)));
            loop.set("end", JsonValue::makeNumber(static_cast<double>(zone.loopEnd)));
            entry.set("loop", std::move(loop));
        }
        zones.append(std::move(entry));
    }
    root.set("zones", std::move(zones));
    return root;
}

std::string multisampleProfileFolder() {
    if (const char* override = std::getenv("VSM_PROFILS"); override != nullptr && *override != '\0')
        return override;
#ifdef _WIN32
    if (const char* appData = std::getenv("APPDATA"); appData != nullptr && *appData != '\0')
        return (std::filesystem::path(appData) / "vsm-studio" / "profils").string();
#endif
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0')
        return (std::filesystem::path(home) / ".local" / "share" / "vsm-studio" / "profils").string();
    return (std::filesystem::current_path() / "profils").string();
}

std::vector<InstalledProfile> installedMultisampleProfiles() {
    std::vector<InstalledProfile> found;
    const std::filesystem::path folder(multisampleProfileFolder());
    std::error_code ignored;
    if (!std::filesystem::is_directory(folder, ignored)) return found;

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ignored)) {
        if (!entry.is_regular_file(ignored)) continue;
        const std::string name = entry.path().filename().string();
        if (name.size() > 13 && name.compare(name.size() - 13, 13, ".profile.json") == 0)
            files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    for (const auto& file : files) {
        InstalledProfile profile;
        profile.path = file.string();
        auto loaded = loadMultisampleProfileFile(profile.path);
        if (loaded.success) {
            profile.name = loaded.spec.name;
            profile.attribution = loaded.spec.attribution;
            profile.zoneCount = static_cast<int>(loaded.spec.zones.size());
        } else {
            profile.name = file.stem().string();
            profile.error = loaded.error;
        }
        found.push_back(std::move(profile));
    }
    return found;
}

MultisampleProfileApplyReport applyMultisampleProfile(vsm::audio::plugin::ISynthPlugin& plugin,
                                                       const std::string& profilePath,
                                                       vsm::audio::plugin::MultisampleSampleCache* cache) {
    MultisampleProfileApplyReport report;
    auto* bank = dynamic_cast<IMultisampleBank*>(&plugin);
    if (bank == nullptr) return report; // la machine n'accepte pas de profil : rien à faire
    if (profilePath.empty()) return report;

    auto loaded = loadMultisampleProfileFile(profilePath);
    report.ignored = loaded.ignored;
    if (!loaded.success) { report.error = loaded.error; return report; }

    std::string error;
    if (!bank->loadProfile(loaded.spec, error, cache)) { report.error = error; return report; }

    report.applied = true;
    report.profileName = bank->profileName();
    report.zoneCount = bank->zoneCount();
    report.memoryBytes = bank->profileMemoryBytes();
    return report;
}

} // namespace vsm::interchange
