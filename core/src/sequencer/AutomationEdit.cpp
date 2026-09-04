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

} // namespace vsm::sequencer
