#include "TestFramework.h"
#include "vsm/sequencer/AutomationEdit.h"

using namespace vsm::midi;
using namespace vsm::sequencer;

// D5.4 de docs/ROADMAP-daw.md — DESSINER UNE COURBE.
//
// Ce qui décide de la valeur d'un paramètre à un instant donné est de la
// logique musicale, pas du dessin : dans le composant, il aurait fallu un
// serveur graphique pour vérifier qu'un fondu passe bien par zéro à mi-course.

VSM_TEST(a_curve_interpolates_between_its_points) {
    AutomationCurve courbe;
    courbe.parameter = "mix.volume";
    setAutomationPoint(courbe, 0, 1.0f);
    setAutomationPoint(courbe, 1000, 0.0f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 0), 1.0f, 1e-6f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 500), 0.5f, 1e-6f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 1000), 0.0f, 1e-6f);
}

VSM_TEST(outside_its_range_a_curve_holds_instead_of_falling_to_zero) {
    // Une courbe qui ne couvre que le refrain ne doit pas faire tomber le
    // paramètre à rien pendant les couplets.
    AutomationCurve courbe;
    setAutomationPoint(courbe, 1000, 0.8f);
    setAutomationPoint(courbe, 2000, 0.2f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 0), 0.8f, 1e-6f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 99999), 0.2f, 1e-6f);
}

VSM_TEST(a_step_point_holds_its_value_until_the_next_one) {
    // C'est ce qu'il faut pour un commutateur, un choix de forme d'onde, ou
    // tout ce qui ne s'interpole pas -- une valeur intermédiaire n'y voudrait
    // rien dire.
    AutomationCurve courbe;
    setAutomationPoint(courbe, 0, 3.0f, /*step=*/true);
    setAutomationPoint(courbe, 1000, 7.0f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 999), 3.0f, 1e-6f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 1000), 7.0f, 1e-6f);
}

VSM_TEST(an_empty_curve_reads_as_zero_and_never_crashes) {
    AutomationCurve courbe;
    VSM_ASSERT_NEAR(automationValueAt(courbe, 0), 0.0f, 1e-9f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 12345), 0.0f, 1e-9f);
    VSM_ASSERT(!removeAutomationPointNear(courbe, 0, 100));
}

VSM_TEST(setting_a_point_twice_moves_it_instead_of_doubling_it) {
    // Dessiner une courbe, c'est cliquer plusieurs fois au même endroit en
    // corrigeant. Deux points au même tick rendraient le segment entre eux
    // indéfini.
    AutomationCurve courbe;
    setAutomationPoint(courbe, 480, 0.3f);
    setAutomationPoint(courbe, 480, 0.9f);
    VSM_ASSERT_EQ(courbe.points.size(), size_t(1));
    VSM_ASSERT_NEAR(courbe.points[0].value, 0.9f, 1e-6f);
}

VSM_TEST(points_stay_sorted_whatever_the_order_they_are_drawn_in) {
    // On dessine rarement de gauche à droite : l'interpolation suppose l'ordre,
    // et c'est ici qu'il est garanti plutôt qu'espéré.
    AutomationCurve courbe;
    setAutomationPoint(courbe, 2000, 0.2f);
    setAutomationPoint(courbe, 0, 1.0f);
    setAutomationPoint(courbe, 1000, 0.6f);
    VSM_ASSERT_EQ(courbe.points.size(), size_t(3));
    VSM_ASSERT_EQ(courbe.points[0].tick, Tick(0));
    VSM_ASSERT_EQ(courbe.points[1].tick, Tick(1000));
    VSM_ASSERT_EQ(courbe.points[2].tick, Tick(2000));
    VSM_ASSERT_NEAR(automationValueAt(courbe, 1500), 0.4f, 1e-6f);
}

VSM_TEST(removing_takes_the_nearest_point_and_only_within_reach) {
    AutomationCurve courbe;
    setAutomationPoint(courbe, 0, 1.0f);
    setAutomationPoint(courbe, 1000, 0.5f);
    setAutomationPoint(courbe, 2000, 0.0f);

    // Hors de portée : on ne retire rien plutôt que le moins loin.
    VSM_ASSERT(!removeAutomationPointNear(courbe, 1500, 100));
    VSM_ASSERT_EQ(courbe.points.size(), size_t(3));

    VSM_ASSERT(removeAutomationPointNear(courbe, 1040, 100));
    VSM_ASSERT_EQ(courbe.points.size(), size_t(2));
    VSM_ASSERT_EQ(courbe.points[1].tick, Tick(2000));
}

VSM_TEST(the_nearest_point_is_the_nearest_and_not_the_first_within_reach) {
    AutomationCurve courbe;
    setAutomationPoint(courbe, 900, 0.1f);
    setAutomationPoint(courbe, 1000, 0.2f);
    // 990 est à portée des deux ; c'est le second qu'on saisit.
    VSM_ASSERT_EQ(automationPointNear(courbe, 990, 200), size_t(1));
    VSM_ASSERT_EQ(automationPointNear(courbe, 5000, 200), courbe.points.size());
}

