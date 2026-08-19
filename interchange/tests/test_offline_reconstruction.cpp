#include "TestFramework.h"
#include "vsm/interchange/OfflineReconstruction.h"
#include "vsm/interchange/ProjectBundle.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace vsm::interchange;
using vsm::sequencer::Project;
using vsm::sequencer::Track;

namespace {

namespace fs = std::filesystem;

/// Dossier temporaire propre, supprimé à la sortie -- un test qui laisse des
/// fichiers derrière lui finit par faire échouer le suivant.
class TempFolder {
public:
    explicit TempFolder(const std::string& name) {
        path_ = fs::temp_directory_path() / ("vsm_test_" + name);
        fs::remove_all(path_);
        fs::create_directories(path_);
    }
    ~TempFolder() { std::error_code code; fs::remove_all(path_, code); }
    TempFolder(const TempFolder&) = delete;
    TempFolder& operator=(const TempFolder&) = delete;

    std::string str() const { return path_.string(); }
    std::string file(const std::string& relative) const { return (path_ / relative).string(); }

private:
    fs::path path_;
};

Project buildPlayableProject() {
    Project project;
    project.title = "Test de rendu";
    project.ticksPerQuarterNote = 480;

    uint64_t ids = 1;
    Track bass;
    bass.name = "Basse acide";
    bass.channel = 0;
    bass.instrumentId = "vsm.tb303";
    bass.volume = 0.9f;
    for (int i = 0; i < 4; ++i)
        bass.addNote(i * 240, i * 240 + 200, static_cast<uint8_t>(36 + i * 2), 110, 0, ids);
    project.tracks.push_back(bass);
    return project;
}

} // namespace

VSM_TEST(bundle_round_trips_through_a_folder) {
    // Le parcours complet : écrire un dossier de projet, le relire, et
    // retrouver le morceau -- notes comprises, puisqu'elles voyagent en MIDI.
    TempFolder folder("bundle_roundtrip");
    const Project original = buildPlayableProject();

    const BundleSaveResult saved = saveProjectBundle(original, folder.str());
    VSM_ASSERT(saved.success);
    VSM_ASSERT(std::filesystem::exists(folder.file("project.json")));
    VSM_ASSERT(std::filesystem::exists(folder.file("midi/arrangement.mid")));
    VSM_ASSERT(std::filesystem::exists(folder.file("instruments/track_00.synth.json")));

    const BundleLoadResult loaded = loadProjectBundle(folder.str());
    VSM_ASSERT(loaded.success);
    VSM_ASSERT_EQ(loaded.bundle.project.title, std::string("Test de rendu"));
    VSM_ASSERT_EQ(loaded.bundle.project.tracks.size(), size_t{1});
    VSM_ASSERT_EQ(loaded.bundle.project.tracks[0].name, std::string("Basse acide"));
    VSM_ASSERT_EQ(loaded.bundle.project.tracks[0].instrumentId, std::string("vsm.tb303"));
    VSM_ASSERT_EQ(loaded.bundle.project.tracks[0].notes.size(), size_t{4});
    VSM_ASSERT_EQ(loaded.bundle.presetsByTrack.size(), size_t{1});
    VSM_ASSERT_NEAR(loaded.bundle.project.tracks[0].volume, 0.9f, 1e-6);
}

VSM_TEST(rendering_a_project_folder_produces_real_audio) {
    TempFolder folder("render_audio");
    saveProjectBundle(buildPlayableProject(), folder.str());

    RenderOptions options;
    options.sampleRate = 48000.0;
    options.tailSeconds = 0.5;
    const RenderResult result = renderProjectFolderToWav(folder.str(), folder.file("render.wav"), options);

    VSM_ASSERT(result.success);
    VSM_ASSERT_EQ(result.tracksWithInstrument, size_t{1});
    VSM_ASSERT(result.framesWritten > 1000);
    VSM_ASSERT(result.peakLevel > 0.01f); // il y a vraiment du son, pas un fichier de silence
    VSM_ASSERT(std::filesystem::exists(folder.file("render.wav")));
    VSM_ASSERT(std::filesystem::file_size(folder.file("render.wav")) > 1000);
}

