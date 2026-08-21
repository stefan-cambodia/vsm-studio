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

VSM_TEST(every_registered_machine_has_a_panel) {
    // Le miroir de `every_panel_targets_an_existing_machine`, et il manquait :
    // ce test-là interdit une façade orpheline, celui-ci interdit une machine
    // SANS façade. Sans lui, ajouter une machine et oublier son panneau donne
    // un instrument jouable mais sans commandes — exactement l'« incomplète en
    // silence » contre laquelle le § 0 de CDC-nouvelle-machine.md met en garde,
    // et qu'aucun autre test n'attrapait.
    //
    // La tonalité d'essai est la seule exception, et elle est de nature :
    // c'est un générateur de vérification, pas un instrument à jouer.
    vsm::audio::plugin::registerBuiltInPlugins();
    for (const auto& [id, displayName] : vsm::audio::plugin::PluginRegistry::instance().listAvailable()) {
        (void)displayName;
        if (id == "vsm.testtone") continue;
        if (findMachinePanel(id) != nullptr) continue;
        throw vsm::test::AssertionFailure("machine " + id + " enregistrée sans façade");
    }
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

VSM_TEST(the_sampler_is_the_drum_machine_of_the_park) {
    // Ce test tient une DÉCISION, pas seulement une mise en page : le cahier
    // des charges prévoyait une boîte à rythmes générique (`vsm.drumkit`), et
    // elle n'a pas été écrite parce que le sampler l'était déjà. Ce qui rend
    // cette décision vraie tient à la façade -- si elle redevenait une grille
    // de « SLOT 1..16 », le parc n'aurait plus de boîte à rythmes du tout et
    // personne ne s'en apercevrait.
    const MachinePanel& sampler = *findMachinePanel("vsm.sampler");

    // Une colonne par PIÈCE, chacune avec ses propres réglages -- surtout pas
    // un bloc « tous les niveaux » puis un bloc « tous les accords », qui
    // serait plus compact et rendrait l'instrument injouable.
    size_t pieceSections = 0;
    for (const auto& section : sampler.sections) {
        size_t own = 0;
        const std::string prefix = "Slot " + std::to_string(pieceSections + 1) + " ";
        for (const auto& control : section.controls)
            if (control.parameterName.rfind(prefix, 0) == 0) ++own;
        if (own == section.controls.size() && own > 0) ++pieceSections;
    }
    VSM_ASSERT_EQ(pieceSections, size_t(16));

    // Chaque section porte SON NUMÉRO puis LE NOM DE SA PIÈCE. Le numéro fait
    // le lien avec les paramètres (« Slot 3 Level ») et avec l'emplacement où
    // l'analyse dépose son échantillon ; le nom dit ce que la convention
    // General MIDI met là. L'un sans l'autre casse la moitié du lien.
    VSM_ASSERT_EQ(sampler.sections[0].title, std::string("1 KICK"));
    VSM_ASSERT_EQ(sampler.sections[1].title, std::string("2 SNARE"));
    VSM_ASSERT_EQ(sampler.sections[2].title, std::string("3 HH CL"));
    for (size_t i = 0; i < 16; ++i) {
        const std::string& title = sampler.sections[i].title;
        const std::string number = std::to_string(i + 1);
        VSM_ASSERT(title.rfind(number + " ", 0) == 0);   // le numéro, en tête
        VSM_ASSERT(title.size() > number.size() + 1);    // et un nom derrière
    }

    // Le séquenceur intégré : sur une boîte à rythmes il n'est pas un
    // accessoire, il EST l'instrument. Ses lignes portent des noms de pièces,
    // et ses notes suivent la convention General MIDI -- c'est ce qui fait
    // qu'un MIDI de batterie transcrit tombe sur les bonnes lignes.
    VSM_ASSERT(sampler.sequencer.kind == SequencerKind::DrumGrid);
    VSM_ASSERT_EQ(sampler.sequencer.stepCount, 16);
    VSM_ASSERT_EQ(sampler.sequencer.lanes.size(), size_t(8));
    VSM_ASSERT_EQ(sampler.sequencer.lanes[0].first, std::string("KICK"));
    VSM_ASSERT_EQ(sampler.sequencer.lanes[0].second, 36);   // GM : grosse caisse
    VSM_ASSERT_EQ(sampler.sequencer.lanes[1].second, 38);   // GM : caisse claire
    VSM_ASSERT_EQ(sampler.sequencer.lanes[2].second, 42);   // GM : charleston fermée
    VSM_ASSERT_EQ(sampler.sequencer.lanes[3].second, 46);   // GM : charleston ouverte
    for (const auto& lane : sampler.sequencer.lanes)
        VSM_ASSERT(lane.first.rfind("SLOT", 0) != 0);       // jamais un numéro nu
}
