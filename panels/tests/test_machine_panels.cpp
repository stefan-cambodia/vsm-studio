#include "TestFramework.h"
#include "vsm/panels/MachinePanel.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <set>

using namespace vsm::panels;

namespace {

/// Noms de paramètres réellement exposés par une machine.
std::set<std::string> realParameterNames(const std::string& pluginId) {
    vsm::audio::plugin::registerBuiltInPlugins();
    auto plugin = vsm::audio::plugin::PluginRegistry::instance().create(pluginId);
    std::set<std::string> names;
    if (!plugin) return names;
    plugin->initialize(48000.0, 512);
    for (const auto& info : plugin->parameterList()) names.insert(info.name);
    return names;
}

bool isHexColour(const std::string& text) {
    if (text.size() != 7 || text[0] != '#') return false;
    return std::all_of(text.begin() + 1, text.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
}

} // namespace

VSM_TEST(every_panel_targets_an_existing_machine) {
    vsm::audio::plugin::registerBuiltInPlugins();
    for (const std::string& id : machinePanelIds()) {
        VSM_ASSERT(vsm::audio::plugin::PluginRegistry::instance().isRegistered(id));
        VSM_ASSERT(findMachinePanel(id) != nullptr);
    }
    VSM_ASSERT(findMachinePanel("vsm.machine-inexistante") == nullptr);
}

VSM_TEST(no_control_points_at_a_parameter_that_does_not_exist) {
    // Le test qui empêche une façade de pourrir en silence : si un paramètre
    // est renommé dans une machine, la commande correspondante deviendrait un
    // bouton mort, sans erreur ni trace. Ici, le build casse.
    for (const std::string& id : machinePanelIds()) {
        const MachinePanel& panel = *findMachinePanel(id);
        const auto real = realParameterNames(id);
        VSM_ASSERT(!real.empty());
        for (const auto& control : allControls(panel)) {
            if (real.count(control.parameterName) > 0) continue;
            throw vsm::test::AssertionFailure("façade " + id + " : la commande \"" +
                                               control.parameterName + "\" ne correspond à aucun paramètre");
        }
    }
}

VSM_TEST(no_parameter_is_silently_unreachable) {
    // L'inverse, tout aussi important : un paramètre absent de la façade
    // serait un réglage que l'utilisateur ne peut PAS atteindre. Il faut donc
    // soit une commande, soit une omission déclarée avec sa raison.
    for (const std::string& id : machinePanelIds()) {
        const MachinePanel& panel = *findMachinePanel(id);
        std::set<std::string> placed;
        for (const auto& control : allControls(panel)) placed.insert(control.parameterName);
        std::set<std::string> omitted;
        for (const auto& [name, reason] : panel.omittedParameters) {
            VSM_ASSERT(!reason.empty()); // une omission sans raison est un oubli
            omitted.insert(name);
        }

        for (const std::string& name : realParameterNames(id)) {
            if (placed.count(name) > 0 || omitted.count(name) > 0) continue;
            throw vsm::test::AssertionFailure("façade " + id + " : le paramètre \"" + name +
                                               "\" n'est ni posé sur la façade ni déclaré omis");
        }
    }
}

VSM_TEST(no_parameter_appears_twice_on_a_panel) {
    // Deux commandes pour un même paramètre se contrediraient à l'écran : en
    // bouger une laisserait l'autre afficher une valeur fausse.
    for (const std::string& id : machinePanelIds()) {
        std::set<std::string> seen;
        for (const auto& control : allControls(*findMachinePanel(id))) {
            if (seen.insert(control.parameterName).second) continue;
            throw vsm::test::AssertionFailure("façade " + id + " : \"" + control.parameterName +
                                               "\" apparaît deux fois");
        }
    }
}

VSM_TEST(sections_stay_inside_the_panel_grid) {
    for (const std::string& id : machinePanelIds()) {
        const MachinePanel& panel = *findMachinePanel(id);
        VSM_ASSERT(panel.gridColumns > 0 && panel.gridRows > 0);
        for (const auto& section : panel.sections) {
            VSM_ASSERT(section.column >= 0 && section.row >= 0);
            VSM_ASSERT(section.columnSpan > 0 && section.rowSpan > 0);
            VSM_ASSERT(section.column + section.columnSpan <= panel.gridColumns);
            VSM_ASSERT(section.row + section.rowSpan <= panel.gridRows);
        }
    }
}

VSM_TEST(sections_never_overlap) {
    // Deux blocs superposés donneraient des commandes empilées, invisibles ou
    // inatteignables selon l'ordre de dessin.
    for (const std::string& id : machinePanelIds()) {
        const MachinePanel& panel = *findMachinePanel(id);
        const auto& sections = panel.sections;
        for (size_t a = 0; a < sections.size(); ++a) {
            for (size_t b = a + 1; b < sections.size(); ++b) {
                const bool separatedHorizontally =
                    sections[a].column + sections[a].columnSpan <= sections[b].column ||
                    sections[b].column + sections[b].columnSpan <= sections[a].column;
                const bool separatedVertically =
                    sections[a].row + sections[a].rowSpan <= sections[b].row ||
                    sections[b].row + sections[b].rowSpan <= sections[a].row;
                if (separatedHorizontally || separatedVertically) continue;
                throw vsm::test::AssertionFailure("façade " + id + " : les blocs \"" + sections[a].title +
                                                   "\" et \"" + sections[b].title + "\" se chevauchent");
            }
        }
    }
}

VSM_TEST(controls_never_overlap_inside_a_section) {
    for (const std::string& id : machinePanelIds()) {
        for (const auto& section : findMachinePanel(id)->sections) {
            const auto& controls = section.controls;
            for (size_t a = 0; a < controls.size(); ++a) {
                for (size_t b = a + 1; b < controls.size(); ++b) {
                    const bool separatedHorizontally =
                        controls[a].column + controls[a].columnSpan <= controls[b].column ||
                        controls[b].column + controls[b].columnSpan <= controls[a].column;
                    const bool separatedVertically =
                        controls[a].row + controls[a].rowSpan <= controls[b].row ||
                        controls[b].row + controls[b].rowSpan <= controls[a].row;
                    if (separatedHorizontally || separatedVertically) continue;
                    throw vsm::test::AssertionFailure("façade " + id + ", bloc \"" + section.title +
                                                       "\" : \"" + controls[a].parameterName + "\" et \"" +
                                                       controls[b].parameterName + "\" se superposent");
                }
            }
        }
    }
}

VSM_TEST(colours_are_valid_and_readable) {
    for (const std::string& id : machinePanelIds()) {
        const MachinePanel& panel = *findMachinePanel(id);
        VSM_ASSERT(isHexColour(panel.panelColour));
        VSM_ASSERT(isHexColour(panel.sectionColour));
        VSM_ASSERT(isHexColour(panel.textColour));
        for (const auto& section : panel.sections) VSM_ASSERT(isHexColour(section.accentColour));
    }
}

VSM_TEST(each_panel_keeps_the_layout_of_its_original) {
    // Quelques repères vérifiables : ce sont eux qui font qu'un utilisateur
    // retrouve ses gestes. Ils ne prouvent pas la ressemblance visuelle, mais
    // ils empêchent une réorganisation « plus logique » qui trahirait la
    // machine (regrouper les niveaux d'une boîte à rythmes, par exemple).

    // Minimoog : le signal se lit de gauche à droite -- oscillateurs, mixage,
    // modifieurs -- et la coupure est le gros potentiomètre.
    const MachinePanel& minimoog = *findMachinePanel("vsm.minimoog");
    VSM_ASSERT_EQ(minimoog.sections[0].title, std::string("OSCILLATOR BANK"));
    VSM_ASSERT_EQ(minimoog.sections[1].title, std::string("MIXER"));
    VSM_ASSERT(minimoog.sections[0].column < minimoog.sections[1].column);
    VSM_ASSERT(minimoog.sections[1].column < minimoog.sections[2].column);
    bool cutoffIsLarge = false;
    for (const auto& control : allControls(minimoog))
        if (control.parameterName == "Filter Cutoff") cutoffIsLarge = (control.style == ControlStyle::LargeKnob);
    VSM_ASSERT(cutoffIsLarge);

    // TB-303 : une rangée unique, tout sur la même ligne.
    const MachinePanel& tb303 = *findMachinePanel("vsm.tb303");
    for (const auto& control : tb303.sections[0].controls) VSM_ASSERT_EQ(control.row, 0);

    // TR-808 : une colonne par pièce, chacune avec SES réglages -- surtout pas
    // un bloc "tous les niveaux" et un bloc "tous les decays".
    const MachinePanel& tr808 = *findMachinePanel("vsm.tr808");
    bool kickSectionHoldsAllKickControls = false;
    for (const auto& section : tr808.sections) {
        if (section.title != "BASS DRUM") continue;
        size_t kickControls = 0;
        for (const auto& control : section.controls)
            if (control.parameterName.rfind("Kick", 0) == 0) ++kickControls;
        kickSectionHoldsAllKickControls = (kickControls == section.controls.size() && kickControls == 3);
    }
    VSM_ASSERT(kickSectionHoldsAllKickControls);
}
