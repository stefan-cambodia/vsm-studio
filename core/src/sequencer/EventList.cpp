#include "vsm/sequencer/EventList.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <set>

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
        // D392 : UN NOM PAR TYPE, le même dans toutes les fenêtres (liste,
        // statistiques, onglet MIDI CC) -- il y en avait trois.
        case EventKind::PitchBend:       return "Pitch bend";
        case EventKind::PolyPressure:    return "Aftertouch poly";
        case EventKind::ChannelPressure: return "Aftertouch";
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

    // D353 : LE CANAL EST COMMUN AUX SIX FAMILLES, comme la position — et comme
    // elle, il se refuse plutôt que de se borner (0-15, rien d'autre).
    const bool canalValide = value >= 0 && value <= 15;

    if (row.kind == EventKind::Note) {
        auto it = std::find_if(track.notes.begin(), track.notes.end(),
                                [&row](const Note& n) { return n.id == row.noteId; });
        if (it == track.notes.end()) return false;
        if (field == EventField::Channel) {
            if (!canalValide) return false;
            it->channel = static_cast<uint8_t>(value);
            return true;
        }
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
            if (field == EventField::Channel) {
                if (!canalValide) return false;
                e->channel = static_cast<uint8_t>(value);
                return true;
            }
            if (!dansSeptBits(value)) return false;
            if (field == EventField::Number) e->controller = static_cast<uint8_t>(value);
            else e->value = static_cast<uint8_t>(value);
            return true;
        }
        case EventKind::PitchBend: {
            auto* e = siCest(track.pitchBends, row.indexInKind, row.tick);
            if (e == nullptr) return false;
            if (field == EventField::Position) { e->tick = position; return true; }
            if (field == EventField::Channel) {
                if (!canalValide) return false;
                e->channel = static_cast<uint8_t>(value);
                return true;
            }
            if (field == EventField::Number) return false;   // un pli n'a pas de numéro
            if (value < -8192 || value > 8191) return false;
            e->value = static_cast<int16_t>(value);
            return true;
        }
        case EventKind::PolyPressure: {
            auto* e = siCest(track.polyAftertouch, row.indexInKind, row.tick);
            if (e == nullptr) return false;
            if (field == EventField::Position) { e->tick = position; return true; }
            if (field == EventField::Channel) {
                if (!canalValide) return false;
                e->channel = static_cast<uint8_t>(value);
                return true;
            }
            if (!dansSeptBits(value)) return false;
            if (field == EventField::Number) e->note = static_cast<uint8_t>(value);
            else e->pressure = static_cast<uint8_t>(value);
            return true;
        }
        case EventKind::ChannelPressure: {
            auto* e = siCest(track.channelPressure, row.indexInKind, row.tick);
            if (e == nullptr) return false;
            if (field == EventField::Position) { e->tick = position; return true; }
            if (field == EventField::Channel) {
                if (!canalValide) return false;
                e->channel = static_cast<uint8_t>(value);
                return true;
            }
            if (field == EventField::Number) return false;   // une pression de canal n'a pas de numéro
            if (!dansSeptBits(value)) return false;
            e->pressure = static_cast<uint8_t>(value);
            return true;
        }
        case EventKind::ProgramChange: {
            auto* e = siCest(track.programChanges, row.indexInKind, row.tick);
            if (e == nullptr) return false;
            if (field == EventField::Position) { e->tick = position; return true; }
            if (field == EventField::Channel) {
                if (!canalValide) return false;
                e->channel = static_cast<uint8_t>(value);
                return true;
            }
            if (field == EventField::Value) return false;    // un programme n'a que son numéro
            if (!dansSeptBits(value)) return false;
            e->program = static_cast<uint8_t>(value);
            return true;
        }
        case EventKind::Note: break;   // traité plus haut
    }
    return false;
}

