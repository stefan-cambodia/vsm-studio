#include "TestFramework.h"
#include "vsm/audio/io/WavFileWriter.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include "vsm/interchange/OfflineReconstruction.h"
#include "vsm/interchange/ProjectBundle.h"
#include "vsm/interchange/ProjectDocument.h"
#include "vsm/interchange/SynthPreset.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace vsm::interchange;
namespace fs = std::filesystem;

namespace {

fs::path scratchFolder(const std::string& name) {
    const fs::path folder = fs::temp_directory_path() / ("vsm-echantillons-" + name);
    fs::remove_all(folder);
    fs::create_directories(folder);
    return folder;
}

/// Écrit un WAV court et reconnaissable : une salve à 1 kHz.
void writeToneWav(const fs::path& path, double seconds = 0.2) {
    fs::create_directories(path.parent_path());
    const int frames = static_cast<int>(48000.0 * seconds);
    std::vector<float> left(static_cast<size_t>(frames)), right(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        const float value = 0.7f * std::sin(2.0f * 3.14159265f * 1000.0f
                                             * static_cast<float>(i) / 48000.0f);
        left[static_cast<size_t>(i)] = value;
        right[static_cast<size_t>(i)] = value;
    }
    vsm::audio::io::WavFileWriter::writeFile(
        left.data(), right.data(), static_cast<size_t>(frames), 48000.0,
        vsm::audio::io::SampleFormat::Float32, path.string());
}

vsm::audio::plugin::SynthPluginPtr makeSampler() {
    vsm::audio::plugin::registerBuiltInPlugins();
    auto plugin = vsm::audio::plugin::PluginRegistry::instance().create("vsm.sampler");
    if (plugin) plugin->initialize(48000.0, 512);
    return plugin;
}

} // namespace

VSM_TEST(preset_without_samples_serialises_exactly_as_before) {
    // L'ajout du champ ne doit RIEN changer aux presets qui n'en ont pas :
    // les fichiers existants doivent rester octet pour octet les mêmes.
    SynthPreset preset;
    preset.name = "Sans échantillon";
    preset.pluginId = "vsm.minimoog";
    preset.values["filter.1.cutoff"] = 1200.0f;
    const std::string text = synthPresetToJson(preset).toString(2);
    VSM_ASSERT(text.find("samples") == std::string::npos);
}

VSM_TEST(preset_samples_round_trip_through_json) {
    SynthPreset preset;
    preset.pluginId = "vsm.sampler";
    preset.samples[0] = "samples/kick.wav";
    preset.samples[3] = "samples/snare.wav";

    const auto reloaded = parseSynthPreset(synthPresetToJson(preset).toString(2));
    VSM_ASSERT(reloaded.success);
    VSM_ASSERT_EQ(reloaded.preset.samples.size(), size_t(2));
    VSM_ASSERT_EQ(reloaded.preset.samples.at(0), std::string("samples/kick.wav"));
    VSM_ASSERT_EQ(reloaded.preset.samples.at(3), std::string("samples/snare.wav"));
}

VSM_TEST(an_older_preset_without_the_field_still_loads) {
    // Le champ est facultatif : son ajout ne casse aucun fichier existant, et
    // c'est pourquoi le numéro de version du format n'a pas bougé.
    const std::string ancien = R"({"format":"vsm-synth-preset","version":1,"name":"Ancien",
        "pluginId":"vsm.tb303","machineName":"TB-303","fidelity":"derived",
        "parameters":{"filter.1.cutoff":700.0}})";
    const auto result = parseSynthPreset(ancien);
    VSM_ASSERT(result.success);
    VSM_ASSERT(result.preset.samples.empty());
    VSM_ASSERT_NEAR(result.preset.values.at("filter.1.cutoff"), 700.0f, 1e-4);
}

VSM_TEST(preset_samples_are_loaded_into_the_machine) {
    const fs::path folder = scratchFolder("chargement");
    writeToneWav(folder / "samples" / "coup.wav");

    SynthPreset preset;
    preset.pluginId = "vsm.sampler";
    preset.samples[2] = "samples/coup.wav";

    auto sampler = makeSampler();
    VSM_ASSERT(sampler != nullptr);
    const SampleLoadReport report = applyPresetSamples(preset, *sampler, folder.string());
    VSM_ASSERT_EQ(report.loaded.size(), size_t(1));
    VSM_ASSERT(report.failures.empty());
    VSM_ASSERT_EQ(report.loaded.front().first, 2);
    fs::remove_all(folder);
}

