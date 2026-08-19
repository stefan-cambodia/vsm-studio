#include "vsm/interchange/ProjectDocument.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace vsm::interchange {

using vsm::sequencer::Project;
using vsm::sequencer::Track;

namespace {

/// 60 s / (µs par noire / 1e6). Passer par le BPM plutôt que par les
/// microsecondes rend le fichier lisible ; la conversion inverse est exacte à
/// l'arrondi de la microseconde près, ce qu'un test vérifie.
double bpmFromMicroseconds(uint32_t microsecondsPerQuarter) {
    return microsecondsPerQuarter > 0 ? 60000000.0 / static_cast<double>(microsecondsPerQuarter) : 120.0;
}

uint32_t microsecondsFromBpm(double bpm) {
    if (!(bpm > 0.0)) return 500000; // 120 BPM : repli sain plutôt qu'une division par zéro
    return static_cast<uint32_t>(std::llround(60000000.0 / bpm));
}

/// Écrit un tempo sous la forme la plus simple qui redonne EXACTEMENT le même
/// tempo interne. Le moteur stocke des microsecondes par noire (héritage du
/// MIDI), donc 130 BPM y devient 461538 µs, qui se reconvertit en
/// 130,00014 BPM : l'écrire tel quel donnerait un fichier illisible et
/// donnerait l'impression d'une dérive alors qu'il n'y en a aucune. On cherche
/// donc la décimale la plus courte qui retombe sur la même valeur en
/// microsecondes -- ici "130", exact ET lisible.
JsonValue makeBpmNumber(double bpm);

uint8_t denominatorToPow2(int denominator) {
    uint8_t power = 2; // 2^2 = 4, la valeur par défaut
    for (uint8_t p = 0; p <= 7; ++p)
        if ((1 << p) == denominator) power = p;
    return power;
}

std::string colourToHex(uint32_t argb) {
    char buffer[10];
    std::snprintf(buffer, sizeof(buffer), "#%08X", argb);
    return buffer;
}

uint32_t colourFromHex(const std::string& text, uint32_t fallback) {
    if (text.size() != 9 || text[0] != '#') return fallback;
    char* end = nullptr;
    const unsigned long value = std::strtoul(text.c_str() + 1, &end, 16);
    if (end != text.c_str() + 9) return fallback;
    return static_cast<uint32_t>(value);
}

bool pluginIsInstalled(const std::string& pluginId) {
    static std::once_flag registration;
    std::call_once(registration, [] { vsm::audio::plugin::registerBuiltInPlugins(); });
    return vsm::audio::plugin::PluginRegistry::instance().isRegistered(pluginId);
}

JsonValue makeBpmNumber(double bpm) {
    const uint32_t target = microsecondsFromBpm(bpm);
    char buffer[32];
    for (int precision = 3; precision <= 12; ++precision) {
        std::snprintf(buffer, sizeof(buffer), "%.*g", precision, bpm);
        const double candidate = std::strtod(buffer, nullptr);
        if (microsecondsFromBpm(candidate) == target) return JsonValue::makeNumber(candidate);
    }
    return JsonValue::makeNumber(bpm);
}

} // namespace

bool isPortableRelativePath(const std::string& path) {
    if (path.empty()) return true; // champ optionnel non renseigné
    if (path.front() == '/') return false;                       // racine POSIX
    if (path.find('\\') != std::string::npos) return false;      // séparateur Windows
    if (path.size() >= 2 && path[1] == ':') return false;        // lettre de lecteur "C:"
    // "..": un projet ne doit pas pouvoir désigner des fichiers hors de son
    // propre dossier -- ni par accident, ni pour aller lire ailleurs.
    if (path.compare(0, 3, "../") == 0) return false;
    if (path.find("/../") != std::string::npos) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Projet en mémoire -> document
// ---------------------------------------------------------------------------

ProjectDocument documentFromProject(const Project& project) {
    ProjectDocument document;
    document.title = project.title;
    document.transport.ticksPerQuarterNote = project.ticksPerQuarterNote;

    for (const auto& change : project.tempoMap.changes())
        document.transport.tempoChanges.push_back({change.tick,
                                                    bpmFromMicroseconds(change.microsecondsPerQuarterNote)});
    for (const auto& change : project.timeSignatureMap.changes())
        document.transport.timeSignatures.push_back({change.tick, change.numerator,
                                                      1 << change.denominatorPow2});

    size_t index = 0;
    for (const auto& track : project.tracks) {
        ProjectTrack entry;
        entry.name = track.name;
        entry.channel = track.channel;
        entry.colorRgba = track.colorRgba;
        entry.preferredPlugin = track.instrumentId;
        if (!track.instrumentId.empty()) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "instruments/track_%02zu.synth.json", index);
            entry.presetPath = buffer;
        }
        entry.volume = track.volume;
        entry.pan = track.pan;
        entry.muted = track.muted;
        entry.solo = track.solo;
        entry.sendLevels = track.sendLevels;
        document.tracks.push_back(std::move(entry));
        ++index;
    }
    return document;
}

