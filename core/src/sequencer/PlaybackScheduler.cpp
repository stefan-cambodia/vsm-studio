#include "vsm/sequencer/PlaybackScheduler.h"
#include <algorithm>

namespace vsm::sequencer {

using namespace vsm::midi;

std::vector<ScheduledEvent> PlaybackScheduler::build(const Project& project,
                                                       Tick startTick, Tick endTick) {
    std::vector<ScheduledEvent> result;

    bool anySolo = std::any_of(project.tracks.begin(), project.tracks.end(),
                                [](const Track& t) { return t.solo; });

    for (size_t trackIndex = 0; trackIndex < project.tracks.size(); ++trackIndex) {
        const Track& track = project.tracks[trackIndex];
        bool audible = anySolo ? track.solo : !track.muted;
        if (!audible) continue;

        auto inRange = [&](Tick t) { return t >= startTick && t < endTick; };

        for (const auto& note : track.notes) {
            if (note.muted) continue; // note rendue muette dans l'éditeur (Note::muted)
            if (inRange(note.startTick))
                result.push_back({project.ticksToSeconds(note.startTick), trackIndex,
                                   NoteOnEvent{note.channel, note.number, note.velocity}});
            if (inRange(note.endTick))
                result.push_back({project.ticksToSeconds(note.endTick), trackIndex,
                                   NoteOffEvent{note.channel, note.number, note.releaseVelocity}});
        }
        for (const auto& cc : track.controlChanges)
            if (inRange(cc.tick))
                result.push_back({project.ticksToSeconds(cc.tick), trackIndex,
                                   ControlChangeEvent{cc.channel, cc.controller, cc.value}});
        for (const auto& pb : track.pitchBends)
            if (inRange(pb.tick))
                result.push_back({project.ticksToSeconds(pb.tick), trackIndex,
                                   PitchBendEvent{pb.channel, pb.value}});
        for (const auto& pa : track.polyAftertouch)
            if (inRange(pa.tick))
                result.push_back({project.ticksToSeconds(pa.tick), trackIndex,
                                   PolyPressureEvent{pa.channel, pa.note, pa.pressure}});
        for (const auto& cp : track.channelPressure)
            if (inRange(cp.tick))
                result.push_back({project.ticksToSeconds(cp.tick), trackIndex,
                                   ChannelPressureEvent{cp.channel, cp.pressure}});
        for (const auto& pc : track.programChanges)
            if (inRange(pc.tick))
                result.push_back({project.ticksToSeconds(pc.tick), trackIndex,
                                   ProgramChangeEvent{pc.channel, pc.program}});
    }

    std::stable_sort(result.begin(), result.end(),
                      [](const ScheduledEvent& a, const ScheduledEvent& b) {
                          return a.timeSeconds < b.timeSeconds;
                      });
    return result;
}

} // namespace vsm::sequencer