VSM_TEST(a_missing_sample_is_reported_never_silently_skipped) {
    // Une piste muette doit DIRE pourquoi elle est muette. C'est le défaut
    // que cette étape corrige : un projet au sampler se chargeait sans un
    // message et rendait du silence.
    const fs::path folder = scratchFolder("manquant");
    SynthPreset preset;
    preset.pluginId = "vsm.sampler";
    preset.samples[0] = "samples/absent.wav";

    auto sampler = makeSampler();
    const SampleLoadReport report = applyPresetSamples(preset, *sampler, folder.string());
    VSM_ASSERT(report.loaded.empty());
    VSM_ASSERT_EQ(report.failures.size(), size_t(1));
    VSM_ASSERT(report.summary().find("absent.wav") != std::string::npos);
    fs::remove_all(folder);
}

VSM_TEST(an_absolute_sample_path_is_refused) {
    // Un chemin absolu s'ouvrirait sur la machine qui a produit le projet et
    // nulle part ailleurs. Le refuser fait voir le défaut à celui qui
    // l'introduit, pas à celui qui reçoit le projet.
    const fs::path folder = scratchFolder("absolu");
    const fs::path absolute = folder / "coup.wav";
    writeToneWav(absolute);

    SynthPreset preset;
    preset.pluginId = "vsm.sampler";
    preset.samples[0] = absolute.string(); // volontairement absolu

    auto sampler = makeSampler();
    const SampleLoadReport report = applyPresetSamples(preset, *sampler, folder.string());
    VSM_ASSERT(report.loaded.empty());
    VSM_ASSERT_EQ(report.failures.size(), size_t(1));
    VSM_ASSERT(report.failures.front().find("absolu") != std::string::npos);
    fs::remove_all(folder);
}

VSM_TEST(samples_declared_for_a_machine_that_cannot_read_them_are_reported) {
    SynthPreset preset;
    preset.pluginId = "vsm.minimoog";
    preset.samples[0] = "samples/coup.wav";

    vsm::audio::plugin::registerBuiltInPlugins();
    auto minimoog = vsm::audio::plugin::PluginRegistry::instance().create("vsm.minimoog");
    minimoog->initialize(48000.0, 512);
    const SampleLoadReport report = applyPresetSamples(preset, *minimoog, ".");
    VSM_ASSERT(report.loaded.empty());
    VSM_ASSERT_EQ(report.failures.size(), size_t(1));
}

VSM_TEST(a_project_using_the_sampler_renders_sound_not_silence) {
    // LE test de l'étape : le parcours complet, du dossier de projet au WAV.
    // Avant ce travail, ce projet se chargeait sans avertissement et rendait
    // un fichier entièrement silencieux.
    const fs::path folder = scratchFolder("projet");
    writeToneWav(folder / "samples" / "coup.wav", 0.3);

    // --- le projet, écrit à la main comme le ferait la chaîne d'analyse ---
    fs::create_directories(folder / "instruments");
    fs::create_directories(folder / "midi");

    SynthPreset preset;
    preset.name = "Kit";
    preset.pluginId = "vsm.sampler";
    preset.machineName = "Sampler (8 emplacements)";
    preset.values["sampler.slot.1.note"] = 36.0f;
    preset.values["sampler.slot.1.level"] = 1.0f;
    preset.samples[0] = "samples/coup.wav";
    std::string error;
    VSM_ASSERT(writeTextFile((folder / "instruments" / "track_00.synth.json").string(),
                              synthPresetToJson(preset).toString(2), error));

    // Projet minimal : une piste, une note sur la note 36.
    vsm::sequencer::Project project;
    project.tracks.emplace_back();
    project.tracks[0].name = "Batterie";
    project.tracks[0].instrumentId = "vsm.sampler";
    project.tracks[0].channel = 9;
    for (int beat = 0; beat < 4; ++beat) {
        vsm::sequencer::Note note;
        note.startTick = beat * 480;
        note.endTick = beat * 480 + 120;
        note.number = 36;
        note.velocity = 110;
        note.channel = 9;
        project.tracks[0].notes.push_back(note);
    }
    std::map<size_t, SynthPreset> presets{{0, preset}};
    const BundleSaveResult saved = saveProjectBundle(project, folder.string(), presets);
    VSM_ASSERT(saved.success);

    // --- rendu ---
    RenderOptions options;
    options.sampleRate = 48000.0;
    const RenderResult rendered =
        renderProjectFolderToWav(folder.string(), (folder / "sortie.wav").string(), options);
    VSM_ASSERT(rendered.success);
    VSM_ASSERT(rendered.peakLevel > 0.01f); // du SON, pas du silence

    fs::remove_all(folder);
}

