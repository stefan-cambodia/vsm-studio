#include "vsm/interchange/ProjectBundle.h"
#include "vsm/midi/MidiFileParser.h"
#include "vsm/midi/MidiFileWriter.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

namespace vsm::interchange {

namespace fs = std::filesystem;
using vsm::sequencer::Project;

namespace {

/// Assemble un chemin de fichier à partir du dossier de projet et d'un chemin
/// RELATIF du document. `fs::path` s'occupe du séparateur natif : le format
/// impose `/`, le système de fichiers reçoit ce qu'il attend.
fs::path resolve(const std::string& folderPath, const std::string& relative) {
    fs::path path(folderPath);
    for (const auto& part : fs::path(relative)) path /= part;
    return path;
}

std::string presetPathForTrack(size_t index) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%s/track_%02zu.synth.json", kInstrumentsFolder, index);
    return buffer;
}

} // namespace

bool readTextFile(const std::string& path, std::string& outText, std::string& outError) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) { outError = "impossible d'ouvrir : " + path; return false; }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    outText = buffer.str();
    return true;
}

bool writeTextFile(const std::string& path, const std::string& text, std::string& outError) {
    std::error_code code;
    const fs::path parent = fs::path(path).parent_path();
    if (!parent.empty()) fs::create_directories(parent, code);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) { outError = "impossible d'écrire : " + path; return false; }
    stream << text;
    if (!stream) { outError = "écriture interrompue : " + path; return false; }
    return true;
}

BundleLoadResult loadProjectBundle(const std::string& folderPath) {
    BundleLoadResult result;

    // 1. project.json -- le contexte.
    std::string projectText;
    std::string error;
    const fs::path projectFile = resolve(folderPath, kProjectFileName);
    if (!readTextFile(projectFile.string(), projectText, error)) {
        result.error = error;
        return result;
    }
    ProjectLoadResult document = parseProjectDocument(projectText);
    if (!document.success) {
        result.error = std::string(kProjectFileName) + " : " + document.error;
        return result;
    }

    // 2. Le MIDI -- les notes. Sans lui, il n'y a pas de morceau : c'est la
    // seule erreur vraiment bloquante après le project.json lui-même.
    const fs::path midiFile = resolve(folderPath, document.document.midiPath);
    std::error_code code;
    if (!fs::exists(midiFile, code)) {
        result.error = "fichier MIDI introuvable : " + document.document.midiPath;
        return result;
    }
    try {
        const auto parsed = vsm::midi::MidiFileParser::parseFile(midiFile.string());
        result.bundle.project = Project::fromParsedFile(parsed);
    } catch (const std::exception& e) {
        result.error = "MIDI illisible (" + document.document.midiPath + ") : " + e.what();
        return result;
    }

    // 3. Le contexte s'applique aux pistes issues du MIDI.
    result.bundle.folderPath = folderPath;
    result.bundle.report = applyDocumentToProject(document.document, result.bundle.project);
    result.bundle.document = document.document;
    for (const auto& missing : result.bundle.report.missingInstruments)
        result.warnings.push_back("Instrument manquant : " + missing);
    for (const auto& warning : result.bundle.report.warnings)
        result.warnings.push_back(warning);

    // 3 bis. LES NOTES DES PRISES, recollées depuis leur fichier. Absent ou
    // illisible, on continue en le DISANT : un projet dont le tiroir manque doit
    // s'ouvrir avec son arrangement plutôt que refuser de s'ouvrir -- mais
    // perdre des prises en silence serait pire que ne pas les avoir gardées.
    {
        bool desPrises = false;
        for (const auto& piste : result.bundle.document.tracks)
            if (!piste.takes.empty()) { desPrises = true; break; }
        if (desPrises) {
            const fs::path takesFile = resolve(folderPath, kTakesMidiPath);
            if (!fs::exists(takesFile, code)) {
                result.warnings.push_back(std::string("prises introuvables : ") + kTakesMidiPath
                                           + " -- elles s'ouvrent vides");
            } else {
                try {
                    const auto tiroir = Project::fromParsedFile(
                        vsm::midi::MidiFileParser::parseFile(takesFile.string()));
                    for (size_t t = 0; t < result.bundle.project.tracks.size()
                                        && t < result.bundle.document.tracks.size(); ++t) {
                        auto& piste = result.bundle.project.tracks[t];
                        const auto& decrite = result.bundle.document.tracks[t];
                        for (size_t k = 0; k < piste.takes.size() && k < decrite.takes.size(); ++k) {
                            const int index = decrite.takes[k].midiTrackIndex;
                            if (index < 0 || index >= static_cast<int>(tiroir.tracks.size())) continue;
                            piste.takes[k].notes = tiroir.tracks[static_cast<size_t>(index)].notes;
                        }
                    }
                } catch (const std::exception& e) {
                    result.warnings.push_back(std::string("prises illisibles (")
                                               + kTakesMidiPath + ") : " + e.what());
                }
            }
        }
    }

    // 4. Les presets -- absents ou illisibles, on continue en le disant : un
    // projet dont un preset manque doit s'ouvrir avec les réglages par défaut
    // plutôt que refuser de s'ouvrir entièrement.
    for (size_t i = 0; i < document.document.tracks.size(); ++i) {
        const std::string& relative = document.document.tracks[i].presetPath;
        if (relative.empty()) continue;
        const fs::path presetFile = resolve(folderPath, relative);
        std::string presetText;
        if (!readTextFile(presetFile.string(), presetText, error)) {
            result.warnings.push_back("Preset introuvable pour la piste " + std::to_string(i) +
                                       " : " + relative);
            continue;
        }
        SynthPresetLoadResult preset = parseSynthPreset(presetText);
        if (!preset.success) {
            result.warnings.push_back("Preset illisible (" + relative + ") : " + preset.error);
            continue;
        }
        result.bundle.presetsByTrack[i] = std::move(preset.preset);
    }

    result.success = true;
    return result;
}

