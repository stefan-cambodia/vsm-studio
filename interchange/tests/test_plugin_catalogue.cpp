#include "TestFramework.h"
#include "vsm/interchange/PluginCatalogue.h"

using namespace vsm::interchange;

// D7.5 — LE CATALOGUE DES PLUGINS INSTALLÉS, ET LE PROTOCOLE QUI LE REMPLIT.
//
// Ce que ces tests couvrent est la partie du balayage qui peut se vérifier sans
// lancer de processus ni charger de code étranger -- et c'est précisément la
// partie où une erreur serait silencieuse : une ligne mal décodée met un plugin
// au mauvais endroit du menu, un catalogue mal relu en propose qui n'existent
// pas. Ce qui reste (lancer un enfant, le voir tomber) est vérifié en le
// faisant, dans l'application.

VSM_TEST(a_scan_line_survives_the_trip_from_child_to_parent) {
    CataloguedPlugin plugin;
    plugin.format = "vst3";
    plugin.path = "/usr/lib/vst3/Machin.vst3";
    plugin.id = "1234567";
    plugin.name = "Machin Deluxe";
    plugin.vendor = "Truc Audio";
    plugin.isInstrument = true;

    CataloguedPlugin relu;
    VSM_ASSERT(decodeScanLine(encodeScanLine(plugin), relu));
    VSM_ASSERT_EQ(relu.format, plugin.format);
    VSM_ASSERT_EQ(relu.path, plugin.path);
    VSM_ASSERT_EQ(relu.id, plugin.id);
    VSM_ASSERT_EQ(relu.name, plugin.name);
    VSM_ASSERT_EQ(relu.vendor, plugin.vendor);
    VSM_ASSERT(relu.isInstrument);
}

VSM_TEST(anything_a_plugin_prints_while_loading_is_not_a_plugin) {
    // Un plugin qui écrit sur la sortie standard pendant son chargement, cela
    // arrive -- des messages de licence, des avertissements, des traces. Rien
    // de tout cela ne doit se retrouver dans le catalogue.
    CataloguedPlugin sortie;
    VSM_ASSERT(!decodeScanLine("Licence expiree, mode demo", sortie));
    VSM_ASSERT(!decodeScanLine("", sortie));
    VSM_ASSERT(!decodeScanLine("PLUGIN\ttrop\tpeu\tde\tchamps", sortie));
    // Le bon nombre de champs, mais vides là où cela compte.
    VSM_ASSERT(!decodeScanLine("PLUGIN\t\t\t\t\t0", sortie));
}

VSM_TEST(a_name_holding_a_tabulation_does_not_split_the_line_in_two) {
    CataloguedPlugin plugin;
    plugin.format = "clap";
    plugin.path = "/opt/x.clap";
    plugin.id = "a.b";
    plugin.name = "Avant\tApres";     // le séparateur, dans le nom
    plugin.vendor = "Ligne\nSuivante";
    plugin.isInstrument = false;

    CataloguedPlugin relu;
    VSM_ASSERT(decodeScanLine(encodeScanLine(plugin), relu));
    // REMPLACÉS, PAS ÉCHAPPÉS : un échappement demanderait un désamorçage
    // symétrique, donc deux occasions de se tromper, pour des noms qui n'en
    // contiennent pas.
    VSM_ASSERT_EQ(relu.name, std::string("Avant Apres"));
    VSM_ASSERT_EQ(relu.vendor, std::string("Ligne Suivante"));
    VSM_ASSERT(!relu.isInstrument);
}

VSM_TEST(the_catalogue_identifier_is_the_one_the_factories_read) {
    CataloguedPlugin plugin;
    plugin.format = "vst3";
    plugin.path = "/usr/lib/vst3/Machin.vst3";
    plugin.id = "42";
    // C'EST LE POINT DE JONCTION AVEC D7.1 À D7.3 : le catalogue ne sert à rien
    // s'il ne rend pas l'identifiant que `PluginRegistry` et `EffectFactory`
    // savent lire.
    VSM_ASSERT_EQ(plugin.instrumentId(), std::string("vst3:/usr/lib/vst3/Machin.vst3#42"));
}

VSM_TEST(a_catalogue_survives_its_file) {
    PluginCatalogue catalogue;
    CataloguedPlugin instrument;
    instrument.format = "clap";
    instrument.path = "/opt/synthe.clap";
    instrument.id = "com.x.synthe";
    instrument.name = "Synthe";
    instrument.isInstrument = true;
    catalogue.plugins.push_back(instrument);

    CataloguedPlugin effet;
    effet.format = "vst3";
    effet.path = "/opt/reverb.vst3";
    effet.id = "7";
    effet.name = "Reverb";
    catalogue.plugins.push_back(effet);

    catalogue.faulty.push_back({"/opt/casse.vst3", "le processus de balayage est tombe"});

    const auto relu = parsePluginCatalogue(pluginCatalogueToJson(catalogue).toString());
    VSM_ASSERT(relu.success);
    VSM_ASSERT_EQ(relu.catalogue.plugins.size(), static_cast<size_t>(2));
    VSM_ASSERT_EQ(relu.catalogue.instruments().size(), static_cast<size_t>(1));
    VSM_ASSERT_EQ(relu.catalogue.effects().size(), static_cast<size_t>(1));
    // LES FAUTIFS SONT GARDÉS. Sans cela, chaque balayage retenterait le même
    // plugin qui fait tomber le processus, et l'utilisateur n'apprendrait
    // jamais lequel des deux cents fichiers de son disque pose problème.
    VSM_ASSERT_EQ(relu.catalogue.faulty.size(), static_cast<size_t>(1));
    VSM_ASSERT_EQ(relu.catalogue.faulty[0].path, std::string("/opt/casse.vst3"));
}

VSM_TEST(a_second_scan_reopens_only_what_it_has_never_seen) {
    PluginCatalogue catalogue;
    CataloguedPlugin connu;
    connu.format = "vst3";
    connu.path = "/opt/connu.vst3";
    catalogue.plugins.push_back(connu);
    catalogue.faulty.push_back({"/opt/casse.vst3", "tombe"});

    VSM_ASSERT(catalogue.alreadyKnown("/opt/connu.vst3"));
    // ROUVRIR UN PLUGIN QUI A DÉJÀ FAIT TOMBER UN PROCESSUS n'apprendrait rien
    // de plus, et ferait payer la même chute à chaque démarrage.
    VSM_ASSERT(catalogue.alreadyKnown("/opt/casse.vst3"));
    VSM_ASSERT(!catalogue.alreadyKnown("/opt/nouveau.vst3"));
}

VSM_TEST(a_file_that_is_not_a_catalogue_is_refused_not_guessed) {
    const auto autre = parsePluginCatalogue(R"({"format":"vsm-project","version":1})");
    VSM_ASSERT(!autre.success);
    VSM_ASSERT(!autre.error.empty());
    // Un catalogue mal interprété proposerait des plugins qui n'existent pas, et
    // l'utilisateur le découvrirait en essayant d'en charger un.
    VSM_ASSERT(autre.catalogue.plugins.empty());
}
