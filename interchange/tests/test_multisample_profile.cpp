#include "TestFramework.h"
#include "vsm/interchange/MultisampleProfile.h"
#include "vsm/interchange/PatchRenderService.h"
#include "vsm/interchange/SynthPreset.h"
#include "vsm/audio/io/WavFileWriter.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace vsm::interchange;
using vsm::audio::plugin::PluginRegistry;
using vsm::audio::plugin::SynthPluginPtr;

namespace {

SynthPluginPtr makeMultisample() {
    vsm::audio::plugin::registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.multisample");
    if (plugin) plugin->initialize(48000.0, 512);
    return plugin;
}

/// Un dossier de travail par test, effacé à la sortie. Les tests de cette
/// couche écrivent de vrais fichiers -- c'est le seul moyen d'éprouver la
/// résolution des chemins relatifs, qui est justement la règle qu'on vérifie.
struct TempFolder {
    std::filesystem::path path;
    explicit TempFolder(const std::string& name)
        : path(std::filesystem::temp_directory_path() / ("vsm-multisample-" + name)) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path / "echantillons");
    }
    ~TempFolder() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

/// Écrit un vrai WAV : une sinusoïde d'une demi-seconde.
void writeSine(const std::filesystem::path& path, double frequency) {
    std::vector<float> samples(24000);
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i] = static_cast<float>(std::sin(2.0 * std::acos(-1.0) * frequency
                                                  * static_cast<double>(i) / 48000.0));
    vsm::audio::io::WavFileWriter::writeFile(samples.data(), nullptr, samples.size(), 48000.0,
                                              vsm::audio::io::SampleFormat::Float32, path.string());
}

std::string readAll(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
}

const char* kMinimalProfile = R"({
  "format": "vsm-multisample-profile",
  "version": 1,
  "name": "Essai",
  "attribution": "engendré par les tests, sans licence tierce",
  "programs": ["Essai"],
  "zones": [
    {"sample": "echantillons/grave.wav", "rootNote": 48, "lowNote": 36, "highNote": 53,
     "lowVelocity": 1, "highVelocity": 127, "loop": {"start": 0, "end": 24000}},
    {"sample": "echantillons/aigu.wav", "rootNote": 60, "lowNote": 54, "highNote": 71,
     "lowVelocity": 1, "highVelocity": 127, "tuneCents": -7.5, "level": 0.8}
  ]
})";

} // namespace

VSM_TEST(multisample_profile_round_trip_through_the_format) {
    TempFolder folder("roundtrip");
    writeSine(folder.path / "echantillons" / "grave.wav", 130.81);
    writeSine(folder.path / "echantillons" / "aigu.wav", 261.63);
    const auto profilePath = folder.path / "essai.profile.json";
    writeText(profilePath, kMinimalProfile);

    auto loaded = loadMultisampleProfileFile(profilePath.string());
    VSM_ASSERT(loaded.error.empty());
    VSM_ASSERT(loaded.success);
    VSM_ASSERT(loaded.ignored.empty());
    VSM_ASSERT_EQ(loaded.spec.zones.size(), size_t(2));
    VSM_ASSERT(loaded.spec.name == "Essai");
    VSM_ASSERT_EQ(loaded.spec.zones[0].rootNote, 48);
    VSM_ASSERT(loaded.spec.zones[0].loopEnabled);
    VSM_ASSERT_EQ(loaded.spec.zones[0].loopEnd, uint64_t(24000));
    VSM_ASSERT_NEAR(loaded.spec.zones[1].tuneCents, -7.5f, 1e-4f);
    // Le chemin est bien résolu par rapport au DOSSIER DU PROFIL.
    VSM_ASSERT(std::filesystem::exists(loaded.spec.zones[0].samplePath));

    // Aller-retour : réécrit puis relu, le profil dit la même chose.
    const auto json = multisampleProfileToJson(loaded.spec, folder.path.string());
    auto again = parseMultisampleProfile(json.toString(), folder.path.string(), profilePath.string());
    VSM_ASSERT(again.error.empty());
    VSM_ASSERT_EQ(again.spec.zones.size(), loaded.spec.zones.size());
    for (size_t i = 0; i < again.spec.zones.size(); ++i) {
        VSM_ASSERT(again.spec.zones[i].relativePath == loaded.spec.zones[i].relativePath);
        VSM_ASSERT_EQ(again.spec.zones[i].rootNote, loaded.spec.zones[i].rootNote);
        VSM_ASSERT_EQ(again.spec.zones[i].lowNote, loaded.spec.zones[i].lowNote);
        VSM_ASSERT_EQ(again.spec.zones[i].highVelocity, loaded.spec.zones[i].highVelocity);
        VSM_ASSERT_EQ(again.spec.zones[i].loopEnabled, loaded.spec.zones[i].loopEnabled);
        VSM_ASSERT_NEAR(again.spec.zones[i].tuneCents, loaded.spec.zones[i].tuneCents, 1e-4f);
        VSM_ASSERT_NEAR(again.spec.zones[i].level, loaded.spec.zones[i].level, 1e-4f);
    }
}

