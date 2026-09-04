#include "vsm/interchange/EffectPreset.h"
#include <algorithm>
#include <cctype>

namespace vsm::interchange {

EffectPreset effectPresetFromDescription(const vsm::sequencer::TrackEffect& described,
                                         const std::string& name) {
    EffectPreset preset;
    preset.name = name;
    preset.type = described.type;
    preset.parameters = described.parameters;
    preset.nativeState = described.nativeState;
    return preset;
}

vsm::sequencer::TrackEffect descriptionFromEffectPreset(const EffectPreset& preset) {
    vsm::sequencer::TrackEffect described;
    described.type = preset.type;
    described.parameters = preset.parameters;
    described.nativeState = preset.nativeState;
    described.enabled = true;
    return described;
}

JsonValue effectPresetToJson(const EffectPreset& preset) {
    JsonValue root = JsonValue::makeObject();
    root.set("format", JsonValue::makeString(kEffectPresetFormat));
    root.set("version", JsonValue::makeNumber(kEffectPresetVersion));
    root.set("name", JsonValue::makeString(preset.name));
    root.set("type", JsonValue::makeString(preset.type));
    JsonValue parameters = JsonValue::makeObject();
    for (const auto& [semanticId, value] : preset.parameters)
        parameters.set(semanticId, JsonValue::makeFloat(value));
    root.set("parameters", std::move(parameters));
    // Écrit seulement s'il existe, comme dans `project.json`.
    if (!preset.nativeState.empty()) root.set("nativeState", JsonValue::makeString(preset.nativeState));
    return root;
}

EffectPresetLoadResult effectPresetFromJson(const JsonValue& json) {
    EffectPresetLoadResult result;
    if (!json.isObject()) { result.error = "racine JSON : objet attendu"; return result; }
    const std::string format = json["format"].asString();
    if (format != kEffectPresetFormat) {
        result.error = "format inattendu : \"" + format + "\" (attendu \"" + kEffectPresetFormat + "\")";
        return result;
    }
    const int version = static_cast<int>(json["version"].asNumber(-1.0));
    if (version != kEffectPresetVersion) {
        result.error = "version de preset d'effet non prise en charge : " + std::to_string(version)
                     + " (cette version du logiciel lit la " + std::to_string(kEffectPresetVersion) + ")";
        return result;
    }
    EffectPreset preset;
    preset.name = json["name"].asString("Sans titre");
    preset.type = json["type"].asString();
    if (preset.type.empty()) { result.error = "champ \"type\" manquant"; return result; }
    const JsonValue& parameters = json["parameters"];
    if (!parameters.isObject()) { result.error = "champ \"parameters\" manquant ou mal typé"; return result; }
    for (const auto& [semanticId, value] : parameters.members())
        if (value.isNumber()) preset.parameters[semanticId] = static_cast<float>(value.asNumber());
    if (json["nativeState"].isString()) preset.nativeState = json["nativeState"].asString();
    result.preset = std::move(preset);
    result.success = true;
    return result;
}

EffectPresetLoadResult parseEffectPreset(const std::string& jsonText) {
    const JsonParseResult parsed = parseJson(jsonText);
    if (!parsed.success) {
        EffectPresetLoadResult result;
        result.error = "JSON invalide : " + parsed.error;
        return result;
    }
    return effectPresetFromJson(parsed.value);
}

bool isEffectPresetFile(const std::string& path) {
    std::string bas = path;
    std::transform(bas.begin(), bas.end(), bas.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::string ext = kEffectPresetExtension;
    return bas.size() >= ext.size() && bas.compare(bas.size() - ext.size(), ext.size(), ext) == 0;
}

} // namespace vsm::interchange
