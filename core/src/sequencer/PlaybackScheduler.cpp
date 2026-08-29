#include "vsm/sequencer/PlaybackScheduler.h"
#include <algorithm>
#include <limits>

namespace vsm::sequencer {

using namespace vsm::midi;

namespace {

/// Une RÉPÉTITION d'un clip : la portion du matériau qu'elle lit, et de
/// combien elle la décale sur la ligne de temps.
///
/// Un clip non bouclé en produit une seule ; un clip plus long que sa fenêtre
/// en produit autant qu'il contient de répétitions. Aucune note n'est copiée :
/// c'est le décalage qui est répété, pas le matériau.
struct Passage {
    Tick sourceFrom = 0;                                   ///< inclus
    Tick sourceTo = std::numeric_limits<Tick>::max();      ///< exclu
    Tick shift = 0;                                        ///< à ajouter au tick source
    Tick outLimit = std::numeric_limits<Tick>::max();      ///< fin dure sur la ligne de temps
};

/// Les passages d'une piste.
///
/// UNE PISTE SANS CLIP DONNE UN PASSAGE IDENTITÉ, et c'est ce qui rend
/// l'absence de régression DÉMONTRABLE plutôt que promise : il n'y a pas un
/// « chemin historique » à côté du chemin des clips, qui pourrait diverger de
/// lui à la première correction. Il y a un seul chemin, et le cas sans découpe
/// est la fenêtre qui ne coupe rien.
std::vector<Passage> passagesOf(const Track& track, Tick materialEnd) {
    std::vector<Passage> passages;
    if (track.clips.empty()) {
        passages.push_back(Passage{});
        return passages;
    }

    for (const auto& clip : track.clips) {
        if (clip.muted) continue;
        const Tick fenetre = clip.sourceLength > 0 ? clip.sourceLength
                                                   : std::max<Tick>(0, materialEnd - clip.sourceStart);
        if (fenetre <= 0) continue;   // fenêtre vide : rien à lire, et pas de boucle infinie
        const Tick jouee = clip.length > 0 ? clip.length : fenetre;

        for (Tick depart = 0; depart < jouee; depart += fenetre) {
            Passage passage;
            passage.sourceFrom = clip.sourceStart;
            passage.sourceTo = clip.sourceStart + fenetre;
            passage.shift = clip.startTick + depart - clip.sourceStart;
            passage.outLimit = clip.startTick + jouee;
            passages.push_back(passage);
        }
    }
    return passages;
}

} // namespace

std::vector<ScheduledEvent> PlaybackScheduler::build(const Project& project,
                                                       Tick startTick, Tick endTick) {
    std::vector<ScheduledEvent> result;

    bool anySolo = std::any_of(project.tracks.begin(), project.tracks.end(),
                                [](const Track& t) { return t.solo; });
    const Tick materialEnd = project.lastUsedTick();

    for (size_t trackIndex = 0; trackIndex < project.tracks.size(); ++trackIndex) {
        const Track& track = project.tracks[trackIndex];
        bool audible = anySolo ? track.solo : !track.muted;
        if (!audible) continue;

        const std::vector<Passage> passages = passagesOf(track, materialEnd);

        for (const auto& passage : passages) {
            // Le tick source appartient-il à ce passage, et où sort-il ?
            auto lu = [&passage](Tick source, Tick& out) {
                if (source < passage.sourceFrom || source >= passage.sourceTo) return false;
                out = source + passage.shift;
                return out < passage.outLimit;
            };
            auto inRange = [&](Tick t) { return t >= startTick && t < endTick; };

            for (const auto& note : track.notes) {
                if (note.muted) continue; // note rendue muette dans l'éditeur (Note::muted)
                Tick debut = 0;
                if (!lu(note.startTick, debut)) continue;
                if (inRange(debut))
                    result.push_back({project.ticksToSeconds(debut), trackIndex,
                                       NoteOnEvent{note.channel, note.number, note.velocity}});

                // LA FIN EST COUPÉE À LA FIN DU CLIP, jamais laissée pendre :
                // une note dont le NoteOff tomberait au-delà resterait tenue
                // pour toujours. C'est la règle de tout éditeur de régions, et
                // sans clip elle ne s'applique jamais (la limite est infinie).
                const Tick fin = std::min(note.endTick + passage.shift, passage.outLimit);
                if (inRange(fin))
                    result.push_back({project.ticksToSeconds(fin), trackIndex,
                                       NoteOffEvent{note.channel, note.number, note.releaseVelocity}});
            }

            Tick t = 0;
            for (const auto& cc : track.controlChanges)
                if (lu(cc.tick, t) && inRange(t))
                    result.push_back({project.ticksToSeconds(t), trackIndex,
                                       ControlChangeEvent{cc.channel, cc.controller, cc.value}});
            for (const auto& pb : track.pitchBends)
                if (lu(pb.tick, t) && inRange(t))
                    result.push_back({project.ticksToSeconds(t), trackIndex,
                                       PitchBendEvent{pb.channel, pb.value}});
            for (const auto& pa : track.polyAftertouch)
                if (lu(pa.tick, t) && inRange(t))
                    result.push_back({project.ticksToSeconds(t), trackIndex,
                                       PolyPressureEvent{pa.channel, pa.note, pa.pressure}});
            for (const auto& cp : track.channelPressure)
                if (lu(cp.tick, t) && inRange(t))
                    result.push_back({project.ticksToSeconds(t), trackIndex,
                                       ChannelPressureEvent{cp.channel, cp.pressure}});
            for (const auto& pc : track.programChanges)
                if (lu(pc.tick, t) && inRange(t))
                    result.push_back({project.ticksToSeconds(t), trackIndex,
                                       ProgramChangeEvent{pc.channel, pc.program}});
        }
    }

    std::stable_sort(result.begin(), result.end(),
                      [](const ScheduledEvent& a, const ScheduledEvent& b) {
                          return a.timeSeconds < b.timeSeconds;
                      });
    return result;
}

} // namespace vsm::sequencer
