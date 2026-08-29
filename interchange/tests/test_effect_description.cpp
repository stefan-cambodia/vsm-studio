#include "TestFramework.h"
#include "vsm/audio/effect/EffectFactory.h"
#include "vsm/interchange/EffectDescription.h"
#include "vsm/interchange/ProjectDocument.h"

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

// --- D4.1 : les quatre effets de la tranche --------------------------------

VSM_TEST(the_channel_strip_effects_are_named_by_what_they_do) {
    // L'IDENTITÉ SÉMANTIQUE EST LE CONTRAT DU FORMAT, et elle est vérifiée ici
    // par son NOM et non par un aller-retour. Un aller-retour passe même quand
    // les deux côtés se trompent de la même façon -- le repli sur le nom
    // affiché y suffirait. Ce qui compte est que `analyse/` puisse viser
    // « effect.compressor.threshold » sans connaître le code du DAW, et cela
    // ne se vérifie qu'en écrivant la chaîne attendue.
    const std::map<std::string, std::vector<std::string>> attendues = {
        {"eq", {"effect.eq.low.gain", "effect.eq.low.freq", "effect.eq.mid.freq",
                "effect.eq.mid.gain", "effect.eq.mid.q", "effect.eq.high.gain",
                "effect.eq.high.freq"}},
        {"compressor", {"effect.compressor.threshold", "effect.compressor.ratio",
                        "effect.compressor.attack", "effect.compressor.release",
                        "effect.compressor.makeup", "effect.compressor.sidechain"}},
        {"gate", {"effect.gate.threshold", "effect.gate.attack", "effect.gate.hold",
                  "effect.gate.release", "effect.gate.range"}},
        {"limiter", {"effect.limiter.ceiling", "effect.limiter.release"}},
    };

    for (const auto& [id, noms] : attendues) {
        auto effet = EffectFactory::create(id);
        VSM_ASSERT(effet != nullptr);
        const auto described = describeEffect(id, *effet);
        VSM_ASSERT_EQ(described.type, id);
        VSM_ASSERT_EQ(described.parameters.size(), noms.size());
        for (const auto& nom : noms)
            VSM_ASSERT(described.parameters.count(nom) == 1);
    }
}

VSM_TEST(the_channel_strip_effects_land_on_the_right_knobs_after_a_reload) {
    // Le vrai risque d'un habillage : un aller-retour qui « marche » en
    // remettant les bonnes valeurs sur les MAUVAIS boutons. On règle donc
    // chaque paramètre à une valeur DISTINCTE et on vérifie chacune.
    auto source = EffectFactory::create("gate");
    VSM_ASSERT(source != nullptr);
    source->setParameter(0, -33.0f);   // seuil
    source->setParameter(1, 2.5f);     // attaque
    source->setParameter(2, 175.0f);   // maintien
    source->setParameter(3, 640.0f);   // relâchement
    source->setParameter(4, -18.0f);   // plage

    const auto described = describeEffect("gate", *source);
    auto cible = EffectFactory::create("gate");
    VSM_ASSERT(applyEffectDescription(described, *cible).unknownParameters.empty());

    VSM_ASSERT_NEAR(cible->getParameter(0), -33.0f, 1e-5);
    VSM_ASSERT_NEAR(cible->getParameter(1), 2.5f, 1e-5);
    VSM_ASSERT_NEAR(cible->getParameter(2), 175.0f, 1e-5);
    VSM_ASSERT_NEAR(cible->getParameter(3), 640.0f, 1e-5);
    VSM_ASSERT_NEAR(cible->getParameter(4), -18.0f, 1e-5);
}

// --- D7.3 : l'état d'un effet tiers traverse le fichier de projet ----------
//
// Un effet interne se décrit entièrement par ses paramètres nommés. Un effet
// tiers, non : il porte des réponses impulsionnelles, des courbes, des tables
// que rien dans le vocabulaire sémantique ne désigne. Ce que ces tests gardent
// est que le champ prévu pour cela VOYAGE -- et qu'un projet sans plugin tiers
// n'en porte aucune trace.

VSM_TEST(a_native_effect_declares_no_native_state) {
    auto reverb = EffectFactory::create("reverb");
    VSM_ASSERT(reverb != nullptr);
    // Vide veut dire « mon son EST ma table de paramètres », et c'est une
    // propriété des treize effets internes qu'on ne veut pas perdre.
    VSM_ASSERT(describeEffect("reverb", *reverb).nativeState.empty());
}

VSM_TEST(a_third_party_effect_state_survives_the_project_json) {
    vsm::sequencer::Project projet;
    vsm::sequencer::Track piste;
    piste.name = "Voix";
    vsm::sequencer::TrackEffect insert;
    insert.type = "vst3:/opt/plugins/Convolver.vst3#4242";
    insert.parameters["mix"] = 0.35f;
    insert.nativeState = "Zm9yZXQtZGUtcGllcnJlcw==";
    piste.effects.push_back(std::move(insert));
    projet.tracks.push_back(std::move(piste));

    const std::string texte =
        projectDocumentToJson(documentFromProject(projet)).toString();
    const auto relu = parseProjectDocument(texte);
    VSM_ASSERT(relu.success);

    vsm::sequencer::Project rendu;
    rendu.tracks.resize(1);
    applyDocumentToProject(relu.document, rendu);
    VSM_ASSERT_EQ(rendu.tracks.size(), static_cast<size_t>(1));
    VSM_ASSERT_EQ(rendu.tracks[0].effects.size(), static_cast<size_t>(1));
    const auto& retrouve = rendu.tracks[0].effects[0];
    VSM_ASSERT_EQ(retrouve.type, std::string("vst3:/opt/plugins/Convolver.vst3#4242"));
    VSM_ASSERT_EQ(retrouve.nativeState, std::string("Zm9yZXQtZGUtcGllcnJlcw=="));
    // LES DEUX COHABITENT : l'état natif ne remplace pas les valeurs nommées,
    // qui restent lisibles et applicables ailleurs.
    VSM_ASSERT_NEAR(retrouve.parameters.at("mix"), 0.35f, 1e-6);
}

VSM_TEST(a_project_without_third_party_effects_writes_no_native_state_field) {
    vsm::sequencer::Project projet;
    vsm::sequencer::Track piste;
    vsm::sequencer::TrackEffect insert;
    insert.type = "reverb";
    insert.parameters["reverb.1.mix"] = 0.2f;
    piste.effects.push_back(std::move(insert));
    projet.tracks.push_back(std::move(piste));

    const std::string texte =
        projectDocumentToJson(documentFromProject(projet)).toString();
    // Le fichier d'un morceau qui n'emploie que des effets internes reste
    // exactement celui qu'il était : un champ facultatif qui apparaîtrait quand
    // même ferait diverger tous les projets existants pour rien.
    VSM_ASSERT(texte.find("nativeState") == std::string::npos);
}
