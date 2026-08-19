#include "TestFramework.h"
#include "vsm/interchange/SynthPreset.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <cmath>

using namespace vsm::interchange;
using vsm::audio::plugin::PluginRegistry;
using vsm::audio::plugin::SynthPluginPtr;

namespace {
SynthPluginPtr makePlugin(const std::string& id) {
    vsm::audio::plugin::registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create(id);
    if (plugin) plugin->initialize(48000.0, 512);
    return plugin;
}

vsm::audio::plugin::ParamId paramByName(const vsm::audio::plugin::ISynthPlugin& plugin, const std::string& name) {
    for (const auto& info : plugin.parameterList())
        if (info.name == name) return info.id;
    return 0;
}
} // namespace

VSM_TEST(preset_captures_the_current_state_of_a_machine) {
    auto minimoog = makePlugin("vsm.minimoog");
    VSM_ASSERT(minimoog != nullptr);
    minimoog->setParameter(paramByName(*minimoog, "Filter Cutoff"), 900.0f);

    const SynthPreset preset = capturePreset(*minimoog, "vsm.minimoog", "Basse chaude");
    VSM_ASSERT_EQ(preset.name, std::string("Basse chaude"));
    VSM_ASSERT_EQ(preset.pluginId, std::string("vsm.minimoog"));
    VSM_ASSERT_NEAR(preset.valueOr("filter.1.cutoff", -1.0f), 900.0f, 0.01f);
    VSM_ASSERT(preset.values.size() > 10);
}

VSM_TEST(preset_round_trips_through_json) {
    auto juno = makePlugin("vsm.juno106");
    juno->setParameter(paramByName(*juno, "VCF Cutoff"), 1234.5f);
    juno->setParameter(paramByName(*juno, "Env Attack"), 0.25f);

    const SynthPreset original = capturePreset(*juno, "vsm.juno106", "Nappe");
    const std::string text = synthPresetToJson(original).toString();

    const SynthPresetLoadResult loaded = parseSynthPreset(text);
    VSM_ASSERT(loaded.success);
    VSM_ASSERT_EQ(loaded.preset.name, original.name);
    VSM_ASSERT_EQ(loaded.preset.pluginId, original.pluginId);
    VSM_ASSERT_EQ(loaded.preset.values.size(), original.values.size());
    for (const auto& [semanticId, value] : original.values)
        VSM_ASSERT_NEAR(loaded.preset.valueOr(semanticId, -999.0f), value, 1e-6);
}

VSM_TEST(preset_restores_the_sound_of_the_same_machine) {
    // Le critère qui compte : sauvegarder, tout dérégler, recharger, et
    // retrouver EXACTEMENT les mêmes valeurs -- pas seulement un fichier bien
    // formé.
    auto source = makePlugin("vsm.jupiter8");
    source->setParameter(paramByName(*source, "Filter Cutoff"), 3000.0f);
    source->setParameter(paramByName(*source, "Filter Resonance"), 2.5f);
    source->setParameter(paramByName(*source, "VCO-2 Detune"), 5.0f);
    const SynthPreset preset = capturePreset(*source, "vsm.jupiter8", "Brass");

    auto target = makePlugin("vsm.jupiter8");
    target->setParameter(paramByName(*target, "Filter Cutoff"), 200.0f); // volontairement à côté
    const PresetApplyReport report = applyPreset(preset, *target, "vsm.jupiter8");

    VSM_ASSERT_EQ(report.unsupportedCount(), size_t{0});
    VSM_ASSERT_NEAR(target->getParameter(paramByName(*target, "Filter Cutoff")), 3000.0f, 0.01f);
    VSM_ASSERT_NEAR(target->getParameter(paramByName(*target, "Filter Resonance")), 2.5f, 0.01f);
    VSM_ASSERT_NEAR(target->getParameter(paramByName(*target, "VCO-2 Detune")), 5.0f, 0.01f);
}

VSM_TEST(preset_applied_to_another_machine_reports_what_it_could_not_do) {
    // Un preset de Jupiter-8 appliqué à un TB-303 : la coupure et la résonance
    // se transposent, la cross-mod n'existe pas. Le rapport doit le DIRE --
    // c'est la règle "aucune approximation silencieuse".
    auto jupiter = makePlugin("vsm.jupiter8");
    const SynthPreset preset = capturePreset(*jupiter, "vsm.jupiter8", "Pad");

    auto tb303 = makePlugin("vsm.tb303");
    const PresetApplyReport report = applyPreset(preset, *tb303, "vsm.tb303");

    VSM_ASSERT(report.appliedCount() > 0);
    VSM_ASSERT(report.unsupportedCount() > 0);
    VSM_ASSERT(report.summary().find("non pris en charge") != std::string::npos);

    bool cutoffApplied = false;
    for (const auto& entry : report.entries)
        if (entry.semanticId == "filter.1.cutoff" && entry.status != SupportStatus::Unsupported)
            cutoffApplied = true;
    VSM_ASSERT(cutoffApplied); // le vocabulaire commun a bien servi à quelque chose
}

