#include "TestFramework.h"
#include "vsm/sequencer/StepPattern.h"
#include <algorithm>

using namespace vsm::sequencer;
using vsm::midi::Tick;

namespace {
StepPattern drumGrid() {
    return makeDrumPattern({{"BASS DRUM", 36}, {"SNARE", 38}, {"CLOSED HAT", 42}});
}
} // namespace

VSM_TEST(pattern_grid_starts_empty_and_well_formed) {
    const StepPattern pattern = drumGrid();
    VSM_ASSERT_EQ(pattern.lanes.size(), size_t{3});
    VSM_ASSERT_EQ(pattern.stepCount, 16);
    VSM_ASSERT_EQ(static_cast<long long>(pattern.lengthTicks()), 1920LL); // une mesure à 480 PPQ
    for (const auto& lane : pattern.lanes) {
        VSM_ASSERT_EQ(lane.steps.size(), size_t{16});
        for (const auto& step : lane.steps) VSM_ASSERT(!step.active);
    }
}

VSM_TEST(active_steps_become_notes_at_the_right_place) {
    StepPattern pattern = drumGrid();
    pattern.lanes[0].steps[0].active = true;   // grosse caisse sur le temps 1
    pattern.lanes[0].steps[8].active = true;   // et sur le temps 3
    pattern.lanes[1].steps[4].active = true;   // caisse claire sur le 2

    uint64_t ids = 0;
    const auto notes = patternToNotes(pattern, 9, ids);
    VSM_ASSERT_EQ(notes.size(), size_t{3});
    VSM_ASSERT_EQ(static_cast<long long>(notes[0].startTick), 0LL);
    VSM_ASSERT_EQ(static_cast<int>(notes[0].number), 36);
    VSM_ASSERT_EQ(static_cast<long long>(notes[1].startTick), 480LL);  // pas 4 = une noire
    VSM_ASSERT_EQ(static_cast<int>(notes[1].number), 38);
    VSM_ASSERT_EQ(static_cast<long long>(notes[2].startTick), 960LL);
    for (const auto& note : notes) VSM_ASSERT_EQ(static_cast<int>(note.channel), 9);
}

VSM_TEST(steps_are_detached_not_held_to_the_next_one) {
    // Le détaché fait partie du son de ces machines : une note tenue jusqu'au
    // pas suivant donnerait une boîte à rythmes qui "bave".
    StepPattern pattern = drumGrid();
    pattern.lanes[0].steps[0].active = true;
    uint64_t ids = 0;
    const auto notes = patternToNotes(pattern, 9, ids);
    VSM_ASSERT(notes[0].durationTicks() < pattern.stepTicks);
    VSM_ASSERT(notes[0].durationTicks() > 0);
}

VSM_TEST(accent_uses_the_only_nuance_these_machines_have) {
    StepPattern pattern = drumGrid();
    pattern.lanes[0].steps[0].active = true;
    pattern.lanes[0].steps[4].active = true;
    pattern.lanes[0].steps[4].accent = true;

    uint64_t ids = 0;
    const auto notes = patternToNotes(pattern, 9, ids);
    VSM_ASSERT_EQ(static_cast<int>(notes[0].velocity), static_cast<int>(kStepVelocity));
    VSM_ASSERT_EQ(static_cast<int>(notes[1].velocity), static_cast<int>(kAccentVelocity));
}

VSM_TEST(slide_produces_the_overlap_the_engine_reads_as_a_glide) {
    // Le TB-303-style du projet interprète DEUX NOTES QUI SE CHEVAUCHENT comme
    // un slide (ARCHITECTURE.md § 8). Le motif ne doit donc rien inventer : il
    // produit ce chevauchement, et rien d'autre.
    StepPattern pattern = makeMonoPattern(36);
    pattern.lanes[0].steps[0].active = true;
    pattern.lanes[0].steps[0].slide = true;
    pattern.lanes[0].steps[0].noteNumber = 36;
    pattern.lanes[0].steps[1].active = true;
    pattern.lanes[0].steps[1].noteNumber = 43;

    uint64_t ids = 0;
    const auto notes = patternToNotes(pattern, 0, ids);
    VSM_ASSERT_EQ(notes.size(), size_t{2});
    VSM_ASSERT(notes[0].endTick > notes[1].startTick); // chevauchement effectif
    VSM_ASSERT_EQ(static_cast<int>(notes[1].number), 43);
}

