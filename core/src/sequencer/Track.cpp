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

void selectTake(Track& track, int index) {
    if (index < 0 || index >= static_cast<int>(track.takes.size())) return;
    if (index == track.activeTake) return;
    rangerLeMateriauCourant(track);
    track.activeTake = index;
    sortirLeMateriau(track, track.takes[static_cast<size_t>(index)]);
}

} // namespace vsm::sequencer
