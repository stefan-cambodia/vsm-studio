#include "TestFramework.h"
#include "vsm/sequencer/Quantizer.h"

using namespace vsm::sequencer;

VSM_TEST(grid_resolution_to_ticks_basic) {
    uint16_t ppq = 480;
    VSM_ASSERT_EQ(gridResolutionToTicks({NoteValue::Quarter, false, false}, ppq), static_cast<Tick>(480));
    VSM_ASSERT_EQ(gridResolutionToTicks({NoteValue::Eighth, false, false}, ppq), static_cast<Tick>(240));
    VSM_ASSERT_EQ(gridResolutionToTicks({NoteValue::Sixteenth, false, false}, ppq), static_cast<Tick>(120));
    VSM_ASSERT_EQ(gridResolutionToTicks({NoteValue::HundredTwentyEighth, false, false}, ppq), static_cast<Tick>(15));
}

VSM_TEST(triplet_grid_is_two_thirds_of_straight) {
    uint16_t ppq = 480;
    Tick straightEighth = gridResolutionToTicks({NoteValue::Eighth, false, false}, ppq);
    Tick tripletEighth = gridResolutionToTicks({NoteValue::Eighth, true, false}, ppq);
    VSM_ASSERT_EQ(tripletEighth, straightEighth * 2 / 3);
}

VSM_TEST(quantize_snaps_to_nearest_grid_line_with_full_strength) {
    uint16_t ppq = 480;
    QuantizeSettings settings;
    settings.grid = {NoteValue::Sixteenth, false, false}; // 120 ticks
    settings.strength = 1.0f;

    VSM_ASSERT_EQ(quantizeTick(125, settings, ppq), static_cast<Tick>(120));
    VSM_ASSERT_EQ(quantizeTick(200, settings, ppq), static_cast<Tick>(240));
    VSM_ASSERT_EQ(quantizeTick(0, settings, ppq), static_cast<Tick>(0));
}

VSM_TEST(quantize_strength_blends_between_original_and_grid) {
    uint16_t ppq = 480;
    QuantizeSettings settings;
    settings.grid = {NoteValue::Sixteenth, false, false}; // 120 ticks
    settings.strength = 0.5f;

    // Tick 130 -> grille la plus proche 120 -> à 50% : 125
    VSM_ASSERT_EQ(quantizeTick(130, settings, ppq), static_cast<Tick>(125));
}

VSM_TEST(swing_offsets_only_odd_grid_steps) {
    uint16_t ppq = 480;
    QuantizeSettings settings;
    settings.grid = {NoteValue::Eighth, false, false}; // 240 ticks
    settings.strength = 1.0f;
    settings.swing = 1.0f; // décalage maximal

    // Tick 0 tombe sur l'index pair (0) -> pas de swing
    VSM_ASSERT_EQ(quantizeTick(10, settings, ppq), static_cast<Tick>(0));
    // Tick proche de 240 (index impair 1) -> décalé de grid/3 = 80 ticks
    VSM_ASSERT_EQ(quantizeTick(230, settings, ppq), static_cast<Tick>(320));
}

VSM_TEST(quantize_notes_preserves_duration_when_end_not_quantized) {
    std::vector<Note> notes = { Note{125, 365, 0, 60, 100, 64, 1} }; // durée 240
    QuantizeSettings settings;
    settings.grid = {NoteValue::Sixteenth, false, false};
    settings.strength = 1.0f;
    settings.quantizeNoteEnd = false;

    quantizeNotes(notes, settings, 480);
    VSM_ASSERT_EQ(notes[0].startTick, static_cast<Tick>(120));
    VSM_ASSERT_EQ(notes[0].durationTicks(), static_cast<Tick>(240)); // durée inchangée
}

VSM_TEST(humanize_is_deterministic_for_same_seed) {
    std::vector<Note> notesA = {
        Note{0, 240, 0, 60, 100, 64, 1},
        Note{240, 480, 0, 62, 90, 64, 2},
        Note{480, 720, 0, 64, 110, 64, 3},
    };
    std::vector<Note> notesB = notesA; // copie identique

    HumanizeSettings settings;
    settings.seed = 42;
    settings.timingAmountTicks = 10.0f;
    settings.velocityAmount = 8.0f;

    humanizeNotes(notesA, settings);
    humanizeNotes(notesB, settings);

    for (size_t i = 0; i < notesA.size(); ++i) {
        VSM_ASSERT_EQ(notesA[i].startTick, notesB[i].startTick);
        VSM_ASSERT_EQ(notesA[i].velocity, notesB[i].velocity);
    }
}

VSM_TEST(humanize_actually_changes_at_least_one_note) {
    std::vector<Note> notes = {
        Note{0, 240, 0, 60, 100, 64, 1},
        Note{240, 480, 0, 62, 90, 64, 2},
        Note{480, 720, 0, 64, 110, 64, 3},
    };
    std::vector<Note> original = notes;

    HumanizeSettings settings;
    settings.seed = 1234;
    settings.timingAmountTicks = 15.0f;
    settings.velocityAmount = 10.0f;

    humanizeNotes(notes, settings);

    bool anyDifference = false;
    for (size_t i = 0; i < notes.size(); ++i) {
        if (notes[i].startTick != original[i].startTick || notes[i].velocity != original[i].velocity)
            anyDifference = true;
    }
    VSM_ASSERT(anyDifference);
}
