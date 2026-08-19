#include "vsm/sequencer/StepPattern.h"
#include <algorithm>
#include <cmath>

namespace vsm::sequencer {

using midi::Tick;

std::vector<Note> patternToNotes(const StepPattern& pattern, uint8_t channel, uint64_t& idCounter) {
    std::vector<Note> notes;
    if (pattern.stepTicks <= 0 || pattern.stepCount <= 0) return notes;

    for (const auto& lane : pattern.lanes) {
        for (int index = 0; index < pattern.stepCount; ++index) {
            if (index >= static_cast<int>(lane.steps.size())) break;
            const StepCell& cell = lane.steps[static_cast<size_t>(index)];
            if (!cell.active) continue;

            Note note;
            note.startTick = pattern.startTick + static_cast<Tick>(index) * pattern.stepTicks;
            // Un pas en slide s'étend jusqu'AU-DELÀ du pas suivant : c'est ce
            // chevauchement que le TB-303-style interprète en glissando.
            const double gate = cell.slide ? 1.5 : kStepGateRatio;
            note.endTick = note.startTick +
                std::max<Tick>(1, static_cast<Tick>(std::llround(static_cast<double>(pattern.stepTicks) * gate)));
            note.channel = channel;
            note.number = cell.noteNumber != 0 ? cell.noteNumber : lane.noteNumber;
            note.velocity = cell.accent ? kAccentVelocity : kStepVelocity;
            note.id = ++idCounter;
            notes.push_back(note);
        }
    }
    std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
        if (a.startTick != b.startTick) return a.startTick < b.startTick;
        return a.number < b.number;
    });
    return notes;
}

StepPattern patternFromNotes(const std::vector<Note>& notes, const StepPattern& reference) {
    StepPattern pattern = reference;
    for (auto& lane : pattern.lanes) {
        lane.steps.assign(static_cast<size_t>(pattern.stepCount), StepCell{});
    }
    if (pattern.stepTicks <= 0) return pattern;

    for (const auto& note : notes) {
        if (note.startTick < pattern.startTick) continue;
        const Tick offset = note.startTick - pattern.startTick;
        if (offset >= pattern.lengthTicks()) continue;
        // Une note qui ne tombe pas sur un pas n'est pas "rapprochée" du plus
        // proche : la grille afficherait alors un motif que le morceau ne joue
        // pas. Elle est simplement laissée de côté (et reste éditable au piano
        // roll, où elle est à sa place).
        if (offset % pattern.stepTicks != 0) continue;
        const size_t index = static_cast<size_t>(offset / pattern.stepTicks);

        for (auto& lane : pattern.lanes) {
            const bool melodic = pattern.lanes.size() == 1;
            if (!melodic && note.number != lane.noteNumber) continue;
            if (index >= lane.steps.size()) continue;

            StepCell& cell = lane.steps[index];
            cell.active = true;
            cell.accent = note.velocity >= kAccentVelocity - 8;
            cell.slide = note.durationTicks() > pattern.stepTicks;
            if (melodic) cell.noteNumber = note.number;
            break;
        }
    }
    return pattern;
}

void writePatternToTrack(Track& track, const StepPattern& pattern, uint64_t& idCounter) {
    const Tick from = pattern.startTick;
    const Tick to = pattern.startTick + pattern.lengthTicks();

    // Seule la fenêtre du motif est remplacée : une grille de 16 pas ne doit
    // pas emporter le reste du morceau avec elle.
    track.notes.erase(std::remove_if(track.notes.begin(), track.notes.end(),
                                      [from, to](const Note& note) {
                                          return note.startTick >= from && note.startTick < to;
                                      }),
                       track.notes.end());

    const auto produced = patternToNotes(pattern, track.channel, idCounter);
    track.notes.insert(track.notes.end(), produced.begin(), produced.end());
    track.sortEvents();
}

StepPattern makeDrumPattern(const std::vector<std::pair<std::string, uint8_t>>& pieces,
                             int stepCount, Tick stepTicks) {
    StepPattern pattern;
    pattern.stepCount = std::max(1, stepCount);
    pattern.stepTicks = std::max<Tick>(1, stepTicks);
    for (const auto& [name, noteNumber] : pieces) {
        StepLane lane;
        lane.name = name;
        lane.noteNumber = noteNumber;
        lane.steps.assign(static_cast<size_t>(pattern.stepCount), StepCell{});
        pattern.lanes.push_back(std::move(lane));
    }
    return pattern;
}

StepPattern makeMonoPattern(uint8_t defaultNote, int stepCount, Tick stepTicks) {
    StepPattern pattern;
    pattern.stepCount = std::max(1, stepCount);
    pattern.stepTicks = std::max<Tick>(1, stepTicks);
    StepLane lane;
    lane.name = "PATTERN";
    lane.noteNumber = defaultNote;
    lane.steps.assign(static_cast<size_t>(pattern.stepCount), StepCell{});
    for (auto& step : lane.steps) step.noteNumber = defaultNote;
    pattern.lanes.push_back(std::move(lane));
    return pattern;
}

} // namespace vsm::sequencer
