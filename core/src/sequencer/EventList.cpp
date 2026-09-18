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

namespace {

/// Le domaine d'un champ MIDI sur 7 bits, et celui du pli sur 14 bits signés.
bool dansSeptBits(long long v) { return v >= 0 && v <= 127; }

/// Retrouve l'événement d'une famille sans identifiant : par son rang, et
/// SEULEMENT s'il porte encore le tick affiché (la garde de `retirerSiCest`).
template <typename V>
typename V::value_type* siCest(V& vecteur, size_t rang, Tick tick) {
    if (rang >= vecteur.size() || vecteur[rang].tick != tick) return nullptr;
    return &vecteur[rang];
}

} // namespace

bool setTrackEventField(Track& track, const EventRow& row, EventField field, long long value) {
    // LA POSITION EST LE SEUL CHAMP COMMUN À TOUTES LES FAMILLES, et le seul
    // qu'on ramène (à 0) au lieu de refuser : un tick négatif n'existe pas.
    const Tick position = static_cast<Tick>(std::max<long long>(0, value));

    if (row.kind == EventKind::Note) {
        auto it = std::find_if(track.notes.begin(), track.notes.end(),
                                [&row](const Note& n) { return n.id == row.noteId; });
        if (it == track.notes.end()) return false;
        switch (field) {
            case EventField::Position: {
                // LA DURÉE SUIT LA NOTE : déplacer une note ne la raccourcit pas.
                const Tick duree = it->endTick - it->startTick;
                it->startTick = position;
                it->endTick = position + duree;
                return true;
            }
            case EventField::Number:
                if (!dansSeptBits(value)) return false;
                it->number = static_cast<uint8_t>(value);
                return true;
            case EventField::Value:
                if (!dansSeptBits(value)) return false;
                it->velocity = static_cast<uint8_t>(value);
                return true;
            case EventField::Length:
                // UNE NOTE DE DURÉE NULLE NE S'ENTEND PAS et ne se voit plus :
                // le refus est plus honnête qu'une note qu'on croit avoir gardée.
                if (value <= 0) return false;
                it->endTick = it->startTick + static_cast<Tick>(value);
                return true;
        }
        return false;
    }

    // Les autres familles n'ont ni identifiant ni durée.
    if (field == EventField::Length) return false;

    switch (row.kind) {
        case EventKind::ControlChange: {
            auto* e = siCest(track.controlChanges, row.indexInKind, row.tick);
            if (e == nullptr) return false;
            if (field == EventField::Position) { e->tick = position; return true; }
            if (!dansSeptBits(value)) return false;
            if (field == EventField::Number) e->controller = static_cast<uint8_t>(value);
            else e->value = static_cast<uint8_t>(value);
            return true;
        }
        case EventKind::PitchBend: {
            auto* e = siCest(track.pitchBends, row.indexInKind, row.tick);
            if (e == nullptr) return false;
            if (field == EventField::Position) { e->tick = position; return true; }
            if (field == EventField::Number) return false;   // un pli n'a pas de numéro
            if (value < -8192 || value > 8191) return false;
            e->value = static_cast<int16_t>(value);
            return true;
        }
        case EventKind::PolyPressure: {
            auto* e = siCest(track.polyAftertouch, row.indexInKind, row.tick);
            if (e == nullptr) return false;
            if (field == EventField::Position) { e->tick = position; return true; }
            if (!dansSeptBits(value)) return false;
            if (field == EventField::Number) e->note = static_cast<uint8_t>(value);
            else e->pressure = static_cast<uint8_t>(value);
            return true;
        }
        case EventKind::ChannelPressure: {
            auto* e = siCest(track.channelPressure, row.indexInKind, row.tick);
            if (e == nullptr) return false;
            if (field == EventField::Position) { e->tick = position; return true; }
            if (field == EventField::Number) return false;   // une pression de canal n'a pas de numéro
            if (!dansSeptBits(value)) return false;
            e->pressure = static_cast<uint8_t>(value);
            return true;
        }
        case EventKind::ProgramChange: {
            auto* e = siCest(track.programChanges, row.indexInKind, row.tick);
            if (e == nullptr) return false;
            if (field == EventField::Position) { e->tick = position; return true; }
            if (field == EventField::Value) return false;    // un programme n'a que son numéro
            if (!dansSeptBits(value)) return false;
            e->program = static_cast<uint8_t>(value);
            return true;
        }
        case EventKind::Note: break;   // traité plus haut
    }
    return false;
}

} // namespace vsm::sequencer