bool addTrackEvent(Track& track, EventKind kind, Tick tick, int channel,
                    int first, int second, Tick length, uint64_t& idCounter) {
    if (tick < 0) return false;
    if (channel < 0 || channel > 15) return false;
    const auto canal = static_cast<uint8_t>(channel);

    switch (kind) {
        case EventKind::Note:
            // UNE NOTE DE DURÉE NULLE NE S'ENTEND PAS et ne se voit pas : la
            // créer serait créer un problème, pas un événement (même règle qu'à
            // D348 pour la modification d'une durée).
            if (length <= 0 || !dansSeptBits(first) || !dansSeptBits(second)) return false;
            track.addNote(tick, tick + length, static_cast<uint8_t>(first),
                           static_cast<uint8_t>(second), canal, idCounter);
            break;
        case EventKind::ControlChange:
            if (!dansSeptBits(first) || !dansSeptBits(second)) return false;
            track.controlChanges.push_back({tick, canal, static_cast<uint8_t>(first),
                                             static_cast<uint8_t>(second)});
            break;
        case EventKind::PitchBend:
            if (second < -8192 || second > 8191) return false;
            track.pitchBends.push_back({tick, canal, static_cast<int16_t>(second)});
            break;
        case EventKind::PolyPressure:
            if (!dansSeptBits(first) || !dansSeptBits(second)) return false;
            track.polyAftertouch.push_back({tick, canal, static_cast<uint8_t>(first),
                                             static_cast<uint8_t>(second)});
            break;
        case EventKind::ChannelPressure:
            if (!dansSeptBits(second)) return false;
            track.channelPressure.push_back({tick, canal, static_cast<uint8_t>(second)});
            break;
        case EventKind::ProgramChange:
            if (!dansSeptBits(first)) return false;
            track.programChanges.push_back({tick, canal, static_cast<uint8_t>(first)});
            break;
    }
    // LES LANES RESTENT TRIÉES PAR TICK : `listTrackEvents` et le séquenceur le
    // supposent tous deux, et un événement posé au milieu du morceau arrive
    // forcément après ceux qui sont déjà là.
    track.sortEvents();
    return true;
}

ControllerCount countControllers(const Track& track) {
    ControllerCount c;
    c.controlChanges = track.controlChanges.size();
    for (const auto& cc : track.controlChanges)
        if (cc.controller == 0 || cc.controller == 32) ++c.bankSelects;
    c.pitchBends = track.pitchBends.size();
    c.channelPressure = track.channelPressure.size();
    c.polyAftertouch = track.polyAftertouch.size();
    return c;
}

ControllerCount removeAllControllers(Track& track) {
    const ControllerCount c = countControllers(track);
    track.controlChanges.clear();
    track.pitchBends.clear();
    track.channelPressure.clear();
    track.polyAftertouch.clear();
    return c;
}

bool isContinuousController(uint8_t controller) {
    if (controller == 0 || controller == 6) return false;              // banque, entrée de données
    if (controller >= 32 && controller <= 63) return false;            // poids faibles (dont 32 et 38)
    if (controller >= 96 && controller <= 101) return false;           // incréments, NRPN, RPN
    if (controller >= 120) return false;                               // modes de canal
    return true;
}

namespace {

// D557.2 : les marques « gardé » d'une famille d'événements, flux par flux. `cle` désigne le flux, `valeur` sa valeur,
// `eclaircir` dit si l'événement appartient à un flux continu. Les vecteurs sont triés par tick (`sortEvents`) ; l'ordre
// d'origine départage deux événements au même tick, comme le séquenceur.
template <typename Point, typename Cle, typename Valeur, typename Eclaircir>
std::vector<bool> marquesGardees(const std::vector<Point>& points, Cle cle, Valeur valeur, Eclaircir eclaircir,
                                 long long tolerance, ControllerThinning& r) {
    std::vector<bool> garde(points.size(), true);
    std::map<long long, std::vector<size_t>> flux;
    for (size_t i = 0; i < points.size(); ++i) {
        if (!eclaircir(points[i])) { ++r.untouched; continue; }
        ++r.examined;
        flux[cle(points[i])].push_back(i);
    }
    if (tolerance < 0) return garde;
    for (auto& [k, indices] : flux) {
        std::stable_sort(indices.begin(), indices.end(),
                         [&points](size_t x, size_t y) { return points[x].tick < points[y].tick; });
        long long gardee = valeur(points[indices.front()]);
        for (size_t j = 1; j < indices.size(); ++j) {
            const long long v = valeur(points[indices[j]]);
            const long long ecart = v > gardee ? v - gardee : gardee - v;
            const bool dernier = j + 1 == indices.size();
            if (ecart > tolerance || (dernier && ecart > 0)) {
                gardee = v;
            } else {
                garde[indices[j]] = false;
                ++r.removed;
            }
        }
    }
    return garde;
}

template <typename Point>
void garderMarques(std::vector<Point>& points, const std::vector<bool>& garde) {
    std::vector<Point> restants;
    restants.reserve(points.size());
    for (size_t i = 0; i < points.size(); ++i)
        if (garde[i]) restants.push_back(points[i]);
    points = std::move(restants);
}

struct MarquesDeLaPiste {
    std::vector<bool> cc, pli, pression, poly;
};

MarquesDeLaPiste marquerLaPiste(const Track& track, int tolerance, ControllerThinning& r) {
    const long long tol = tolerance;
    MarquesDeLaPiste m;
    m.cc = marquesGardees(
        track.controlChanges, [](const CcPoint& p) { return p.channel * 128LL + p.controller; },
        [](const CcPoint& p) { return static_cast<long long>(p.value); },
        [](const CcPoint& p) { return isContinuousController(p.controller); }, tol, r);
    m.pli = marquesGardees(
        track.pitchBends, [](const PitchBendPoint& p) { return static_cast<long long>(p.channel); },
        [](const PitchBendPoint& p) { return static_cast<long long>(p.value); },
        [](const PitchBendPoint&) { return true; }, tol < 0 ? tol : tol * 128, r);
    m.pression = marquesGardees(
        track.channelPressure, [](const ChannelPressurePoint& p) { return static_cast<long long>(p.channel); },
        [](const ChannelPressurePoint& p) { return static_cast<long long>(p.pressure); },
        [](const ChannelPressurePoint&) { return true; }, tol, r);
    m.poly = marquesGardees(
        track.polyAftertouch, [](const PolyAftertouchPoint& p) { return p.channel * 128LL + p.note; },
        [](const PolyAftertouchPoint& p) { return static_cast<long long>(p.pressure); },
        [](const PolyAftertouchPoint&) { return true; }, tol, r);
    return m;
}

// La valeur TENUE de chaque flux, tick par tick : (flux, tick) → valeur après tous les événements de ce tick.
template <typename Point, typename Cle, typename Valeur>
void valeursTenues(const std::vector<Point>& points, Cle cle, Valeur valeur,
                   std::map<long long, std::map<Tick, double>>& out) {
    for (const auto& p : points) out[cle(p)][p.tick] = valeur(p);   // trié : le dernier d'un tick l'emporte
}

double ecartDesFlux(const std::map<long long, std::map<Tick, double>>& a,
                    const std::map<long long, std::map<Tick, double>>& b) {
    std::set<long long> cles;
    for (const auto& [k, v] : a) cles.insert(k);
    for (const auto& [k, v] : b) cles.insert(k);
    const std::map<Tick, double> vide;
    double pire = 0.0;
    for (const long long k : cles) {
        const auto ia = a.find(k);
        const auto ib = b.find(k);
        const auto& fa = ia != a.end() ? ia->second : vide;
        const auto& fb = ib != b.end() ? ib->second : vide;
        std::set<Tick> ticks;
        for (const auto& [t, v] : fa) ticks.insert(t);
        for (const auto& [t, v] : fb) ticks.insert(t);
        auto tenue = [](const std::map<Tick, double>& f, Tick t) {
            auto it = f.upper_bound(t);
            return it == f.begin() ? 0.0 : std::prev(it)->second;
        };
        for (const Tick t : ticks) pire = std::max(pire, std::fabs(tenue(fa, t) - tenue(fb, t)));
    }
    return pire;
}

} // namespace

