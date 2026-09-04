#include "vsm/sequencer/ProjectImport.h"
#include <algorithm>
#include <cmath>

namespace vsm::sequencer {

namespace {
using midi::Tick;
Tick convertir(Tick t, double rapport) { return static_cast<Tick>(std::llround(static_cast<double>(t) * rapport)); }
}

ImportOutcome appendTracksFrom(Project& destination, const Project& source, Tick atTick) {
    ImportOutcome bilan;
    const double rapport = static_cast<double>(destination.ticksPerQuarterNote)
                           / static_cast<double>(std::max<uint16_t>(1, source.ticksPerQuarterNote));
    // LE PREMIER ÉVÉNEMENT de la source, pour poser l'import à `atTick`.
    Tick premier = -1;
    for (const auto& t : source.tracks) {
        for (const auto& n : t.notes) premier = premier < 0 ? n.startTick : std::min(premier, n.startTick);
        for (const auto& c : t.clips) premier = premier < 0 ? c.startTick : std::min(premier, c.startTick);
    }
    if (premier < 0) premier = 0;
    const Tick decalage = std::max<Tick>(0, atTick) - convertir(premier, rapport);
    const auto recaler = [&](Tick t) { return std::max<Tick>(0, convertir(t, rapport) + decalage); };

    uint64_t idNote = destination.peekNextNoteId();
    uint64_t idClip = destination.peekNextClipId();
    for (const auto& piste : source.tracks) {
        Track copie = piste;
        copie.notes.clear();
        for (auto n : piste.notes) {
            n.startTick = recaler(n.startTick);
            n.endTick = std::max<Tick>(n.startTick + 1, recaler(n.endTick));
            n.id = idNote++;
            copie.notes.push_back(n);
            ++bilan.notesAdded;
        }
        for (auto& c : copie.controlChanges) c.tick = recaler(c.tick);
        for (auto& c : copie.pitchBends) c.tick = recaler(c.tick);
        for (auto& c : copie.polyAftertouch) c.tick = recaler(c.tick);
        for (auto& c : copie.channelPressure) c.tick = recaler(c.tick);
        for (auto& c : copie.programChanges) c.tick = recaler(c.tick);
        for (auto& c : copie.miscEvents) c.tick = recaler(c.tick);
        for (auto& c : copie.clips) {
            c.startTick = recaler(c.startTick);
            c.sourceStart = convertir(c.sourceStart, rapport);
            c.sourceLength = convertir(c.sourceLength, rapport);
            c.length = convertir(c.length, rapport);
            c.id = idClip++;
        }
        for (auto& curve : copie.automation)
            for (auto& p : curve.points) p.tick = recaler(p.tick);
        copie.takes.clear();   // les prises d'un autre projet n'ont pas de sens ici
        destination.tracks.push_back(std::move(copie));
        ++bilan.tracksAdded;
    }
    destination.ensureNoteIdAbove(idNote - 1);
    destination.ensureClipIdAbove(idClip - 1);
    // Le tempo et les mesures de la source sont ignorés, et comptés.
    bilan.tempoChangesIgnored = source.tempoMap.changes().size() > 1 ? source.tempoMap.changes().size() - 1 : 0;
    bilan.timeSignaturesIgnored = source.timeSignatureMap.changes().size() > 1 ? source.timeSignatureMap.changes().size() - 1 : 0;
    return bilan;
}

} // namespace vsm::sequencer