VSM_TEST(multisample_profile_installs_into_the_machine) {
    TempFolder folder("install");
    writeSine(folder.path / "echantillons" / "grave.wav", 130.81);
    writeSine(folder.path / "echantillons" / "aigu.wav", 261.63);
    const auto profilePath = folder.path / "essai.profile.json";
    writeText(profilePath, kMinimalProfile);

    auto synth = makeMultisample();
    VSM_ASSERT(synth != nullptr);
    const auto report = applyMultisampleProfile(*synth, profilePath.string());
    VSM_ASSERT(report.error.empty());
    VSM_ASSERT(report.applied);
    VSM_ASSERT_EQ(report.zoneCount, 2);
    VSM_ASSERT(report.memoryBytes > 0u);
    VSM_ASSERT(report.summary().find("Essai") != std::string::npos);
}

VSM_TEST(multisample_profile_refuses_absolute_paths) {
    TempFolder folder("absolu");
    const auto sample = folder.path / "echantillons" / "grave.wav";
    writeSine(sample, 130.81);
    const std::string text = std::string(R"({
  "format": "vsm-multisample-profile",
  "version": 1,
  "attribution": "essai",
  "zones": [{"sample": ")") + sample.string() + R"(", "rootNote": 48}]
})";
    auto loaded = parseMultisampleProfile(text, folder.path.string());
    VSM_ASSERT(!loaded.success);
    VSM_ASSERT(loaded.error.find("absolu") != std::string::npos);
}

VSM_TEST(multisample_profile_refuses_what_it_cannot_understand) {
    TempFolder folder("refus");

    // Format inconnu.
    auto wrongFormat = parseMultisampleProfile(
        R"({"format":"sfz","version":1,"attribution":"x","zones":[]})", folder.path.string());
    VSM_ASSERT(!wrongFormat.success);
    VSM_ASSERT(wrongFormat.error.find("format") != std::string::npos);

    // Version inconnue : refusée, pas lue au mieux.
    auto wrongVersion = parseMultisampleProfile(
        R"({"format":"vsm-multisample-profile","version":99,"attribution":"x","zones":[]})",
        folder.path.string());
    VSM_ASSERT(!wrongVersion.success);
    VSM_ASSERT(wrongVersion.error.find("99") != std::string::npos);

    // Attribution absente : licence inconnue, banque refusée.
    auto noAttribution = parseMultisampleProfile(
        R"({"format":"vsm-multisample-profile","version":1,"zones":[{"sample":"a.wav"}]})",
        folder.path.string());
    VSM_ASSERT(!noAttribution.success);
    VSM_ASSERT(noAttribution.error.find("attribution") != std::string::npos);

    // Aucune zone.
    auto noZones = parseMultisampleProfile(
        R"({"format":"vsm-multisample-profile","version":1,"attribution":"x","zones":[]})",
        folder.path.string());
    VSM_ASSERT(!noZones.success);

    // Étendue vide : une zone que rien ne peut atteindre.
    auto emptyRange = parseMultisampleProfile(
        R"({"format":"vsm-multisample-profile","version":1,"attribution":"x",
            "zones":[{"sample":"a.wav","lowNote":72,"highNote":60}]})",
        folder.path.string());
    VSM_ASSERT(!emptyRange.success);
    VSM_ASSERT(emptyRange.error.find("étendue vide") != std::string::npos);

    // Échantillon absent : signalé par la machine, en nommant le fichier.
    writeText(folder.path / "manquant.profile.json",
              R"({"format":"vsm-multisample-profile","version":1,"attribution":"x",
                  "zones":[{"sample":"echantillons/absent.wav","rootNote":60}]})");
    auto synth = makeMultisample();
    const auto report = applyMultisampleProfile(*synth, (folder.path / "manquant.profile.json").string());
    VSM_ASSERT(!report.applied);
    VSM_ASSERT(report.error.find("absent.wav") != std::string::npos);
    VSM_ASSERT(report.summary().find("non chargé") != std::string::npos);
}