BundleSaveResult saveProjectBundle(const Project& project, const std::string& folderPath,
                                    const std::map<size_t, SynthPreset>& presetsByTrack) {
    BundleSaveResult result;
    std::error_code code;
    fs::create_directories(folderPath, code);

    ProjectDocument document = documentFromProject(project);

    // 1. Le MIDI : les notes, et elles seules.
    const fs::path midiFile = resolve(folderPath, document.midiPath);
    fs::create_directories(midiFile.parent_path(), code);
    try {
        vsm::midi::MidiFileWriter::writeFile(project.toParsedFile(), midiFile.string());
    } catch (const std::exception& e) {
        result.error = std::string("écriture MIDI impossible : ") + e.what();
        return result;
    }
    result.writtenFiles.push_back(document.midiPath);

    // 1 bis. LES NOTES DES PRISES CONSERVÉES, dans leur propre fichier.
    //
    // Elles ne peuvent pas aller dans l'arrangement : celui-ci est ce qu'on
    // ENTEND, et c'est aussi ce qu'on exporte. Y verser les passes écartées en
    // ferait une archive, et les montrerait comme des pistes muettes à qui
    // l'ouvrirait ailleurs. On fabrique donc un SECOND projet, dont chaque
    // piste est une prise, et on l'écrit avec le même écrivain -- il n'y a
    // qu'un format de notes dans ce logiciel, et il est déjà testé.
    {
        Project tiroir;
        tiroir.ticksPerQuarterNote = project.ticksPerQuarterNote;
        tiroir.title = project.title + " (prises)";
        for (size_t t = 0; t < project.tracks.size() && t < document.tracks.size(); ++t) {
            const auto& piste = project.tracks[t];
            for (size_t k = 0; k < piste.takes.size() && k < document.tracks[t].takes.size(); ++k) {
                // Une prise SANS note -- une prise purement audio -- n'occupe
                // aucune piste du fichier : `midiTrack` reste à -1, et le
                // fichier ne porte pas de piste vide dont il faudrait ensuite
                // expliquer la présence.
                if (piste.takes[k].notes.empty()) {
                    document.tracks[t].takes[k].midiTrackIndex = -1;
                    continue;
                }
                vsm::sequencer::Track porteuse;
                porteuse.name = piste.name + " / " + piste.takes[k].name;
                porteuse.channel = piste.channel;
                porteuse.notes = piste.takes[k].notes;
                document.tracks[t].takes[k].midiTrackIndex = static_cast<int>(tiroir.tracks.size());
                tiroir.tracks.push_back(std::move(porteuse));
            }
        }
        if (!tiroir.tracks.empty()) {
            const fs::path takesFile = resolve(folderPath, kTakesMidiPath);
            fs::create_directories(takesFile.parent_path(), code);
            try {
                vsm::midi::MidiFileWriter::writeFile(tiroir.toParsedFile(), takesFile.string());
            } catch (const std::exception& e) {
                result.error = std::string("écriture des prises impossible : ") + e.what();
                return result;
            }
            result.writtenFiles.push_back(kTakesMidiPath);
        }
    }

    // 2. Un preset par piste instrumentée.
    static std::once_flag registration;
    std::call_once(registration, [] { vsm::audio::plugin::registerBuiltInPlugins(); });

    for (size_t i = 0; i < document.tracks.size(); ++i) {
        const std::string& pluginId = document.tracks[i].preferredPlugin;
        if (pluginId.empty()) { document.tracks[i].presetPath.clear(); continue; }

        SynthPreset preset;
        auto provided = presetsByTrack.find(i);
        if (provided != presetsByTrack.end()) {
            preset = provided->second;
        } else {
            auto plugin = vsm::audio::plugin::PluginRegistry::instance().create(pluginId);
            if (!plugin) { document.tracks[i].presetPath.clear(); continue; }
            plugin->initialize(48000.0, 512);
            preset = capturePreset(*plugin, pluginId, project.tracks[i].name);
        }

        const std::string relative = presetPathForTrack(i);
        std::string error;
        if (!writeTextFile(resolve(folderPath, relative).string(),
                            synthPresetToJson(preset).toString(), error)) {
            result.error = error;
            return result;
        }
        document.tracks[i].presetPath = relative;
        result.writtenFiles.push_back(relative);
    }

    // 3. project.json en DERNIER : il référence les chemins de presets qu'on
    // vient de fixer, y compris ceux qu'on a effacés faute d'instrument.
    std::string error;
    if (!writeTextFile(resolve(folderPath, kProjectFileName).string(),
                        projectDocumentToJson(document).toString(), error)) {
        result.error = error;
        return result;
    }
    result.writtenFiles.push_back(kProjectFileName);
    result.success = true;
    return result;
}

} // namespace vsm::interchange
