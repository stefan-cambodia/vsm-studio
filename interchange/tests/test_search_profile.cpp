#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/interchange/ParameterDescriptor.h"
#include "vsm/interchange/SearchProfile.h"
#include <algorithm>
#include <set>
#include <string>

using namespace vsm::interchange;

namespace {

SearchProfile profileFor(const std::string& machine) {
    vsm::audio::plugin::registerBuiltInPlugins();
    return buildSearchProfile(machine);
}

} // namespace

VSM_TEST(search_profile_is_empty_for_an_unknown_machine) {
    // Une machine inconnue ne doit PAS recevoir un profil par défaut : elle
    // serait cherchée sur des paramètres qu'elle n'a pas.
    VSM_ASSERT(profileFor("vsm.inexistante").empty());
}

VSM_TEST(every_registered_machine_has_a_usable_search_profile) {
    // C'est l'exigence de l'étape 8.2 : ajouter une machine doit lui donner un
    // espace de recherche, sans une ligne de Python. Les boîtes à rythmes et
    // le sampler sont exclus : on ne cherche pas leur timbre par optimisation,
    // on leur fournit des échantillons.
    vsm::audio::plugin::registerBuiltInPlugins();
    const std::set<std::string> notSearched = {"vsm.testtone", "vsm.tr808", "vsm.tr909", "vsm.sampler"};
    size_t checked = 0;
    for (const auto& machine : knownSemanticPluginIds()) {
        if (machine.rfind("vsm.", 0) != 0) continue;   // les effets ne sont pas concernés
        if (notSearched.count(machine) > 0) continue;
        const SearchProfile profile = buildSearchProfile(machine);
        VSM_ASSERT(!profile.empty());
        VSM_ASSERT(profile.dimensions().size() >= 4); // de quoi chercher réellement
        ++checked;
    }
    VSM_ASSERT(checked >= 14);
}

VSM_TEST(search_dimensions_stay_inside_the_real_parameter_range) {
    // Une borne utile qui sortirait de ce que la machine accepte ferait
    // explorer un plateau de valeurs écrêtées.
    vsm::audio::plugin::registerBuiltInPlugins();
    for (const auto& machine : knownSemanticPluginIds()) {
        if (machine.rfind("vsm.", 0) != 0) continue;
        const SemanticProfile semantic = buildSemanticProfile(machine);
        // Le profil est nommé, et ce n'est pas du style : itérer directement
        // sur `buildSearchProfile(m).dimensions()` lierait la boucle à la
        // RÉFÉRENCE rendue par `dimensions()`, sans prolonger la vie du profil
        // temporaire -- la boucle parcourrait de la mémoire libérée. C'est le
        // piège classique du range-for en C++20, et il s'est déclenché ici.
        const SearchProfile profile = buildSearchProfile(machine);
        for (const auto& dimension : profile.dimensions()) {
            const ParameterDescriptor* parameter = semantic.findBySemanticId(dimension.semanticId);
            VSM_ASSERT(parameter != nullptr);
            VSM_ASSERT(dimension.low >= parameter->minimum - 1e-4f);
            VSM_ASSERT(dimension.high <= parameter->maximum + 1e-4f);
            VSM_ASSERT(dimension.high > dimension.low); // une constante n'est pas une dimension
        }
    }
}

VSM_TEST(logarithmic_dimensions_never_start_at_zero) {
    // Chercher en logarithmique depuis zéro est impossible : la borne basse
    // doit être strictement positive.
    vsm::audio::plugin::registerBuiltInPlugins();
    for (const auto& machine : knownSemanticPluginIds()) {
        if (machine.rfind("vsm.", 0) != 0) continue;
        const SearchProfile profile = buildSearchProfile(machine); // cf. remarque ci-dessus
        for (const auto& dimension : profile.dimensions())
            if (dimension.scale == SearchScale::Logarithmic) VSM_ASSERT(dimension.low > 0.0f);
    }
}

