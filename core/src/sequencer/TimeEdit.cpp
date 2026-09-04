#include "vsm/sequencer/TimeEdit.h"
#include "vsm/sequencer/ClipEdit.h"
#include <algorithm>

namespace vsm::sequencer {

namespace {

using midi::Tick;

/// Décale d'`delta` les ticks `>= at` d'une liste de points ; à la
/// suppression (`delta < 0`), retire ceux qui étaient dans [at, at - delta).
template <class T>
size_t glisser(std::vector<T>& points, Tick at, Tick delta) {
    size_t touches = 0;
    if (delta < 0) {
        const Tick fin = at - delta;
        const auto avant = points.size();
        points.erase(std::remove_if(points.begin(), points.end(),
                                    [&](const T& p) { return p.tick >= at && p.tick < fin; }),
                     points.end());
        touches += avant - points.size();
        for (auto& p : points) if (p.tick >= fin) { p.tick += delta; ++touches; }
    } else {
        for (auto& p : points) if (p.tick >= at) { p.tick += delta; ++touches; }
    }
    return touches;
}

/// Les notes : coupées à l'insertion, raccourcies à la suppression.
size_t glisserNotes(std::vector<Note>& notes, Tick at, Tick delta, uint64_t& idCounter) {
    size_t touches = 0;
    if (delta > 0) {
        std::vector<Note> coupees;
        for (auto& n : notes) {
            if (n.startTick >= at) { n.startTick += delta; n.endTick += delta; ++touches; }
            else if (n.endTick > at) {
                // À CHEVAL : la queue glisse, la tête reste.
                Note queue = n;
                queue.id = idCounter++;
                queue.startTick = at + delta;
                queue.endTick = n.endTick + delta;
                n.endTick = at;
                coupees.push_back(queue);
                ++touches;
            }
        }
        for (auto& n : coupees) notes.push_back(n);
    } else {
        const Tick fin = at - delta;
        const auto avant = notes.size();
        notes.erase(std::remove_if(notes.begin(), notes.end(),
                                   [&](const Note& n) { return n.startTick >= at && n.endTick <= fin; }),
                    notes.end());
        touches += avant - notes.size();
        for (auto& n : notes) {
            if (n.endTick <= at) continue;
            if (n.startTick >= fin) { n.startTick += delta; n.endTick += delta; ++touches; continue; }
            // À CHEVAL : ce qu'elle avait dedans disparaît, les bords se rejoignent.
            n.startTick = std::min(n.startTick, at);
            n.endTick = n.endTick >= fin ? n.endTick + delta : at;
            if (n.endTick <= n.startTick) n.endTick = n.startTick + 1;
            ++touches;
        }
    }
    return touches;
}

/// Les clips : coupés aux bornes par `splitClips`, puis glissés.
size_t glisserClips(std::vector<Clip>& clips, Tick at, Tick delta, Tick materialEnd,
                    uint64_t& idCounter, const std::function<double(Tick)>& ticksToSeconds) {
    size_t touches = 0;
    ClipSelection tous;
    for (const auto& c : clips) tous.insert(c.id);
    if (delta > 0) {
        touches += splitClips(clips, tous, at, materialEnd, idCounter, ticksToSeconds);
        for (auto& c : clips) if (c.startTick >= at) { c.startTick += delta; ++touches; }
    } else {
        const Tick fin = at - delta;
        touches += splitClips(clips, tous, at, materialEnd, idCounter, ticksToSeconds);
        tous.clear();
        for (const auto& c : clips) tous.insert(c.id);
        touches += splitClips(clips, tous, fin, materialEnd, idCounter, ticksToSeconds);
        const auto avant = clips.size();
        clips.erase(std::remove_if(clips.begin(), clips.end(), [&](const Clip& c) {
                        const Tick finClip = c.startTick + clipPlayedLength(c, materialEnd);
                        return c.startTick >= at && finClip <= fin;
                    }), clips.end());
        touches += avant - clips.size();
        for (auto& c : clips) if (c.startTick >= fin) { c.startTick += delta; ++touches; }
    }
    return touches;
}

Tick finDuMateriau(const Track& track, const std::function<double(Tick)>&) {
    Tick fin = 0;
    for (const auto& note : track.notes) fin = std::max(fin, note.endTick);
    for (const auto& clip : track.clips) fin = std::max(fin, clip.startTick + clip.length);
    return fin;
}

size_t appliquer(Project& project, Tick at, Tick delta,
                 const std::function<double(Tick)>& ticksToSeconds) {
    if (delta == 0 || at < 0) return 0;
    size_t touches = 0;
    uint64_t idNotes = 1;
    for (const auto& t : project.tracks) for (const auto& n : t.notes) idNotes = std::max(idNotes, n.id + 1);
    uint64_t idClips = project.peekNextClipId();

    for (auto& track : project.tracks) {
        // LE MATÉRIAU N'EST PAS LA FENÊTRE : les notes glissent, et un clip
        // MIDI qui les regarde par une fenêtre en ticks de matériau les suit,
        // puisque sa propre fenêtre (`sourceStart`) glisse du même pas.
        const Tick materiau = std::max<Tick>(finDuMateriau(track, ticksToSeconds), at - std::min<Tick>(0, delta)) + 1;
        touches += glisserNotes(track.notes, at, delta, idNotes);
        touches += glisser(track.controlChanges, at, delta);
        touches += glisser(track.pitchBends, at, delta);
        touches += glisser(track.polyAftertouch, at, delta);
        touches += glisser(track.channelPressure, at, delta);
        touches += glisser(track.programChanges, at, delta);
        touches += glisser(track.miscEvents, at, delta);
        for (auto& curve : track.automation) touches += glisser(curve.points, at, delta);
        // Les clips MIDI regardent le matériau en ticks : leur fenêtre glisse
        // avec lui ; les clips audio ont leur fenêtre en secondes, et
        // `splitClips` la coupe correctement.
        for (auto& c : track.clips)
            if (track.kind != Track::Kind::Audio && c.sourceStart >= at) c.sourceStart += delta;
        touches += glisserClips(track.clips, at, delta, materiau, idClips, ticksToSeconds);
        for (auto& take : track.takes) {
            touches += glisserNotes(take.notes, at, delta, idNotes);
            for (auto& c : take.clips)
                if (track.kind != Track::Kind::Audio && c.sourceStart >= at) c.sourceStart += delta;
            touches += glisserClips(take.clips, at, delta, materiau, idClips, ticksToSeconds);
            if (take.startTick >= at) take.startTick += delta;
            if (take.endTick >= at) take.endTick = std::max(take.startTick, take.endTick + delta);
        }
    }
    project.ensureClipIdAbove(idClips - 1);
    touches += glisser(project.markers, at, delta);

    // LE TEMPO ET LES MESURES : l'entrée au tick 0 ne bouge jamais.
    {
        auto changes = project.tempoMap.changes();
        std::vector<TempoChange> gardes;
        for (const auto& c : changes) {
            if (c.tick == 0) { gardes.push_back(c); continue; }
            if (delta < 0 && c.tick >= at && c.tick < at - delta) { ++touches; continue; }
            TempoChange d = c;
            if (d.tick >= at) { d.tick += delta; ++touches; }
            gardes.push_back(d);
        }
        project.tempoMap.clearTempoChanges();
        for (const auto& c : gardes) project.tempoMap.addTempoChange(c.tick, c.microsecondsPerQuarterNote);
    }
    {
        auto changes = project.timeSignatureMap.changes();
        std::vector<TimeSignatureChange> gardes;
        for (const auto& c : changes) {
            if (c.tick == 0) { gardes.push_back(c); continue; }
            if (delta < 0 && c.tick >= at && c.tick < at - delta) { ++touches; continue; }
            TimeSignatureChange d = c;
            if (d.tick >= at) { d.tick += delta; ++touches; }
            gardes.push_back(d);
        }
        project.timeSignatureMap.clear();
        for (const auto& c : gardes) project.timeSignatureMap.addChange(c.tick, c.numerator, c.denominatorPow2);
    }
    // Les régions : leurs bornes glissent comme des points ; une borne dans
    // une plage supprimée se rabat sur son début.
    auto borne = [&](Tick& t) {
        if (delta < 0 && t >= at && t < at - delta) t = at;
        else if (t >= at) t += delta;
    };
    borne(project.loopStartTick); borne(project.loopEndTick);
    borne(project.punchStartTick); borne(project.punchEndTick);
    return touches;
}

} // namespace

size_t insertTime(Project& project, Tick atTick, Tick deltaTicks,
                  const std::function<double(Tick)>& ticksToSeconds) {
    if (deltaTicks <= 0) return 0;
    return appliquer(project, atTick, deltaTicks, ticksToSeconds);
}

size_t deleteTime(Project& project, Tick fromTick, Tick toTick,
                  const std::function<double(Tick)>& ticksToSeconds) {
    if (toTick <= fromTick) return 0;
    return appliquer(project, fromTick, fromTick - toTick, ticksToSeconds);
}

} // namespace vsm::sequencer
