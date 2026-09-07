#include "vsm/sequencer/AutomationEdit.h"
#include <algorithm>
#include <map>
#include <cmath>

namespace vsm::sequencer {

float automationValueAt(const AutomationCurve& curve, Tick tick) {
    const auto& points = curve.points;
    if (points.empty()) return 0.0f;
    if (tick <= points.front().tick) return points.front().value;
    if (tick >= points.back().tick) return points.back().value;

    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const auto& a = points[i];
        const auto& b = points[i + 1];
        if (tick < a.tick || tick > b.tick) continue;
        // UN PALIER TIENT SA VALEUR JUSQU'AU POINT SUIVANT : c'est ce que veut
        // dire `step`, et c'est ce qu'il faut pour un commutateur, un choix de
        // forme d'onde ou tout ce qui ne s'interpole pas.
        if (a.step || b.tick == a.tick) return a.value;
        const float ratio = static_cast<float>(tick - a.tick)
                            / static_cast<float>(b.tick - a.tick);
        // LA COURBURE (D17.7) : la MÊME fonction que le moteur, appelée depuis
        // le modèle. Deux formules qui divergeraient feraient dessiner une
        // courbe et en entendre une autre.
        const float avance = automationCurveEase(a.curve, ratio);
        return a.value + avance * (b.value - a.value);
    }
    return points.back().value;
}

size_t shiftAutomationRange(AutomationCurve& curve, Tick fromTick, Tick toTick,
                            Tick deltaTicks) {
    if (deltaTicks == 0 || toTick <= fromTick || curve.points.empty()) return 0;

    std::map<Tick, AutomationPoint> parTick;
    std::vector<AutomationPoint> deplaces;
    for (const auto& p : curve.points) {
        if (p.tick >= fromTick && p.tick < toTick) {
            AutomationPoint bouge = p;
            bouge.tick = p.tick + deltaTicks;
            // Aucun point ne passe avant zéro : la ligne de temps commence là,
            // et un point négatif ne serait ni dessiné ni joué.
            if (bouge.tick < 0) continue;
            deplaces.push_back(bouge);
        } else {
            parTick[p.tick] = p;
        }
    }
    // EN DERNIER, donc vainqueurs : ce qu'on vient de tirer.
    for (const auto& p : deplaces) parTick[p.tick] = p;

    curve.points.clear();
    curve.points.reserve(parTick.size());
    for (const auto& [tick, point] : parTick) curve.points.push_back(point);
    return deplaces.size();
}

void writeAutomationRange(AutomationCurve& curve, Tick fromTick, Tick toTick,
                           const std::vector<AutomationPoint>& written) {
    if (written.empty() || toTick < fromTick) return;

    // CE QUE LA COURBE DISAIT AUX BORDS, LU AVANT DE TOUCHER À QUOI QUE CE
    // SOIT : après suppression, il n'y aurait plus rien à lire.
    const bool avait = !curve.points.empty();
    const float avant = avait ? automationValueAt(curve, fromTick > 0 ? fromTick - 1 : 0) : 0.0f;
    const float apres = avait ? automationValueAt(curve, toTick + 1) : 0.0f;

    // UNE CARTE PAR TICK, et non un tri suivi d'un dédoublonnage : deux points
    // au même tick rendraient le segment entre eux indéfini, et il faut que ce
    // soit le POINT JOUÉ qui gagne, pas celui que l'ordre de tri a mis devant.
    // Un `std::unique` garde le premier, et le tri n'est pas stable sur les
    // ex æquo : la courbe aurait dépendu de l'implémentation de `std::sort`.
    std::map<Tick, AutomationPoint> parTick;
    for (const auto& p : curve.points)
        if (p.tick < fromTick || p.tick > toTick) parTick[p.tick] = p;

    // LES RACCORDS, à UN TICK de la plage et non à ses bords : posés dessus,
    // ils écraseraient le premier et le dernier point de ce qu'on vient de
    // jouer. Seulement si la courbe disait quelque chose, et seulement s'il y
    // a la place (une plage qui commence au tick 0 n'a pas de « juste avant »).
    if (avait && fromTick > 0) parTick[fromTick - 1] = {fromTick - 1, avant, false};
    if (avait) parTick[toTick + 1] = {toTick + 1, apres, false};

    // EN DERNIER, donc vainqueurs : ce qu'on vient de jouer.
    for (const auto& p : written)
        if (p.tick >= fromTick && p.tick <= toTick) parTick[p.tick] = p;

    curve.points.clear();
    curve.points.reserve(parTick.size());
    for (const auto& [tick, point] : parTick) curve.points.push_back(point);
}