VSM_TEST(rendering_is_deterministic) {
    // La condition posée au § 6 de la roadmap : mêmes fichiers d'entrée =>
    // même WAV. Sans ça, une boucle d'optimisation pilotée par Python
    // comparerait du bruit d'un tour à l'autre.
    TempFolder folder("render_determinism");
    saveProjectBundle(buildPlayableProject(), folder.str());

    RenderOptions options;
    options.tailSeconds = 0.25;
    const RenderResult first = renderProjectFolderToWav(folder.str(), folder.file("a.wav"), options);
    const RenderResult second = renderProjectFolderToWav(folder.str(), folder.file("b.wav"), options);
    VSM_ASSERT(first.success && second.success);

    auto readAll = [](const std::string& path) {
        std::ifstream stream(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    };
    const std::string a = readAll(folder.file("a.wav"));
    const std::string b = readAll(folder.file("b.wav"));
    VSM_ASSERT(!a.empty());
    VSM_ASSERT_EQ(a.size(), b.size());
    VSM_ASSERT(a == b); // octet pour octet
}

VSM_TEST(preset_values_actually_reach_the_machine_and_change_the_sound) {
    // Vérifie que le preset n'est pas seulement lu, mais APPLIQUÉ : deux
    // projets identiques, ne différant que par la coupure du filtre, doivent
    // produire des rendus différents. C'est le test qui distingue une chaîne
    // d'interopérabilité qui marche d'une qui se contente de ne pas planter.
    auto renderWithCutoff = [](const std::string& name, float cutoff) {
        TempFolder folder(name);
        const Project project = buildPlayableProject();
        SynthPreset preset;
        preset.pluginId = "vsm.tb303";
        preset.values["filter.1.cutoff"] = cutoff;
        preset.values["filter.1.resonance"] = 0.9f;
        saveProjectBundle(project, folder.str(), {{0, preset}});

        RenderOptions options;
        options.tailSeconds = 0.25;
        const RenderResult result = renderProjectFolderToWav(folder.str(), folder.file("out.wav"), options);
        std::ifstream stream(folder.file("out.wav"), std::ios::binary);
        return std::make_pair(result.peakLevel,
                               std::string((std::istreambuf_iterator<char>(stream)),
                                           std::istreambuf_iterator<char>()));
    };

    const auto dark = renderWithCutoff("preset_dark", 200.0f);
    const auto bright = renderWithCutoff("preset_bright", 6000.0f);
    VSM_ASSERT(!dark.second.empty() && !bright.second.empty());
    VSM_ASSERT(dark.second != bright.second);   // le preset a bien agi
    VSM_ASSERT(bright.first > dark.first);       // filtre ouvert => plus d'énergie
}

VSM_TEST(missing_instrument_renders_silence_and_says_so) {
    // Règle « jamais de reconstruction silencieuse fausse » appliquée jusqu'au
    // rendu : le WAV existe, il est silencieux pour cette piste, et le rapport
    // nomme la machine absente.
    TempFolder folder("missing_instrument");
    Project project = buildPlayableProject();
    project.tracks[0].instrumentId = "com.autre.editeur.synth-absent";
    saveProjectBundle(project, folder.str());

    const RenderResult result = renderProjectFolderToWav(folder.str(), folder.file("out.wav"));
    VSM_ASSERT(result.success);           // le rendu aboutit
    VSM_ASSERT_EQ(result.tracksWithInstrument, size_t{0});
    VSM_ASSERT_NEAR(result.peakLevel, 0.0f, 1e-6); // et il est silencieux
    bool mentioned = false;
    for (const auto& warning : result.warnings)
        if (warning.find("com.autre.editeur.synth-absent") != std::string::npos) mentioned = true;
    VSM_ASSERT(mentioned);
}

VSM_TEST(loading_reports_a_missing_preset_without_refusing_the_project) {
    // Un projet auquel il manque un preset doit s'ouvrir avec les réglages par
    // défaut, en le disant -- pas refuser de s'ouvrir.
    TempFolder folder("missing_preset");
    saveProjectBundle(buildPlayableProject(), folder.str());
    std::filesystem::remove(folder.file("instruments/track_00.synth.json"));

    const BundleLoadResult loaded = loadProjectBundle(folder.str());
    VSM_ASSERT(loaded.success);
    VSM_ASSERT_EQ(loaded.bundle.presetsByTrack.size(), size_t{0});
    bool mentioned = false;
    for (const auto& warning : loaded.warnings)
        if (warning.find("Preset introuvable") != std::string::npos) mentioned = true;
    VSM_ASSERT(mentioned);
}

VSM_TEST(loading_fails_clearly_when_the_project_or_midi_is_absent) {
    TempFolder folder("incomplete");
    const BundleLoadResult noProject = loadProjectBundle(folder.str());
    VSM_ASSERT(!noProject.success);
    VSM_ASSERT(noProject.error.find("project.json") != std::string::npos);

    // project.json présent mais MIDI absent : erreur explicite, pas un rendu
    // vide qui laisserait croire à un morceau silencieux.
    std::string error;
    writeTextFile(folder.file("project.json"),
                   R"({"format":"vsm-project","version":1,"midi":{"file":"midi/arrangement.mid"},"tracks":[]})",
                   error);
    const BundleLoadResult noMidi = loadProjectBundle(folder.str());
    VSM_ASSERT(!noMidi.success);
    VSM_ASSERT(noMidi.error.find("MIDI") != std::string::npos);
}

VSM_TEST(render_duration_follows_the_content_and_the_requested_tail) {
    TempFolder folder("duration");
    saveProjectBundle(buildPlayableProject(), folder.str()); // ~1 s de notes à 120 BPM

    RenderOptions options;
    options.tailSeconds = 1.0;
    const RenderResult automatic = renderProjectFolderToWav(folder.str(), folder.file("auto.wav"), options);
    VSM_ASSERT(automatic.success);
    VSM_ASSERT(automatic.renderedSeconds > 1.0);

    options.durationSeconds = 3.0; // durée imposée : elle prime
    const RenderResult fixed = renderProjectFolderToWav(folder.str(), folder.file("fixed.wav"), options);
    VSM_ASSERT(fixed.success);
    VSM_ASSERT_NEAR(fixed.renderedSeconds, 3.0, 1e-9);
    VSM_ASSERT_EQ(fixed.framesWritten, static_cast<size_t>(3.0 * 48000.0));
}
