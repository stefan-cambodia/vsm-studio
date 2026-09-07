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

// --------------------------------------------------------------------------
// D17.7 — L'AUTOMATION QUI COURBE.
//
// Un fondu de volume DROIT EN GAIN n'est pas un fondu droit à l'oreille :
// l'oreille entend des décibels, et une droite en gain passe la moitié de sa
// course dans les six derniers décibels — elle s'entend comme une chute
// brutale à la fin. C'est le geste d'automation le plus courant qui soit.
// --------------------------------------------------------------------------

VSM_TEST(a_curve_of_zero_is_exactly_the_straight_line_it_replaced) {
    // Le défaut ne change rien : un projet d'avant D17.7 sonne et se dessine
    // comme avant, à la valeur près.
    AutomationCurve droite;
    droite.points = {{0, 0.0f, false, 0.0f}, {1000, 1.0f, false, 0.0f}};
    for (Tick t = 0; t <= 1000; t += 100)
        VSM_ASSERT_NEAR(automationValueAt(droite, t), static_cast<float>(t) / 1000.0f, 1e-6f);
}

VSM_TEST(a_positive_curve_rises_fast_and_a_negative_one_drags) {
    AutomationCurve vite, lente;
    vite.points = {{0, 0.0f, false, 1.0f}, {1000, 1.0f, false, 0.0f}};
    lente.points = {{0, 0.0f, false, -1.0f}, {1000, 1.0f, false, 0.0f}};

    // À mi-course, la droite vaut 0,5 ; la courbure les écarte des deux côtés.
    VSM_ASSERT(automationValueAt(vite, 500) > 0.7f);
    VSM_ASSERT(automationValueAt(lente, 500) < 0.3f);
    // Les bornes ne bougent JAMAIS : une courbure qui déplacerait ses propres
    // extrémités ne serait plus une courbure.
    VSM_ASSERT_NEAR(automationValueAt(vite, 0), 0.0f, 1e-6f);
    VSM_ASSERT_NEAR(automationValueAt(vite, 1000), 1.0f, 1e-6f);
    VSM_ASSERT_NEAR(automationValueAt(lente, 0), 0.0f, 1e-6f);
    VSM_ASSERT_NEAR(automationValueAt(lente, 1000), 1.0f, 1e-6f);

    // Et les deux courbures opposées sont réciproques l'une de l'autre : à
    // 0,25 de course, l'une vaut ce que l'autre vaut à 0,75 de valeur.
    VSM_ASSERT_NEAR(automationCurveEase(1.0f, automationCurveEase(-1.0f, 0.37f)), 0.37f, 1e-5f);
}

VSM_TEST(a_step_still_holds_its_value_whatever_the_curve_says) {
    // Un palier ne s'interpole pas : la courbure n'a rien à y faire, et la
    // lire quand même ferait glisser un commutateur.
    AutomationCurve palier;
    palier.points = {{0, 0.2f, true, 1.0f}, {1000, 0.9f, false, 0.0f}};
    VSM_ASSERT_NEAR(automationValueAt(palier, 500), 0.2f, 1e-6f);
    VSM_ASSERT_NEAR(automationValueAt(palier, 999), 0.2f, 1e-6f);
}

VSM_TEST(the_ease_is_monotonic_and_stays_inside_its_bounds) {
    // Ce qu'on demande à une courbure : ne jamais reculer, ne jamais sortir.
    for (float c : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f}) {
        float precedent = -1.0f;
        for (int i = 0; i <= 100; ++i) {
            const float y = automationCurveEase(c, static_cast<float>(i) / 100.0f);
            VSM_ASSERT(y >= -1e-6f && y <= 1.0f + 1e-6f);
            VSM_ASSERT(y >= precedent - 1e-6f);
            precedent = y;
        }
    }
}

// ---------------------------------------------------------------------------
// D30.5 — RÉDUIRE LES POINTS D'UNE COURBE.
//
// L'attendu était écrit avant la mesure (ROADMAP-daw.md, phase D30) : une
// réduction d'au moins UN ORDRE DE GRANDEUR sur une passe réelle, pour un
// écart maximal SOUS la tolérance demandée. Ces tests sont ce qui le tranche.
// ---------------------------------------------------------------------------

/// Une passe d'automation telle que D16.8 en écrit : un point par tick touché,
/// sur un geste qui n'en vaut que quelques-uns. Ici une rampe de 0 à 1 sur
/// deux mesures, échantillonnée tous les deux ticks -- ce qu'un fader donne.
static AutomationCurve unePasseEnW() {
    AutomationCurve courbe;
    courbe.parameter = "mix.volume";
    for (Tick t = 0; t <= 1920; t += 2)
        courbe.points.push_back({t, static_cast<float>(t) / 1920.0f, false, 0.0f});
    return courbe;
}

