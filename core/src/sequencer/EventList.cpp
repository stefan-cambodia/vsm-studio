#include "vsm/sequencer/EventList.h"
#include <algorithm>

namespace vsm::sequencer {

std::vector<EventRow> listTrackEvents(const Track& track) {
    std::vector<EventRow> lignes;
    lignes.reserve(track.notes.size() + track.controlChanges.size() + track.pitchBends.size()
                    + track.polyAftertouch.size() + track.channelPressure.size()
                    + track.programChanges.size());

    for (size_t i = 0; i < track.notes.size(); ++i) {
        const auto& n = track.notes[i];
        lignes.push_back({n.startTick, EventKind::Note, n.channel, n.number, n.velocity,
                           n.endTick - n.startTick, n.id, i});
    }
    for (size_t i = 0; i < track.controlChanges.size(); ++i) {
        const auto& c = track.controlChanges[i];
        lignes.push_back({c.tick, EventKind::ControlChange, c.channel, c.controller, c.value, 0, 0, i});
    }
    for (size_t i = 0; i < track.pitchBends.size(); ++i) {
        const auto& p = track.pitchBends[i];
        // LE PLI EST SIGNÉ, et il est montré tel quel : -8192..8191. Le ramener
        // à 0..127 ou à un pourcentage ferait perdre ce qu'on vient lire ici.
        lignes.push_back({p.tick, EventKind::PitchBend, p.channel, 0, p.value, 0, 0, i});
    }
    for (size_t i = 0; i < track.polyAftertouch.size(); ++i) {
        const auto& a = track.polyAftertouch[i];
        lignes.push_back({a.tick, EventKind::PolyPressure, a.channel, a.note, a.pressure, 0, 0, i});
    }
    for (size_t i = 0; i < track.channelPressure.size(); ++i) {
        const auto& c = track.channelPressure[i];
        lignes.push_back({c.tick, EventKind::ChannelPressure, c.channel, 0, c.pressure, 0, 0, i});
    }
    for (size_t i = 0; i < track.programChanges.size(); ++i) {
        const auto& p = track.programChanges[i];
        lignes.push_back({p.tick, EventKind::ProgramChange, p.channel, p.program, 0, 0, 0, i});
    }

    // TRI STABLE, par tick puis par famille : deux événements au même instant
    // doivent apparaître dans le même ordre d'une ouverture à l'autre, sans
    // quoi la ligne qu'on s'apprête à supprimer aurait bougé.
    std::stable_sort(lignes.begin(), lignes.end(), [](const EventRow& a, const EventRow& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        if (a.kind != b.kind) return static_cast<int>(a.kind) < static_cast<int>(b.kind);
        return a.indexInKind < b.indexInKind;
    });
    return lignes;
}

std::string eventKindLabel(EventKind kind) {
    switch (kind) {
        case EventKind::Note:            return "Note";
        case EventKind::ControlChange:   return "CC";
        case EventKind::PitchBend:       return "Pli";
        case EventKind::PolyPressure:    return "Pression poly";
        case EventKind::ChannelPressure: return "Pression canal";
        case EventKind::ProgramChange:   return "Programme";
    }
    return "?";
}

namespace {

/// Retire l'élément de rang `rang` s'il correspond encore à `tick` -- la
/// vérification est ce qui empêche de supprimer le VOISIN quand la piste a
/// changé entre l'affichage et le clic.
template <typename V>
bool retirerSiCest(V& vecteur, size_t rang, Tick tick) {
    if (rang >= vecteur.size() || vecteur[rang].tick != tick) return false;
    vecteur.erase(vecteur.begin() + static_cast<std::ptrdiff_t>(rang));
    return true;
}

} // namespace

bool removeTrackEvent(Track& track, const EventRow& row) {
    switch (row.kind) {
        case EventKind::Note: {
            // UNE NOTE SE DÉSIGNE PAR SON IDENTIFIANT, pas par son rang : c'est
            // ce qui la rend insensible à tout ce qui a pu bouger avant elle.
            auto it = std::find_if(track.notes.begin(), track.notes.end(),
                                    [&row](const Note& n) { return n.id == row.noteId; });
            if (it == track.notes.end()) return false;
            track.notes.erase(it);
            return true;
        }
        case EventKind::ControlChange:   return retirerSiCest(track.controlChanges, row.indexInKind, row.tick);
        case EventKind::PitchBend:       return retirerSiCest(track.pitchBends, row.indexInKind, row.tick);
        case EventKind::PolyPressure:    return retirerSiCest(track.polyAftertouch, row.indexInKind, row.tick);
        case EventKind::ChannelPressure: return retirerSiCest(track.channelPressure, row.indexInKind, row.tick);
        case EventKind::ProgramChange:   return retirerSiCest(track.programChanges, row.indexInKind, row.tick);
    }
    return false;
}

} // namespace vsm::sequencer
