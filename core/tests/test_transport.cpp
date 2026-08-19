#include "TestFramework.h"
#include "fixtures/TestMidiFixtures.h"
#include "vsm/midi/MidiFileParser.h"
#include "vsm/sequencer/PlaybackScheduler.h"
#include "vsm/sequencer/Project.h"
#include "vsm/sequencer/RealtimeTransport.h"

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
// RealtimeTransport : test de fumée. On tolère une marge large car on ne
// veut PAS d'un test flaky dépendant du scheduling OS ; la précision fine
// du timing est déjà couverte, sans aucun aléa, par les tests ci-dessus.
// ---------------------------------------------------------------------------

namespace {
class CountingSink : public IMidiEventSink {
public:
    void onMidiEvent(size_t trackIndex, const MidiEventData&) override {
        std::lock_guard<std::mutex> lock(mutex_);
        ++count_;
        (void)trackIndex;
    }
    int count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }
private:
    mutable std::mutex mutex_;
    int count_ = 0;
};
}

VSM_TEST(realtime_transport_fires_all_events_and_autostops) {
    Project project = buildFixtureProject(); // durée ~0.75s à 120 BPM
    CountingSink sink;
    RealtimeTransport transport(sink);
    transport.loadProject(project);

    VSM_ASSERT(transport.state() == TransportState::Stopped);
    transport.play();
    VSM_ASSERT(transport.state() == TransportState::Playing);

    std::this_thread::sleep_for(std::chrono::milliseconds(2000)); // marge large, anti-flaky

    VSM_ASSERT_EQ(sink.count(), 6);
    VSM_ASSERT(transport.state() == TransportState::Stopped); // arrêt automatique en fin de lecture
}

VSM_TEST(realtime_transport_stop_resets_position_and_thread_shuts_down_cleanly) {
    Project project = buildFixtureProject();
    CountingSink sink;
    {
        RealtimeTransport transport(sink);
        transport.loadProject(project);
        transport.play();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        transport.stop();
        VSM_ASSERT(transport.state() == TransportState::Stopped);
        VSM_ASSERT_EQ(transport.currentTick(), static_cast<Tick>(0));
    } // le destructeur doit joindre le thread sans blocage ni crash
}
