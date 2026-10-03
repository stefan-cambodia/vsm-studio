#pragma once
// EN-TÊTE INTERNE À core/src/sequencer : CE QU'UNE PISTE FAIT ENTENDRE DANS UNE PLAGE.
//
// D537 l'a écrit pour « Aplatir l'ordre de jeu », D535.3 en a besoin pour « Copier la plage » :
// une seule lecture, par les passages de la lecture et de l'export (`clipPassages`, ici clips
// MUETS COMPRIS — aplatir ou copier ne doit pas perdre les notes d'un clip qu'on a seulement fait
// taire). Deux recopies de cette lecture divergeraient à la première correction.
#include "vsm/sequencer/ClipEdit.h"
#include "vsm/sequencer/Track.h"
#include <algorithm>
#include <map>
#include <vector>

namespace vsm::sequencer::detail {

/// Les notes que la piste fait entendre dans [debut, fin) de la ligne de temps, posées à
/// `t − debut + ou`, coupées à la fin de la plage et à celle de leur clip.
inline void entendreNotes(const std::vector<Note>& notes, const std::vector<ClipPassage>& passages, Tick debut,
                          Tick fin, Tick ou, std::vector<Note>& sortie) {
    for (const auto& note : notes)
        for (const auto& passage : passages) {
            const Tick t = passageOut(passage, note.startTick);
            if (t < 0 || t < debut || t >= fin) continue;
            Note copie = note;
            copie.startTick = t - debut + ou;
            copie.endTick = std::min({note.endTick + passage.shift, passage.outLimit, fin}) - debut + ou;
            if (copie.endTick <= copie.startTick) copie.endTick = copie.startTick + 1;
            copie.id = 0;
            sortie.push_back(copie);
        }
}

/// Les contrôleurs entendus dans [debut, fin), posés de même ; et, si `poursuivre`, pour chaque
/// clé (un « réglage »), le dernier entendu AVANT `debut`, posé à `ou` s'il n'y en a pas déjà un
/// — la règle des courbes d'automation : un créneau n'hérite pas de ce qui joue avant lui.
template <typename Point, typename Cle>
void entendreControleurs(const std::vector<Point>& source, const std::vector<ClipPassage>& passages, Tick debut,
                         Tick fin, Tick ou, Cle cle, bool poursuivre, std::vector<Point>& sortie) {
    if (source.empty()) return;
    std::map<decltype(cle(source.front())), std::pair<Tick, Point>> avant;
    std::vector<Point> dedans;
    for (const auto& point : source) {
        // OÙ CE POINT S'ENTEND : par chaque passage qui le couvre — ou, couvert par aucun, au début
        // du passage suivant (D335 : la banque et le volume d'un fichier General MIDI, posés au
        // tick 0 avant le premier clip). C'est ce que font la lecture et l'export.
        std::vector<Tick> entendus;
        for (const auto& passage : passages)
            if (const Tick t = passageOut(passage, point.tick); t >= 0) entendus.push_back(t);
        // Comme le planificateur : CC, pli, pression et programmes, pas la pression polyphonique.
        if (entendus.empty() && poursuivre)
            if (const Tick t = rattacheAuPassageSuivant(passages, point.tick); t >= 0) entendus.push_back(t);
        for (const Tick t : entendus) {
            if (t >= debut && t < fin) {
                Point copie = point;
                copie.tick = t - debut + ou;
                dedans.push_back(copie);
            } else if (t < debut && poursuivre) {
                auto ici = avant.find(cle(point));
                if (ici == avant.end() || ici->second.first <= t) avant[cle(point)] = {t, point};
            }
        }
    }
    for (auto& [k, entendu] : avant) {
        bool deja = false;
        for (const auto& p : dedans)
            if (cle(p) == k && p.tick == ou) deja = true;
        if (deja) continue;
        Point ouverture = entendu.second;
        ouverture.tick = ou;
        sortie.push_back(ouverture);
    }
    sortie.insert(sortie.end(), dedans.begin(), dedans.end());
}

template <typename Point>
void trierParTick(std::vector<Point>& points) {
    std::stable_sort(points.begin(), points.end(), [](const Point& a, const Point& b) { return a.tick < b.tick; });
}

} // namespace vsm::sequencer::detail
