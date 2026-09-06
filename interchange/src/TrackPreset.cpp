#include "vsm/interchange/TrackPreset.h"
#include <algorithm>
#include <cctype>

namespace vsm::interchange {

TrackPreset trackPresetFromTrack(const vsm::sequencer::Track& track, const std::string& name,
                                 std::optional<SynthPreset> synth) {
    TrackPreset preset;
    preset.name = name;
    preset.kind = track.kind == vsm::sequencer::Track::Kind::Audio ? "audio" : "midi";
    preset.instrumentId = track.instrumentId;
    preset.synth = std::move(synth);
    for (const auto& effet : track.effects) {
        preset.effects.push_back(effectPresetFromDescription(effet, effet.type));
        preset.effectsEnabled.push_back(effet.enabled);
    }
    preset.sendLevels = track.sendLevels;
    preset.volume = track.volume;
    preset.pan = track.pan;
    preset.invertPhase = track.invertPhase;
    preset.colorRgba = track.colorRgba;
    preset.channel = track.channel;
    preset.transposeSemitones = track.transposeSemitones;
    preset.delayMs = track.delayMs;
    preset.outputGroup = track.outputGroup;
    return preset;
}

void applyTrackPresetToTrack(const TrackPreset& preset, vsm::sequencer::Track& track) {
    // LE GENRE NE CHANGE PAS : un preset audio posé sur une piste MIDI ne
    // fait pas d'elle une piste audio -- ses notes n'auraient plus de
    // machine. Il apporte ses inserts et son mixage, et c'est tout ; la
    // machine n'est touchée que si le preset en a une et la piste aussi.
    if (track.kind != vsm::sequencer::Track::Kind::Audio && !preset.instrumentId.empty())
        track.instrumentId = preset.instrumentId;
    track.effects.clear();
    for (size_t i = 0; i < preset.effects.size(); ++i) {
        auto effet = descriptionFromEffectPreset(preset.effects[i]);
        effet.enabled = i < preset.effectsEnabled.size() ? preset.effectsEnabled[i] : true;
        track.effects.push_back(std::move(effet));
    }
    track.sendLevels = preset.sendLevels;
    track.volume = preset.volume;
    track.pan = preset.pan;
    track.invertPhase = preset.invertPhase;
    track.colorRgba = preset.colorRgba;
    track.channel = static_cast<uint8_t>(std::clamp(preset.channel, 0, 15));
    track.transposeSemitones = preset.transposeSemitones;
    track.delayMs = preset.delayMs;
    track.outputGroup = preset.outputGroup;
}

JsonValue trackPresetToJson(const TrackPreset& preset) {
    JsonValue root = JsonValue::makeObject();
    root.set("format", JsonValue::makeString(kTrackPresetFormat));
    root.set("version", JsonValue::makeNumber(kTrackPresetVersion));
    root.set("name", JsonValue::makeString(preset.name));
    root.set("kind", JsonValue::makeString(preset.kind));
    if (!preset.instrumentId.empty()) root.set("instrumentId", JsonValue::makeString(preset.instrumentId));
    if (preset.synth) root.set("synth", synthPresetToJson(*preset.synth));
    JsonValue effets = JsonValue::makeArray();
    for (size_t i = 0; i < preset.effects.size(); ++i) {
        JsonValue e = effectPresetToJson(preset.effects[i]);
        e.set("enabled", JsonValue::makeBoolean(i < preset.effectsEnabled.size() ? preset.effectsEnabled[i] : true));
        effets.append(std::move(e));
    }
    root.set("effects", std::move(effets));
    JsonValue departs = JsonValue::makeArray();
    for (float niveau : preset.sendLevels) departs.append(JsonValue::makeFloat(niveau));
    root.set("sendLevels", std::move(departs));
    root.set("volume", JsonValue::makeFloat(preset.volume));
    root.set("pan", JsonValue::makeFloat(preset.pan));
    if (preset.invertPhase) root.set("invertPhase", JsonValue::makeBoolean(true));
    root.set("colorRgba", JsonValue::makeNumber(static_cast<double>(preset.colorRgba)));
    root.set("channel", JsonValue::makeNumber(preset.channel));
    root.set("transposeSemitones", JsonValue::makeNumber(preset.transposeSemitones));
    root.set("delayMs", JsonValue::makeNumber(preset.delayMs));
    root.set("outputGroup", JsonValue::makeNumber(preset.outputGroup));
    return root;
}

TrackPresetLoadResult trackPresetFromJson(const JsonValue& json) {
    TrackPresetLoadResult result;
    if (!json.isObject()) { result.error = "racine JSON : objet attendu"; return result; }
    const std::string format = json["format"].asString();
    if (format != kTrackPresetFormat) {
        result.error = "format inattendu : \"" + format + "\" (attendu \"" + kTrackPresetFormat + "\")";
        return result;
    }
    const int version = static_cast<int>(json["version"].asNumber(-1.0));
    if (version != kTrackPresetVersion) {
        result.error = "version de preset de piste non prise en charge : " + std::to_string(version)
                     + " (cette version du logiciel lit la " + std::to_string(kTrackPresetVersion) + ")";
        return result;
    }
    TrackPreset preset;
    preset.name = json["name"].asString("Sans titre");
    preset.kind = json["kind"].asString("midi");
    if (preset.kind != "midi" && preset.kind != "audio") {
        result.error = "genre de piste inconnu : \"" + preset.kind + "\"";
        return result;
    }
    preset.instrumentId = json["instrumentId"].asString();
    if (json["synth"].isObject()) {
        const auto synth = synthPresetFromJson(json["synth"]);
        if (!synth.success) { result.error = "machine : " + synth.error; return result; }
        preset.synth = synth.preset;
    }
    for (const auto& e : json["effects"].elements()) {
        const auto lu = effectPresetFromJson(e);
        if (!lu.success) { result.error = "insert : " + lu.error; return result; }
        preset.effects.push_back(lu.preset);
        preset.effectsEnabled.push_back(e["enabled"].asBoolean(true));
    }
    for (const auto& niveau : json["sendLevels"].elements())
        preset.sendLevels.push_back(static_cast<float>(niveau.asNumber(0.0)));
    preset.volume = static_cast<float>(json["volume"].asNumber(1.0));
    preset.pan = static_cast<float>(json["pan"].asNumber(0.0));
    preset.invertPhase = json["invertPhase"].asBoolean(false);
    preset.colorRgba = static_cast<uint32_t>(json["colorRgba"].asNumber(static_cast<double>(0xFF6B9BFFu)));
    preset.channel = static_cast<int>(json["channel"].asNumber(0.0));
    preset.transposeSemitones = static_cast<int>(json["transposeSemitones"].asNumber(0.0));
    preset.delayMs = json["delayMs"].asNumber(0.0);
    preset.outputGroup = static_cast<int>(json["outputGroup"].asNumber(-1.0));
    result.preset = std::move(preset);
    result.success = true;
    return result;
}

TrackPresetLoadResult parseTrackPreset(const std::string& jsonText) {
    const JsonParseResult parsed = parseJson(jsonText);
    if (!parsed.success) {
        TrackPresetLoadResult result;
        result.error = "JSON invalide : " + parsed.error;
        return result;
    }
    return trackPresetFromJson(parsed.value);
}

bool isTrackPresetFile(const std::string& path) {
    const std::string extension(kTrackPresetExtension);
    return path.size() >= extension.size()
        && std::equal(extension.rbegin(), extension.rend(), path.rbegin(),
                      [](char a, char b) { return std::tolower(static_cast<unsigned char>(a))
                                                == std::tolower(static_cast<unsigned char>(b)); });
}

} // namespace vsm::interchange