VSM_TEST(frequencies_and_durations_are_searched_logarithmically) {
    // L'oreille perçoit les fréquences et les durées en RAPPORT. Une recherche
    // linéaire entre 80 Hz et 12 kHz passerait l'essentiel de son budget
    // au-dessus de 6 kHz, où elle distingue le moins.
    const SearchProfile minimoog = profileFor("vsm.minimoog");
    const SearchDimension* cutoff = minimoog.find("filter.1.cutoff");
    VSM_ASSERT(cutoff != nullptr);
    VSM_ASSERT(cutoff->scale == SearchScale::Logarithmic);
    VSM_ASSERT(cutoff->low >= 20.0f && cutoff->high <= 18000.0f);

    const SearchDimension* attack = minimoog.find("envelope.1.attack");
    VSM_ASSERT(attack != nullptr);
    VSM_ASSERT(attack->scale == SearchScale::Logarithmic);

    const SearchDimension* sustain = minimoog.find("envelope.1.sustain");
    VSM_ASSERT(sustain != nullptr);
    VSM_ASSERT(sustain->scale == SearchScale::Linear); // un niveau, pas une durée
}

VSM_TEST(the_cutoff_is_the_most_important_dimension_of_a_subtractive_machine) {
    for (const char* machine : {"vsm.minimoog", "vsm.juno106", "vsm.obx", "vsm.sh101"}) {
        const SearchProfile profile = profileFor(machine);
        VSM_ASSERT(!profile.empty());
        // Le profil est trié : la première dimension est la plus importante.
        VSM_ASSERT_EQ(profile.dimensions().front().semanticId, std::string("filter.1.cutoff"));
    }
}

VSM_TEST(machines_without_a_filter_are_searched_on_what_makes_their_sound) {
    // LE point de l'étape 8.2. L'orgue n'a pas de filtre : le chercher sur
    // « filter.1.cutoff » était absurde, et c'est pourtant ce que faisait
    // l'espace écrit en dur. Son timbre, ce sont ses tirettes.
    const SearchProfile organ = profileFor("vsm.tonewheel");
    VSM_ASSERT(organ.find("filter.1.cutoff") == nullptr);
    size_t drawbars = 0;
    for (const auto& dimension : organ.dimensions())
        if (dimension.semanticId.rfind("organ.drawbar.", 0) == 0) ++drawbars;
    VSM_ASSERT_EQ(drawbars, size_t(9));
    VSM_ASSERT(organ.dimensions().front().semanticId.rfind("organ.drawbar.", 0) == 0);

    // Le supersaw : son timbre vient du désaccord, pas de la coupure.
    const SearchProfile supersaw = profileFor("vsm.supersaw");
    const SearchDimension* detune = supersaw.find("oscillator.supersaw.detune");
    VSM_ASSERT(detune != nullptr);
    VSM_ASSERT(detune->importance >= 0.85f);

    // La table d'ondes : c'est la position dans la table qui fait le son.
    const SearchProfile wavetable = profileFor("vsm.wavetable");
    const SearchDimension* position = wavetable.find("oscillator.1.wavePosition");
    VSM_ASSERT(position != nullptr);
    VSM_ASSERT(position->importance >= 0.9f);
}

VSM_TEST(discrete_parameters_keep_their_full_declared_range) {
    // Un choix de table ou de forme d'onde est DISCRET : le borner à l'aveugle
    // rendrait certaines valeurs inatteignables.
    const SearchProfile wavetable = profileFor("vsm.wavetable");
    const SearchDimension* table = wavetable.find("oscillator.1.wavetable");
    VSM_ASSERT(table != nullptr);
    VSM_ASSERT_NEAR(table->low, 0.0f, 1e-6);
    VSM_ASSERT_NEAR(table->high, 3.0f, 1e-6); // les quatre tables restent atteignables
}

VSM_TEST(noise_making_parameters_are_excluded_from_the_search) {
    // La dérive analogique ajoute du bruit aléatoire. L'optimiseur la
    // pousserait à zéro pour réduire la distance -- ce qui ne donne pas un
    // réglage plus juste, mais une machine plus morte.
    vsm::audio::plugin::registerBuiltInPlugins();
    for (const auto& machine : knownSemanticPluginIds()) {
        if (machine.rfind("vsm.", 0) != 0) continue;
        const SearchProfile profile = buildSearchProfile(machine);
        VSM_ASSERT(profile.find("voice.analogCharacter") == nullptr);
        VSM_ASSERT(profile.find("output.level") == nullptr);
    }
}

