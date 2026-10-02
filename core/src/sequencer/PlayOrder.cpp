#include "vsm/sequencer/PlayOrder.h"
#include "vsm/sequencer/AutomationEdit.h"
#include "vsm/sequencer/ChordTrack.h"
#include "vsm/sequencer/ClipEdit.h"
#include <algorithm>
#include <limits>
#include <map>
#include <tuple>

namespace vsm::sequencer {

namespace {

/// D537 : UNE FENÊTRE DE LECTURE, celle de `clipPassages` — le matériau [de, a) sort à
/// `+decalage`, jusqu'à `limite` exclue —, MAIS LES CLIPS MUETS COMPRIS : aplatir ne doit pas
/// perdre les notes d'un clip qu'on a seulement fait taire ; son clip neuf restera muet.
struct Fenetre {
    Tick de = 0;
    Tick a = std::numeric_limits<Tick>::max();
    Tick decalage = 0;
    Tick limite = std::numeric_limits<Tick>::max();
};

std::vector<Fenetre> fenetresDe(const Track& piste, Tick finMateriau) {
    std::vector<Fenetre> fenetres;
    if (piste.clips.empty()) {
        fenetres.push_back({});   // la piste sans clip : le passage identité
        return fenetres;
    }
    for (const auto& clip : piste.clips) {
        const Tick fenetre = clip.sourceLength > 0 ? clip.sourceLength : std::max<Tick>(0, finMateriau - clip.sourceStart);
        if (fenetre <= 0) continue;
        const Tick jouee = clip.length > 0 ? clip.length : fenetre;
        for (Tick depart = 0; depart < jouee; depart += fenetre)
            fenetres.push_back({clip.sourceStart, clip.sourceStart + fenetre, clip.startTick + depart - clip.sourceStart,
                                clip.startTick + jouee});
    }
    return fenetres;
}

/// Où un événement du matériau SORT par cette fenêtre — faux s'il n'y passe pas.
bool sortie(const Fenetre& f, Tick source, Tick& out) {
    if (source < f.de || source >= f.a) return false;
    out = source + f.decalage;
    return out < f.limite;
}

/// Les contrôleurs d'un créneau : ceux que la section fait entendre, déplacés ; et, pour chacun
/// des « réglages » (même clé), le dernier entendu AVANT la section, posé à l'entrée du créneau
/// s'il n'y en a pas déjà un — la règle des courbes d'automation, pour la même raison.
template <typename Point, typename Cle>
void poserControleurs(const std::vector<Point>& source, const std::vector<Fenetre>& fenetres, Tick debut, Tick fin,
                      Tick creneau, Cle cle, bool poursuivre, std::vector<Point>& sortieListe) {
    std::map<decltype(cle(source.front())), std::pair<Tick, Point>> avant;
    std::vector<Point> dedans;
    for (const auto& point : source)
        for (const auto& f : fenetres) {
            Tick t = 0;
            if (!sortie(f, point.tick, t)) continue;
            if (t >= debut && t < fin) {
                Point copie = point;
                copie.tick = t - debut + creneau;
                dedans.push_back(copie);
            } else if (t < debut && poursuivre) {
                auto ici = avant.find(cle(point));
                if (ici == avant.end() || ici->second.first <= t) avant[cle(point)] = {t, point};
            }
        }
    for (auto& [k, entendu] : avant) {
        bool deja = false;
        for (const auto& p : dedans)
            if (cle(p) == k && p.tick == creneau) deja = true;
        if (deja) continue;
        Point ouverture = entendu.second;
        ouverture.tick = creneau;
        sortieListe.push_back(ouverture);
    }
    sortieListe.insert(sortieListe.end(), dedans.begin(), dedans.end());
}

template <typename Point>
void trierParTick(std::vector<Point>& points) {
    std::stable_sort(points.begin(), points.end(), [](const Point& a, const Point& b) { return a.tick < b.tick; });
}

} // namespace

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