VSM_TEST(thinning_a_recorded_pass_keeps_the_curve_within_its_tolerance) {
    const AutomationCurve avant = unePasseEnW();
    AutomationCurve apres = avant;
    const float tolerance = 0.01f;             // 1 % de l'amplitude 0..1
    const size_t retires = thinAutomation(apres, tolerance);

    // LA RAMPE EST UNE DROITE : deux points suffisent à la dire, et c'est
    // exactement ce qu'on attend d'une réduction qui a compris ce qu'elle lit.
    VSM_ASSERT(apres.points.size() == 2);
    VSM_ASSERT(retires == avant.points.size() - 2);
    // Les deux extrémités sont là, et aux mêmes valeurs.
    VSM_ASSERT(apres.points.front().tick == avant.points.front().tick);
    VSM_ASSERT(apres.points.back().tick == avant.points.back().tick);
    // ET L'ÉCART TIENT, mesuré sur l'union des ticks -- donc AUX POINTS
    // RETIRÉS, là où il est le plus grand.
    VSM_ASSERT(maxAutomationDeviation(avant, apres) <= tolerance);
}

VSM_TEST(thinning_keeps_what_the_gesture_actually_said) {
    // Une passe qui MONTE PUIS REDESCEND : la réduction ne doit pas raboter le
    // sommet, sans quoi elle changerait le geste au lieu de le nettoyer.
    AutomationCurve avant;
    avant.parameter = "mix.volume";
    for (Tick t = 0; t <= 960; t += 2)
        avant.points.push_back({t, static_cast<float>(t) / 960.0f, false, 0.0f});
    for (Tick t = 962; t <= 1920; t += 2)
        avant.points.push_back({t, static_cast<float>(1920 - t) / 960.0f, false, 0.0f});

    AutomationCurve apres = avant;
    const float tolerance = 0.01f;
    thinAutomation(apres, tolerance);
    VSM_ASSERT(apres.points.size() >= 3);          // le sommet a survécu
    VSM_ASSERT(apres.points.size() <= 8);          // et rien d'autre, ou presque
    VSM_ASSERT(maxAutomationDeviation(avant, apres) <= tolerance);
    // Le sommet est bien à 1, et à peu près au bon endroit.
    VSM_ASSERT_NEAR(automationValueAt(apres, 960), 1.0f, tolerance);
}

VSM_TEST(thinning_never_touches_a_step_or_a_bent_segment) {
    // Un palier n'est pas une rampe et une courbure n'est pas une droite : les
    // retirer ne déplacerait pas la courbe d'un peu, cela en changerait la
    // nature. Le point qui les porte ET son voisin de droite sont gardés.
    AutomationCurve avant;
    avant.parameter = "filter.1.cutoff";
    avant.points.push_back({0,    0.0f, false, 0.0f});
    avant.points.push_back({100,  0.0f, false, 0.0f});   // sur la droite : retirable
    avant.points.push_back({200,  0.0f, true,  0.0f});   // PALIER
    avant.points.push_back({300,  1.0f, false, 0.8f});   // COURBURE
    avant.points.push_back({400,  1.0f, false, 0.0f});
    avant.points.push_back({500,  1.0f, false, 0.0f});

    AutomationCurve apres = avant;
    thinAutomation(apres, 0.5f);   // une tolérance énorme : tout ce qui peut tomber tombe
    bool palier = false, courbure = false, voisin = false, fin = false;
    for (const auto& p : apres.points) {
        if (p.tick == 200 && p.step) palier = true;
        if (p.tick == 300 && p.curve == 0.8f) courbure = true;
        if (p.tick == 400) voisin = true;
        if (p.tick == 500) fin = true;
    }
    VSM_ASSERT(palier && courbure && voisin && fin);
    // Et la courbe rendue vaut toujours ce qu'elle valait aux instants clés,
    // paliers compris.
    VSM_ASSERT_NEAR(automationValueAt(apres, 250), automationValueAt(avant, 250), 1e-6f);
    VSM_ASSERT_NEAR(automationValueAt(apres, 350), automationValueAt(avant, 350), 1e-6f);
}

VSM_TEST(thinning_refuses_to_act_when_it_has_nothing_to_go_on) {
    // Tolérance nulle ou négative : rien n'est retiré. C'est ce qui permet à
    // l'appelant de refuser une courbe dont il ignore l'amplitude sans avoir à
    // inventer un nombre -- une tolérance inventée retirerait des points selon
    // une échelle qui n'est pas la sienne.
    AutomationCurve courbe = unePasseEnW();
    const size_t avant = courbe.points.size();
    VSM_ASSERT(thinAutomation(courbe, 0.0f) == 0);
    VSM_ASSERT(thinAutomation(courbe, -1.0f) == 0);
    VSM_ASSERT(courbe.points.size() == avant);

    // Et une courbe de deux points est déjà irréductible.
    AutomationCurve deux;
    deux.points.push_back({0, 0.0f, false, 0.0f});
    deux.points.push_back({960, 1.0f, false, 0.0f});
    VSM_ASSERT(thinAutomation(deux, 0.5f) == 0);
}