/// Un champ que cette version ne connaît pas est RAPPORTÉ, pas avalé. C'est la
/// règle que suivra l'import SoundFont pour ses générateurs exotiques, et elle
/// se met en place ici, sur le format natif, où elle est facile à vérifier.
VSM_TEST(multisample_profile_names_what_it_ignores) {
    TempFolder folder("ignore");
    auto loaded = parseMultisampleProfile(
        R"({"format":"vsm-multisample-profile","version":1,"attribution":"x",
            "chorus": 0.5,
            "zones":[{"sample":"a.wav","rootNote":60,"roundRobin":3}]})",
        folder.path.string());
    VSM_ASSERT(loaded.success);
    VSM_ASSERT_EQ(loaded.ignored.size(), size_t(2));
    bool sawChorus = false, sawRoundRobin = false;
    for (const auto& field : loaded.ignored) {
        if (field == "chorus") sawChorus = true;
        if (field == "zone 0.roundRobin") sawRoundRobin = true;
    }
    VSM_ASSERT(sawChorus);
    VSM_ASSERT(sawRoundRobin);
}

/// Un preset qui déclare un profil le fait charger, et un preset qui en
/// déclare un pour une machine qui n'en lit pas le DIT.
VSM_TEST(multisample_preset_carries_the_profile_path) {
    TempFolder folder("preset");
    writeSine(folder.path / "echantillons" / "grave.wav", 130.81);
    writeSine(folder.path / "echantillons" / "aigu.wav", 261.63);
    writeText(folder.path / "essai.profile.json", kMinimalProfile);

    SynthPreset preset;
    preset.pluginId = "vsm.multisample";
    preset.profile = "essai.profile.json";
    preset.values["filter.1.cutoff"] = 4000.0f;

    // Aller-retour par le JSON du preset : le champ survit.
    const auto json = synthPresetToJson(preset);
    auto reread = parseSynthPreset(json.toString());
    VSM_ASSERT(reread.success);
    VSM_ASSERT(reread.preset.profile == "essai.profile.json");

    auto synth = makeMultisample();
    const auto report = applyPresetSamples(reread.preset, *synth, folder.path.string());
    VSM_ASSERT(report.failures.empty());
    VSM_ASSERT_EQ(report.loaded.size(), size_t(1));

    // Machine qui n'accepte pas de profil : signalé, jamais ignoré en silence.
    vsm::audio::plugin::registerBuiltInPlugins();
    auto minimoog = PluginRegistry::instance().create("vsm.minimoog");
    minimoog->initialize(48000.0, 512);
    const auto refused = applyPresetSamples(reread.preset, *minimoog, folder.path.string());
    VSM_ASSERT_EQ(refused.failures.size(), size_t(1));
    VSM_ASSERT(refused.failures[0].find("profil") != std::string::npos);
}

/// Le service de rendu REFUSE de rendre la machine sans profil, plutôt que de
/// rendre du silence. Une distance mesurée contre du silence est un chiffre, et
/// un chiffre faux coûte plus cher qu'une erreur franche : sur une cible douce,
/// la machine muette gagnerait.
VSM_TEST(multisample_render_service_refuses_without_a_profile) {
    PatchRenderService service;
    PatchRenderRequest request;
    request.machineId = "vsm.multisample";
    request.durationSeconds = 0.2;
    request.notes.push_back({60, 100, 0.0, 0.15});
    const auto response = service.render(request);
    VSM_ASSERT(!response.ok);
    VSM_ASSERT(response.error.find("profil") != std::string::npos);
}

VSM_TEST(multisample_render_service_uses_the_profile_it_is_given) {
    TempFolder folder("service");
    writeSine(folder.path / "echantillons" / "grave.wav", 130.81);
    writeSine(folder.path / "echantillons" / "aigu.wav", 261.63);
    const auto profilePath = folder.path / "essai.profile.json";
    writeText(profilePath, kMinimalProfile);

    PatchRenderService service;
    PatchRenderRequest request;
    request.machineId = "vsm.multisample";
    request.profilePath = profilePath.string();
    request.durationSeconds = 0.3;
    request.notes.push_back({60, 100, 0.0, 0.25});
    request.parameters.emplace_back("filter.1.cutoff", 20000.0f);
    const auto response = service.render(request);
    VSM_ASSERT(response.error.empty());
    VSM_ASSERT(response.ok);
    VSM_ASSERT(response.peak > 0.05f);
}