VSM_TEST(top_dimensions_returns_the_most_important_ones_in_order) {
    const SearchProfile profile = profileFor("vsm.minimoog");
    const auto top = profile.topDimensions(4);
    VSM_ASSERT_EQ(top.size(), size_t(4));
    for (size_t i = 1; i < top.size(); ++i)
        VSM_ASSERT(top[i - 1].importance >= top[i].importance);
    // Demander plus que disponible rend tout, sans erreur.
    VSM_ASSERT_EQ(profile.topDimensions(1000).size(), profile.dimensions().size());
}

VSM_TEST(search_profile_order_is_stable_across_calls) {
    // La recherche doit être reproductible : deux exécutions doivent proposer
    // les mêmes dimensions dans le même ordre.
    const SearchProfile a = profileFor("vsm.jupiter8");
    const SearchProfile b = profileFor("vsm.jupiter8");
    VSM_ASSERT_EQ(a.dimensions().size(), b.dimensions().size());
    for (size_t i = 0; i < a.dimensions().size(); ++i)
        VSM_ASSERT_EQ(a.dimensions()[i].semanticId, b.dimensions()[i].semanticId);
}

VSM_TEST(search_profile_serialises_to_json) {
    const SearchProfile profile = profileFor("vsm.obx");
    const JsonValue json = profile.toJson();
    VSM_ASSERT_EQ(json["machine"].asString(), std::string("vsm.obx"));
    VSM_ASSERT(json["dimensions"].elements().size() == profile.dimensions().size());
    const JsonValue& first = json["dimensions"].elements().front();
    VSM_ASSERT(!first["id"].asString().empty());
    VSM_ASSERT(first["high"].asNumber() > first["low"].asNumber());
    const std::string scale = first["scale"].asString();
    VSM_ASSERT(scale == "log" || scale == "linear");
}

VSM_TEST(machine_specific_quantities_keep_their_declared_range) {
    // Une fenêtre de recherche FIXE n'est légitime que pour une grandeur à
    // unité absolue (hertz, secondes). La résonance n'en est pas une : elle
    // va de 0 à 4,2 sur le Minimoog et de 0 à 1 sur la TB-303.
    //
    // Ce test existe parce que la borner à 0..0,8 a produit une régression
    // MESURÉE : sur une cible Minimoog de résonance 2,2, la valeur devenait
    // inatteignable, la recherche compensait en faussant la coupure
    // (1190 Hz pour 900 visés) et la distance finale doublait.
    vsm::audio::plugin::registerBuiltInPlugins();
    for (const char* machine : {"vsm.minimoog", "vsm.tb303", "vsm.obx", "vsm.juno106"}) {
        const SemanticProfile semantic = buildSemanticProfile(machine);
        const SearchProfile search = buildSearchProfile(machine);
        const ParameterDescriptor* declared = semantic.findBySemanticId("filter.1.resonance");
        const SearchDimension* searched = search.find("filter.1.resonance");
        VSM_ASSERT(declared != nullptr);
        VSM_ASSERT(searched != nullptr);
        VSM_ASSERT_NEAR(searched->low, declared->minimum, 1e-4);
        VSM_ASSERT_NEAR(searched->high, declared->maximum, 1e-4);
    }
}

VSM_TEST(the_amplitude_envelope_outranks_the_modulation_envelopes) {
    // L'instance 1 est la principale par convention du projet (enveloppe
    // d'amplitude, filtre principal). Sans décote sur les instances
    // suivantes, `envelope.2.attack` occupait le rang 3 du Minimoog et
    // EXPULSAIT la résonance du filtre de l'espace réellement cherché.
    const SearchProfile minimoog = profileFor("vsm.minimoog");
    const SearchDimension* ampAttack = minimoog.find("envelope.1.attack");
    const SearchDimension* filterAttack = minimoog.find("envelope.2.attack");
    const SearchDimension* resonance = minimoog.find("filter.1.resonance");
    VSM_ASSERT(ampAttack != nullptr && filterAttack != nullptr && resonance != nullptr);
    VSM_ASSERT(ampAttack->importance > filterAttack->importance);
    VSM_ASSERT(resonance->importance > filterAttack->importance);

    // ...et concrètement, la résonance doit tenir dans les six premières.
    const auto top = minimoog.topDimensions(6);
    bool resonanceIsSearched = false;
    for (const auto& dimension : top)
        if (dimension.semanticId == "filter.1.resonance") resonanceIsSearched = true;
    VSM_ASSERT(resonanceIsSearched);
}
