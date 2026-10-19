#include "TestFramework.h"
#include "vsm/sequencer/Quantizer.h"
#include "vsm/sequencer/NoteEdit.h"

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

// H22 (D557.1) : « Quantifier début ET fin » réduisait à UN TICK une note plus courte qu'une demi-case — les deux bouts
// allaient à la même ligne, et `endTick = startTick + 1` rattrapait la durée nulle. La fin va désormais à la PREMIÈRE
// ligne après le début.
VSM_TEST(quantize_start_and_end_never_collapses_a_short_note_to_one_tick) {
    std::vector<Note> notes = { Note{100, 150, 0, 60, 100, 64, 1} };   // 50 ticks, moins d'une demi-case
    QuantizeSettings settings;
    settings.grid = {NoteValue::Sixteenth, false, false};   // 120
    settings.quantizeNoteEnd = true;
    quantizeNotes(notes, settings, 480);
    VSM_ASSERT_EQ(notes[0].startTick, static_cast<Tick>(120));
    VSM_ASSERT_EQ(notes[0].durationTicks(), static_cast<Tick>(120));
}

// D557.1 — QUANTIFIER LES FINS (*Quantize MIDI Event Ends* de Cubase) : le début ne bouge pas, la fin va à la ligne la
// plus proche ; une fin qui y tomberait sur ou avant le début va à la première ligne APRÈS lui.
VSM_TEST(quantize_ends_only_keeps_starts_and_moves_ends_to_the_grid) {
    std::vector<Note> notes = {
        Note{125, 365, 0, 60, 100, 64, 1},   // fin 365 → 360
        Note{0, 200, 0, 62, 100, 64, 2},     // fin 200 → 240
        Note{130, 170, 0, 64, 100, 64, 3},   // fin 170 → 120, avant le début : → 240, la ligne après 130
        Note{240, 290, 0, 65, 100, 64, 4},   // fin 290 → 240, SUR le début : → 360
    };
    QuantizeSettings settings;
    settings.grid = {NoteValue::Sixteenth, false, false};
    settings.quantizeNoteStart = false;
    settings.quantizeNoteEnd = true;
    quantizeNotes(notes, settings, 480);
    VSM_ASSERT_EQ(notes[0].startTick, static_cast<Tick>(125));
    VSM_ASSERT_EQ(notes[0].endTick, static_cast<Tick>(360));
    VSM_ASSERT_EQ(notes[1].startTick, static_cast<Tick>(0));
    VSM_ASSERT_EQ(notes[1].endTick, static_cast<Tick>(240));
    VSM_ASSERT_EQ(notes[2].startTick, static_cast<Tick>(130));
    VSM_ASSERT_EQ(notes[2].endTick, static_cast<Tick>(240));
    VSM_ASSERT_EQ(notes[3].startTick, static_cast<Tick>(240));
    VSM_ASSERT_EQ(notes[3].endTick, static_cast<Tick>(360));
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

// D566.1 — ANNULER ET FIGER LA QUANTIFICATION (*Reset Quantize*, *Freeze Quantize* de Cubase). La note garde le
// DÉPLACEMENT que les quantifications lui ont fait ; l'annuler retire ce déplacement, et rien de ce qu'on a fait à la
// main depuis.
VSM_TEST(quantizing_records_the_shift_and_a_second_pass_adds_nothing) {
    std::vector<Note> notes = { Note{125, 365, 0, 60, 100, 64, 1}, Note{250, 300, 0, 62, 100, 64, 2} };
    QuantizeSettings settings;
    settings.grid = {NoteValue::Sixteenth, false, false};   // 120
    quantizeNotes(notes, settings, 480);
    VSM_ASSERT_EQ(notes[0].quantizeShiftStart, static_cast<Tick>(-5));
    VSM_ASSERT_EQ(notes[0].quantizeShiftEnd, static_cast<Tick>(-5));
    VSM_ASSERT_EQ(notes[1].quantizeShiftStart, static_cast<Tick>(-10));
    quantizeNotes(notes, settings, 480);   // déjà sur la grille : rien ne bouge, rien ne s'ajoute
    VSM_ASSERT_EQ(notes[0].quantizeShiftStart, static_cast<Tick>(-5));
    VSM_ASSERT_EQ(notes[1].quantizeShiftEnd, static_cast<Tick>(-10));
}

VSM_TEST(a_partial_strength_quantize_accumulates_and_resets_to_the_played_position) {
    std::vector<Note> notes = { Note{100, 340, 0, 60, 100, 64, 1} };
    QuantizeSettings settings;
    settings.grid = {NoteValue::Sixteenth, false, false};
    settings.strength = 0.5f;
    quantizeNotes(notes, settings, 480);   // 100 → 110
    quantizeNotes(notes, settings, 480);   // 110 → 115
    VSM_ASSERT_EQ(notes[0].startTick, static_cast<Tick>(115));
    VSM_ASSERT_EQ(notes[0].quantizeShiftStart, static_cast<Tick>(15));
    VSM_ASSERT_EQ(resetQuantize(notes, NoteSelection{1}), static_cast<size_t>(1));
    VSM_ASSERT_EQ(notes[0].startTick, static_cast<Tick>(100));
    VSM_ASSERT_EQ(notes[0].endTick, static_cast<Tick>(340));
    VSM_ASSERT_EQ(notes[0].quantizeShiftStart, static_cast<Tick>(0));
}

VSM_TEST(resetting_a_quantize_keeps_what_was_done_by_hand_since) {
    std::vector<Note> notes = { Note{125, 365, 0, 60, 100, 64, 1}, Note{250, 300, 0, 62, 100, 64, 2},
                                Note{500, 600, 0, 64, 100, 64, 3} };
    QuantizeSettings settings;
    settings.grid = {NoteValue::Sixteenth, false, false};
    std::vector<Note> choisies(notes.begin(), notes.begin() + 2);
    quantizeNotes(choisies, settings, 480);            // 120-360, 240-290
    notes[0] = choisies[0];
    notes[1] = choisies[1];
    nudgeNotes(notes, NoteSelection{1, 2}, 120);       // à la main : 240-480, 360-410
    VSM_ASSERT_EQ(countQuantizedNotes(notes, NoteSelection{1, 2, 3}), static_cast<size_t>(2));
    VSM_ASSERT_EQ(resetQuantize(notes, NoteSelection{1, 2, 3}), static_cast<size_t>(2));
    VSM_ASSERT_EQ(notes[0].startTick, static_cast<Tick>(245));   // 125 + 120 : le geste à la main reste
    VSM_ASSERT_EQ(notes[0].endTick, static_cast<Tick>(485));
    VSM_ASSERT_EQ(notes[1].startTick, static_cast<Tick>(370));
    VSM_ASSERT_EQ(notes[1].endTick, static_cast<Tick>(420));
    VSM_ASSERT_EQ(notes[2].startTick, static_cast<Tick>(500));   // jamais quantifiée : intacte
    VSM_ASSERT_EQ(countQuantizedNotes(notes, NoteSelection{1, 2, 3}), static_cast<size_t>(0));
}

VSM_TEST(resetting_a_quantize_of_the_ends_gives_the_ends_back) {
    std::vector<Note> notes = { Note{130, 170, 0, 64, 100, 64, 1} };
    QuantizeSettings settings;
    settings.grid = {NoteValue::Sixteenth, false, false};
    settings.quantizeNoteStart = false;
    settings.quantizeNoteEnd = true;
    quantizeNotes(notes, settings, 480);   // 130-240
    VSM_ASSERT_EQ(notes[0].quantizeShiftStart, static_cast<Tick>(0));
    VSM_ASSERT_EQ(notes[0].quantizeShiftEnd, static_cast<Tick>(70));
    resetQuantize(notes, NoteSelection{1});
    VSM_ASSERT_EQ(notes[0].startTick, static_cast<Tick>(130));
    VSM_ASSERT_EQ(notes[0].endTick, static_cast<Tick>(170));
}

VSM_TEST(a_reset_end_that_would_fall_before_the_start_keeps_the_note_length) {
    std::vector<Note> notes = { Note{0, 200, 0, 60, 100, 64, 1} };
    QuantizeSettings settings;
    settings.grid = {NoteValue::Sixteenth, false, false};
    settings.quantizeNoteStart = false;
    settings.quantizeNoteEnd = true;
    quantizeNotes(notes, settings, 480);   // 0-240, fin +40
    notes[0].endTick = 30;                 // raccourcie à la main
    resetQuantize(notes, NoteSelection{1});
    VSM_ASSERT_EQ(notes[0].startTick, static_cast<Tick>(0));
    VSM_ASSERT_EQ(notes[0].endTick, static_cast<Tick>(30));
}

VSM_TEST(freezing_a_quantize_forgets_the_shift_and_moves_nothing) {
    std::vector<Note> notes = { Note{125, 365, 0, 60, 100, 64, 1}, Note{250, 300, 0, 62, 100, 64, 2} };
    QuantizeSettings settings;
    settings.grid = {NoteValue::Sixteenth, false, false};
    quantizeNotes(notes, settings, 480);
    VSM_ASSERT_EQ(freezeQuantize(notes, NoteSelection{1}), static_cast<size_t>(1));   // la seule choisie
    VSM_ASSERT_EQ(notes[0].startTick, static_cast<Tick>(120));
    VSM_ASSERT_EQ(notes[0].quantizeShiftStart, static_cast<Tick>(0));
    VSM_ASSERT_EQ(notes[1].quantizeShiftStart, static_cast<Tick>(-10));
    VSM_ASSERT_EQ(resetQuantize(notes, NoteSelection{1}), static_cast<size_t>(0));
    VSM_ASSERT_EQ(notes[0].startTick, static_cast<Tick>(120));
}

