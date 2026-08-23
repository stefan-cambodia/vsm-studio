#include "TestFramework.h"
#include "vsm/interchange/SoundFont.h"
#include "vsm/interchange/MultisampleProfile.h"
#include "vsm/interchange/SynthPreset.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace vsm::interchange;
using vsm::audio::plugin::PluginRegistry;

namespace {

struct TempFolder {
    std::filesystem::path path;
    explicit TempFolder(const std::string& name)
        : path(std::filesystem::temp_directory_path() / ("vsm-sf2-" + name)) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempFolder() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};

/// Écrit le SF2 minimal et rend son chemin.
std::string minimalSoundFont(const TempFolder& folder) {
    const auto path = (folder.path / "minimal.sf2").string();
    std::string error;
    VSM_ASSERT(writeMinimalSoundFont(path, error));
    VSM_ASSERT(error.empty());
    return path;
}

} // namespace

VSM_TEST(soundfont_minimal_is_readable_and_declares_its_preset) {
    TempFolder folder("index");
    const auto path = minimalSoundFont(folder);

    const auto index = readSoundFontIndex(path);
    VSM_ASSERT(index.error.empty());
    VSM_ASSERT(index.success);
    VSM_ASSERT_EQ(index.majorVersion, 2);
    VSM_ASSERT(index.bankName == "VSM SF2 minimal");
    VSM_ASSERT_EQ(index.presets.size(), size_t(1));
    VSM_ASSERT(index.presets[0].name == "Essai minimal");
    VSM_ASSERT_EQ(index.presets[0].program, 0);
    VSM_ASSERT_EQ(index.presets[0].bank, 0);
}

/// L'ALLER-RETOUR COMPLET du § 8.6 du cahier des charges : le SF2 minimal est
/// engendré, converti, écrit en profil, rechargé par la machine, et ce qu'elle
/// joue correspond à ce que le SF2 déclarait.
VSM_TEST(soundfont_round_trips_into_a_playable_profile) {
    TempFolder folder("roundtrip");
    const auto path = minimalSoundFont(folder);

    const auto conversion = convertSoundFontPreset(path, 0, 0);
    VSM_ASSERT(conversion.error.empty());
    VSM_ASSERT(conversion.success);
    VSM_ASSERT_EQ(conversion.zones.size(), size_t(4)); // 2 étendues × 2 couches

    // Les étendues déclarées se retrouvent telles quelles. La vélocité basse
    // passe de 0 (convention SF2) à 1 (convention du profil) : une vélocité
    // nulle est un note-off, pas une nuance.
    bool sawLowLayer = false, sawHighLayer = false;
    for (const auto& zone : conversion.zones) {
        VSM_ASSERT(zone.lowVelocity >= 1);
        VSM_ASSERT(zone.loopEnabled);
        VSM_ASSERT(zone.loopEnd > zone.loopStart);
        if (zone.lowNote == 48 && zone.highNote == 60) {
            VSM_ASSERT_EQ(zone.rootNote, 57);
            if (zone.highVelocity == 63) { sawLowLayer = true; VSM_ASSERT_NEAR(zone.level, 0.5012f, 1e-3f); }
            if (zone.lowVelocity == 64) { sawHighLayer = true; VSM_ASSERT_NEAR(zone.level, 1.0f, 1e-4f); }
        }
        if (zone.lowNote == 61) VSM_ASSERT_EQ(zone.rootNote, 69);
    }
    VSM_ASSERT(sawLowLayer);
    VSM_ASSERT(sawHighLayer);

    // L'enveloppe de volume déclarée par la zone GLOBALE de l'instrument est
    // remontée : 2^(-7200/1200) = 15,6 ms d'attaque, 2^(-1200/1200) = 500 ms.
    VSM_ASSERT_NEAR(conversion.attackSeconds, 0.015625f, 1e-4f);
    VSM_ASSERT_NEAR(conversion.releaseSeconds, 0.5f, 1e-4f);

    // Écriture, puis relecture par le chemin normal du DAW.
    const auto written = writeSoundFontProfile(conversion, folder.path.string(), "essai",
                                                "engendré par les tests");
    VSM_ASSERT(written.error.empty());
    VSM_ASSERT(written.success);

    auto loaded = loadMultisampleProfileFile(written.profilePath);
    VSM_ASSERT(loaded.error.empty());
    VSM_ASSERT_EQ(loaded.spec.zones.size(), size_t(4));

    vsm::audio::plugin::registerBuiltInPlugins();
    auto synth = PluginRegistry::instance().create("vsm.multisample");
    VSM_ASSERT(synth != nullptr);
    synth->initialize(44100.0, 256);
    const auto applied = applyMultisampleProfile(*synth, written.profilePath);
    VSM_ASSERT(applied.error.empty());
    VSM_ASSERT_EQ(applied.zoneCount, 4);

    // Et ça sonne, à la bonne hauteur : la note 57 est la racine de la zone
    // grave, donc elle doit rendre les 220 Hz du SF2.
    std::vector<float> left(22050, 0.0f), right(22050, 0.0f);
    vsm::audio::plugin::MidiNoteEvent on{
        vsm::audio::plugin::MidiNoteEvent::Kind::NoteOn, 0, 0, 57, 100};
    synth->process(&on, 1, left.data(), right.data(), 22050);

    float peak = 0.0f;
    for (float value : left) peak = std::max(peak, std::abs(value));
    VSM_ASSERT(peak > 0.05f);

    // Fréquence mesurée entre le PREMIER et le DERNIER passage par zéro
    // montant, pas sur une fenêtre fixe : compter les passages d'une fenêtre
    // arbitraire perd jusqu'à une période aux bords, ce qui vaut ici vingt-six
    // cents — assez pour faire échouer un test juste.
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for (size_t i = 4411; i < 17640; ++i) {
        if (left[i - 1] < 0.0f && left[i] >= 0.0f) {
            const double t = static_cast<double>(i - 1)
                           + static_cast<double>(-left[i - 1]) / static_cast<double>(left[i] - left[i - 1]);
            if (first < 0.0) first = t; else { last = t; ++crossings; }
        }
    }
    VSM_ASSERT(crossings > 10);
    const double measured = static_cast<double>(crossings) * 44100.0 / (last - first);
    VSM_ASSERT(std::abs(1200.0 * std::log2(measured / 220.0)) < 5.0);

    // Le preset écrit à côté porte l'enveloppe et DÉSIGNE le profil.
    std::ifstream presetFile(written.presetPath, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(presetFile)),
                            std::istreambuf_iterator<char>());
    auto preset = parseSynthPreset(text);
    VSM_ASSERT(preset.success);
    VSM_ASSERT(preset.preset.pluginId == "vsm.multisample");
    VSM_ASSERT(preset.preset.profile == "essai.profile.json");
    VSM_ASSERT_NEAR(preset.preset.valueOr("envelope.1.release", 0.0f), 0.5f, 1e-4f);
}

