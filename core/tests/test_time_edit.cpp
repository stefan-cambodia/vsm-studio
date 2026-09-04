#include "TestFramework.h"
#include "vsm/sequencer/ClipEdit.h"
#include "vsm/sequencer/TimeEdit.h"
#include <vector>

using namespace vsm::midi;
using namespace vsm::sequencer;

// D13.3 de docs/ROADMAP-daw.md — INSÉRER OU SUPPRIMER UNE PLAGE DE TEMPS sur
// tout le morceau : tout glisse ensemble, ce qui est à cheval est coupé.

namespace {
/// 480 ticks par noire, 120 BPM : un tick vaut 1/960 s.
double enSecondes(Tick tick) { return static_cast<double>(tick) / 960.0; }

Project morceau() {
    Project p;
    p.ticksPerQuarterNote = 480;
    Track midi;
    midi.name = "Basse";
    // Trois notes : avant, à cheval sur 1920, après.
    midi.notes.push_back({0, 960, 40, 100, 0, 1});
    midi.notes.push_back({1440, 2400, 43, 100, 0, 2});
    midi.notes.push_back({3840, 4800, 45, 100, 0, 3});
    midi.controlChanges.push_back({480, 0, 7, 100});
    midi.controlChanges.push_back({3000, 0, 7, 80});
    AutomationCurve c;
    c.parameter = "mix.volume";
    c.points = {{0, 0.5f, false}, {2400, 0.9f, false}, {4800, 0.2f, false}};
    midi.automation.push_back(c);
    p.tracks.push_back(midi);

    Track audio;
    audio.name = "Voix";
    audio.kind = Track::Kind::Audio;
    audio.audio.path = "samples/voix.wav";
    audio.audio.sampleRate = 48000.0;
    audio.audio.frames = 48000 * 20;
    Clip clip;
    clip.id = 1; clip.startTick = 960; clip.length = 3840; clip.sourceLength = 3840;
    clip.sourceStartSeconds = 2.0;
    audio.clips.push_back(clip);
    p.ensureClipIdAbove(1);
    p.tracks.push_back(audio);

    p.markers.push_back({960, "Couplet"});
    p.markers.push_back({2880, "Refrain"});
    p.tempoMap.addTempoChange(2880, 500000);       // 120 BPM à 2880 (le tempo « change » là)
    p.timeSignatureMap.addChange(3840, 3, 2);      // 3/4 à 3840
    p.loopStartTick = 1920; p.loopEndTick = 3840;
    return p;
}
} // namespace

VSM_TEST(inserting_time_shifts_everything_after_and_cuts_what_straddles) {
    Project p = morceau();
    const size_t touches = insertTime(p, 1920, 1920, enSecondes);
    VSM_ASSERT(touches > 0);
    const auto& notes = p.tracks[0].notes;
    // La note d'avant ne bouge pas ; celle d'après glisse ; celle à cheval
    // est coupée : sa tête s'arrête à 1920, sa queue repart à 3840.
    VSM_ASSERT_EQ(notes[0].startTick, Tick(0));
    VSM_ASSERT_EQ(notes[1].startTick, Tick(1440));
    VSM_ASSERT_EQ(notes[1].endTick, Tick(1920));
    VSM_ASSERT_EQ(notes[2].startTick, Tick(3840 + 1920));
    VSM_ASSERT_EQ(notes.size(), size_t(4));
    VSM_ASSERT_EQ(notes[3].startTick, Tick(3840));
    VSM_ASSERT_EQ(notes[3].endTick, Tick(2400 + 1920));
    VSM_ASSERT(notes[3].id != notes[1].id);
    // Les contrôleurs, l'automation, les repères, le tempo, la mesure.
    VSM_ASSERT_EQ(p.tracks[0].controlChanges[1].tick, Tick(3000 + 1920));
    VSM_ASSERT_EQ(p.tracks[0].automation[0].points[1].tick, Tick(2400 + 1920));
    VSM_ASSERT_EQ(p.markers[1].tick, Tick(2880 + 1920));
    VSM_ASSERT_EQ(p.markers[0].tick, Tick(960));
    VSM_ASSERT_EQ(p.tempoMap.changes().back().tick, Tick(2880 + 1920));
    VSM_ASSERT_EQ(p.timeSignatureMap.changes().back().tick, Tick(3840 + 1920));
    VSM_ASSERT_EQ(p.loopStartTick, Tick(3840));
    VSM_ASSERT_EQ(p.loopEndTick, Tick(3840 + 1920));
    // Le clip audio (960 -> 4800) est coupé à 1920 : sa seconde moitié
    // repart à 3840 et reprend le fichier là où la première l'a laissé.
    const auto& clips = p.tracks[1].clips;
    VSM_ASSERT_EQ(clips.size(), size_t(2));
    VSM_ASSERT_EQ(clips[0].startTick, Tick(960));
    VSM_ASSERT_EQ(clips[0].length, Tick(960));
    VSM_ASSERT_EQ(clips[1].startTick, Tick(3840));
    VSM_ASSERT_NEAR(clips[1].sourceStartSeconds, 3.0, 1e-9);
}