/// La découverte des profils installés lit le dossier que `VSM_PROFILS`
/// désigne, et rend AUSSI les fichiers cassés, avec leur erreur -- un profil
/// fautif qui disparaîtrait de la liste enverrait chercher une installation
/// manquante là où il y a une installation défectueuse.
VSM_TEST(multisample_installed_profiles_are_discovered_including_broken_ones) {
    TempFolder folder("decouverte");
    writeSine(folder.path / "echantillons" / "grave.wav", 130.81);
    writeSine(folder.path / "echantillons" / "aigu.wav", 261.63);
    writeText(folder.path / "bon.profile.json", kMinimalProfile);
    writeText(folder.path / "casse.profile.json", R"({"format":"sfz","version":1})");
    writeText(folder.path / "pas-un-profil.txt", "rien");

    const std::string previous = std::getenv("VSM_PROFILS") ? std::getenv("VSM_PROFILS") : "";
#ifdef _WIN32
    _putenv_s("VSM_PROFILS", folder.path.string().c_str());
#else
    setenv("VSM_PROFILS", folder.path.string().c_str(), 1);
#endif
    VSM_ASSERT(multisampleProfileFolder() == folder.path.string());

    const auto found = installedMultisampleProfiles();
    VSM_ASSERT_EQ(found.size(), size_t(2)); // le .txt n'en est pas un
    // Triés par nom de fichier : « bon » avant « casse ».
    VSM_ASSERT(found[0].error.empty());     // bon.profile.json
    VSM_ASSERT(found[0].name == "Essai");
    VSM_ASSERT_EQ(found[0].zoneCount, 2);
    VSM_ASSERT(!found[1].error.empty());    // casse.profile.json, présent ET signalé
    VSM_ASSERT(found[1].error.find("format") != std::string::npos);

#ifdef _WIN32
    _putenv_s("VSM_PROFILS", previous.c_str());
#else
    if (previous.empty()) unsetenv("VSM_PROFILS"); else setenv("VSM_PROFILS", previous.c_str(), 1);
#endif
}

/// Un projet peut désigner son profil par son NOM au lieu d'un chemin : c'est
/// ce qui rend transportable un projet dont le profil pèse deux cents
/// mégaoctets. Le recopier dans chaque projet serait absurde, et un chemin
/// absolu ne s'ouvrirait que sur la machine qui l'a écrit.
VSM_TEST(multisample_preset_can_name_an_installed_profile) {
    TempFolder banque("installe");
    writeSine(banque.path / "echantillons" / "grave.wav", 130.81);
    writeSine(banque.path / "echantillons" / "aigu.wav", 261.63);
    writeText(banque.path / "piano.profile.json", kMinimalProfile);

    TempFolder projet("projet"); // dossier de projet SANS profil dedans

    const std::string previous = std::getenv("VSM_PROFILS") ? std::getenv("VSM_PROFILS") : "";
#ifdef _WIN32
    _putenv_s("VSM_PROFILS", banque.path.string().c_str());
#else
    setenv("VSM_PROFILS", banque.path.string().c_str(), 1);
#endif

    SynthPreset preset;
    preset.pluginId = "vsm.multisample";

    // 1. par nom de FICHIER dans le dossier des profils installés
    preset.profile = "piano.profile.json";
    auto synth = makeMultisample();
    auto report = applyPresetSamples(preset, *synth, projet.path.string());
    VSM_ASSERT(report.failures.empty());
    VSM_ASSERT_EQ(report.loaded.size(), size_t(1));

    // 2. par NOM DÉCLARÉ du profil
    preset.profile = "Essai";
    auto other = makeMultisample();
    report = applyPresetSamples(preset, *other, projet.path.string());
    VSM_ASSERT(report.failures.empty());

    // 3. inconnu : refusé, en disant OÙ il a cherché
    preset.profile = "Inexistant";
    auto third = makeMultisample();
    report = applyPresetSamples(preset, *third, projet.path.string());
    VSM_ASSERT_EQ(report.failures.size(), size_t(1));
    VSM_ASSERT(report.failures[0].find("Cherché") != std::string::npos);

#ifdef _WIN32
    _putenv_s("VSM_PROFILS", previous.c_str());
#else
    if (previous.empty()) unsetenv("VSM_PROFILS"); else setenv("VSM_PROFILS", previous.c_str(), 1);
#endif
}