/// Les générateurs non pris en charge sont NOMMÉS, jamais avalés. Le SF2
/// minimal en pose un exprès (`initialFilterQ`) : sans ce rapport, une banque
/// dont le caractère tient à son filtre se convertirait en silence, et
/// personne ne saurait pourquoi elle sonne plat.
VSM_TEST(soundfont_names_the_generators_it_does_not_apply) {
    TempFolder folder("ignores");
    const auto conversion = convertSoundFontPreset(minimalSoundFont(folder), 0, 0);
    VSM_ASSERT(conversion.success);

    bool sawFilterQ = false;
    for (const auto& [name, count] : conversion.ignoredGenerators)
        if (name == "initialFilterQ") { sawFilterQ = true; VSM_ASSERT_EQ(count, 4); }
    VSM_ASSERT(sawFilterQ);
}

VSM_TEST(soundfont_refuses_what_it_cannot_read) {
    TempFolder folder("refus");

    auto absent = readSoundFontIndex((folder.path / "inexistant.sf2").string());
    VSM_ASSERT(!absent.success);
    VSM_ASSERT(absent.error.find("illisible") != std::string::npos);

    const auto notRiff = (folder.path / "pas-riff.sf2").string();
    { std::ofstream f(notRiff, std::ios::binary); f << "ceci n'est pas un RIFF, pas du tout"; }
    auto wrong = readSoundFontIndex(notRiff);
    VSM_ASSERT(!wrong.success);
    VSM_ASSERT(wrong.error.find("RIFF") != std::string::npos);

    // Preset inexistant : refusé en nommant ce qui a été demandé.
    const auto path = minimalSoundFont(folder);
    auto missing = convertSoundFontPreset(path, 0, 42);
    VSM_ASSERT(!missing.success);
    VSM_ASSERT(missing.error.find("42") != std::string::npos);

    // Profil sans attribution : refusé à l'écriture, comme partout ailleurs.
    const auto conversion = convertSoundFontPreset(path, 0, 0);
    auto written = writeSoundFontProfile(conversion, folder.path.string(), "essai", "");
    VSM_ASSERT(!written.success);
    VSM_ASSERT(written.error.find("attribution") != std::string::npos);
}
