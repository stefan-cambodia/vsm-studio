#include "TestFramework.h"
#include "vsm/interchange/ClapParameterIds.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <map>
#include <set>

using namespace vsm::interchange;

VSM_TEST(clap_ids_are_deterministic) {
    // Un hôte mémorise ces identifiants dans SES projets : ils doivent être
    // les mêmes à chaque exécution, sur chaque machine, dans chaque build.
    VSM_ASSERT_EQ(clapParameterId("filter.1.cutoff"), clapParameterId("filter.1.cutoff"));
    VSM_ASSERT(clapParameterId("filter.1.cutoff") != clapParameterId("filter.1.resonance"));
    VSM_ASSERT(clapParameterId("filter.1.cutoff") != clapParameterId("filter.2.cutoff"));
}

VSM_TEST(clap_ids_are_frozen_values) {
    // Valeurs GELÉES. Si ce test casse, c'est que la fonction de hachage a
    // changé -- et avec elle, tous les projets des utilisateurs qui
    // automatisaient ces paramètres dans un hôte CLAP. Le seul correctif
    // acceptable est de restaurer l'ancien hachage, jamais de mettre à jour
    // ces nombres.
    VSM_ASSERT_EQ(clapParameterId("filter.1.cutoff"), 2023045386u);
    VSM_ASSERT_EQ(clapParameterId("envelope.1.attack"), 592361751u);
    VSM_ASSERT_EQ(clapParameterId("voice.glideTime"), 814340599u);
}

VSM_TEST(clap_ids_never_collide_across_every_machine) {
    // Le prix d'un hachage est le risque de collision : il se vérifie, il ne
    // se suppose pas. Deux paramètres partageant un clap_id rendraient l'un
    // des deux inatteignable depuis l'hôte.
    vsm::audio::plugin::registerBuiltInPlugins();
    std::map<uint32_t, std::string> seen;
    size_t total = 0;
    for (const std::string& pluginId : knownSemanticPluginIds()) {
        for (const auto& mapping : clapParameterMap(pluginId)) {
            ++total;
            auto existing = seen.find(mapping.clapId);
            if (existing != seen.end() && existing->second != mapping.semanticId)
                throw vsm::test::AssertionFailure("collision de clap_id entre \"" + existing->second +
                                                   "\" et \"" + mapping.semanticId + "\"");
            seen[mapping.clapId] = mapping.semanticId;
        }
    }
    VSM_ASSERT(total > 300); // toutes les machines et tous les effets ont bien été parcourus
}

VSM_TEST(clap_ids_are_never_the_invalid_sentinel) {
    // CLAP_INVALID_ID vaut 0xFFFFFFFF : un paramètre portant cette valeur
    // serait ignoré par l'hôte.
    for (const std::string& pluginId : knownSemanticPluginIds())
        for (const auto& mapping : clapParameterMap(pluginId)) {
            VSM_ASSERT(mapping.clapId != 0xFFFFFFFFu);
            VSM_ASSERT(mapping.clapId < 0x80000000u); // tient dans un entier signé
        }
}

VSM_TEST(clap_map_keeps_ranges_and_names_of_the_machine) {
    const auto mappings = clapParameterMap("vsm.minimoog");
    VSM_ASSERT(!mappings.empty());
    bool foundCutoff = false;
    for (const auto& mapping : mappings) {
        VSM_ASSERT(mapping.maximum > mapping.minimum);
        VSM_ASSERT(mapping.defaultValue >= mapping.minimum && mapping.defaultValue <= mapping.maximum);
        if (mapping.semanticId == "filter.1.cutoff") {
            foundCutoff = true;
            VSM_ASSERT_EQ(mapping.displayName, std::string("Filter Cutoff"));
        }
    }
    VSM_ASSERT(foundCutoff);
}

VSM_TEST(clap_plugin_ids_follow_a_reverse_domain_convention) {
    VSM_ASSERT_EQ(clapPluginId("vsm.minimoog"), std::string("com.vsmstudio.minimoog"));
    VSM_ASSERT_EQ(clapPluginId("vsm.tb303"), std::string("com.vsmstudio.tb303"));

    // Unicité : deux machines ne peuvent pas partager un identifiant de plugin.
    vsm::audio::plugin::registerBuiltInPlugins();
    std::set<std::string> ids;
    for (const auto& [vsmId, displayName] : vsm::audio::plugin::PluginRegistry::instance().listAvailable()) {
        if (vsmId.rfind("vsm.", 0) != 0) continue;
        VSM_ASSERT(ids.insert(clapPluginId(vsmId)).second);
    }
    VSM_ASSERT(ids.size() >= 12);
}