VSM_TEST(the_deviation_is_measured_on_the_union_of_both_curves_ticks) {
    // Le piège que ce chiffre doit éviter : mesuré sur les seuls ticks de la
    // courbe RÉDUITE, l'écart vaudrait zéro à tous les coups -- un chiffre qui
    // se contente de confirmer ce qu'on veut croire.
    AutomationCurve pleine;
    pleine.points.push_back({0,   0.0f, false, 0.0f});
    pleine.points.push_back({480, 1.0f, false, 0.0f});   // un pic, au milieu
    pleine.points.push_back({960, 0.0f, false, 0.0f});
    AutomationCurve plate;
    plate.points.push_back({0,   0.0f, false, 0.0f});
    plate.points.push_back({960, 0.0f, false, 0.0f});
    // Le pic est à 480, un tick que la courbe plate n'a pas : l'écart vaut 1.
    VSM_ASSERT_NEAR(maxAutomationDeviation(pleine, plate), 1.0f, 1e-6f);
}

// ---------------------------------------------------------------------------
// D34.5 — DESSINER UNE AUTOMATION PAR UNE FORME.
//
// L'attendu, écrit avant la mesure : « le sinus rendu par `automationValueAt`
// s'écarte du sinus idéal de moins de 1 % de l'étendue, et le nombre de points
// posés reste borné -- au plus deux par période au-delà de ce qu'il faut pour
// tenir ce 1 %. Le critère de tolérance et celui de parcimonie tirent en sens
// contraires, et ne mesurer que le premier laisserait passer une forme qui
// triche en posant mille points. »
// ---------------------------------------------------------------------------

namespace {

/// Le plus grand écart entre la courbe tracée et la forme idéale, mesuré sur
/// 512 points de la plage -- c'est-à-dire là où l'échantillonnage n'a PAS posé
/// de point, ce qui est le seul endroit où une réduction peut mentir.
double ecartAuSinus(const AutomationCurve& courbe, Tick de, Tick a, float bas, float haut,
                     int periodes) {
    double pire = 0.0;
    for (int i = 0; i <= 512; ++i) {
        const double x = static_cast<double>(i) / 512.0;
        const Tick tick = de + static_cast<Tick>(std::llround((a - de) * x));
        const double ideal = bas + (haut - bas)
            * (1.0 - std::cos(x * periodes * 2.0 * 3.14159265358979323846)) * 0.5;
        pire = std::max(pire, std::abs(automationValueAt(courbe, tick) - ideal));
    }
    return pire;
}

size_t pointsDansLaPlage(const AutomationCurve& courbe, Tick de, Tick a) {
    size_t n = 0;
    for (const auto& p : courbe.points) if (p.tick >= de && p.tick <= a) ++n;
    return n;
}

} // namespace

VSM_TEST(a_drawn_sine_follows_the_ideal_within_one_percent_and_stays_sparse) {
    // QUATRE MESURES, QUATRE PÉRIODES, valeurs de 0 à 1 : la tolérance de 1 %
    // vaut donc 0,01 en unités du paramètre.
    AutomationCurve courbe;
    const Tick de = 0, a = 4 * 1920;
    const auto fait = drawAutomationShape(courbe, de, a, AutomationShape::Sine,
                                           0.0f, 1.0f, 4, 0.01f);
    const double ecart = ecartAuSinus(courbe, de, a, 0.0f, 1.0f, 4);
    const size_t poses = pointsDansLaPlage(courbe, de, a);
    std::printf("      [D34.5] sinus 4 périodes : écart max %.4f, %zu points posés\n",
                ecart, poses);

    VSM_ASSERT(ecart < 0.01);                 // le 1 % annoncé
    VSM_ASSERT_EQ(fait.added, poses);
    // LA PARCIMONIE, mesurée elle aussi : un sinus se décrit en une poignée de
    // points par période, et l'échantillonnage en avait posé 64.
    VSM_ASSERT(poses < 20 * 4);
    VSM_ASSERT(poses >= 3 * 4);               // sinon ce n'est plus un sinus
}