VSM_TEST(pattern_is_read_back_from_the_notes_it_produced) {
    // Aller-retour complet : c'est ce qui garantit que la grille montre le
    // morceau, et pas une copie parallèle qui aurait divergé.
    StepPattern original = drumGrid();
    original.lanes[0].steps[0].active = true;
    original.lanes[0].steps[8].active = true;
    original.lanes[0].steps[8].accent = true;
    original.lanes[2].steps[2].active = true;

    uint64_t ids = 0;
    const auto notes = patternToNotes(original, 9, ids);
    const StepPattern reread = patternFromNotes(notes, drumGrid());

    for (size_t lane = 0; lane < original.lanes.size(); ++lane) {
        for (int step = 0; step < original.stepCount; ++step) {
            const auto& a = original.lanes[lane].steps[static_cast<size_t>(step)];
            const auto& b = reread.lanes[lane].steps[static_cast<size_t>(step)];
            VSM_ASSERT_EQ(a.active, b.active);
            VSM_ASSERT_EQ(a.accent, b.accent);
        }
    }
}

VSM_TEST(notes_drawn_off_the_grid_are_not_faked_into_steps) {
    // Une note posée entre deux pas au piano roll ne doit pas être "rapprochée"
    // du pas voisin : la grille afficherait un motif que le morceau ne joue
    // pas. Elle est ignorée par la grille, et reste intacte dans la piste.
    std::vector<Note> notes;
    Note offGrid;
    offGrid.startTick = 60; // à mi-chemin entre deux doubles croches
    offGrid.endTick = 120;
    offGrid.number = 36;
    offGrid.id = 1;
    notes.push_back(offGrid);

    const StepPattern pattern = patternFromNotes(notes, drumGrid());
    for (const auto& lane : pattern.lanes)
        for (const auto& step : lane.steps) VSM_ASSERT(!step.active);
}

VSM_TEST(writing_a_pattern_only_replaces_its_own_window) {
    // Une grille de 16 pas ne doit pas emporter le reste du morceau.
    Track track;
    track.channel = 9;
    uint64_t ids = 0;
    track.addNote(5000, 5200, 60, 100, 9, ids); // note bien après le motif

    StepPattern pattern = drumGrid();
    pattern.lanes[0].steps[0].active = true;
    writePatternToTrack(track, pattern, ids);

    VSM_ASSERT_EQ(track.notes.size(), size_t{2});
    bool distantNoteSurvived = false;
    for (const auto& note : track.notes)
        if (note.startTick == 5000) distantNoteSurvived = true;
    VSM_ASSERT(distantNoteSurvived);
}

VSM_TEST(rewriting_a_pattern_replaces_the_previous_take) {
    Track track;
    uint64_t ids = 0;
    StepPattern pattern = drumGrid();
    pattern.lanes[0].steps[0].active = true;
    writePatternToTrack(track, pattern, ids);
    VSM_ASSERT_EQ(track.notes.size(), size_t{1});

    pattern.lanes[0].steps[0].active = false;
    pattern.lanes[1].steps[4].active = true;
    writePatternToTrack(track, pattern, ids);
    VSM_ASSERT_EQ(track.notes.size(), size_t{1});
    VSM_ASSERT_EQ(static_cast<int>(track.notes[0].number), 38);
    VSM_ASSERT_EQ(static_cast<long long>(track.notes[0].startTick), 480LL);
}

VSM_TEST(pattern_can_start_anywhere_in_the_song) {
    StepPattern pattern = drumGrid();
    pattern.startTick = 3840; // troisième mesure
    pattern.lanes[0].steps[2].active = true;

    uint64_t ids = 0;
    const auto notes = patternToNotes(pattern, 9, ids);
    VSM_ASSERT_EQ(static_cast<long long>(notes[0].startTick), 3840LL + 240LL);

    const StepPattern reread = patternFromNotes(notes, pattern);
    VSM_ASSERT(reread.lanes[0].steps[2].active);
}

VSM_TEST(mono_pattern_carries_a_pitch_per_step) {
    StepPattern pattern = makeMonoPattern(36);
    pattern.lanes[0].steps[0].active = true;
    pattern.lanes[0].steps[0].noteNumber = 40;
    pattern.lanes[0].steps[3].active = true;
    pattern.lanes[0].steps[3].noteNumber = 52;

    uint64_t ids = 0;
    const auto notes = patternToNotes(pattern, 0, ids);
    VSM_ASSERT_EQ(static_cast<int>(notes[0].number), 40);
    VSM_ASSERT_EQ(static_cast<int>(notes[1].number), 52);

    const StepPattern reread = patternFromNotes(notes, makeMonoPattern(36));
    VSM_ASSERT_EQ(static_cast<int>(reread.lanes[0].steps[0].noteNumber), 40);
    VSM_ASSERT_EQ(static_cast<int>(reread.lanes[0].steps[3].noteNumber), 52);
}
