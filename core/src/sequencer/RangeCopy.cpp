#include "vsm/sequencer/RangeCopy.h"
#include "LectureEntendue.h"
#include "vsm/sequencer/AutomationEdit.h"
#include "vsm/sequencer/ChordTrack.h"
#include "vsm/sequencer/ClipEdit.h"
#include "vsm/sequencer/TimeEdit.h"
#include <algorithm>
#include <tuple>

namespace vsm::sequencer {

namespace {

using detail::entendreControleurs;
using detail::entendreNotes;
using detail::trierParTick;

const auto cleCc = [](const CcPoint& p) { return std::make_tuple(p.channel, p.controller); };
const auto clePli = [](const PitchBendPoint& p) { return p.channel; };
const auto clePoly = [](const PolyAftertouchPoint& p) { return std::make_tuple(p.channel, p.note); };
const auto clePression = [](const ChannelPressurePoint& p) { return p.channel; };
const auto cleProgramme = [](const ProgramChangePoint& p) { return p.channel; };

/// Un point de courbe à `tick`, s'il n'y en a pas déjà un.
void poserPoint(AutomationCurve& courbe, Tick tick, float valeur) {
    for (const auto& p : courbe.points)
        if (p.tick == tick) return;
    courbe.points.push_back({tick, valeur, false, 0.0f});
}

void trierCourbe(AutomationCurve& courbe) {
    std::stable_sort(courbe.points.begin(), courbe.points.end(),
                     [](const AutomationPoint& a, const AutomationPoint& b) { return a.tick < b.tick; });
}

} // namespace

RangeClipboard copyRange(const Project& project, Tick from, Tick to,
                         const std::function<double(Tick)>& ticksToSeconds) {
    RangeClipboard copie;
    if (to <= from || from < 0) return copie;
    copie.length = to - from;
    const Tick finMateriau = project.lastUsedTick();
    for (const auto& piste : project.tracks) {
        RangeTrackContent contenu;
        contenu.trackUid = piste.uid;
        const bool audio = piste.kind == Track::Kind::Audio;
        // CE QUE LA PISTE FAIT ENTENDRE, clips muets compris (le clip collé restera muet).
        const auto passages = clipPassages(piste, finMateriau, /*includeMuted=*/true);
        if (!audio) {
            entendreNotes(piste.notes, passages, from, to, 0, contenu.notes);
            entendreControleurs(piste.controlChanges, passages, from, to, 0, cleCc, true, contenu.controlChanges);
            entendreControleurs(piste.pitchBends, passages, from, to, 0, clePli, true, contenu.pitchBends);
            entendreControleurs(piste.polyAftertouch, passages, from, to, 0, clePoly, false, contenu.polyAftertouch);
            entendreControleurs(piste.channelPressure, passages, from, to, 0, clePression, true, contenu.channelPressure);
            entendreControleurs(piste.programChanges, passages, from, to, 0, cleProgramme, true, contenu.programChanges);
        }
        // LES MORCEAUX DE CLIPS, coupés aux deux bornes par `splitClips` — qui sait couper une
        // fenêtre audio en secondes, étirement compris. Une copie de travail : la piste n'est pas
        // touchée.
        if (!piste.clips.empty()) {
            std::vector<Clip> travail = piste.clips;
            uint64_t ids = 1;
            for (const auto& c : travail) ids = std::max(ids, c.id + 1);
            for (const Tick borne : {from, to}) {
                ClipSelection tous;
                for (const auto& c : travail) tous.insert(c.id);
                splitClips(travail, tous, borne, finMateriau, ids, ticksToSeconds);
            }
            for (auto c : travail) {
                if (c.startTick < from || c.startTick >= to) continue;
                c.startTick -= from;
                c.id = 0;
                contenu.clips.push_back(c);
            }
        }
        // LES COURBES, chacune ouverte par sa valeur au début de la plage.
        for (const auto& courbe : piste.automation) {
            AutomationCurve morceau;
            morceau.parameter = courbe.parameter;
            if (!courbe.points.empty()) {
                for (const auto& p : courbe.points)
                    if (p.tick >= from && p.tick < to) {
                        AutomationPoint q = p;
                        q.tick -= from;
                        morceau.points.push_back(q);
                    }
                poserPoint(morceau, 0, automationValueAt(courbe, from));
                poserPoint(morceau, copie.length - 1, automationValueAt(courbe, to - 1));
                trierCourbe(morceau);
            }
            contenu.automation.push_back(std::move(morceau));
        }
        copie.tracks.push_back(std::move(contenu));
    }
    // LA LIGNE D'ACCORDS de la plage, ouverte par l'accord en vigueur à son début.
    if (const ChordEvent* entree = chordAt(project.chords, from)) {
        ChordEvent ouverture = *entree;
        ouverture.tick = 0;
        setChordAt(copie.chords, ouverture);
    }
    for (const auto& accord : project.chords)
        if (accord.tick > from && accord.tick < to) {
            ChordEvent c = accord;
            c.tick -= from;
            setChordAt(copie.chords, c);
        }
    return copie;
}

RangePasteReport pasteRangeInserting(Project& project, const RangeClipboard& clipboard, Tick at,
                                     const std::function<double(Tick)>& ticksToSeconds) {
    RangePasteReport bilan;
    if (clipboard.empty() || at < 0) return bilan;
    const Tick longueur = clipboard.length;

    // CE QUI EST AUTOUR DU TROU, relevé AVANT de l'ouvrir : la valeur de chaque courbe juste avant
    // la tête et à la tête, et l'accord en vigueur à la tête — pour que ce qui précède et ce qui
    // suit le collage s'entende comme avant.
    std::vector<std::vector<std::pair<float, float>>> autour(project.tracks.size());
    for (size_t t = 0; t < project.tracks.size(); ++t)
        for (const auto& courbe : project.tracks[t].automation)
            autour[t].push_back(courbe.points.empty()
                                    ? std::pair<float, float>{0.0f, 0.0f}
                                    : std::pair<float, float>{automationValueAt(courbe, std::max<Tick>(0, at - 1)),
                                                              automationValueAt(courbe, at)});
    const ChordEvent* enVigueur = chordAt(project.chords, at);
    const bool accordApres = enVigueur != nullptr;
    const ChordEvent reprise = accordApres ? *enVigueur : ChordEvent{};

    insertTime(project, at, longueur, ticksToSeconds);

    for (const auto& contenu : clipboard.tracks) {
        auto piste = std::find_if(project.tracks.begin(), project.tracks.end(),
                                  [&](const Track& t) { return contenu.trackUid != 0 && t.uid == contenu.trackUid; });
        if (piste == project.tracks.end()) {
            ++bilan.tracksMissing;
            continue;
        }
        ++bilan.tracksPasted;
        const size_t index = static_cast<size_t>(piste - project.tracks.begin());
        const bool audio = piste->kind == Track::Kind::Audio;
        // LES NOTES, posées dans le matériau à leur place sur la ligne de temps : `insertTime` vient
        // d'y ouvrir le trou, et un clip MIDI collé est une fenêtre IDENTITÉ (D537).
        for (auto note : contenu.notes) {
            note.startTick += at;
            note.endTick += at;
            note.id = project.nextNoteId();
            piste->notes.push_back(note);
            ++bilan.notes;
        }
        std::stable_sort(piste->notes.begin(), piste->notes.end(),
                         [](const Note& a, const Note& b) { return a.startTick < b.startTick; });
        const auto decaler = [at](auto liste, auto& vers) {
            for (auto p : liste) {
                p.tick += at;
                vers.push_back(p);
            }
            trierParTick(vers);
        };
        decaler(contenu.controlChanges, piste->controlChanges);
        decaler(contenu.pitchBends, piste->pitchBends);
        decaler(contenu.polyAftertouch, piste->polyAftertouch);
        decaler(contenu.channelPressure, piste->channelPressure);
        decaler(contenu.programChanges, piste->programChanges);
        // LES CLIPS : seulement sur une piste qui en a. Une piste SANS clip joue tout son matériau
        // à sa place ; lui en donner un la ferait taire partout ailleurs.
        if (!piste->clips.empty()) {
            std::vector<Clip> poses = contenu.clips;
            // Copiée d'une piste sans clip, collée sur une piste qui en a maintenant : un clip
            // identité sur toute la plage, sans quoi les notes collées ne s'entendraient pas.
            if (poses.empty() && !contenu.notes.empty()) {
                Clip c;
                c.startTick = 0;
                c.length = longueur;
                poses.push_back(c);
            }
            for (auto c : poses) {
                c.startTick += at;
                if (!audio) {
                    c.sourceStart = c.startTick;
                    c.sourceLength = c.length;
                }
                c.id = 0;
                piste->clips.push_back(c);
                ++bilan.clips;
            }
            std::stable_sort(piste->clips.begin(), piste->clips.end(),
                             [](const Clip& a, const Clip& b) { return a.startTick < b.startTick; });
        }
        // LES COURBES, par paramètre ; et les deux raccords : la valeur d'avant la tête juste avant
        // le trou, celle de la tête juste après.
        for (const auto& morceau : contenu.automation) {
            for (size_t c = 0; c < piste->automation.size(); ++c) {
                AutomationCurve& courbe = piste->automation[c];
                if (courbe.parameter != morceau.parameter) continue;
                if (morceau.points.empty()) break;
                const bool avaitDesPoints = !courbe.points.empty();
                for (auto p : morceau.points) {
                    p.tick += at;
                    courbe.points.erase(std::remove_if(courbe.points.begin(), courbe.points.end(),
                                                       [&](const AutomationPoint& q) { return q.tick == p.tick; }),
                                        courbe.points.end());
                    courbe.points.push_back(p);
                }
                if (avaitDesPoints && c < autour[index].size()) {
                    if (at > 0) poserPoint(courbe, at - 1, autour[index][c].first);
                    poserPoint(courbe, at + longueur, autour[index][c].second);
                }
                trierCourbe(courbe);
                break;
            }
        }
    }
    // LA LIGNE D'ACCORDS : la plage, puis l'accord qui était en vigueur à la tête reprend après.
    for (auto accord : clipboard.chords) {
        accord.tick += at;
        setChordAt(project.chords, accord);
    }
    if (accordApres && chordAt(project.chords, at + longueur) != nullptr
        && chordAt(project.chords, at + longueur)->tick < at + longueur) {
        ChordEvent r = reprise;
        r.tick = at + longueur;
        setChordAt(project.chords, r);
    }
    project.assignClipIds();
    return bilan;
}

} // namespace vsm::sequencer