VSM_TEST(out_of_range_values_are_clamped_and_reported) {
    auto tb303 = makePlugin("vsm.tb303");
    SynthPreset preset;
    preset.pluginId = "vsm.tb303";
    preset.values["filter.1.cutoff"] = 50000.0f; // bien au-delà de la plage réelle

    const PresetApplyReport report = applyPreset(preset, *tb303, "vsm.tb303");
    VSM_ASSERT_EQ(report.clampedCount(), size_t{1});
    // Comparé via son nom : un enum class ne s'affiche pas tout seul, et un
    // échec doit dire "approximated attendu, unsupported obtenu", pas "2 != 1".
    VSM_ASSERT_EQ(std::string(supportStatusName(report.entries.front().status)),
                   std::string("approximated"));
    VSM_ASSERT(report.entries.front().appliedValue < 50000.0f);
    VSM_ASSERT(report.summary().find("borné") != std::string::npos);
}

VSM_TEST(preset_refuses_an_unknown_format_or_version) {
    // Refus explicite : charger un fichier d'un format inconnu "au mieux"
    // produirait un son faux sans prévenir.
    const auto wrongFormat = parseSynthPreset(R"({"format":"autre-chose","version":1,"parameters":{}})");
    VSM_ASSERT(!wrongFormat.success);
    VSM_ASSERT(wrongFormat.error.find("format") != std::string::npos);

    const auto futureVersion = parseSynthPreset(R"({"format":"vsm-synth-preset","version":99,"parameters":{}})");
    VSM_ASSERT(!futureVersion.success);
    VSM_ASSERT(futureVersion.error.find("version") != std::string::npos);

    const auto brokenJson = parseSynthPreset("{ pas du json");
    VSM_ASSERT(!brokenJson.success);
    VSM_ASSERT(brokenJson.error.find("JSON invalide") != std::string::npos);
}

VSM_TEST(preset_json_is_human_readable_and_semantic) {
    // Un fichier de preset doit se lire et se relire à l'œil : c'est ce qui
    // permet à quelqu'un (ou à un script Python) de l'écrire à la main.
    auto minimoog = makePlugin("vsm.minimoog");
    const std::string text = synthPresetToJson(capturePreset(*minimoog, "vsm.minimoog", "Init")).toString();
    VSM_ASSERT(text.find("\"format\": \"vsm-synth-preset\"") != std::string::npos);
    VSM_ASSERT(text.find("\"filter.1.cutoff\"") != std::string::npos);
    VSM_ASSERT(text.find("\"envelope.1.attack\"") != std::string::npos);
    VSM_ASSERT(text.find("\"fidelity\": \"derived\"") != std::string::npos); // jamais "measured"
}

VSM_TEST(a_preset_written_by_hand_is_accepted) {
    // Le cas d'usage visé par la Phase 7 : un script Python produit ce fichier
    // sans jamais avoir vu le code du DAW.
    const char* handWritten = R"({
        "format": "vsm-synth-preset",
        "version": 1,
        "name": "Acid depuis Python",
        "pluginId": "vsm.tb303",
        "parameters": {
            "filter.1.cutoff": 700,
            "filter.1.resonance": 0.8,
            "accent.amount": 0.6
        }
    })";
    const auto loaded = parseSynthPreset(handWritten);
    VSM_ASSERT(loaded.success);
    VSM_ASSERT_EQ(loaded.preset.values.size(), size_t{3});

    auto tb303 = makePlugin("vsm.tb303");
    const PresetApplyReport report = applyPreset(loaded.preset, *tb303, "vsm.tb303");
    VSM_ASSERT_EQ(report.unsupportedCount(), size_t{0});
    VSM_ASSERT_NEAR(tb303->getParameter(paramByName(*tb303, "Cutoff")), 700.0f, 0.01f);
    VSM_ASSERT_NEAR(tb303->getParameter(paramByName(*tb303, "Accent")), 0.6f, 0.01f);
}