ControllerThinning thinControllers(Track& track, int tolerance) {
    ControllerThinning r;
    const MarquesDeLaPiste m = marquerLaPiste(track, tolerance, r);
    garderMarques(track.controlChanges, m.cc);
    garderMarques(track.pitchBends, m.pli);
    garderMarques(track.channelPressure, m.pression);
    garderMarques(track.polyAftertouch, m.poly);
    return r;
}

ControllerThinning countThinnableControllers(const Track& track, int tolerance) {
    ControllerThinning r;
    marquerLaPiste(track, tolerance, r);
    return r;
}

double maxControllerDeviation(const Track& a, const Track& b) {
    // Chaque famille a ses clés à elle : une pression de canal et un pli du même canal ne sont pas le même flux.
    auto ecartFamille = [](auto const& pa, auto const& pb, auto cle, auto valeur) {
        std::map<long long, std::map<Tick, double>> ta, tb;
        valeursTenues(pa, cle, valeur, ta);
        valeursTenues(pb, cle, valeur, tb);
        return ecartDesFlux(ta, tb);
    };
    double pire = 0.0;
    pire = std::max(pire, ecartFamille(a.controlChanges, b.controlChanges,
                                       [](const CcPoint& p) { return p.channel * 128LL + p.controller; },
                                       [](const CcPoint& p) { return static_cast<double>(p.value); }));
    pire = std::max(pire, ecartFamille(a.pitchBends, b.pitchBends,
                                       [](const PitchBendPoint& p) { return static_cast<long long>(p.channel); },
                                       [](const PitchBendPoint& p) { return static_cast<double>(p.value) / 128.0; }));
    pire = std::max(pire, ecartFamille(a.channelPressure, b.channelPressure,
                                       [](const ChannelPressurePoint& p) { return static_cast<long long>(p.channel); },
                                       [](const ChannelPressurePoint& p) { return static_cast<double>(p.pressure); }));
    pire = std::max(pire, ecartFamille(a.polyAftertouch, b.polyAftertouch,
                                       [](const PolyAftertouchPoint& p) { return p.channel * 128LL + p.note; },
                                       [](const PolyAftertouchPoint& p) { return static_cast<double>(p.pressure); }));
    return pire;
}

} // namespace vsm::sequencer
