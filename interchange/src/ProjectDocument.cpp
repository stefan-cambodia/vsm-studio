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

/// Un clip en JSON. EXTRAIT parce que les prises en portent aussi : deux
/// écritures d'un même objet finiraient par diverger, et un clip de prise qui
/// perdrait son fondu ne se remarquerait qu'à la relecture.
JsonValue clipToJson(const ProjectClip& clip) {
    JsonValue c = JsonValue::makeObject();
    c.set("sourceStart", JsonValue::makeNumber(static_cast<double>(clip.sourceStart)));
    c.set("sourceLength", JsonValue::makeNumber(static_cast<double>(clip.sourceLength)));
    c.set("start", JsonValue::makeNumber(static_cast<double>(clip.startTick)));
    c.set("length", JsonValue::makeNumber(static_cast<double>(clip.length)));
    if (clip.muted) c.set("muted", JsonValue::makeBoolean(true));
    if (!clip.name.empty()) c.set("name", JsonValue::makeString(clip.name));
    c.set("color", JsonValue::makeString(colourToHex(clip.colorRgba)));
    // Écrits seulement s'ils disent quelque chose : un clip MIDI ne porte pas
    // de fenêtre en secondes ni de fondu.
    if (clip.sourceStartSeconds != 0.0)
        c.set("sourceStartSeconds", JsonValue::makeFloat(static_cast<float>(clip.sourceStartSeconds)));
    if (clip.fadeInSeconds != 0.0)
        c.set("fadeIn", JsonValue::makeFloat(static_cast<float>(clip.fadeInSeconds)));
    if (clip.fadeOutSeconds != 0.0)
        c.set("fadeOut", JsonValue::makeFloat(static_cast<float>(clip.fadeOutSeconds)));
    // D17.1 : la forme n'est écrite que si elle n'est pas la droite -- un
    // projet d'avant D17.1 se réécrit octet pour octet.
    if (!clip.fadeShape.empty()) c.set("fadeShape", JsonValue::makeString(clip.fadeShape));
    if (clip.gain != 1.0f) c.set("gain", JsonValue::makeFloat(clip.gain));
    if (clip.invertPhase) c.set("invertPhase", JsonValue::makeBoolean(true));
    // LE SUIVI DE TEMPO (D12), écrit seulement s'il est allumé : c'est ce qui
    // laisse un projet sans étirement en version 2.
    if (clip.warpMode != 0) {
        c.set("warp", JsonValue::makeString(clip.warpMode == 2 ? "repitch"
                                            : clip.warpMode == 3 ? "keepPitchWsola" : "keepPitch"));
        JsonValue marqueurs = JsonValue::makeArray();
        for (const auto& [secondes, tick] : clip.warpMarkers) {
            JsonValue m = JsonValue::makeObject();
            m.set("seconds", JsonValue::makeFloat(secondes));
            m.set("tick", JsonValue::makeNumber(static_cast<double>(tick)));
            marqueurs.append(std::move(m));
        }
        c.set("warpMarkers", std::move(marqueurs));
    }
    if (clip.reversed) c.set("reversed", JsonValue::makeBoolean(true));
    return c;
}

/// Un clip DEPUIS le JSON. Même raison que ci-dessus.
ProjectClip clipFromJson(const JsonValue& clipJson) {
    ProjectClip clip;
    clip.sourceStart = static_cast<int64_t>(clipJson["sourceStart"].asNumber(0.0));
    clip.sourceLength = static_cast<int64_t>(clipJson["sourceLength"].asNumber(0.0));
    clip.startTick = static_cast<int64_t>(clipJson["start"].asNumber(0.0));
    clip.length = static_cast<int64_t>(clipJson["length"].asNumber(0.0));
    clip.muted = clipJson["muted"].asBoolean(false);
    clip.name = clipJson["name"].asString();
    clip.colorRgba = colourFromHex(clipJson["color"].asString(), 0xFF6B9BFFu);
    clip.sourceStartSeconds = clipJson["sourceStartSeconds"].asNumber(0.0);
    clip.fadeInSeconds = clipJson["fadeIn"].asNumber(0.0);
    clip.fadeOutSeconds = clipJson["fadeOut"].asNumber(0.0);
    clip.fadeShape = clipJson["fadeShape"].asString();
    clip.gain = static_cast<float>(clipJson["gain"].asNumber(1.0));
    clip.invertPhase = clipJson["invertPhase"].asBoolean(false);
    const std::string warp = clipJson["warp"].asString();
    clip.warpMode = warp == "keepPitch" ? 1 : warp == "repitch" ? 2 : warp == "keepPitchWsola" ? 3 : 0;
    for (const auto& m : clipJson["warpMarkers"].elements())
        clip.warpMarkers.emplace_back(m["seconds"].asNumber(0.0),
                                      static_cast<int64_t>(m["tick"].asNumber(0.0)));
    clip.reversed = clipJson["reversed"].asBoolean(false);
    return clip;
}