// ---------------------------------------------------------------------------
// Document -> projet en mémoire
// ---------------------------------------------------------------------------

std::string ImportReport::summary() const {
    std::ostringstream out;
    out << tracksInDocument << " piste(s) décrite(s), " << tracksInProject << " dans le MIDI";
    for (const auto& missing : missingInstruments)
        out << "\nInstrument manquant : " << missing;
    for (const auto& warning : warnings)
        out << "\n" << warning;
    return out.str();
}

ImportReport applyDocumentToProject(const ProjectDocument& document, Project& project) {
    ImportReport report;
    report.tracksInDocument = document.tracks.size();
    report.tracksInProject = project.tracks.size();

    project.title = document.title;
    project.ticksPerQuarterNote = static_cast<uint16_t>(document.transport.ticksPerQuarterNote);

    if (!document.transport.tempoChanges.empty()) {
        project.tempoMap.clearTempoChanges();
        for (const auto& change : document.transport.tempoChanges)
            project.tempoMap.addTempoChange(change.tick, microsecondsFromBpm(change.bpm));
    }
    if (!document.transport.timeSignatures.empty()) {
        project.timeSignatureMap.clear();
        for (const auto& change : document.transport.timeSignatures)
            project.timeSignatureMap.addChange(change.tick, static_cast<uint8_t>(change.numerator),
                                                denominatorToPow2(change.denominator));
    }

    if (document.tracks.size() != project.tracks.size()) {
        std::ostringstream warning;
        warning << "Le projet décrit " << document.tracks.size() << " piste(s) mais le MIDI en contient "
                << project.tracks.size() << " : seules les pistes communes sont configurées.";
        report.warnings.push_back(warning.str());
    }

    const size_t count = std::min(document.tracks.size(), project.tracks.size());
    for (size_t i = 0; i < count; ++i) {
        const ProjectTrack& source = document.tracks[i];
        Track& target = project.tracks[i];

        if (!source.name.empty()) target.name = source.name;
        target.channel = static_cast<uint8_t>(std::clamp(source.channel, 0, 15));
        target.colorRgba = source.colorRgba;
        target.volume = source.volume;
        target.pan = std::clamp(source.pan, -1.0f, 1.0f);
        target.muted = source.muted;
        target.solo = source.solo;
        target.sendLevels = source.sendLevels;

        if (source.preferredPlugin.empty()) continue;
        if (pluginIsInstalled(source.preferredPlugin)) {
            target.instrumentId = source.preferredPlugin;
        } else {
            // Machine absente : on NE substitue rien. L'intention est
            // conservée telle quelle (piste, notes, identifiant demandé), et
            // signalée -- une reconstruction silencieuse avec une autre
            // machine donnerait un résultat faux que personne ne remarquerait.
            report.missingInstruments.push_back(source.preferredPlugin);
            target.instrumentId.clear();
        }
    }
    return report;
}

// ---------------------------------------------------------------------------
// Sérialisation
// ---------------------------------------------------------------------------

