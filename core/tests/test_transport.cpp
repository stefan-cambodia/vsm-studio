#include "TestFramework.h"
#include "fixtures/TestMidiFixtures.h"
#include "vsm/midi/MidiFileParser.h"
#include "vsm/sequencer/PlaybackScheduler.h"
#include "vsm/sequencer/Project.h"

#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

using namespace vsm::midi;
using namespace vsm::sequencer;
using namespace vsm::test::fixtures;

namespace {
Project buildFixtureProject() {
    auto bytes = buildTestSmf(480);
    ParsedFile parsed = MidiFileParser::parse(bytes);
    return Project::fromParsedFile(parsed);
}
}

// ---------------------------------------------------------------------------
// PlaybackScheduler : fonction pure, testable sans aucune dépendance au temps
// réel ni au threading -> zéro flakiness possible ici.
// ---------------------------------------------------------------------------

VSM_TEST(scheduler_is_deterministic_across_calls) {
    Project project = buildFixtureProject();
    auto pass1 = PlaybackScheduler::build(project, 0, 100000);
    auto pass2 = PlaybackScheduler::build(project, 0, 100000);

    VSM_ASSERT_EQ(pass1.size(), pass2.size());
    for (size_t i = 0; i < pass1.size(); ++i) {
        VSM_ASSERT_NEAR(pass1[i].timeSeconds, pass2[i].timeSeconds, 1e-9);
        VSM_ASSERT_EQ(pass1[i].trackIndex, pass2[i].trackIndex);
    }
}

VSM_TEST(scheduler_produces_expected_event_count_and_order) {
    Project project = buildFixtureProject();
    auto pass = PlaybackScheduler::build(project, 0, 100000);

    // Piste Bass : 2 NoteOn + 2 NoteOff + 1 CC + 1 PitchBend = 6 événements.
    // La piste conductor ne contient aucun événement "jouable".
    VSM_ASSERT_EQ(pass.size(), static_cast<size_t>(6));

    for (size_t i = 1; i < pass.size(); ++i)
        VSM_ASSERT(pass[i].timeSeconds >= pass[i - 1].timeSeconds); // ordre chronologique
}

VSM_TEST(scheduler_respects_mute) {
    Project project = buildFixtureProject();
    project.tracks[1].muted = true;
    auto pass = PlaybackScheduler::build(project, 0, 100000);
    VSM_ASSERT_EQ(pass.size(), static_cast<size_t>(0));
}

VSM_TEST(scheduler_skips_muted_notes_but_keeps_the_others) {
    // Note::muted est un concept d'ÉDITEUR (rendre une note silencieuse sans
    // la supprimer) : la piste reste audible, seule la note marquée disparaît
    // du planning.
    Project project = buildFixtureProject();
    const size_t before = PlaybackScheduler::build(project, 0, 100000).size();
    VSM_ASSERT(before >= 2);
    VSM_ASSERT(!project.tracks[1].notes.empty());

    project.tracks[1].notes[0].muted = true;
    const size_t after = PlaybackScheduler::build(project, 0, 100000).size();
    VSM_ASSERT_EQ(after, before - 2); // le NoteOn ET le NoteOff de cette note
}

VSM_TEST(scheduler_respects_solo) {
    Project project = buildFixtureProject();
    project.tracks.push_back(project.tracks[1]); // duplique la piste Bass -> piste 2
    project.tracks[1].solo = true;                // seule la piste 1 (Bass) est soloée
    auto pass = PlaybackScheduler::build(project, 0, 100000);
    for (const auto& ev : pass)
        VSM_ASSERT_EQ(ev.trackIndex, static_cast<size_t>(1));
}

// ---------------------------------------------------------------------------
// LE TRANSPORT TEMPS RÉEL A DISPARU (D8.3), et avec lui ses deux tests de
// fumée. Il tenait sa propre position sur un thread dédié, à côté de celle du
// moteur audio ; il n'y en a plus qu'une, celle du graphe, et c'est
// `audio/tests/test_transport_unifie.cpp` qui l'éprouve -- au bon endroit,
// puisque c'est le moteur audio qui la produit.
// ---------------------------------------------------------------------------
