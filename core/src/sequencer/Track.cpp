#include "vsm/sequencer/Track.h"
#include <algorithm>

namespace vsm::sequencer {

void Track::sortEvents() {
    auto byTick = [](const auto& a, const auto& b) { return a.tick < b.tick; };
    std::stable_sort(notes.begin(), notes.end(),
                      [](const Note& a, const Note& b) { return a.startTick < b.startTick; });
    std::stable_sort(controlChanges.begin(), controlChanges.end(), byTick);
    std::stable_sort(pitchBends.begin(), pitchBends.end(), byTick);
    std::stable_sort(polyAftertouch.begin(), polyAftertouch.end(), byTick);
    std::stable_sort(channelPressure.begin(), channelPressure.end(), byTick);
    std::stable_sort(programChanges.begin(), programChanges.end(), byTick);
    std::stable_sort(miscEvents.begin(), miscEvents.end(),
                      [](const vsm::midi::MidiEvent& a, const vsm::midi::MidiEvent& b) { return a.tick < b.tick; });
}

Note& Track::addNote(Tick start, Tick end, uint8_t number, uint8_t velocity,
                      uint8_t channelOverride, uint64_t& idCounter) {
    Note note;
    note.startTick = start;
    note.endTick = end;
    note.number = number;
    note.velocity = velocity;
    note.channel = channelOverride;
    note.id = idCounter++;
    notes.push_back(note);
    return notes.back();
}

namespace {
/// Range le matériau courant de la piste dans la prise à laquelle il
/// appartient. Sans effet si aucune prise n'est active -- le matériau
/// n'appartient alors à personne, et l'écrire quelque part serait l'inventer.
void rangerLeMateriauCourant(Track& track) {
    if (track.activeTake < 0 || track.activeTake >= static_cast<int>(track.takes.size())) return;
    Take& prise = track.takes[static_cast<size_t>(track.activeTake)];
    prise.notes = track.notes;
    prise.audio = track.audio;
    prise.clips = track.clips;
}

void sortirLeMateriau(Track& track, const Take& prise) {
    track.notes = prise.notes;
    track.audio = prise.audio;
    track.clips = prise.clips;
    track.sortEvents();
}
} // namespace

void pushTake(Track& track, Take take, const std::string& nomDeLOrigine) {
    // CE QUI ÉTAIT LÀ AVANT LA PREMIÈRE PRISE DEVIENT LA PRISE N° 0. Sans cela,
    // le premier enregistrement empilé effacerait le matériau existant --
    // typiquement une partie reconstruite, c'est-à-dire ce qu'on avait de plus
    // précieux. On ne le fait qu'une fois, et seulement s'il y avait quelque
    // chose : empiler une prise sur une piste vide ne doit pas fabriquer une
    // prise vide qu'il faudrait ensuite expliquer.
    if (track.takes.empty() && (!track.notes.empty() || !track.audio.empty())) {
        Take origine;
        origine.name = nomDeLOrigine;
        origine.notes = track.notes;
        origine.audio = track.audio;
        origine.clips = track.clips;
        origine.startTick = 0;
        origine.endTick = 0;
        track.takes.push_back(std::move(origine));
    } else {
        rangerLeMateriauCourant(track);
    }

    track.takes.push_back(std::move(take));
    track.activeTake = static_cast<int>(track.takes.size()) - 1;
    sortirLeMateriau(track, track.takes[static_cast<size_t>(track.activeTake)]);
}

std::vector<Note> buildCompositeTake(const Track& track,
                                      const std::vector<CompSegment>& segments,
                                      uint64_t& idCounter) {
    std::vector<Note> composite;
    for (const auto& troncon : segments) {
        if (troncon.toTick <= troncon.fromTick) continue;
        if (troncon.takeIndex < 0 || troncon.takeIndex >= static_cast<int>(track.takes.size()))
            continue;
        // LA VÉRITÉ DE LA PRISE ACTIVE EST DANS LA PISTE, pas dans la prise :
        // son contenu rangé est périmé tant qu'elle est active. Lire
        // `takes[i].notes` pour celle-là rendrait l'état d'AVANT, c'est-à-dire
        // exactement pas ce qu'on vient de juger bon.
        const std::vector<Note>& source = (troncon.takeIndex == track.activeTake)
                                              ? track.notes
                                              : track.takes[static_cast<size_t>(troncon.takeIndex)].notes;
        for (const auto& note : source) {
            if (note.startTick < troncon.fromTick || note.startTick >= troncon.toTick) continue;
            Note copie = note;
            // Coupée au bord du tronçon : laissée entière, elle sonnerait
            // par-dessus le tronçon suivant, qui vient d'une AUTRE passe.
            copie.endTick = std::min(note.endTick, troncon.toTick);
            if (copie.endTick <= copie.startTick) copie.endTick = copie.startTick + 1;
            copie.id = idCounter++;
            composite.push_back(copie);
        }
    }
    std::stable_sort(composite.begin(), composite.end(),
                      [](const Note& a, const Note& b) { return a.startTick < b.startTick; });
    return composite;
}

bool applyCompositeTake(Track& track, const std::vector<CompSegment>& segments,
                         uint64_t& idCounter) {
    const auto composite = buildCompositeTake(track, segments, idCounter);
    if (composite.empty()) return false;
    // LA PASSE QU'ON ÉCOUTAIT EST RANGÉE D'ABORD : sans cela, choisir la
    // composite la perdrait, et c'est souvent l'une de celles qu'on assemble.
    rangerLeMateriauCourant(track);
    track.notes = composite;
    // UNE COMPOSITE N'APPARTIENT À AUCUNE PRISE. La dire active écraserait
    // cette prise-là au prochain changement.
    track.activeTake = -1;
    track.sortEvents();
    return true;
}

void selectTake(Track& track, int index) {
    if (index < 0 || index >= static_cast<int>(track.takes.size())) return;
    if (index == track.activeTake) return;
    rangerLeMateriauCourant(track);
    track.activeTake = index;
    sortirLeMateriau(track, track.takes[static_cast<size_t>(index)]);
}


// --- D532.2 : LES VERSIONS DE PISTE ------------------------------------------

namespace {
// L'ÉCHANGE TIENT EN DEUX FONCTIONS, ET CHAQUE CHAMP DE LA MATIÈRE Y EST NOMMÉ UNE
// FOIS : un champ oublié ici partirait avec la version suivante sans prévenir (le
// test `chaque_champ_de_la_matiere_suit_sa_version` les remplit tous et les relit).
// Le NOM de la version n'est jamais touché : seul le contenu de la version active est
// périmé, pas son nom.
void rangerLaVersion(Track& track) {
    if (track.activeVersion < 0 || track.activeVersion >= static_cast<int>(track.versions.size())) return;
    TrackVersion& v = track.versions[static_cast<size_t>(track.activeVersion)];
    v.notes = track.notes;
    v.controlChanges = track.controlChanges;
    v.pitchBends = track.pitchBends;
    v.polyAftertouch = track.polyAftertouch;
    v.channelPressure = track.channelPressure;
    v.programChanges = track.programChanges;
    v.audio = track.audio;
    v.clips = track.clips;
    v.automation = track.automation;
}

void sortirLaVersion(Track& track, const TrackVersion& v) {
    track.notes = v.notes;
    track.controlChanges = v.controlChanges;
    track.pitchBends = v.pitchBends;
    track.polyAftertouch = v.polyAftertouch;
    track.channelPressure = v.channelPressure;
    track.programChanges = v.programChanges;
    track.audio = v.audio;
    track.clips = v.clips;
    track.automation = v.automation;
    track.sortEvents();
}

// La prise active est RANGÉE dans le tiroir de la piste, qui ne change pas de
// version : la matière qui sort ne lui appartient pas.
void rangerLaPriseActive(Track& track) {
    rangerLeMateriauCourant(track);
    track.activeTake = -1;
}
} // namespace

int createVersion(Track& track, const std::string& nom, bool dupliquer, const std::string& nomDeLOrigine) {
    if (track.frozen) return -1;
    rangerLaPriseActive(track);
    if (track.versions.empty()) {
        TrackVersion origine;
        origine.name = nomDeLOrigine;
        track.versions.push_back(std::move(origine));
        track.activeVersion = 0;
    }
    rangerLaVersion(track);
    TrackVersion neuve;
    if (dupliquer) neuve = track.versions[static_cast<size_t>(track.activeVersion)];
    neuve.name = nom;
    track.versions.push_back(std::move(neuve));
    track.activeVersion = static_cast<int>(track.versions.size()) - 1;
    sortirLaVersion(track, track.versions.back());
    return track.activeVersion;
}

bool selectVersion(Track& track, int index) {
    if (index < 0 || index >= static_cast<int>(track.versions.size())) return false;
    if (index == track.activeVersion) return true;
    if (track.frozen) return false;
    rangerLaPriseActive(track);
    rangerLaVersion(track);
    track.activeVersion = index;
    sortirLaVersion(track, track.versions[static_cast<size_t>(index)]);
    return true;
}

bool removeVersion(Track& track, int index) {
    if (index < 0 || index >= static_cast<int>(track.versions.size())) return false;
    if (track.versions.size() == 1) {
        // LA DERNIÈRE : le tiroir se vide, la matière reste — c'est ce qu'on entend.
        track.versions.clear();
        track.activeVersion = -1;
        return true;
    }
    if (index == track.activeVersion) {
        const int voisine = index > 0 ? index - 1 : index + 1;
        if (!selectVersion(track, voisine)) return false;
    }
    track.versions.erase(track.versions.begin() + index);
    if (track.activeVersion > index) --track.activeVersion;
    return true;
}

TakeRemoval removeTake(Track& track, int index) {
    TakeRemoval bilan;
    if (index < 0 || index >= static_cast<int>(track.takes.size())) return bilan;

    bilan.done = true;
    bilan.name = track.takes[static_cast<size_t>(index)].name;
    bilan.wasActive = (track.activeTake == index);

    track.takes.erase(track.takes.begin() + index);

    // LE MATÉRIAU COURANT NE BOUGE PAS. S'il appartenait à la prise retirée, il
    // n'appartient plus à aucune ; sinon, la prise à laquelle il appartient a
    // peut-être reculé d'un rang.
    if (track.activeTake == index) track.activeTake = -1;
    else if (track.activeTake > index) --track.activeTake;

    std::vector<CompSegment> restants;
    restants.reserve(track.compSegments.size());
    for (auto troncon : track.compSegments) {
        if (troncon.takeIndex == index) { ++bilan.droppedSegments; continue; }
        if (troncon.takeIndex > index) { --troncon.takeIndex; ++bilan.shiftedSegments; }
        restants.push_back(troncon);
    }
    track.compSegments = std::move(restants);
    return bilan;
}

} // namespace vsm::sequencer