VSM_TEST(opening_a_bundle_applies_presets_and_samples_like_the_application_does) {
    // Ce test suit EXACTEMENT le chemin que prend l'application à l'ouverture
    // d'un projet (étape 11.1) : charger le dossier, assigner l'instrument de
    // chaque piste, appliquer le preset, puis charger ses échantillons. Le
    // code de l'interface n'est qu'une glu autour de ces quatre appels ; c'est
    // ici qu'ils sont vérifiés, là où ils sont testables sans fenêtre.
    const fs::path folder = scratchFolder("ouverture");
    writeToneWav(folder / "samples" / "coup.wav", 0.25);
    fs::create_directories(folder / "instruments");
    fs::create_directories(folder / "midi");

    SynthPreset kit;
    kit.name = "Kit";
    kit.pluginId = "vsm.sampler";
    kit.values["sampler.slot.1.note"] = 36.0f;
    kit.values["sampler.slot.1.level"] = 1.0f;
    kit.samples[0] = "samples/coup.wav";

    SynthPreset basse;
    basse.name = "Basse";
    basse.pluginId = "vsm.tb303";
    basse.values["filter.1.cutoff"] = 480.0f;
    basse.values["filter.1.resonance"] = 0.9f;

    vsm::sequencer::Project project;
    for (const auto& [nom, plugin] : {std::pair<const char*, const char*>{"Batterie", "vsm.sampler"},
                                       std::pair<const char*, const char*>{"Basse", "vsm.tb303"}}) {
        vsm::sequencer::Track piste;
        piste.name = nom;
        piste.instrumentId = plugin;
        vsm::sequencer::Note note;
        note.startTick = 0;
        note.endTick = 240;
        note.number = 36;
        note.velocity = 100;
        piste.notes.push_back(note);
        project.tracks.push_back(piste);
    }
    std::map<size_t, SynthPreset> presets{{0, kit}, {1, basse}};
    VSM_ASSERT(saveProjectBundle(project, folder.string(), presets).success);

    // --- ce que fait l'application ---
    const BundleLoadResult loaded = loadProjectBundle(folder.string());
    VSM_ASSERT(loaded.success);
    VSM_ASSERT_EQ(loaded.bundle.project.tracks.size(), size_t(2));
    // Le dossier d'origine DOIT être connu, sinon les chemins relatifs des
    // échantillons ne se résolvent pas.
    VSM_ASSERT(!loaded.bundle.folderPath.empty());

    vsm::audio::plugin::registerBuiltInPlugins();
    size_t echantillonsCharges = 0;
    for (const auto& [index, preset] : loaded.bundle.presetsByTrack) {
        VSM_ASSERT(index < loaded.bundle.project.tracks.size());
        auto instrument = vsm::audio::plugin::PluginRegistry::instance().create(
            loaded.bundle.project.tracks[index].instrumentId);
        VSM_ASSERT(instrument != nullptr);
        instrument->initialize(48000.0, 512);

        const PresetApplyReport applique = applyPreset(
            preset, *instrument, loaded.bundle.project.tracks[index].instrumentId);
        VSM_ASSERT(applique.appliedCount() > 0);

        const SampleLoadReport charges =
            applyPresetSamples(preset, *instrument, loaded.bundle.folderPath);
        VSM_ASSERT(charges.failures.empty());
        echantillonsCharges += charges.loaded.size();
    }
    VSM_ASSERT_EQ(echantillonsCharges, size_t(1)); // celui du kit, et lui seul
    fs::remove_all(folder);
}


