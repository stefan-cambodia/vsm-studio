#include "vsm/sequencer/AutomationEdit.h"
#include <algorithm>
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
        const double ratio = static_cast<double>(tick - a.tick)
                           / static_cast<double>(b.tick - a.tick);
        return static_cast<float>(a.value + ratio * (b.value - a.value));
    }
    return points.back().value;
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