    const Tick finMateriau = project.lastUsedTick();   // celle du planificateur
    for (auto& piste : project.tracks) {
        // D537 : UNE PISTE MIDI SE RECOPIE PAR CE QU'ELLE FAIT ENTENDRE. Recopier le matériau
        // d'après ses ticks et les fenêtres d'après leur place laissait chaque fenêtre lire
        // l'ANCIEN emplacement des notes : après {B, A}, sol sortait à 0,5 s et do à 1 s au lieu
        // de 0 et 0,5. Ce qui sort par les fenêtres est posé sur un matériau neuf, et chaque clip
        // devient une fenêtre IDENTITÉ. Les clips audio (fenêtre en secondes) gardent le reste.
        const bool parLesFenetres = piste.kind != Track::Kind::Audio;
        const std::vector<Fenetre> fenetres = fenetresDe(piste, finMateriau);
        std::vector<CcPoint> ccs;
        std::vector<PitchBendPoint> plis;
        std::vector<PolyAftertouchPoint> pressionsPoly;
        std::vector<ChannelPressurePoint> pressions;
        std::vector<ProgramChangePoint> programmes;
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

            if (parLesFenetres) {
                for (const auto& note : piste.notes)
                    for (const auto& f : fenetres) {
                        Tick t = 0;
                        if (!sortie(f, note.startTick, t) || t < debut || t >= fin) continue;
                        Note copie = note;
                        copie.startTick = t + delta;
                        // COUPÉE À LA FIN DE SA SECTION, et à celle de son clip : laissée entière,
                        // elle empiéterait sur la section suivante, que personne n'a arrangée
                        // ainsi. C'est la règle de `splitClips` au bord d'un clip.
                        copie.endTick = std::min({note.endTick + f.decalage, f.limite, fin}) + delta;
                        if (copie.endTick <= copie.startTick) copie.endTick = copie.startTick + 1;
                        copie.id = 0;
                        notes.push_back(copie);
                    }
                poserControleurs(piste.controlChanges, fenetres, debut, fin, creneau.sortie,
                                 [](const CcPoint& p) { return std::make_tuple(p.channel, p.controller); }, true, ccs);
                poserControleurs(piste.pitchBends, fenetres, debut, fin, creneau.sortie,
                                 [](const PitchBendPoint& p) { return p.channel; }, true, plis);
                poserControleurs(piste.polyAftertouch, fenetres, debut, fin, creneau.sortie,
                                 [](const PolyAftertouchPoint& p) { return std::make_tuple(p.channel, p.note); }, false,
                                 pressionsPoly);
                poserControleurs(piste.channelPressure, fenetres, debut, fin, creneau.sortie,
                                 [](const ChannelPressurePoint& p) { return p.channel; }, true, pressions);
                poserControleurs(piste.programChanges, fenetres, debut, fin, creneau.sortie,
                                 [](const ProgramChangePoint& p) { return p.channel; }, true, programmes);
                // LES CLIPS : la part de chacun qui joue dans la section, en fenêtre identité.
                for (const auto& clip : piste.clips) {
                    const Tick clipFin = clip.startTick + clipPlayedLength(clip, finMateriau);
                    if (clipFin <= debut || clip.startTick >= fin) continue;
                    Clip copie = clip;
                    copie.id = 0;
                    copie.startTick = std::max(clip.startTick, debut) + delta;
                    copie.length = std::min(clipFin, fin) - std::max(clip.startTick, debut);
                    copie.sourceStart = copie.startTick;
                    copie.sourceLength = copie.length;
                    clips.push_back(copie);
                }
            }

            for (const auto& clip : piste.clips) {
                if (parLesFenetres) break;
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

        if (!parLesFenetres) {
            // Une piste AUDIO n'a pas de notes à lire par des fenêtres ; ses notes (s'il y en a)
            // gardent la règle d'avant, et ses contrôleurs restent où ils sont.
            for (const auto& creneau : creneaux) {
                const Tick delta = creneau.sortie - creneau.section.startTick;
                for (const auto& note : piste.notes) {
                    if (note.startTick < creneau.section.startTick || note.startTick >= creneau.section.endTick) continue;
                    Note copie = note;
                    copie.startTick += delta;
                    copie.endTick = std::min(note.endTick, creneau.section.endTick) + delta;
                    if (copie.endTick <= copie.startTick) copie.endTick = copie.startTick + 1;
                    copie.id = 0;
                    notes.push_back(copie);
                }
            }
        } else {
            trierParTick(ccs);
            trierParTick(plis);
            trierParTick(pressionsPoly);
            trierParTick(pressions);
            trierParTick(programmes);
            piste.controlChanges = std::move(ccs);
            piste.pitchBends = std::move(plis);
            piste.polyAftertouch = std::move(pressionsPoly);
            piste.channelPressure = std::move(pressions);
            piste.programChanges = std::move(programmes);
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

    // D532.3 : LA LIGNE D'ACCORDS SUIT, créneau par créneau, ouverte par l'accord en vigueur
    // à l'entrée de la section — la règle des courbes ci-dessus, pour la même raison : un
    // créneau hériterait sinon de l'harmonie du créneau précédent.
    if (!project.chords.empty()) {
        std::vector<ChordEvent> accords;
        for (const auto& creneau : creneaux) {
            const Tick delta = creneau.sortie - creneau.section.startTick;
            if (const ChordEvent* entree = chordAt(project.chords, creneau.section.startTick)) {
                ChordEvent ouverture = *entree;
                ouverture.tick = creneau.sortie;
                setChordAt(accords, ouverture);
            }
            for (const auto& accord : project.chords) {
                if (accord.tick <= creneau.section.startTick || accord.tick >= creneau.section.endTick) continue;
                ChordEvent copie = accord;
                copie.tick += delta;
                setChordAt(accords, copie);
            }
        }
        project.chords = std::move(accords);
    }

    // DES IDENTIFIANTS NEUFS : une section jouée deux fois a produit deux fois
    // les mêmes notes, et deux notes de même identifiant rendraient la
    // sélection et l'automation liée indéchiffrables.
    for (auto& piste : project.tracks)
        for (auto& note : piste.notes) note.id = project.nextNoteId();
    project.assignClipIds();
    return true;
}

} // namespace vsm::sequencer