JsonValue projectDocumentToJson(const ProjectDocument& document) {
    JsonValue root = JsonValue::makeObject();
    root.set("format", JsonValue::makeString(kProjectFormat));
    root.set("version", JsonValue::makeNumber(kProjectVersion));
    root.set("title", JsonValue::makeString(document.title));

    JsonValue midi = JsonValue::makeObject();
    midi.set("file", JsonValue::makeString(document.midiPath));
    root.set("midi", std::move(midi));

    JsonValue transport = JsonValue::makeObject();
    transport.set("ticksPerQuarterNote", JsonValue::makeNumber(document.transport.ticksPerQuarterNote));

    JsonValue tempoChanges = JsonValue::makeArray();
    for (const auto& change : document.transport.tempoChanges) {
        JsonValue entry = JsonValue::makeObject();
        entry.set("tick", JsonValue::makeNumber(static_cast<double>(change.tick)));
        entry.set("bpm", makeBpmNumber(change.bpm));
        tempoChanges.append(std::move(entry));
    }
    transport.set("tempoChanges", std::move(tempoChanges));

    JsonValue timeSignatures = JsonValue::makeArray();
    for (const auto& change : document.transport.timeSignatures) {
        JsonValue entry = JsonValue::makeObject();
        entry.set("tick", JsonValue::makeNumber(static_cast<double>(change.tick)));
        entry.set("numerator", JsonValue::makeNumber(change.numerator));
        entry.set("denominator", JsonValue::makeNumber(change.denominator));
        timeSignatures.append(std::move(entry));
    }
    transport.set("timeSignatures", std::move(timeSignatures));

    JsonValue loop = JsonValue::makeObject();
    loop.set("enabled", JsonValue::makeBoolean(document.transport.loopEnabled));
    loop.set("startTick", JsonValue::makeNumber(static_cast<double>(document.transport.loopStartTick)));
    loop.set("endTick", JsonValue::makeNumber(static_cast<double>(document.transport.loopEndTick)));
    transport.set("loop", std::move(loop));
    root.set("transport", std::move(transport));

    JsonValue tracks = JsonValue::makeArray();
    for (const auto& track : document.tracks) {
        JsonValue entry = JsonValue::makeObject();
        entry.set("name", JsonValue::makeString(track.name));
        entry.set("channel", JsonValue::makeNumber(track.channel));
        entry.set("color", JsonValue::makeString(colourToHex(track.colorRgba)));

        JsonValue instrument = JsonValue::makeObject();
        instrument.set("preferredPlugin", JsonValue::makeString(track.preferredPlugin));
        instrument.set("preset", JsonValue::makeString(track.presetPath));
        entry.set("instrument", std::move(instrument));

        JsonValue mix = JsonValue::makeObject();
        mix.set("volume", JsonValue::makeFloat(track.volume));
        mix.set("pan", JsonValue::makeFloat(track.pan));
        mix.set("muted", JsonValue::makeBoolean(track.muted));
        mix.set("solo", JsonValue::makeBoolean(track.solo));
        JsonValue sends = JsonValue::makeArray();
        for (float level : track.sendLevels) sends.append(JsonValue::makeFloat(level));
        mix.set("sends", std::move(sends));
        entry.set("mix", std::move(mix));

        JsonValue effects = JsonValue::makeArray();
        for (const auto& effect : track.effects) {
            JsonValue fx = JsonValue::makeObject();
            fx.set("type", JsonValue::makeString(effect.type));
            JsonValue parameters = JsonValue::makeObject();
            for (const auto& [semanticId, value] : effect.parameters)
                parameters.set(semanticId, JsonValue::makeFloat(value));
            fx.set("parameters", std::move(parameters));
            effects.append(std::move(fx));
        }
        entry.set("effects", std::move(effects));

        tracks.append(std::move(entry));
    }
    root.set("tracks", std::move(tracks));
    return root;
}

