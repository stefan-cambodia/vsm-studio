#include "vsm/sequencer/Groove.h"
#include <algorithm>
#include <cmath>

namespace vsm::sequencer {

namespace {
/// Le pas le plus proche d'un tick, et l'écart en fraction de pas.
void plusProche(Tick tick, double pas, int stepsPerBar, int& index, double& ecart) {
    const double position = static_cast<double>(tick) / pas;
    const auto arrondi = static_cast<long long>(std::llround(position));
    ecart = position - static_cast<double>(arrondi);
    index = static_cast<int>(((arrondi % stepsPerBar) + stepsPerBar) % stepsPerBar);
}
} // namespace

Groove extractGroove(const std::vector<Note>& notes, Tick ticksPerBar, int stepsPerBar,
                      const std::string& name) {
    Groove groove;
    groove.name = name;
    groove.stepsPerBar = std::max(1, stepsPerBar);
    if (ticksPerBar <= 0 || notes.empty()) return groove;

    const double pas = static_cast<double>(ticksPerBar) / groove.stepsPerBar;
    groove.steps.assign(static_cast<size_t>(groove.stepsPerBar), GrooveStep{});
    std::vector<int> compte(static_cast<size_t>(groove.stepsPerBar), 0);
    std::vector<double> sommeEcart(static_cast<size_t>(groove.stepsPerBar), 0.0);
    std::vector<double> sommeVelocite(static_cast<size_t>(groove.stepsPerBar), 0.0);

    for (const auto& note : notes) {
        if (note.muted) continue;   // une note qu'on n'entend pas ne décrit aucun placement
        int index = 0;
        double ecart = 0.0;
        plusProche(note.startTick, pas, groove.stepsPerBar, index, ecart);
        // AU-DELÀ D'UN DEMI-PAS, la note n'est plus en avance sur son pas :
        // elle est sur un autre. (L'arrondi garantit déjà |ecart| <= 0,5 ; la
        // garde reste écrite pour que la règle se lise.)
        if (std::abs(ecart) > 0.5) continue;
        const auto i = static_cast<size_t>(index);
        ++compte[i];
        sommeEcart[i] += ecart;
        sommeVelocite[i] += static_cast<double>(note.velocity) / 127.0;
    }

    for (size_t i = 0; i < groove.steps.size(); ++i) {
        if (compte[i] == 0) continue;
        groove.steps[i].offset = sommeEcart[i] / compte[i];
        groove.steps[i].velocity = static_cast<float>(sommeVelocite[i] / compte[i]);
        groove.steps[i].present = true;
    }
    return groove;
}

size_t applyGroove(std::vector<Note>& notes, const NoteSelection& selection,
                    const Groove& groove, Tick ticksPerBar, float strength,
                    bool applyVelocity) {
    if (groove.empty() || ticksPerBar <= 0) return 0;
    const double pas = static_cast<double>(ticksPerBar) / std::max(1, groove.stepsPerBar);
    const double force = std::clamp(static_cast<double>(strength), 0.0, 1.0);
    if (force <= 0.0) return 0;

    size_t deplacees = 0;
    for (auto& note : notes) {
        if (!selection.empty() && selection.count(note.id) == 0) continue;
        int index = 0;
        double ecart = 0.0;
        plusProche(note.startTick, pas, groove.stepsPerBar, index, ecart);
        const auto i = static_cast<size_t>(index);
        if (i >= groove.steps.size() || !groove.steps[i].present) continue;

        // LA CIBLE EST LE PAS PLUS L'ÉCART DU GROOVE ; la force dit quelle
        // part du chemin on fait DEPUIS LÀ OÙ LA NOTE EST. À un, la note prend
        // exactement le placement du groove ; à un demi, elle s'en rapproche
        // de moitié -- ce qui « teinte » sans déplacer.
        const double pasArrondi = std::llround(static_cast<double>(note.startTick) / pas);
        const double cible = (pasArrondi + groove.steps[i].offset) * pas;
        const auto nouvelle = static_cast<Tick>(
            std::llround(static_cast<double>(note.startTick) + force * (cible - static_cast<double>(note.startTick))));
        const Tick duree = note.endTick - note.startTick;
        if (nouvelle == note.startTick && !applyVelocity) continue;
        // AUCUNE NOTE NE PASSE AVANT ZÉRO, et sa durée ne change pas : un
        // groove déplace, il n'étire pas.
        note.startTick = std::max<Tick>(0, nouvelle);
        note.endTick = note.startTick + std::max<Tick>(1, duree);
        if (applyVelocity && groove.steps[i].velocity > 0.0f) {
            const double actuelle = static_cast<double>(note.velocity);
            const double voulue = groove.steps[i].velocity * 127.0;
            note.velocity = static_cast<uint8_t>(
                std::clamp(std::llround(actuelle + force * (voulue - actuelle)), 1LL, 127LL));
        }
        ++deplacees;
    }
    if (deplacees > 0)
        std::stable_sort(notes.begin(), notes.end(),
                          [](const Note& a, const Note& b) { return a.startTick < b.startTick; });
    return deplacees;
}

} // namespace vsm::sequencer