/// Le cache d'échantillons ne change RIEN au son. C'est la condition à laquelle
/// il est acceptable : il partage de la donnée décodée entre instances, jamais
/// de l'état de machine, et l'invariant « deux rendus identiques donnent le
/// même audio au bit près » doit tenir avec lui comme sans lui.
VSM_TEST(multisample_sample_cache_changes_speed_not_sound) {
    TempFolder folder("cache");
    writeSine(folder.path / "echantillons" / "grave.wav", 130.81);
    writeSine(folder.path / "echantillons" / "aigu.wav", 261.63);
    const auto profilePath = folder.path / "essai.profile.json";
    writeText(profilePath, kMinimalProfile);

    auto rendu = [&](vsm::audio::plugin::MultisampleSampleCache* cache) {
        auto synth = makeMultisample();
        const auto report = applyMultisampleProfile(*synth, profilePath.string(), cache);
        VSM_ASSERT(report.applied);
        std::vector<float> left(24000, 0.0f), right(24000, 0.0f);
        vsm::audio::plugin::MidiNoteEvent on{
            vsm::audio::plugin::MidiNoteEvent::Kind::NoteOn, 0, 0, 60, 100};
        synth->process(&on, 1, left.data(), right.data(), 24000);
        return left;
    };

    vsm::audio::plugin::MultisampleSampleCache cache;
    const auto sansCache = rendu(nullptr);
    const auto avecCacheFroid = rendu(&cache);   // remplit le cache
    const auto avecCacheChaud = rendu(&cache);   // le relit

    VSM_ASSERT(cache.bytes() > 0u);
    VSM_ASSERT_EQ(sansCache.size(), avecCacheFroid.size());
    for (size_t i = 0; i < sansCache.size(); ++i) {
        VSM_ASSERT(sansCache[i] == avecCacheFroid[i]);
        VSM_ASSERT(sansCache[i] == avecCacheChaud[i]);
    }
}

/// Un profil dont le NOM est aussi celui de son dossier d'échantillons — ce que
/// produit `vsm-sf2` — doit se résoudre vers le FICHIER, pas vers le dossier.
///
/// Trouvé en installant une deuxième banque de piano. La première n'avait pas
/// la collision (« Salamander Grand Piano » contre le dossier
/// « piano-salamander ») ; la seconde l'avait, et la machine rendait du silence
/// sans un mot. Une seule banque d'essai n'aurait jamais montré ce défaut.
VSM_TEST(multisample_profile_name_that_collides_with_a_folder_resolves_to_the_file) {
    TempFolder banque("collision");
    writeSine(banque.path / "echantillons" / "grave.wav", 130.81);
    writeSine(banque.path / "echantillons" / "aigu.wav", 261.63);
    writeText(banque.path / "Essai.profile.json", kMinimalProfile);
    // Le piège : un DOSSIER portant exactement le nom déclaré du profil.
    std::filesystem::create_directories(banque.path / "Essai");

    TempFolder projet("collision-projet");
    const std::string previous = std::getenv("VSM_PROFILS") ? std::getenv("VSM_PROFILS") : "";
#ifdef _WIN32
    _putenv_s("VSM_PROFILS", banque.path.string().c_str());
#else
    setenv("VSM_PROFILS", banque.path.string().c_str(), 1);
#endif

    SynthPreset preset;
    preset.pluginId = "vsm.multisample";
    preset.profile = "Essai";           // nom déclaré ET nom du dossier
    auto synth = makeMultisample();
    const auto report = applyPresetSamples(preset, *synth, projet.path.string());

#ifdef _WIN32
    _putenv_s("VSM_PROFILS", previous.c_str());
#else
    if (previous.empty()) unsetenv("VSM_PROFILS"); else setenv("VSM_PROFILS", previous.c_str(), 1);
#endif

    VSM_ASSERT(report.failures.empty());
    VSM_ASSERT_EQ(report.loaded.size(), size_t(1));
    auto* bank = dynamic_cast<vsm::audio::plugin::IMultisampleBank*>(synth.get());
    VSM_ASSERT(bank != nullptr);
    VSM_ASSERT_EQ(bank->zoneCount(), 2);
}