size_t setAutomationPoint(AutomationCurve& curve, Tick tick, float value, bool step) {
    for (size_t i = 0; i < curve.points.size(); ++i) {
        if (curve.points[i].tick != tick) continue;
        curve.points[i].value = value;
        curve.points[i].step = step;
        return i;
    }
    curve.points.push_back({tick, value, step});
    std::stable_sort(curve.points.begin(), curve.points.end(),
                      [](const AutomationPoint& a, const AutomationPoint& b) {
                          return a.tick < b.tick;
                      });
    for (size_t i = 0; i < curve.points.size(); ++i)
        if (curve.points[i].tick == tick) return i;
    return curve.points.size() - 1;
}

size_t automationPointNear(const AutomationCurve& curve, Tick tick, Tick tolerance) {
    size_t meilleur = curve.points.size();
    Tick distance = tolerance + 1;
    for (size_t i = 0; i < curve.points.size(); ++i) {
        const Tick d = std::abs(curve.points[i].tick - tick);
        if (d <= tolerance && d < distance) { distance = d; meilleur = i; }
    }
    return meilleur;
}

bool removeAutomationPointNear(AutomationCurve& curve, Tick tick, Tick tolerance) {
    const size_t index = automationPointNear(curve, tick, tolerance);
    if (index >= curve.points.size()) return false;
    curve.points.erase(curve.points.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

// ---------------------------------------------------------------------------
// D30.5 — RÉDUIRE LES POINTS D'UNE COURBE.
// ---------------------------------------------------------------------------

namespace {

/// Un tronçon [debut, fin] gardé tel quel ; la récursion l'ouvre en deux au
/// point qui s'écarte le plus.
void simplifier(const std::vector<AutomationPoint>& points, size_t debut, size_t fin,
                 float tolerance, std::vector<bool>& garde) {
    if (fin <= debut + 1) return;
    const AutomationPoint& a = points[debut];
    const AutomationPoint& b = points[fin];
    // LA DROITE DE RÉFÉRENCE EST CELLE QUI RESTERAIT si l'on retirait tout
    // l'intérieur -- donc la même interpolation que `automationValueAt`, qui
    // est linéaire entre deux points. Un palier est traité par l'appelant :
    // ici la droite serait fausse, et c'est pourquoi `step` et `curve` coupent.
    const double span = static_cast<double>(b.tick - a.tick);
    size_t pire = debut;
    double pireEcart = 0.0;
    for (size_t i = debut + 1; i < fin; ++i) {
        const double t = span > 0.0 ? static_cast<double>(points[i].tick - a.tick) / span : 0.0;
        const double surLaDroite = a.value + (b.value - a.value) * t;
        const double ecart = std::fabs(points[i].value - surLaDroite);
        if (ecart > pireEcart) { pireEcart = ecart; pire = i; }
    }
    if (pireEcart <= static_cast<double>(tolerance)) return;   // tout l'intérieur tombe
    garde[pire] = true;
    simplifier(points, debut, pire, tolerance, garde);
    simplifier(points, pire, fin, tolerance, garde);
}

} // namespace

size_t thinAutomation(AutomationCurve& curve, float tolerance) {
    if (tolerance <= 0.0f || curve.points.size() <= 2) return 0;
    const auto& points = curve.points;
    const size_t n = points.size();
    std::vector<bool> garde(n, false);
    garde[0] = true;
    garde[n - 1] = true;

    // LES SEGMENTS QUI NE SONT PAS DES DROITES COUPENT LA COURBE EN TRONÇONS,
    // et l'on simplifie chacun séparément. Il y en a DEUX SORTES, et oublier
    // la seconde aurait fait mentir la tolérance :
    //  - le PALIER (`step`) : le segment qui en part est plat ;
    //  - la COURBURE (`curve`, D17.7) : le segment qui en part est fléchi.
    // Dans les deux cas, la droite que la simplification suppose entre deux
    // points n'existe pas, et l'écart qu'elle mesurerait serait celui d'une
    // courbe qu'on ne joue pas. On garde donc ce point ET celui qui le suit --
    // retirer le second déplacerait la marche ou la flèche.
    size_t debut = 0;
    for (size_t i = 0; i < n; ++i) {
        if (!points[i].step && points[i].curve == 0.0f) continue;
        garde[i] = true;
        if (i + 1 < n) garde[i + 1] = true;
        simplifier(points, debut, i, tolerance, garde);
        debut = i + 1 < n ? i + 1 : i;
    }
    simplifier(points, debut, n - 1, tolerance, garde);

    std::vector<AutomationPoint> restants;
    restants.reserve(n);
    for (size_t i = 0; i < n; ++i)
        if (garde[i]) restants.push_back(points[i]);
    const size_t retires = n - restants.size();
    curve.points = std::move(restants);
    return retires;
}

float maxAutomationDeviation(const AutomationCurve& a, const AutomationCurve& b) {
    // SUR L'UNION DES TICKS, et non sur ceux de l'une des deux : le pire écart
    // d'une réduction se trouve précisément AUX POINTS RETIRÉS, qui ne sont
    // plus dans la courbe réduite. Ne mesurer que sur celle-ci rendrait zéro à
    // tous les coups -- un chiffre qui se contente de confirmer ce qu'on veut
    // croire.
    std::vector<Tick> ticks;
    ticks.reserve(a.points.size() + b.points.size());
    for (const auto& p : a.points) ticks.push_back(p.tick);
    for (const auto& p : b.points) ticks.push_back(p.tick);
    std::sort(ticks.begin(), ticks.end());
    ticks.erase(std::unique(ticks.begin(), ticks.end()), ticks.end());
    float pire = 0.0f;
    for (Tick t : ticks)
        pire = std::max(pire, std::fabs(automationValueAt(a, t) - automationValueAt(b, t)));
    return pire;
}

// ---------------------------------------------------------------------------
// DESSINER UNE FORME (D34.5)
// ---------------------------------------------------------------------------

namespace {

/// La forme, sur un avancement `x` de 0 à 1, ramenée entre 0 et 1.
/// `Line` monte tout droit ; les autres oscillent `periods` fois en partant du
/// creux, comme le fait un LFO qu'on met en marche.
float formeA(AutomationShape shape, double x, int periods) {
    if (shape == AutomationShape::Line) return static_cast<float>(x);
    const double phase = x * periods;
    const double dansLaPeriode = phase - std::floor(phase);
    switch (shape) {
        case AutomationShape::Sine:
            // DÉPART AU CREUX ET RETOUR AU CREUX : `(1 - cos)/2` plutôt qu'un
            // sinus brut, qui commencerait à mi-course. Un LFO qu'on met en
            // marche part du bas, et c'est ce qu'on dessine à la main.
            return static_cast<float>((1.0 - std::cos(dansLaPeriode * 2.0 * 3.14159265358979323846)) * 0.5);
        case AutomationShape::Triangle:
            return static_cast<float>(dansLaPeriode < 0.5 ? dansLaPeriode * 2.0
                                                          : (1.0 - dansLaPeriode) * 2.0);
        case AutomationShape::Square:
            return dansLaPeriode < 0.5 ? 0.0f : 1.0f;
        case AutomationShape::Line:
        default:
            return static_cast<float>(x);
    }
}

} // namespace

AutomationDraw drawAutomationShape(AutomationCurve& curve, Tick fromTick, Tick toTick,
                                    AutomationShape shape, float from, float to,
                                    int periods, float tolerance) {
    AutomationDraw fait;
    if (toTick <= fromTick) return fait;
    if (periods <= 0) periods = 1;   // une forme sans oscillation n'a pas de sens

    // CE QUE LA COURBE DISAIT AUX BORDS, relevé AVANT de toucher à quoi que ce
    // soit : les raccords s'appuient dessus, et les lire après le retrait
    // rendrait la valeur maintenue de la plage voisine.
    const bool avaitQuelqueChose = !curve.points.empty();
    const float avant = avaitQuelqueChose ? automationValueAt(curve, fromTick - 1) : 0.0f;
    const float apres = avaitQuelqueChose ? automationValueAt(curve, toTick + 1) : 0.0f;

    const size_t nAvant = curve.points.size();
    curve.points.erase(std::remove_if(curve.points.begin(), curve.points.end(),
                                       [&](const AutomationPoint& p) {
                                           return p.tick >= fromTick && p.tick <= toTick;
                                       }),
                        curve.points.end());
    fait.removed = nAvant - curve.points.size();

    const Tick etendue = toTick - fromTick;
    std::vector<AutomationPoint> traces;

    if (shape == AutomationShape::Square) {
        // UN CARRÉ NE S'ÉCHANTILLONNE PAS. Approché par des rampes très
        // raides, il n'est plus un carré : il devient une suite de fondus
        // courts, ce qui s'entend. Deux points par période, marqués `step`, et
        // la courbe dit exactement ce qu'elle joue.
        for (int k = 0; k < periods; ++k) {
            const Tick base = fromTick + static_cast<Tick>(etendue * k / periods);
            const Tick mi = fromTick + static_cast<Tick>(etendue * (2 * k + 1) / (2 * periods));
            traces.push_back({base, from, true, 0.0f});
            if (mi > base) traces.push_back({mi, to, true, 0.0f});
        }
        traces.push_back({toTick, from, true, 0.0f});
    } else {
        // ON ÉCHANTILLONNE FIN, PUIS ON RÉDUIT. Le nombre de points de départ
        // n'a pas à être malin : `thinAutomation` (D30.5) est écrite pour
        // exactement ce problème, et lui confier la parcimonie vaut mieux que
        // de la deviner ici forme par forme.
        // 256 ÉCHANTILLONS PAR PÉRIODE, ET NON 64. La tolérance promise porte
        // sur l'écart à la forme IDÉALE, or DEUX erreurs s'y ajoutent : celle
        // de la réduction, que `thinAutomation` borne, et celle de la
        // POLYLIGNE échantillonnée elle-même, dont les cordes coupent les
        // sommets. À 64 points par période, cette seconde erreur vaut à elle
        // seule un huitième du budget, et la mesure rendait 1,01 % pour 1 %
        // promis -- un dépassement de rien du tout, qui restait un dépassement.
        // À 256 elle tombe d'un facteur seize et devient négligeable.
        const int parPeriode = shape == AutomationShape::Line ? 1 : 256;
        const int echantillons = std::max(2, parPeriode * std::max(1, periods));
        for (int i = 0; i <= echantillons; ++i) {
            const double x = static_cast<double>(i) / echantillons;
            const Tick tick = fromTick + static_cast<Tick>(std::llround(etendue * x));
            traces.push_back({tick, from + (to - from) * formeA(shape, x, periods), false, 0.0f});
        }
    }

    for (const auto& point : traces) setAutomationPoint(curve, point.tick, point.value, point.step);

    // LES RACCORDS, à un tick de la plage et jamais dedans : un raccord posé à
    // l'intérieur écraserait le début de la forme qu'on vient de tracer.
    if (avaitQuelqueChose) {
        if (fromTick > 0) setAutomationPoint(curve, fromTick - 1, avant);
        setAutomationPoint(curve, toTick + 1, apres);
    }

    // LA RÉDUCTION NE TOUCHE QUE CE QU'ON VIENT DE POSER : `thinAutomation`
    // travaille sur toute la courbe, ce qui amincirait aussi des passages que
    // personne n'a demandé de retoucher. On réduit donc une courbe qui ne
    // contient que la plage, puis on la recolle.
    if (shape != AutomationShape::Square && tolerance > 0.0f) {
        // ET L'ON RÉSERVE UN VINGTIÈME DU BUDGET à ce qui reste d'erreur
        // d'échantillonnage : promettre `tolerance` puis en dépenser la
        // totalité à la réduction seule, c'est promettre ce qu'on ne tient
        // qu'au bord. Le témoin est la mesure, qui doit passer SOUS le chiffre
        // annoncé, pas dessus.
        const float budget = tolerance * 0.95f;
        AutomationCurve plage;
        AutomationCurve reste;
        for (const auto& p : curve.points)
            (p.tick >= fromTick && p.tick <= toTick ? plage : reste).points.push_back(p);
        thinAutomation(plage, budget);
        curve.points = std::move(reste.points);
        for (const auto& p : plage.points) curve.points.push_back(p);
        std::sort(curve.points.begin(), curve.points.end(),
                  [](const AutomationPoint& a, const AutomationPoint& b) { return a.tick < b.tick; });
    }

    for (const auto& p : curve.points)
        if (p.tick >= fromTick && p.tick <= toTick) ++fait.added;
    return fait;
}

} // namespace vsm::sequencer
