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

} // namespace vsm::sequencer