VSM_TEST(automation_in_a_project_actually_moves_the_parameter) {
    // La preuve AUDIBLE, pas seulement structurelle : une rampe de coupure
    // écrite dans project.json doit rendre la fin du son plus brillante que
    // le début. Sans application de l'automation, un filtre à 250 Hz reste à
    // 250 Hz et les deux moitiés se ressemblent.
    const fs::path folder = scratchFolder("automation");
    fs::create_directories(folder);

    vsm::sequencer::Project project;
    project.tracks.emplace_back();
    project.tracks[0].name = "Basse";
    project.tracks[0].instrumentId = "vsm.tb303";
    vsm::sequencer::Note note;
    note.startTick = 0;
    note.endTick = 4 * 480;      // 2 s à 120 BPM
    note.number = 33;
    note.velocity = 100;
    project.tracks[0].notes.push_back(note);
    const BundleSaveResult saved = saveProjectBundle(project, folder.string(), {});
    VSM_ASSERT(saved.success);

    // La rampe est ajoutée EN RELISANT le fichier, comme le ferait la chaîne
    // d'analyse : c'est le format qui est testé, pas une structure en mémoire.
    std::string texte, erreur;
    VSM_ASSERT(readTextFile((folder / "project.json").string(), texte, erreur));
    ProjectLoadResult charge = parseProjectDocument(texte);
    VSM_ASSERT(charge.success);
    ProjectAutomationLane lane;
    lane.parameter = "filter.1.cutoff";
    lane.points = {{0, 250.0f, false}, {4 * 480, 5000.0f, false}};
    charge.document.tracks[0].automation.push_back(lane);
    VSM_ASSERT(writeTextFile((folder / "project.json").string(),
                              projectDocumentToJson(charge.document).toString(2), erreur));

    RenderOptions options;
    options.sampleRate = 48000.0;
    options.tailSeconds = 0.1;
    const RenderResult rendu =
        renderProjectFolderToWav(folder.string(), (folder / "sortie.wav").string(), options);
    VSM_ASSERT(rendu.success);
    VSM_ASSERT(rendu.peakLevel > 0.01f);

    // Brillance des deux moitiés, par différences premières sur le WAV rendu.
    std::string texteWav;
    std::vector<float> gauche;
    {
        // lecture 16 bits du WAV écrit par le rendu
        std::ifstream fichier((folder / "sortie.wav").string(), std::ios::binary);
        VSM_ASSERT(fichier.good());
        std::vector<char> octets((std::istreambuf_iterator<char>(fichier)),
                                  std::istreambuf_iterator<char>());
        const size_t entete = 44; // en-tête canonique écrit par WavFileWriter
        VSM_ASSERT(octets.size() > entete);
        // float32 stéréo entrelacé (format d'écriture du rendu) : voir
        // RenderOptions/WavFileWriter. On lit le canal gauche.
        const float* donnees = reinterpret_cast<const float*>(octets.data() + entete);
        const size_t nombre = (octets.size() - entete) / sizeof(float);
        for (size_t i = 0; i + 1 < nombre; i += 2) gauche.push_back(donnees[i]);
    }
    VSM_ASSERT(gauche.size() > 48000);
    // Brillance NORMALISÉE par l'énergie (part d'aigu) : la 303 est à
    // décroissance, son niveau tombe pendant la note -- une brillance brute
    // confondrait « plus aigu » et « plus fort », et une première version de
    // ce test s'y est laissé prendre.
    auto tilt = [&](size_t debut, size_t fin) {
        double d2 = 0.0, energie = 0.0;
        for (size_t i = debut + 1; i < fin && i < gauche.size(); ++i) {
            const double d = static_cast<double>(gauche[i]) - gauche[i - 1];
            d2 += d * d;
            energie += static_cast<double>(gauche[i]) * gauche[i];
        }
        return d2 / (energie + 1e-12);
    };
    const size_t demi = gauche.size() / 2;
    const double avant = tilt(0, demi);
    const double apres = tilt(demi, gauche.size());
    // La rampe monte de 250 Hz à 5 kHz : la part d'aigu de la seconde moitié
    // doit être NETTEMENT plus haute. Sans automation appliquée, la coupure
    // reste au preset et ce rapport vaut ~1.
    VSM_ASSERT(apres > avant * 2.0);

    fs::remove_all(folder);
}

VSM_TEST(automation_targeting_a_missing_parameter_is_reported) {
    const fs::path folder = scratchFolder("automation-inconnue");
    fs::create_directories(folder);
    vsm::sequencer::Project project;
    project.tracks.emplace_back();
    project.tracks[0].instrumentId = "vsm.tb303";
    VSM_ASSERT(saveProjectBundle(project, folder.string(), {}).success);

    std::string texte, erreur;
    VSM_ASSERT(readTextFile((folder / "project.json").string(), texte, erreur));
    ProjectLoadResult charge = parseProjectDocument(texte);
    VSM_ASSERT(charge.success);
    ProjectAutomationLane lane;
    lane.parameter = "fm.operator.3.ratio"; // la TB-303 n'a pas d'opérateur FM
    lane.points = {{0, 1.0f, false}};
    charge.document.tracks[0].automation.push_back(lane);
    VSM_ASSERT(writeTextFile((folder / "project.json").string(),
                              projectDocumentToJson(charge.document).toString(2), erreur));

    RenderOptions options;
    options.sampleRate = 48000.0;
    const RenderResult rendu =
        renderProjectFolderToWav(folder.string(), (folder / "sortie.wav").string(), options);
    VSM_ASSERT(rendu.success);
    bool signale = false;
    for (const auto& avertissement : rendu.warnings)
        if (avertissement.find("fm.operator.3.ratio") != std::string::npos) signale = true;
    VSM_ASSERT(signale); // rapportée, jamais ignorée en silence

    fs::remove_all(folder);
}