/// Un projet a-t-il un clip qui suit le tempo ? C'est ce qui décide de la
/// version écrite.
bool usesWarp(const ProjectDocument& document) {
    for (const auto& track : document.tracks) {
        for (const auto& clip : track.clips) if (clip.warpMode != 0 || clip.reversed) return true;
        for (const auto& take : track.takes)
            for (const auto& clip : take.clips) if (clip.warpMode != 0 || clip.reversed) return true;
    }
    return false;
}

/// Un clip du MODÈLE vers le document, et retour. Un seul endroit pour les
/// deux sens : un champ ajouté au clip (le suivi de tempo, D12) se recopie
/// ici, pas dans quatre agrégats positionnels.
ProjectClip clipToDocument(const vsm::sequencer::Clip& clip) {
    ProjectClip c{clip.sourceStart, clip.sourceLength, clip.startTick,
                  clip.length, clip.muted, clip.name, clip.colorRgba,
                  clip.sourceStartSeconds, clip.fadeInSeconds,
                  clip.fadeOutSeconds, clip.gain, clip.invertPhase, 0, {}};
    c.warpMode = static_cast<int>(clip.warpMode);
    for (const auto& m : clip.warpMarkers) c.warpMarkers.emplace_back(m.sourceSeconds, m.tick);
    c.reversed = clip.reversed;
    c.fadeShape = clip.fadeShape == vsm::sequencer::FadeShape::EqualPower ? "equalPower"
                : clip.fadeShape == vsm::sequencer::FadeShape::Slow       ? "slow"
                : clip.fadeShape == vsm::sequencer::FadeShape::Fast       ? "fast"
                                                                          : "";
    return c;
}
vsm::sequencer::Clip clipToModel(const ProjectClip& clip) {
    vsm::sequencer::Clip c{clip.sourceStart, clip.sourceLength, clip.startTick,
                           clip.length, clip.muted, clip.name, clip.colorRgba,
                           clip.sourceStartSeconds, clip.fadeInSeconds,
                           clip.fadeOutSeconds, clip.gain, clip.invertPhase,
                           vsm::sequencer::WarpMode::Off, {}, 0};
    c.warpMode = clip.warpMode == 1 ? vsm::sequencer::WarpMode::KeepPitch
               : clip.warpMode == 2 ? vsm::sequencer::WarpMode::Repitch
               : clip.warpMode == 3 ? vsm::sequencer::WarpMode::KeepPitchWsola
                                    : vsm::sequencer::WarpMode::Off;
    for (const auto& [secondes, tick] : clip.warpMarkers) c.warpMarkers.push_back({secondes, tick});
    c.reversed = clip.reversed;
    c.fadeShape = clip.fadeShape == "equalPower" ? vsm::sequencer::FadeShape::EqualPower
                : clip.fadeShape == "slow"       ? vsm::sequencer::FadeShape::Slow
                : clip.fadeShape == "fast"       ? vsm::sequencer::FadeShape::Fast
                                                 : vsm::sequencer::FadeShape::Linear;
    return c;
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
                                                    bpmFromMicroseconds(change.microsecondsPerQuarterNote),
                                                    change.rampToNext});
    for (const auto& change : project.timeSignatureMap.changes())
        document.transport.timeSignatures.push_back({change.tick, change.numerator,
                                                      1 << change.denominatorPow2});
    for (const auto& marker : project.markers)
        document.markers.push_back({marker.tick, marker.name});
    document.master = project.masterParameters;
    document.transport.loopEnabled = project.loopEnabled;
    document.transport.loopStartTick = project.loopStartTick;
    document.transport.loopEndTick = project.loopEndTick;
    for (const auto& bus : project.sends)
        document.sends.push_back({bus.name, bus.effectType, bus.parameters, bus.returnGain,
                                   bus.preFader});
    document.transport.punchEnabled = project.punchEnabled;
    document.transport.punchStartTick = project.punchStartTick;
    document.transport.punchEndTick = project.punchEndTick;

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

        // Effets et automation : simple recopie, les deux modèles ayant
        // désormais la même forme. Tant que `Track` ne les portait pas, ces
        // deux boucles n'existaient pas -- et le format écrivait donc des
        // tableaux vides sans que personne ne s'en aperçoive.
        for (const auto& effect : track.effects) {
            ProjectEffect described;
            described.type = effect.type;
            described.parameters = effect.parameters;
            described.nativeState = effect.nativeState;
            described.enabled = effect.enabled;
            entry.effects.push_back(std::move(described));
        }
        entry.outputGroup = track.outputGroup;
        entry.arrangementHeight = track.arrangementHeight;
        entry.folded = track.folded;
        entry.frozen = track.frozen;
        entry.locked = track.locked;
        entry.hidden = track.hidden;
        entry.delayMs = track.delayMs;
        entry.automationMode = track.automationMode == vsm::sequencer::AutomationMode::Touch ? "touch"
                             : track.automationMode == vsm::sequencer::AutomationMode::Latch ? "latch"
                                                                                        : "";
        entry.frozenAudio.path = track.frozenAudio.path;
        entry.frozenAudio.sampleRate = track.frozenAudio.sampleRate;
        entry.frozenAudio.frames = track.frozenAudio.frames;
        entry.frozenAudio.channels = track.frozenAudio.channels;
        if (track.kind == vsm::sequencer::Track::Kind::Group) {
            entry.kind = "group";
        } else if (track.kind == vsm::sequencer::Track::Kind::Audio) {
            entry.kind = "audio";
            entry.audio = {track.audio.path, track.audio.sampleRate,
                            track.audio.frames, track.audio.channels};
        }
        for (const auto& clip : track.clips)
            entry.clips.push_back(clipToDocument(clip));
        for (const auto& curve : track.automation) {
            ProjectAutomationLane lane;
            lane.parameter = curve.parameter;
            for (const auto& point : curve.points)
                lane.points.push_back({point.tick, point.value, point.step});
            entry.automation.push_back(std::move(lane));
        }

        // LES PRISES. Leurs NOTES ne sont pas décrites ici -- elles vont dans
        // `midi/prises.mid`, comme celles de l'arrangement vont dans son propre
        // `.mid` : c'est la règle du format, et `midiTrackIndex` est rempli par
        // l'écrivain du dossier, qui seul sait combien de pistes ce fichier
        // porte déjà.
        for (const auto& take : track.takes) {
            ProjectTake described;
            described.name = take.name;
            described.startTick = take.startTick;
            described.endTick = take.endTick;
            // Champ par champ plutôt qu'un temporaire entre accolades : le
            // compilateur ne voyait pas que la chaîne du temporaire était
            // initialisée et prévenait d'une lecture douteuse. Une construction
            // agrégée qui inquiète le compilateur n'est pas plus lisible qu'une
            // affectation qui ne l'inquiète pas.
            described.audio.path = take.audio.path;
            described.audio.sampleRate = take.audio.sampleRate;
            described.audio.frames = take.audio.frames;
            described.audio.channels = take.audio.channels;
            for (const auto& clip : take.clips)
                described.clips.push_back(clipToDocument(clip));
            entry.takes.push_back(std::move(described));
        }
        entry.activeTake = track.activeTake;

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
            project.tempoMap.addTempoChange(change.tick, microsecondsFromBpm(change.bpm), change.ramp);
    }
    if (!document.transport.timeSignatures.empty()) {
        project.timeSignatureMap.clear();
        for (const auto& change : document.transport.timeSignatures)
            project.timeSignatureMap.addChange(change.tick, static_cast<uint8_t>(change.numerator),
                                                denominatorToPow2(change.denominator));
    }

    if (!document.markers.empty()) {
        project.markers.clear();
        for (const auto& marker : document.markers)
            project.markers.push_back({marker.tick, marker.name});
    }
    if (!document.master.empty()) project.masterParameters = document.master;
    project.loopEnabled = document.transport.loopEnabled;
    project.loopStartTick = document.transport.loopStartTick;
    project.loopEndTick = document.transport.loopEndTick;
    project.sends.clear();
    for (const auto& bus : document.sends)
        project.sends.push_back({bus.name, bus.effectType, bus.parameters, bus.returnGain,
                                  bus.preFader});
    project.punchEnabled = document.transport.punchEnabled;
    project.punchStartTick = document.transport.punchStartTick;
    project.punchEndTick = document.transport.punchEndTick;

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

        // AVANT le `continue` ci-dessous, délibérément : une piste sans
        // instrument peut parfaitement porter des effets et de l'automation,
        // et les lui retirer au chargement serait une perte silencieuse.
        target.effects.clear();
        for (const auto& effect : source.effects)
            target.effects.push_back({effect.type, effect.parameters, effect.nativeState, effect.enabled});
        target.kind = source.kind == "audio"  ? Track::Kind::Audio
                    : source.kind == "group"  ? Track::Kind::Group
                                               : Track::Kind::Midi;
        target.outputGroup = source.outputGroup;
        target.arrangementHeight = source.arrangementHeight;
        target.folded = source.folded;
        target.frozen = source.frozen;
        target.locked = source.locked;
        target.hidden = source.hidden;
        target.delayMs = source.delayMs;
        target.automationMode = source.automationMode == "touch" ? vsm::sequencer::AutomationMode::Touch
                              : source.automationMode == "latch" ? vsm::sequencer::AutomationMode::Latch
                                                                 : vsm::sequencer::AutomationMode::Off;
        target.frozenAudio = {source.frozenAudio.path, source.frozenAudio.sampleRate,
                               source.frozenAudio.frames, source.frozenAudio.channels};
        target.audio = {source.audio.path, source.audio.sampleRate,
                         source.audio.frames, source.audio.channels};
        // Les prises, SANS leurs notes : celles-ci viennent de `prises.mid` et
        // sont recollées par le lecteur de dossier, qui est le seul à l'avoir lu.
        target.takes.clear();
        for (const auto& take : source.takes) {
            vsm::sequencer::Take restauree;
            restauree.name = take.name;
            restauree.startTick = take.startTick;
            restauree.endTick = take.endTick;
            restauree.audio.path = take.audio.path;
            restauree.audio.sampleRate = take.audio.sampleRate;
            restauree.audio.frames = take.audio.frames;
            restauree.audio.channels = take.audio.channels;
            for (const auto& clip : take.clips)
                restauree.clips.push_back(clipToModel(clip));
            target.takes.push_back(std::move(restauree));
        }
        target.activeTake = source.activeTake < static_cast<int>(target.takes.size())
                                ? source.activeTake : -1;

        target.clips.clear();
        for (const auto& clip : source.clips)
            target.clips.push_back(clipToModel(clip));
        target.automation.clear();
        for (const auto& lane : source.automation) {
            vsm::sequencer::AutomationCurve curve;
            curve.parameter = lane.parameter;
            for (const auto& point : lane.points)
                curve.points.push_back({point.tick, point.value, point.step});
            target.automation.push_back(std::move(curve));
        }

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
    root.set("version", JsonValue::makeNumber(usesWarp(document) ? kProjectVersion
                                                                  : kProjectVersionWithoutWarp));
    root.set("title", JsonValue::makeString(document.title));

    // Écrit SEULEMENT s'il y a quelque chose à écrire : un projet sans réglage
    // de master garde le fichier qu'il a toujours eu, octet pour octet.
    if (!document.markers.empty()) {
        JsonValue markers = JsonValue::makeArray();
        for (const auto& marker : document.markers) {
            JsonValue m = JsonValue::makeObject();
            m.set("tick", JsonValue::makeNumber(static_cast<double>(marker.tick)));
            m.set("name", JsonValue::makeString(marker.name));
            markers.append(std::move(m));
        }
        root.set("markers", std::move(markers));
    }

    if (!document.master.empty()) {
        JsonValue master = JsonValue::makeObject();
        for (const auto& [name, value] : document.master)
            master.set(name, JsonValue::makeFloat(value));
        root.set("master", std::move(master));
    }

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
        if (change.ramp) entry.set("ramp", JsonValue::makeBoolean(true));   // D15.5, écrit seulement si vrai
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
    
    // RÉGION DE PUNCH : écrite SEULEMENT si elle dit quelque chose, comme les
    // clips et l'automation -- un projet qui n'en a pas garde exactement le
    // fichier qu'il avait avant que le punch existe.
    if (document.transport.punchEnabled || document.transport.punchEndTick > 0) {
        JsonValue punch = JsonValue::makeObject();
        punch.set("enabled", JsonValue::makeBoolean(document.transport.punchEnabled));
        punch.set("startTick", JsonValue::makeNumber(static_cast<double>(document.transport.punchStartTick)));
        punch.set("endTick", JsonValue::makeNumber(static_cast<double>(document.transport.punchEndTick)));
        transport.set("punch", std::move(punch));
    }
    transport.set("loop", std::move(loop));
    root.set("transport", std::move(transport));

    // LES BUS DE DÉPART, écrits seulement s'il y en a : un projet qui n'en
    // déclare pas garde le fichier qu'il avait avant qu'ils soient nommés.
    if (!document.sends.empty()) {
        JsonValue sends = JsonValue::makeArray();
        for (const auto& bus : document.sends) {
            JsonValue b = JsonValue::makeObject();
            b.set("name", JsonValue::makeString(bus.name));
            b.set("effect", JsonValue::makeString(bus.effectType));
            b.set("returnGain", JsonValue::makeFloat(bus.returnGain));
            // Écrit seulement quand il dit quelque chose : post-fader est le
            // défaut, et c'était le seul comportement possible avant D4.3.
            if (bus.preFader) b.set("preFader", JsonValue::makeBoolean(true));
            JsonValue params = JsonValue::makeObject();
            for (const auto& [semanticId, value] : bus.parameters)
                params.set(semanticId, JsonValue::makeFloat(value));
            b.set("parameters", std::move(params));
            sends.append(std::move(b));
        }
        root.set("sends", std::move(sends));
    }

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

        // Le routage n'est écrit que s'il dit quelque chose : une piste qui va
        // au master garde le fichier qu'elle avait avant les groupes.
        if (track.outputGroup >= 0)
            entry.set("output", JsonValue::makeNumber(static_cast<double>(track.outputGroup)));
        // Écrits seulement s'ils disent quelque chose : une piste à la hauteur
        // standard et dépliée garde le fichier qu'elle avait.
        if (track.arrangementHeight != 56)
            entry.set("height", JsonValue::makeNumber(static_cast<double>(track.arrangementHeight)));
        if (track.folded) entry.set("folded", JsonValue::makeBoolean(true));
        // D16.5 : LE VERROU, écrit seulement quand il est mis, et INDÉPENDANT
        // du gel -- ce sont deux choses sans rapport (l'un est une affaire de
        // CPU, l'autre de montage), et l'imbriquer dans le second aurait fait
        // perdre le premier sur toute piste non gelée.
        if (track.locked) entry.set("locked", JsonValue::makeBoolean(true));
        if (track.hidden) entry.set("hidden", JsonValue::makeBoolean(true));
        if (track.delayMs != 0.0) entry.set("delayMs", JsonValue::makeFloat(track.delayMs));
        if (!track.automationMode.empty())
            entry.set("automationMode", JsonValue::makeString(track.automationMode));
        if (track.frozen || !track.frozenAudio.path.empty()) {
            entry.set("frozen", JsonValue::makeBoolean(track.frozen));
            JsonValue gel = JsonValue::makeObject();
            gel.set("file", JsonValue::makeString(track.frozenAudio.path));
            gel.set("sampleRate", JsonValue::makeNumber(track.frozenAudio.sampleRate));
            gel.set("frames", JsonValue::makeNumber(static_cast<double>(track.frozenAudio.frames)));
            gel.set("channels", JsonValue::makeNumber(track.frozenAudio.channels));
            entry.set("frozenAudio", std::move(gel));
        }
        if (!track.kind.empty() && track.kind != "midi") {
            entry.set("kind", JsonValue::makeString(track.kind));
            JsonValue source = JsonValue::makeObject();
            source.set("file", JsonValue::makeString(track.audio.path));
            source.set("sampleRate", JsonValue::makeNumber(track.audio.sampleRate));
            source.set("frames", JsonValue::makeNumber(static_cast<double>(track.audio.frames)));
            source.set("channels", JsonValue::makeNumber(track.audio.channels));
            entry.set("audio", std::move(source));
        }

        JsonValue effects = JsonValue::makeArray();
        for (const auto& effect : track.effects) {
            JsonValue fx = JsonValue::makeObject();
            fx.set("type", JsonValue::makeString(effect.type));
            JsonValue parameters = JsonValue::makeObject();
            for (const auto& [semanticId, value] : effect.parameters)
                parameters.set(semanticId, JsonValue::makeFloat(value));
            fx.set("parameters", std::move(parameters));
            // ÉCRIT SEULEMENT S'IL EXISTE : c'est le seul champ illisible du
            // fichier, et un projet sans plugin tiers n'a aucune raison d'en
            // porter la trace.
            if (!effect.nativeState.empty())
                fx.set("nativeState", JsonValue::makeString(effect.nativeState));
            // Contourné : dit seulement quand c'est le cas (D15.1) ; un projet
            // dont tous les inserts sont actifs garde exactement son fichier.
            if (!effect.enabled) fx.set("enabled", JsonValue::makeBoolean(false));
            effects.append(std::move(fx));
        }
        entry.set("effects", std::move(effects));

        // Écrits SEULEMENT s'il y en a : une piste non découpée garde le
        // fichier qu'elle a toujours eu.
        if (!track.clips.empty()) {
            JsonValue clips = JsonValue::makeArray();
            for (const auto& clip : track.clips) clips.append(clipToJson(clip));
            entry.set("clips", std::move(clips));
        }

        // LES PRISES, écrites seulement s'il y en a. Leurs NOTES ne sont pas
        // ici : elles sont dans `midi/prises.mid`, et `midiTrack` dit laquelle
        // de ses pistes porte celles-ci.
        if (!track.takes.empty()) {
            JsonValue takes = JsonValue::makeArray();
            for (const auto& take : track.takes) {
                JsonValue t = JsonValue::makeObject();
                t.set("name", JsonValue::makeString(take.name));
                t.set("startTick", JsonValue::makeNumber(static_cast<double>(take.startTick)));
                t.set("endTick", JsonValue::makeNumber(static_cast<double>(take.endTick)));
                t.set("midiTrack", JsonValue::makeNumber(static_cast<double>(take.midiTrackIndex)));
                if (!take.audio.path.empty()) {
                    JsonValue source = JsonValue::makeObject();
                    source.set("file", JsonValue::makeString(take.audio.path));
                    source.set("sampleRate", JsonValue::makeNumber(take.audio.sampleRate));
                    source.set("frames", JsonValue::makeNumber(static_cast<double>(take.audio.frames)));
                    source.set("channels", JsonValue::makeNumber(take.audio.channels));
                    t.set("audio", std::move(source));
                }
                if (!take.clips.empty()) {
                    JsonValue clips = JsonValue::makeArray();
                    for (const auto& clip : take.clips) clips.append(clipToJson(clip));
                    t.set("clips", std::move(clips));
                }
                takes.append(std::move(t));
            }
            entry.set("takes", std::move(takes));
            entry.set("activeTake", JsonValue::makeNumber(static_cast<double>(track.activeTake)));
        }

        // L'automation n'est écrite QUE si elle existe : un projet sans
        // automation garde exactement le fichier qu'il a toujours eu.
        if (!track.automation.empty()) {
            JsonValue lanes = JsonValue::makeArray();
            for (const auto& lane : track.automation) {
                JsonValue entree = JsonValue::makeObject();
                entree.set("parameter", JsonValue::makeString(lane.parameter));
                JsonValue points = JsonValue::makeArray();
                for (const auto& point : lane.points) {
                    JsonValue p = JsonValue::makeObject();
                    p.set("tick", JsonValue::makeFloat(static_cast<double>(point.tick)));
                    p.set("value", JsonValue::makeFloat(point.value));
                    if (point.step) p.set("step", JsonValue::makeBoolean(true));
                    points.append(std::move(p));
                }
                entree.set("points", std::move(points));
                lanes.append(std::move(entree));
            }
            entry.set("automation", std::move(lanes));
        }

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
    if (version < kOldestReadableProjectVersion || version > kProjectVersion) {
        result.error = "version de projet non prise en charge : " + std::to_string(version)
                     + " (cette version du logiciel lit de la "
                     + std::to_string(kOldestReadableProjectVersion) + " à la "
                     + std::to_string(kProjectVersion) + ")";
        return result;
    }

    ProjectDocument document;
    document.title = json["title"].asString("Sans titre");
    for (const auto& markerJson : json["markers"].elements()) {
        ProjectMarker marker;
        marker.tick = static_cast<int64_t>(markerJson["tick"].asNumber(0.0));
        marker.name = markerJson["name"].asString();
        // Un repère sans nom ne repère rien : filtré à la lecture plutôt que
        // traîné jusqu'à l'écran.
        if (!marker.name.empty()) document.markers.push_back(std::move(marker));
    }
    for (const auto& [name, value] : json["master"].members())
        if (value.isNumber()) document.master[name] = static_cast<float>(value.asNumber());
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
        change.ramp = entry["ramp"].asBoolean(false);
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

    const JsonValue& punch = transport["punch"];
    document.transport.punchEnabled = punch["enabled"].asBoolean(false);
    document.transport.punchStartTick = static_cast<int64_t>(punch["startTick"].asNumber(0.0));
    document.transport.punchEndTick = static_cast<int64_t>(punch["endTick"].asNumber(0.0));

    for (const auto& busJson : json["sends"].elements()) {
        ProjectSendBus bus;
        bus.name = busJson["name"].asString();
        bus.effectType = busJson["effect"].asString();
        bus.returnGain = static_cast<float>(busJson["returnGain"].asNumber(1.0));
        bus.preFader = busJson["preFader"].asBoolean(false);
        for (const auto& [semanticId, value] : busJson["parameters"].members())
            if (value.isNumber()) bus.parameters[semanticId] = static_cast<float>(value.asNumber());
        document.sends.push_back(std::move(bus));
    }

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
        // LA TAILLE VIENT DU FICHIER, et c'est un piège qu'a tendu le passage
        // du tableau de deux au vecteur : borner la boucle par la taille du
        // VECTEUR, qui part vide, ne lisait plus rien du tout et faisait
        // disparaître tous les niveaux d'envoi en silence.
        track.sendLevels.assign(mix["sends"].size(), 0.0f);
        for (size_t i = 0; i < track.sendLevels.size(); ++i)
            track.sendLevels[i] = static_cast<float>(mix["sends"].at(i).asNumber(0.0));

        track.kind = entry["kind"].asString();
        track.outputGroup = static_cast<int>(entry["output"].asNumber(-1.0));
        track.arrangementHeight = static_cast<int>(entry["height"].asNumber(56.0));
        track.folded = entry["folded"].asBoolean(false);
        track.frozen = entry["frozen"].asBoolean(false);
        track.locked = entry["locked"].asBoolean(false);
        track.hidden = entry["hidden"].asBoolean(false);
        track.delayMs = entry["delayMs"].asNumber(0.0);
        track.automationMode = entry["automationMode"].asString();
        if (entry["frozenAudio"].isObject()) {
            track.frozenAudio.path = entry["frozenAudio"]["file"].asString();
            // MÊME RÈGLE QUE PARTOUT : un chemin absolu est refusé, jamais
            // réécrit -- c'est ce qui permet d'ouvrir le projet ailleurs.
            if (!isPortableRelativePath(track.frozenAudio.path)) {
                result.error = "chemin de gel non portable : \"" + track.frozenAudio.path + "\"";
                return result;
            }
            track.frozenAudio.sampleRate = entry["frozenAudio"]["sampleRate"].asNumber(0.0);
            track.frozenAudio.frames = static_cast<int64_t>(entry["frozenAudio"]["frames"].asNumber(0.0));
            track.frozenAudio.channels = static_cast<int>(entry["frozenAudio"]["channels"].asNumber(0.0));
        }
        if (entry["audio"].isObject()) {
            track.audio.path = entry["audio"]["file"].asString();
            // MÊME RÈGLE QUE POUR LES PRESETS, et pour la même raison : un
            // projet doit s'ouvrir sur une autre machine. Un chemin absolu est
            // refusé, jamais réécrit en douce -- réécrire reviendrait à
            // deviner où le fichier a bien pu partir.
            if (!isPortableRelativePath(track.audio.path)) {
                result.error = "chemin audio non portable : \"" + track.audio.path + "\"";
                return result;
            }
            track.audio.sampleRate = entry["audio"]["sampleRate"].asNumber(0.0);
            track.audio.frames = static_cast<int64_t>(entry["audio"]["frames"].asNumber(0.0));
            track.audio.channels = static_cast<int>(entry["audio"]["channels"].asNumber(0.0));
        }

        for (const auto& fx : entry["effects"].elements()) {
            ProjectEffect effect;
            effect.type = fx["type"].asString();
            for (const auto& [semanticId, value] : fx["parameters"].members())
                if (value.isNumber()) effect.parameters[semanticId] = static_cast<float>(value.asNumber());
            if (fx["nativeState"].isString()) effect.nativeState = fx["nativeState"].asString();
            if (fx["enabled"].isBoolean()) effect.enabled = fx["enabled"].asBoolean(true);
            if (!effect.type.empty()) track.effects.push_back(std::move(effect));
        }

        for (const auto& clipJson : entry["clips"].elements())
            track.clips.push_back(clipFromJson(clipJson));

        // LES PRISES, sans leurs notes : celles-ci sont dans `midi/prises.mid`
        // et sont recollées par le lecteur de dossier, seul à l'avoir ouvert.
        for (const auto& takeJson : entry["takes"].elements()) {
            ProjectTake take;
            take.name = takeJson["name"].asString();
            take.startTick = static_cast<int64_t>(takeJson["startTick"].asNumber(0.0));
            take.endTick = static_cast<int64_t>(takeJson["endTick"].asNumber(0.0));
            take.midiTrackIndex = static_cast<int>(takeJson["midiTrack"].asNumber(-1.0));
            if (takeJson["audio"].isObject()) {
                take.audio.path = takeJson["audio"]["file"].asString();
                // MÊME RÈGLE QUE PARTOUT : un chemin absolu est refusé, jamais
                // réécrit -- c'est ce qui permet d'ouvrir le projet ailleurs.
                if (!isPortableRelativePath(take.audio.path)) {
                    result.error = "chemin de prise non portable : \"" + take.audio.path + "\"";
                    return result;
                }
                take.audio.sampleRate = takeJson["audio"]["sampleRate"].asNumber(0.0);
                take.audio.frames = static_cast<int64_t>(takeJson["audio"]["frames"].asNumber(0.0));
                take.audio.channels = static_cast<int>(takeJson["audio"]["channels"].asNumber(0.0));
            }
            for (const auto& clipJson : takeJson["clips"].elements())
                take.clips.push_back(clipFromJson(clipJson));
            track.takes.push_back(std::move(take));
        }
        track.activeTake = static_cast<int>(entry["activeTake"].asNumber(-1.0));
        if (track.activeTake >= static_cast<int>(track.takes.size())) track.activeTake = -1;

        for (const auto& laneJson : entry["automation"].elements()) {
            ProjectAutomationLane lane;
            lane.parameter = laneJson["parameter"].asString();
            for (const auto& pointJson : laneJson["points"].elements()) {
                ProjectAutomationPoint point;
                point.tick = static_cast<int64_t>(pointJson["tick"].asNumber(0.0));
                point.value = static_cast<float>(pointJson["value"].asNumber(0.0));
                point.step = pointJson["step"].asBoolean(false);
                lane.points.push_back(point);
            }
            // Une courbe sans cible ou sans point ne commande rien : refusée
            // à la lecture plutôt que traînée jusqu'au rendu.
            if (!lane.parameter.empty() && !lane.points.empty())
                track.automation.push_back(std::move(lane));
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
