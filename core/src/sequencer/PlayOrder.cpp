#include "vsm/sequencer/PlayOrder.h"
#include "vsm/sequencer/AutomationEdit.h"
#include "vsm/sequencer/ClipEdit.h"
#include <algorithm>

namespace vsm::sequencer {

std::vector<Section> sectionsFromMarkers(const Project& project) {
    std::vector<Section> sections;
    if (project.markers.empty()) return sections;

    std::vector<Marker> reperes = project.markers;
    std::stable_sort(reperes.begin(), reperes.end(),
                      [](const Marker& a, const Marker& b) { return a.tick < b.tick; });

    // LA DERNIÈRE CHOSE QUI SONNE, et non la dernière NOTE (la leçon de D8.3,
    // repayée ici) : `lastUsedTick()` ne connaît que le matériau MIDI, et une
    // reconstruction faite de clips AUDIO n'aurait alors eu aucune section
    // au-delà de son dernier repère -- c'est-à-dire, le plus souvent, aucune.
    const Tick fin = project.lastSoundingTick();
    for (size_t i = 0; i < reperes.size(); ++i) {
        Section s;
        s.name = reperes[i].name;
        s.startTick = std::max<Tick>(0, reperes[i].tick);
        s.endTick = (i + 1 < reperes.size()) ? reperes[i + 1].tick : fin;
        // UNE SECTION VIDE NE SE VOIT QU'À CE QU'ELLE NE FAIT RIEN : on ne la
        // propose pas. C'est le cas d'un repère posé après tout le matériau.
        if (s.endTick > s.startTick) sections.push_back(std::move(s));
    }
    return sections;
}

bool flattenChangesTempoMeaning(const Project& project) {
    return project.tempoMap.changes().size() > 1
        || project.timeSignatureMap.changes().size() > 1;
}

bool flattenPlayOrder(Project& project, const std::vector<int>& order) {
    const auto sections = sectionsFromMarkers(project);
    if (sections.empty() || order.empty()) return false;

    // LES CRÉNEAUX : où chaque section demandée atterrit sur la nouvelle ligne
    // de temps. Relevés d'abord, appliqués ensuite -- écrire au fur et à
    // mesure ferait relire du matériau qu'on vient de poser.
    struct Creneau { Section section; Tick sortie; };
    std::vector<Creneau> creneaux;
    Tick curseur = 0;
    for (int index : order) {
        if (index < 0 || static_cast<size_t>(index) >= sections.size()) continue;
        const Section& s = sections[static_cast<size_t>(index)];
        creneaux.push_back({s, curseur});
        curseur += s.length();
    }
    if (creneaux.empty()) return false;

    for (auto& piste : project.tracks) {
        std::vector<Note> notes;
        std::vector<Clip> clips;
        std::vector<AutomationCurve> courbes;
        for (const auto& courbe : piste.automation) {
            AutomationCurve neuve;
            neuve.parameter = courbe.parameter;
            courbes.push_back(std::move(neuve));
        }

        for (const auto& creneau : creneaux) {
            const Tick delta = creneau.sortie - creneau.section.startTick;
            const Tick debut = creneau.section.startTick;
            const Tick fin = creneau.section.endTick;

            for (const auto& note : piste.notes) {
                if (note.startTick < debut || note.startTick >= fin) continue;
                Note copie = note;
                copie.startTick += delta;
                // COUPÉE À LA FIN DE SA SECTION : laissée entière, elle
                // empiéterait sur la section suivante, que personne n'a
                // arrangée ainsi. C'est la règle de `splitClips` au bord d'un
                // clip, et pour la même raison.
                copie.endTick = std::min(note.endTick, fin) + delta;
                if (copie.endTick <= copie.startTick) copie.endTick = copie.startTick + 1;
                copie.id = 0;
                notes.push_back(copie);
            }

            for (const auto& clip : piste.clips) {
                const Tick jouee = clipPlayedLength(clip, project.lastSoundingTick());
                const Tick clipFin = clip.startTick + jouee;
                if (clipFin <= debut || clip.startTick >= fin) continue;
                Clip copie = clip;
                copie.id = 0;
                // Rogné aux bords de la section, fenêtre comprise : un clip à
                // cheval ne joue que ce que la section contient.
                const Tick rogneAvant = std::max<Tick>(0, debut - clip.startTick);
                copie.sourceStart = clip.sourceStart + rogneAvant;
                copie.startTick = std::max(clip.startTick, debut) + delta;
                const Tick longueur = std::min(clipFin, fin) - std::max(clip.startTick, debut);
                copie.length = longueur;
                copie.sourceLength = longueur;
                clips.push_back(copie);
            }

            for (size_t c = 0; c < piste.automation.size(); ++c) {
                for (const auto& point : piste.automation[c].points) {
                    if (point.tick < debut || point.tick >= fin) continue;
                    AutomationPoint copie = point;
                    copie.tick += delta;
                    courbes[c].points.push_back(copie);
                }
                // UN POINT AU DÉBUT DU CRÉNEAU, à la valeur que la courbe avait
                // à l'entrée de la section : sans lui, un créneau qui commence
                // au milieu d'un fondu hériterait de la valeur du créneau
                // précédent, et le paramètre sauterait au raccord.
                if (!piste.automation[c].points.empty()) {
                    const float valeur = automationValueAt(piste.automation[c], debut);
                    bool deja = false;
                    for (const auto& p : courbes[c].points)
                        if (p.tick == creneau.sortie) { deja = true; break; }
                    if (!deja) courbes[c].points.push_back({creneau.sortie, valeur, false, 0.0f});
                }
            }
        }

        std::stable_sort(notes.begin(), notes.end(),
                          [](const Note& a, const Note& b) { return a.startTick < b.startTick; });
        std::stable_sort(clips.begin(), clips.end(),
                          [](const Clip& a, const Clip& b) { return a.startTick < b.startTick; });
        for (auto& courbe : courbes)
            std::stable_sort(courbe.points.begin(), courbe.points.end(),
                              [](const AutomationPoint& a, const AutomationPoint& b) {
                                  return a.tick < b.tick;
                              });
        piste.notes = std::move(notes);
        piste.clips = std::move(clips);
        piste.automation = std::move(courbes);
    }

    // LES REPÈRES SUIVENT, un par créneau : le morceau aplati doit se relire.
    // Sans eux, on aurait la structure qu'on voulait et plus aucun moyen de
    // savoir où elle commence.
    std::vector<Marker> reperes;
    for (const auto& creneau : creneaux)
        reperes.push_back({creneau.sortie, creneau.section.name});
    project.markers = std::move(reperes);

    // DES IDENTIFIANTS NEUFS : une section jouée deux fois a produit deux fois
    // les mêmes notes, et deux notes de même identifiant rendraient la
    // sélection et l'automation liée indéchiffrables.
    for (auto& piste : project.tracks)
        for (auto& note : piste.notes) note.id = project.nextNoteId();
    project.assignClipIds();
    return true;
}

} // namespace vsm::sequencer
