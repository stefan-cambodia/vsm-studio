#include "TestFramework.h"
#include "vsm/interchange/BrowserIndex.h"
#include "vsm/interchange/EffectPreset.h"
#include <filesystem>
#include <fstream>

using namespace vsm::interchange;
namespace fs = std::filesystem;

// D15.4 -- LES PRESETS D'EFFET. Un aller-retour disque EXACT : ce qui sort du
// fichier est ce qui y est entré, valeur pour valeur, et un preset se
// distingue d'un preset de machine par son extension et son format.

VSM_TEST(an_effect_preset_round_trips_through_json_exactly) {
    vsm::sequencer::TrackEffect insert;
    insert.type = "reverb";
    insert.parameters = {{"reverb.1.mix", 0.35f}, {"reverb.1.size", 0.8125f}, {"reverb.1.damping", 0.1f}};
    insert.enabled = false;   // le contournement n'est PAS un réglage de l'effet
    const EffectPreset preset = effectPresetFromDescription(insert, "Salle claire");
    VSM_ASSERT_EQ(preset.name, std::string("Salle claire"));
    VSM_ASSERT_EQ(preset.type, std::string("reverb"));

    const std::string texte = effectPresetToJson(preset).toString();
    const EffectPresetLoadResult relu = parseEffectPreset(texte);
    VSM_ASSERT(relu.success);
    VSM_ASSERT_EQ(relu.preset.name, preset.name);
    VSM_ASSERT_EQ(relu.preset.type, preset.type);
    VSM_ASSERT_EQ(relu.preset.parameters.size(), size_t(3));
    for (const auto& [id, valeur] : preset.parameters) VSM_ASSERT_EQ(relu.preset.parameters.at(id), valeur);
    VSM_ASSERT(relu.preset.nativeState.empty());
    VSM_ASSERT(texte.find("nativeState") == std::string::npos);

    const auto retour = descriptionFromEffectPreset(relu.preset);
    VSM_ASSERT_EQ(retour.type, insert.type);
    VSM_ASSERT(retour.parameters == insert.parameters);
    VSM_ASSERT(retour.enabled);   // un preset chargé est actif, quoi qu'ait été l'insert d'origine
}

VSM_TEST(a_third_party_effect_preset_keeps_its_native_state) {
    EffectPreset preset;
    preset.name = "Convolution";
    preset.type = "clap:/usr/lib/clap/ir.clap#org.example.ir";
    preset.nativeState = "QUJDRA==";
    const EffectPresetLoadResult relu = parseEffectPreset(effectPresetToJson(preset).toString());
    VSM_ASSERT(relu.success);
    VSM_ASSERT_EQ(relu.preset.nativeState, std::string("QUJDRA=="));
    VSM_ASSERT_EQ(relu.preset.type, preset.type);
}

VSM_TEST(a_synth_preset_or_a_wrong_version_is_refused_by_name) {
    const auto machine = parseEffectPreset(R"({"format":"vsm-synth-preset","version":1,"type":"reverb","parameters":{}})");
    VSM_ASSERT(!machine.success);
    VSM_ASSERT(machine.error.find("vsm-synth-preset") != std::string::npos);
    const auto version = parseEffectPreset(R"({"format":"vsm-effect-preset","version":7,"type":"reverb","parameters":{}})");
    VSM_ASSERT(!version.success);
    VSM_ASSERT(version.error.find("7") != std::string::npos);
    const auto sansType = parseEffectPreset(R"({"format":"vsm-effect-preset","version":1,"parameters":{}})");
    VSM_ASSERT(!sansType.success);
    VSM_ASSERT(!parseEffectPreset("{").success);
}

VSM_TEST(the_browser_lists_effect_presets_as_their_own_kind_without_the_double_extension) {
    const fs::path racine = fs::temp_directory_path() / "vsm-d154";
    fs::remove_all(racine);
    fs::create_directories(racine / "effets");
    std::ofstream(racine / "effets" / "Salle claire.effect.json") << "{}";
    std::ofstream(racine / "effets" / "basse.synth.json") << "{}";
    std::vector<BrowserItem> entrees;
    indexFolder(racine.string(), "Bibliothèque", entrees);
    VSM_ASSERT_EQ(entrees.size(), size_t(2));
    size_t effets = 0;
    for (const auto& e : entrees) {
        if (e.kind != BrowserItemKind::EffectPreset) continue;
        ++effets;
        VSM_ASSERT_EQ(e.name, std::string("Salle claire"));
        VSM_ASSERT(isEffectPresetFile(e.reference));
    }
    VSM_ASSERT_EQ(effets, size_t(1));
    VSM_ASSERT(isEffectPresetFile("X.EFFECT.JSON"));
    VSM_ASSERT(!isEffectPresetFile("x.synth.json"));
    fs::remove_all(racine);
}
