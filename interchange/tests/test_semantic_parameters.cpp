#include "TestFramework.h"
#include "vsm/interchange/ParameterDescriptor.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include "vsm/audio/effect/EffectFactory.h"
#include <set>

using namespace vsm::interchange;

VSM_TEST(every_machine_has_a_semantic_profile) {
    vsm::audio::plugin::registerBuiltInPlugins();
    for (const auto& [id, displayName] : vsm::audio::plugin::PluginRegistry::instance().listAvailable()) {
        if (id.rfind("vsm.", 0) != 0) continue; // plugins factices des tests
        const SemanticProfile profile = buildSemanticProfile(id);
        VSM_ASSERT(!profile.empty());
        VSM_ASSERT_EQ(profile.pluginId(), id);
    }
}

VSM_TEST(no_parameter_of_any_machine_is_left_without_a_semantic_id) {
    // LE test de complétude. Ajouter une machine (ou un paramètre) sans lui
    // donner d'identité sémantique casse ici, à l'endroit exact du manque,
    // plutôt que de produire silencieusement un preset amputé.
    vsm::audio::plugin::registerBuiltInPlugins();
    for (const auto& [id, displayName] : vsm::audio::plugin::PluginRegistry::instance().listAvailable()) {
        if (id.rfind("vsm.", 0) != 0) continue;
        const SemanticProfile profile = buildSemanticProfile(id);
        for (const auto& descriptor : profile.parameters()) {
            if (!descriptor.semanticId.empty()) continue;
            throw vsm::test::AssertionFailure(
                "paramètre sans identité sémantique : " + id + " / \"" + descriptor.displayName +
                "\" -- ajouter l'entrée correspondante dans interchange/src/ParameterDescriptor.cpp");
        }
    }
}

VSM_TEST(every_effect_has_a_semantic_profile) {
    for (const auto& info : vsm::audio::effect::EffectFactory::available()) {
        const SemanticProfile profile = buildSemanticProfile("fx." + info.id);
        VSM_ASSERT(!profile.empty());
        for (const auto& descriptor : profile.parameters()) {
            if (!descriptor.semanticId.empty()) continue;
            throw vsm::test::AssertionFailure("effet sans identité sémantique : " + info.id +
                                               " / \"" + descriptor.displayName + "\"");
        }
    }
}

VSM_TEST(semantic_ids_are_unique_within_a_machine) {
    // Deux paramètres partageant une identité s'écraseraient l'un l'autre à
    // l'import d'un preset -- et la perte serait silencieuse.
    vsm::audio::plugin::registerBuiltInPlugins();
    for (const std::string& id : knownSemanticPluginIds()) {
        const SemanticProfile profile = buildSemanticProfile(id);
        std::set<std::string> seen;
        for (const auto& descriptor : profile.parameters()) {
            if (descriptor.semanticId.empty()) continue;
            if (!seen.insert(descriptor.semanticId).second)
                throw vsm::test::AssertionFailure("identité sémantique en double dans " + id + " : " +
                                                   descriptor.semanticId);
        }
    }
}

VSM_TEST(the_same_semantic_id_designates_the_same_thing_across_machines) {
    // C'est tout l'intérêt du vocabulaire : "filter.1.cutoff" doit désigner la
    // coupure du filtre principal, que la machine l'appelle "Filter Cutoff"
    // (Minimoog), "VCF Cutoff" (Juno-106), "Cutoff" (TB-303) ou "LPF Cutoff"
    // (MS-20). Un script Python qui écrit filter.1.cutoff = 800 doit agir sur
    // le bon bouton partout.
    for (const char* machine : {"vsm.minimoog", "vsm.juno106", "vsm.tb303", "vsm.ms20",
                                 "vsm.jupiter8", "vsm.prophet", "vsm.sh101"}) {
        const SemanticProfile profile = buildSemanticProfile(machine);
        const ParameterDescriptor* cutoff = profile.findBySemanticId("filter.1.cutoff");
        VSM_ASSERT(cutoff != nullptr);
        VSM_ASSERT_EQ(cutoff->unit, std::string("Hz")); // vraiment une fréquence
        VSM_ASSERT(cutoff->maximum > cutoff->minimum);
        VSM_ASSERT(profile.findBySemanticId("filter.1.resonance") != nullptr);
    }
}

VSM_TEST(envelope_vocabulary_is_consistent) {
    // envelope.1 = enveloppe d'amplitude, sur toutes les machines qui en ont
    // une réglable. Convention documentée : sans elle, un preset appliquerait
    // l'attaque du filtre au VCA d'une autre machine.
    for (const char* machine : {"vsm.minimoog", "vsm.juno106", "vsm.sh101", "vsm.jupiter8", "vsm.prophet"}) {
        const SemanticProfile profile = buildSemanticProfile(machine);
        VSM_ASSERT(profile.findBySemanticId("envelope.1.attack") != nullptr);
        VSM_ASSERT(profile.findBySemanticId("envelope.1.decay") != nullptr);
    }
}

VSM_TEST(descriptors_carry_real_ranges_and_units) {
    const SemanticProfile profile = buildSemanticProfile("vsm.minimoog");
    const ParameterDescriptor* cutoff = profile.findBySemanticId("filter.1.cutoff");
    VSM_ASSERT(cutoff != nullptr);
    // Valeurs en unités PHYSIQUES, pas normalisées : c'est ce qui permet à un
    // script extérieur d'écrire "1200 Hz" sans connaître la machine.
    VSM_ASSERT(cutoff->minimum >= 1.0f && cutoff->maximum >= 10000.0f);
    VSM_ASSERT_EQ(cutoff->module, std::string("filter"));
    VSM_ASSERT_EQ(cutoff->displayName, std::string("Filter Cutoff"));
    VSM_ASSERT(cutoff->paramId != 0 || profile.parameters().front().paramId == cutoff->paramId);
}

VSM_TEST(fidelity_is_never_claimed_as_measured) {
    // Aucune machine du projet n'a été comparée à du matériel réel : annoncer
    // "measured" serait un mensonge (ARCHITECTURE.md § 27).
    for (const std::string& id : knownSemanticPluginIds()) {
        const SemanticProfile profile = buildSemanticProfile(id);
        for (const auto& descriptor : profile.parameters())
            VSM_ASSERT(descriptor.fidelity != Fidelity::Measured);
    }
}

VSM_TEST(unknown_plugin_gives_an_empty_profile_not_a_crash) {
    const SemanticProfile profile = buildSemanticProfile("vsm.machine-qui-nexiste-pas");
    VSM_ASSERT(profile.empty());
    VSM_ASSERT(profile.findBySemanticId("filter.1.cutoff") == nullptr);
}