VSM_TEST(a_tighter_tolerance_buys_accuracy_with_points_and_the_trade_is_measured) {
    // LE COMPROMIS EST LA CHOSE À MONTRER : serrer la tolérance doit rapprocher
    // du sinus ET coûter des points. Une mesure qui ne montrerait qu'un des
    // deux ne dirait pas si la fonction travaille ou si elle triche.
    const Tick de = 0, a = 4 * 1920;
    for (float tol : {0.05f, 0.01f, 0.002f}) {
        AutomationCurve courbe;
        drawAutomationShape(courbe, de, a, AutomationShape::Sine, 0.0f, 1.0f, 4, tol);
        std::printf("      [D34.5] tolérance %.3f : écart %.4f, %zu points\n",
                    tol, ecartAuSinus(courbe, de, a, 0.0f, 1.0f, 4),
                    pointsDansLaPlage(courbe, de, a));
    }
    AutomationCurve large, serree;
    drawAutomationShape(large, de, a, AutomationShape::Sine, 0.0f, 1.0f, 4, 0.05f);
    drawAutomationShape(serree, de, a, AutomationShape::Sine, 0.0f, 1.0f, 4, 0.002f);
    VSM_ASSERT(ecartAuSinus(serree, de, a, 0.0f, 1.0f, 4)
                < ecartAuSinus(large, de, a, 0.0f, 1.0f, 4));
    VSM_ASSERT(pointsDansLaPlage(serree, de, a) > pointsDansLaPlage(large, de, a));
}

VSM_TEST(a_drawn_line_is_two_points_and_not_one_per_tick) {
    AutomationCurve courbe;
    drawAutomationShape(courbe, 0, 1920, AutomationShape::Line, 0.2f, 0.8f, 1, 0.01f);
    VSM_ASSERT_EQ(pointsDansLaPlage(courbe, 0, 1920), size_t(2));
    VSM_ASSERT_NEAR(automationValueAt(courbe, 0), 0.2f, 1e-5f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 960), 0.5f, 1e-3f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 1920), 0.8f, 1e-5f);
}

VSM_TEST(a_drawn_square_uses_steps_and_never_ramps_between_its_levels) {
    // UN CARRÉ APPROCHÉ PAR UNE RAMPE TRÈS RAIDE N'EST PAS UN CARRÉ : c'est une
    // suite de fondus courts, et cela s'entend. Chaque point est un palier.
    AutomationCurve courbe;
    drawAutomationShape(courbe, 0, 1920, AutomationShape::Square, 0.0f, 1.0f, 2, 0.01f);
    for (const auto& p : courbe.points)
        if (p.tick >= 0 && p.tick <= 1920) VSM_ASSERT(p.step);
    // Deux périodes : bas, haut, bas, haut, puis le retour au bas à la fin.
    VSM_ASSERT_NEAR(automationValueAt(courbe, 100), 0.0f, 1e-5f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 600), 1.0f, 1e-5f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 1100), 0.0f, 1e-5f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 1600), 1.0f, 1e-5f);
}

VSM_TEST(drawing_a_shape_joins_the_curve_at_both_edges_instead_of_stepping) {
    // Tracer au MILIEU d'un fondu ne doit pas casser les deux voisines : c'est
    // la règle de `writeAutomationRange`, et elle vaut ici pour la même raison.
    AutomationCurve courbe;
    setAutomationPoint(courbe, 0, 0.0f);
    setAutomationPoint(courbe, 4000, 1.0f);
    const float avant = automationValueAt(courbe, 999);
    const float apres = automationValueAt(courbe, 2001);

    drawAutomationShape(courbe, 1000, 2000, AutomationShape::Line, 0.9f, 0.9f, 1, 0.01f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 999), avant, 1e-5f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 2001), apres, 1e-5f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 1500), 0.9f, 1e-5f);
}

VSM_TEST(drawing_a_shape_replaces_what_the_range_held_and_says_how_much) {
    AutomationCurve courbe;
    for (Tick t = 0; t <= 2000; t += 100) setAutomationPoint(courbe, t, 0.5f);
    const auto fait = drawAutomationShape(courbe, 500, 1500, AutomationShape::Line,
                                           0.0f, 1.0f, 1, 0.01f);
    // Onze points étaient dans [500, 1500] : ils sont remplacés, et le nombre
    // est RENDU plutôt que perdu.
    VSM_ASSERT_EQ(fait.removed, size_t(11));
    VSM_ASSERT(fait.added >= 2);
    // Hors de la plage, rien n'a bougé.
    VSM_ASSERT_NEAR(automationValueAt(courbe, 200), 0.5f, 1e-5f);
    VSM_ASSERT_NEAR(automationValueAt(courbe, 1800), 0.5f, 1e-5f);
}

VSM_TEST(an_empty_or_backwards_range_draws_nothing_at_all) {
    AutomationCurve courbe;
    VSM_ASSERT_EQ(drawAutomationShape(courbe, 1000, 1000, AutomationShape::Sine,
                                       0.0f, 1.0f, 4, 0.01f).added, size_t(0));
    VSM_ASSERT_EQ(drawAutomationShape(courbe, 2000, 1000, AutomationShape::Sine,
                                       0.0f, 1.0f, 4, 0.01f).added, size_t(0));
    VSM_ASSERT(courbe.points.empty());
}
