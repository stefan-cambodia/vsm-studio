#include "TestFramework.h"
#include "vsm/audio/effect/EffectFactory.h"
#include "vsm/interchange/EffectDescription.h"

using namespace vsm::interchange;
using vsm::audio::effect::EffectFactory;

VSM_TEST(an_effect_describes_itself_by_semantic_names) {
    auto reverb = EffectFactory::create("reverb");
    VSM_ASSERT(reverb != nullptr);

    const auto described = describeEffect("reverb", *reverb);
    VSM_ASSERT_EQ(described.type, std::string("reverb"));
    VSM_ASSERT_EQ(described.parameters.size(), reverb->parameterList().size());

    // AUCUN paramètre n'est nommé par son numéro : une clé purement numérique
    // signifierait qu'on écrit une position dans une liste, ce qui se décale
    // dès qu'un réglage est intercalé.
    for (const auto& [key, value] : described.parameters) {
        (void)value;
        VSM_ASSERT(!key.empty());
        VSM_ASSERT(key.find_first_not_of("0123456789") != std::string::npos);
    }
}

VSM_TEST(every_factory_effect_survives_a_full_round_trip) {
    // Le test porte sur les NEUF effets, pas sur un échantillon : un effet
    // ajouté plus tard sans table sémantique doit tomber sur le repli et
    // continuer de faire l'aller-retour.
    for (const auto& info : EffectFactory::available()) {
        auto source = EffectFactory::create(info.id);
        VSM_ASSERT(source != nullptr);

        // On déplace chaque réglage à un endroit qui n'est pas son défaut.
        for (const auto& param : source->parameterList())
            source->setParameter(param.id, param.minValue + 0.37f * (param.maxValue - param.minValue));

        const auto described = describeEffect(info.id, *source);

        auto cible = EffectFactory::create(info.id);
        const EffectApplyReport report = applyEffectDescription(described, *cible);
        VSM_ASSERT_EQ(report.applied, static_cast<int>(cible->parameterList().size()));
        VSM_ASSERT(report.unknownParameters.empty());

        for (const auto& param : source->parameterList())
            VSM_ASSERT_NEAR(cible->getParameter(param.id), source->getParameter(param.id), 1e-5);
    }
}

VSM_TEST(a_parameter_the_effect_does_not_know_is_named_not_swallowed) {
    // Un projet écrit par une version qui avait un réglage de plus doit
    // s'ouvrir, appliquer ce qu'il peut, et DIRE ce qu'il n'a pas pu appliquer.
    auto delay = EffectFactory::create("delay");
    auto described = describeEffect("delay", *delay);
    described.parameters["delay.1.reglage-qui-nexiste-pas"] = 0.5f;

    const EffectApplyReport report = applyEffectDescription(described, *delay);
    VSM_ASSERT_EQ(report.unknownParameters.size(), size_t(1));
    VSM_ASSERT_EQ(report.unknownParameters[0], std::string("delay.1.reglage-qui-nexiste-pas"));
    VSM_ASSERT_EQ(report.applied, static_cast<int>(delay->parameterList().size()));
}
