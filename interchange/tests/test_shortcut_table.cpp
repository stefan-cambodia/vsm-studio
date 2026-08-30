#include "TestFramework.h"
#include "vsm/interchange/ShortcutTable.h"
#include <set>
#include <string>

using namespace vsm::interchange;

// D10.3 de docs/ROADMAP-daw.md — LES RACCOURCIS SE LISENT ET SE CHANGENT.
//
// Ce qui existait : deux `switch` sur des codes de touches, dans deux fichiers,
// et rien qui les liste. La seule façon de savoir ce que faisait une touche
// était de l'essayer.

VSM_TEST(every_command_is_declared_once_and_completely) {
    // LE CATALOGUE EST LA SOURCE : une commande déclarée à moitié produirait
    // une ligne vide dans la page, ou un raccourci qui ne se retrouve pas dans
    // les préférences.
    std::set<std::string> identifiants;
    for (const auto& commande : shortcutCommands()) {
        VSM_ASSERT(std::string(commande.key).size() > 0);
        VSM_ASSERT(std::string(commande.label).size() > 0);
        VSM_ASSERT(std::string(commande.category).size() > 0);
        VSM_ASSERT(std::string(commande.defaultKey).size() > 0);
        // Les identifiants sont uniques : deux commandes qui partageraient le
        // leur s'écraseraient dans les préférences.
        VSM_ASSERT(identifiants.insert(commande.key).second);
    }
    VSM_ASSERT_EQ(shortcutCommands().size(),
                   static_cast<size_t>(ShortcutId::Count));
}

VSM_TEST(no_two_commands_share_a_default_key) {
    // DEUX COMMANDES SUR LA MÊME TOUCHE, c'est une seule qui répond et rien qui
    // dise laquelle. Le catalogue par défaut ne doit pas en contenir.
    ShortcutTable table;
    for (const auto& commande : shortcutCommands()) {
        const auto conflits = table.conflictsFor(commande.defaultKey, commande.id);
        VSM_ASSERT_EQ(conflits.size(), size_t{0});
    }
}

VSM_TEST(a_key_finds_its_command_aliases_included) {
    ShortcutTable table;
    ShortcutId trouve{};
    VSM_ASSERT(table.commandForKey("spacebar", trouve));
    VSM_ASSERT(trouve == ShortcutId::TransportPlayStop);
    // L'alias existe pour ne pas surprendre : Retour arrière supprime aussi.
    VSM_ASSERT(table.commandForKey("backspace", trouve));
    VSM_ASSERT(trouve == ShortcutId::EditDelete);
    VSM_ASSERT(!table.commandForKey("F13", trouve));
}

VSM_TEST(a_rebound_command_drops_its_alias) {
    // L'ALIAS NE SUIT PAS LA PERSONNALISATION. Sinon, réassigner « Supprimer »
    // à F1 laisserait Retour arrière effacer encore, sans que rien le dise --
    // et l'utilisateur chercherait longtemps pourquoi sa touche « supprime
    // toujours ».
    ShortcutTable table;
    table.setKey(ShortcutId::EditDelete, "F1");
    ShortcutId trouve{};
    VSM_ASSERT(table.commandForKey("F1", trouve));
    VSM_ASSERT(trouve == ShortcutId::EditDelete);
    VSM_ASSERT(!table.commandForKey("backspace", trouve));
}

VSM_TEST(rebinding_back_to_the_default_is_not_a_customisation) {
    ShortcutTable table;
    table.setKey(ShortcutId::EditCopy, "ctrl + W");
    VSM_ASSERT(table.isCustom(ShortcutId::EditCopy));
    table.setKey(ShortcutId::EditCopy, "ctrl + C");
    VSM_ASSERT(!table.isCustom(ShortcutId::EditCopy));
    VSM_ASSERT_EQ(table.overrides().size(), size_t{0});
}

VSM_TEST(a_conflict_is_reported_rather_than_created_in_silence) {
    ShortcutTable table;
    const auto conflits = table.conflictsFor("ctrl + C", ShortcutId::EditCut);
    VSM_ASSERT_EQ(conflits.size(), size_t{1});
    VSM_ASSERT(conflits.front() == ShortcutId::EditCopy);
}

VSM_TEST(an_empty_key_disables_a_command) {
    // C'est un choix possible, et le taire obligerait à inventer une touche
    // pour se débarrasser d'un raccourci gênant.
    ShortcutTable table;
    table.setKey(ShortcutId::ReferenceCycle, "");
    ShortcutId trouve{};
    VSM_ASSERT(!table.commandForKey("", trouve));
    VSM_ASSERT(table.keyFor(ShortcutId::ReferenceCycle).empty());
    VSM_ASSERT(!table.commandForKey("R", trouve));
}

VSM_TEST(customisations_survive_a_round_trip) {
    ShortcutTable ecrite;
    ecrite.setKey(ShortcutId::TransportPlayStop, "P");
    ecrite.setKey(ShortcutId::EditQuantize, "");

    ShortcutTable relue;
    VSM_ASSERT(shortcutTableFromJson(shortcutTableToJson(ecrite), relue));
    VSM_ASSERT_EQ(relue.keyFor(ShortcutId::TransportPlayStop), std::string("P"));
    VSM_ASSERT(relue.keyFor(ShortcutId::EditQuantize).empty());
    // Ce qui n'a pas été changé garde son défaut.
    VSM_ASSERT_EQ(relue.keyFor(ShortcutId::EditCopy), std::string("ctrl + C"));
}

VSM_TEST(nothing_customised_yet_is_not_an_error) {
    ShortcutTable table;
    VSM_ASSERT(shortcutTableFromJson("", table));
    VSM_ASSERT_EQ(table.keyFor(ShortcutId::FileSave), std::string("ctrl + S"));
}

VSM_TEST(the_printable_page_lists_them_all) {
    // LE CRITÈRE DE D10.3 EST LITTÉRALEMENT « UNE PAGE LES LISTE TOUS ». Ce
    // test le vérifie comme il est écrit : chaque commande du catalogue, avec
    // son libellé et sa touche, doit s'y trouver.
    ShortcutTable table;
    table.setKey(ShortcutId::TransportPlayStop, "P");
    const std::string page = shortcutTableToPrintableText(table);

    for (const auto& commande : shortcutCommands()) {
        VSM_ASSERT(page.find(commande.label) != std::string::npos);
        VSM_ASSERT(page.find(table.keyFor(commande.id)) != std::string::npos);
    }
    // Y compris ce qui NE se reconfigure pas : une page qui prétend tout
    // lister et tait les flèches ment davantage qu'une page qui dit
    // « celles-ci ne bougent pas ».
    for (const auto& fixe : fixedShortcuts())
        VSM_ASSERT(page.find(fixe.label) != std::string::npos);
    // Et une touche modifiée rappelle son défaut : sans quoi on ne saurait
    // plus à quoi revenir.
    VSM_ASSERT(page.find("spacebar") != std::string::npos);
}