VSM_TEST(deleting_time_removes_what_is_inside_and_joins_the_edges) {
    Project p = morceau();
    const size_t touches = deleteTime(p, 1920, 3840, enSecondes);
    VSM_ASSERT(touches > 0);
    const auto& notes = p.tracks[0].notes;
    VSM_ASSERT_EQ(notes.size(), size_t(3));
    VSM_ASSERT_EQ(notes[0].endTick, Tick(960));
    // À cheval (1440 -> 2400) : ce qu'elle avait dans la plage disparaît.
    VSM_ASSERT_EQ(notes[1].startTick, Tick(1440));
    VSM_ASSERT_EQ(notes[1].endTick, Tick(1920));
    // Après (3840 -> 4800) : elle glisse de 1920.
    VSM_ASSERT_EQ(notes[2].startTick, Tick(1920));
    VSM_ASSERT_EQ(notes[2].endTick, Tick(2880));
    // Le contrôleur à 3000 était dedans : retiré. L'automation à 2400 aussi.
    VSM_ASSERT_EQ(p.tracks[0].controlChanges.size(), size_t(1));
    VSM_ASSERT_EQ(p.tracks[0].automation[0].points.size(), size_t(2));
    VSM_ASSERT_EQ(p.tracks[0].automation[0].points[1].tick, Tick(4800 - 1920));
    // Le repère « Refrain » (2880) était dedans : retiré. Le tempo à 2880 aussi.
    VSM_ASSERT_EQ(p.markers.size(), size_t(1));
    VSM_ASSERT_EQ(p.tempoMap.changes().size(), size_t(1));
    VSM_ASSERT_EQ(p.timeSignatureMap.changes().back().tick, Tick(1920));
    // La boucle 1920 -> 3840 était la plage : ses bornes se rejoignent.
    VSM_ASSERT_EQ(p.loopStartTick, Tick(1920));
    VSM_ASSERT_EQ(p.loopEndTick, Tick(1920));
    // Le clip audio (960 -> 4800) perd sa tranche 1920 -> 3840 : deux clips
    // bout à bout, le second reprenant le fichier à 2 + 3 = 5 s.
    const auto& clips = p.tracks[1].clips;
    VSM_ASSERT_EQ(clips.size(), size_t(2));
    VSM_ASSERT_EQ(clips[0].startTick, Tick(960));
    VSM_ASSERT_EQ(clips[0].length, Tick(960));
    VSM_ASSERT_EQ(clips[1].startTick, Tick(1920));
    VSM_ASSERT_EQ(clips[1].length, Tick(960));
    VSM_ASSERT_NEAR(clips[1].sourceStartSeconds, 5.0, 1e-9);
}

VSM_TEST(time_edits_refuse_nonsense_and_leave_tick_zero_alone) {
    Project p = morceau();
    VSM_ASSERT_EQ(insertTime(p, 1920, 0, enSecondes), size_t(0));
    VSM_ASSERT_EQ(deleteTime(p, 3840, 1920, enSecondes), size_t(0));
    deleteTime(p, 0, 960, enSecondes);
    VSM_ASSERT_EQ(p.tempoMap.changes().front().tick, Tick(0));
    VSM_ASSERT_EQ(p.timeSignatureMap.changes().front().tick, Tick(0));
}