ProjectLoadResult projectDocumentFromJson(const JsonValue& json) {
    ProjectLoadResult result;
    if (!json.isObject()) { result.error = "racine JSON : objet attendu"; return result; }

    const std::string format = json["format"].asString();
    if (format != kProjectFormat) {
        result.error = "format inattendu : \"" + format + "\" (attendu \"" + kProjectFormat + "\")";
        return result;
    }
    const int version = static_cast<int>(json["version"].asNumber(-1.0));
    if (version != kProjectVersion) {
        result.error = "version de projet non prise en charge : " + std::to_string(version)
                     + " (cette version du logiciel lit la " + std::to_string(kProjectVersion) + ")";
        return result;
    }

    ProjectDocument document;
    document.title = json["title"].asString("Sans titre");
    document.midiPath = json["midi"]["file"].asString("midi/arrangement.mid");
    if (!isPortableRelativePath(document.midiPath)) {
        result.error = "chemin MIDI non portable : \"" + document.midiPath
                     + "\" (un projet doit s'ouvrir sur une autre machine : chemins relatifs, séparateurs /)";
        return result;
    }

    const JsonValue& transport = json["transport"];
    document.transport.ticksPerQuarterNote = static_cast<int>(transport["ticksPerQuarterNote"].asNumber(480.0));
    if (document.transport.ticksPerQuarterNote <= 0) {
        result.error = "ticksPerQuarterNote invalide";
        return result;
    }
    for (const auto& entry : transport["tempoChanges"].elements()) {
        ProjectTempoChange change;
        change.tick = static_cast<int64_t>(entry["tick"].asNumber(0.0));
        change.bpm = entry["bpm"].asNumber(120.0);
        document.transport.tempoChanges.push_back(change);
    }
    for (const auto& entry : transport["timeSignatures"].elements()) {
        ProjectTimeSignature change;
        change.tick = static_cast<int64_t>(entry["tick"].asNumber(0.0));
        change.numerator = static_cast<int>(entry["numerator"].asNumber(4.0));
        change.denominator = static_cast<int>(entry["denominator"].asNumber(4.0));
        document.transport.timeSignatures.push_back(change);
    }
    const JsonValue& loop = transport["loop"];
    document.transport.loopEnabled = loop["enabled"].asBoolean(false);
    document.transport.loopStartTick = static_cast<int64_t>(loop["startTick"].asNumber(0.0));
    document.transport.loopEndTick = static_cast<int64_t>(loop["endTick"].asNumber(0.0));

    for (const auto& entry : json["tracks"].elements()) {
        ProjectTrack track;
        track.name = entry["name"].asString();
        track.channel = static_cast<int>(entry["channel"].asNumber(0.0));
        track.colorRgba = colourFromHex(entry["color"].asString(), 0xFF6B9BFFu);
        track.preferredPlugin = entry["instrument"]["preferredPlugin"].asString();
        track.presetPath = entry["instrument"]["preset"].asString();
        if (!isPortableRelativePath(track.presetPath)) {
            result.error = "chemin de preset non portable : \"" + track.presetPath + "\"";
            return result;
        }

        const JsonValue& mix = entry["mix"];
        track.volume = static_cast<float>(mix["volume"].asNumber(1.0));
        track.pan = static_cast<float>(mix["pan"].asNumber(0.0));
        track.muted = mix["muted"].asBoolean(false);
        track.solo = mix["solo"].asBoolean(false);
        for (size_t i = 0; i < track.sendLevels.size() && i < mix["sends"].size(); ++i)
            track.sendLevels[i] = static_cast<float>(mix["sends"].at(i).asNumber(0.0));

        for (const auto& fx : entry["effects"].elements()) {
            ProjectEffect effect;
            effect.type = fx["type"].asString();
            for (const auto& [semanticId, value] : fx["parameters"].members())
                if (value.isNumber()) effect.parameters[semanticId] = static_cast<float>(value.asNumber());
            if (!effect.type.empty()) track.effects.push_back(std::move(effect));
        }
        document.tracks.push_back(std::move(track));
    }

    result.success = true;
    result.document = std::move(document);
    return result;
}

ProjectLoadResult parseProjectDocument(const std::string& jsonText) {
    ProjectLoadResult result;
    JsonParseResult parsed = parseJson(jsonText);
    if (!parsed.success) {
        result.error = "JSON invalide : " + parsed.error + " (position " + std::to_string(parsed.errorOffset) + ")";
        return result;
    }
    return projectDocumentFromJson(parsed.value);
}

} // namespace vsm::interchange