// --------------------------------------------------------------------------
// D16.8 — ÉCRIRE L'AUTOMATION EN JOUANT. Ce qu'un passage dépose : la plage
// est remplacée, et les deux bords sont RACCORDÉS à ce que la courbe disait.
// --------------------------------------------------------------------------

VSM_TEST(writing_a_range_replaces_it_and_joins_both_edges) {
    // LE CRITÈRE DE L'ÉTAPE : écrire 0,5 de 0 à 960 dans une courbe à 1,0
    // laisse la courbe à 1,0 juste après la plage.
    AutomationCurve courbe;
    courbe.parameter = "mix.volume";
    courbe.points = {{0, 1.0f, false}, {3840, 1.0f, false}};

    writeAutomationRange(courbe, 0, 960, {{0, 0.5f, false}, {480, 0.5f, false}, {960, 0.5f, false}});

    VSM_ASSERT_NEAR(automationValueAt(courbe, 0), 0.5f, 1e-6f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 480), 0.5f, 1e-6f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 960), 0.5f, 1e-6f);
    // Le raccord : juste après la plage, la courbe redit ce qu'elle disait.
    VSM_ASSERT_NEAR(automationValueAt(courbe, 961), 1.0f, 1e-6f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 3840), 1.0f, 1e-6f);
    // La plage commençait au tick 0 : il n'y a pas de « juste avant » où
    // poser un raccord, et on n'en invente pas.
    for (const auto& p : courbe.points) VSM_ASSERT(p.tick >= 0);
}

VSM_TEST(writing_in_the_middle_of_a_ramp_breaks_neither_side) {
    // Sans raccord, corriger deux mesures au milieu d'un fondu ferait sauter
    // le paramètre à l'entrée ET à la sortie : on aurait réparé deux mesures
    // en cassant les deux voisines.
    AutomationCurve courbe;
    courbe.points = {{0, 0.0f, false}, {3840, 1.0f, false}};   // un fondu linéaire
    const float avant = automationValueAt(courbe, 959);
    const float apres = automationValueAt(courbe, 1921);

    writeAutomationRange(courbe, 960, 1920, {{960, 0.9f, false}, {1920, 0.9f, false}});

    VSM_ASSERT_NEAR(automationValueAt(courbe, 960), 0.9f, 1e-6f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 1920), 0.9f, 1e-6f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 959), avant, 1e-6f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 1921), apres, 1e-6f);
    // Et les extrémités du fondu n'ont pas bougé.
    VSM_ASSERT_NEAR(automationValueAt(courbe, 0), 0.0f, 1e-6f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 3840), 1.0f, 1e-6f);
}

VSM_TEST(a_pass_where_nothing_was_touched_erases_nothing) {
    // Un passage sans point joué ne doit pas vider la plage : on a laissé le
    // fader tranquille, ce n'est pas la même chose que l'avoir mis à zéro.
    AutomationCurve courbe;
    courbe.points = {{0, 0.2f, false}, {960, 0.8f, false}};
    const auto avant = courbe.points;
    writeAutomationRange(courbe, 0, 960, {});
    VSM_ASSERT_EQ(courbe.points.size(), avant.size());
    VSM_ASSERT_NEAR(courbe.points[1].value, avant[1].value, 1e-9f);
}

VSM_TEST(the_played_point_wins_over_the_one_that_was_there) {
    // Deux points au même tick rendraient le segment indéfini. C'est celui
    // qu'on vient de jouer qui gagne, et cela ne dépend d'aucun ordre de tri.
    AutomationCurve courbe;
    courbe.points = {{480, 0.1f, false}};
    writeAutomationRange(courbe, 0, 960, {{480, 0.7f, false}});
    VSM_ASSERT_EQ(courbe.points.size(), size_t(2));   // le point joué + le raccord d'après
    VSM_ASSERT_NEAR(automationValueAt(courbe, 480), 0.7f, 1e-6f);
}

VSM_TEST(writing_on_an_empty_curve_lays_the_pass_down_without_inventing_joins) {
    // Une courbe vide ne disait rien : il n'y a rien à raccorder, et poser un
    // raccord à zéro ferait plonger le paramètre hors de la plage.
    AutomationCurve courbe;
    writeAutomationRange(courbe, 960, 1920, {{960, 0.4f, false}, {1920, 0.6f, false}});
    VSM_ASSERT_EQ(courbe.points.size(), size_t(2));
    VSM_ASSERT_NEAR(automationValueAt(courbe, 0), 0.4f, 1e-6f);      // maintenue, pas à zéro
    VSM_ASSERT_NEAR(automationValueAt(courbe, 5000), 0.6f, 1e-6f);
}
