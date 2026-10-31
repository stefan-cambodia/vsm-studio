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
        preset.effectsPostFader.push_back(effet.postFader);   // D576.1
    }
    preset.sendLevels = track.sendLevels;
    preset.volume = track.volume;
    preset.pan = track.pan;
    preset.stereoWidth = track.stereoWidth;   // D573.1
    preset.invertPhase = track.invertPhase;
    preset.sendsBypassed = track.sendsBypassed;   // D567.3
    preset.sendsFollowPan = track.sendsFollowPan;   // D572.2
    preset.colorRgba = track.colorRgba;
    preset.channel = track.channel;
    preset.transposeSemitones = track.transposeSemitones;
    preset.drumMap = track.drumMap;   // D610.2
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
        effet.postFader = i < preset.effectsPostFader.size() && preset.effectsPostFader[i];   // D576.1
        track.effects.push_back(std::move(effet));
    }
    track.sendLevels = preset.sendLevels;
    track.volume = preset.volume;
    track.pan = preset.pan;
    // D573.1 : une piste qui n'en porte pas (dossier, VCA, groupe) la garde entière, comme le menu le refuse.
    track.stereoWidth = track.isFolder() || track.isVca() || track.kind == vsm::sequencer::Track::Kind::Group
                            ? 1.0f : std::clamp(preset.stereoWidth, 0.0f, 1.0f);
    track.invertPhase = preset.invertPhase;
    track.sendsBypassed = preset.sendsBypassed;   // D567.3
    track.sendsFollowPan = preset.sendsFollowPan;   // D572.2
    track.colorRgba = preset.colorRgba;
    track.channel = static_cast<uint8_t>(std::clamp(preset.channel, 0, 15));
    track.transposeSemitones = preset.transposeSemitones;
    track.drumMap = preset.drumMap;   // D610.2
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
        if (i < preset.effectsPostFader.size() && preset.effectsPostFader[i])
            e.set("postFader", JsonValue::makeBoolean(true));   // D576.1, écrit seulement s'il l'est
        effets.append(std::move(e));
    }
    root.set("effects", std::move(effets));
    JsonValue departs = JsonValue::makeArray();
    for (float niveau : preset.sendLevels) departs.append(JsonValue::makeFloat(niveau));
    root.set("sendLevels", std::move(departs));
    root.set("volume", JsonValue::makeFloat(preset.volume));
    root.set("pan", JsonValue::makeFloat(preset.pan));
    if (preset.stereoWidth != 1.0f) root.set("width", JsonValue::makeFloat(preset.stereoWidth));   // D573.1
    if (preset.invertPhase) root.set("invertPhase", JsonValue::makeBoolean(true));
    if (preset.sendsBypassed) root.set("sendsBypassed", JsonValue::makeBoolean(true));   // D567.3
    if (preset.sendsFollowPan) root.set("sendsFollowPan", JsonValue::makeBoolean(true));   // D572.2
    root.set("colorRgba", JsonValue::makeNumber(static_cast<double>(preset.colorRgba)));
    root.set("channel", JsonValue::makeNumber(preset.channel));
    root.set("transposeSemitones", JsonValue::makeNumber(preset.transposeSemitones));
    if (!preset.drumMap.empty()) {   // D610.2 : seulement si elle déplace une note, comme au projet
        JsonValue carte = JsonValue::makeArray();
        for (const auto& [ecrite, jouee] : preset.drumMap) {
            JsonValue e = JsonValue::makeObject();
            e.set("in", JsonValue::makeNumber(ecrite));
            e.set("out", JsonValue::makeNumber(jouee));
            carte.append(std::move(e));
        }
        root.set("drumMap", std::move(carte));
    }
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
        preset.effectsPostFader.push_back(e["postFader"].asBoolean(false));   // D576.1
    }
    for (const auto& niveau : json["sendLevels"].elements())
        preset.sendLevels.push_back(static_cast<float>(niveau.asNumber(0.0)));
    preset.volume = static_cast<float>(json["volume"].asNumber(1.0));
    preset.pan = static_cast<float>(json["pan"].asNumber(0.0));
    preset.stereoWidth = static_cast<float>(json["width"].asNumber(1.0));   // D573.1 : bornée à l'application
    preset.invertPhase = json["invertPhase"].asBoolean(false);
    preset.sendsBypassed = json["sendsBypassed"].asBoolean(false);   // D567.3
    preset.sendsFollowPan = json["sendsFollowPan"].asBoolean(false);   // D572.2
    preset.colorRgba = static_cast<uint32_t>(json["colorRgba"].asNumber(static_cast<double>(0xFF6B9BFFu)));
    preset.channel = static_cast<int>(json["channel"].asNumber(0.0));
    preset.transposeSemitones = static_cast<int>(json["transposeSemitones"].asNumber(0.0));
    // D610.2 : les entrées hors de 0..127, ou vers elles-mêmes, sont écartées — la règle du projet (D609.1).
    for (const auto& e : json["drumMap"].elements()) {
        const double ecrite = e["in"].asNumber(-1.0), jouee = e["out"].asNumber(-1.0);
        if (ecrite >= 0.0 && ecrite <= 127.0 && jouee >= 0.0 && jouee <= 127.0 && ecrite != jouee)
            preset.drumMap[static_cast<uint8_t>(ecrite)] = static_cast<uint8_t>(jouee);
    }
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
