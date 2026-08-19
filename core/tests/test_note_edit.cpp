#include "TestFramework.h"
#include "vsm/sequencer/NoteEdit.h"
#include <algorithm>

using namespace vsm::sequencer;
using vsm::midi::Tick;

namespace {

/// Piste de travail : 4 notes, do-mi-sol-do sur une mesure de noires.
std::vector<Note> makeNotes() {
    std::vector<Note> notes;
    const uint8_t pitches[4] = {60, 64, 67, 72};
    for (int i = 0; i < 4; ++i) {
        Note n;
        n.startTick = static_cast<Tick>(i) * 480;
        n.endTick = n.startTick + 480;
        n.number = pitches[i];
        n.velocity = static_cast<uint8_t>(80 + i);
        n.id = static_cast<uint64_t>(i + 1);
        notes.push_back(n);
    }
    return notes;
}

NoteSelection allIds(const std::vector<Note>& notes) { return selectAllNotes(notes); }

const Note* findById(const std::vector<Note>& notes, uint64_t id) {
    auto it = std::find_if(notes.begin(), notes.end(), [id](const Note& n) { return n.id == id; });
    return it == notes.end() ? nullptr : &*it;
}

} // namespace

// --- Gammes ----------------------------------------------------------------

VSM_TEST(scale_major_contains_expected_degrees) {
    Scale cMajor{0, ScaleType::Major};
    VSM_ASSERT(isNoteInScale(60, cMajor));  // do
    VSM_ASSERT(!isNoteInScale(61, cMajor)); // do#
    VSM_ASSERT(isNoteInScale(62, cMajor));  // ré
    VSM_ASSERT(isNoteInScale(64, cMajor));  // mi
    VSM_ASSERT(isNoteInScale(65, cMajor));  // fa
    VSM_ASSERT(isNoteInScale(67, cMajor));  // sol
    VSM_ASSERT(isNoteInScale(69, cMajor));  // la
    VSM_ASSERT(isNoteInScale(71, cMajor));  // si
}

VSM_TEST(scale_respects_root_transposition) {
    // La gamme de Ré majeur contient fa# (66) mais pas fa (65).
    Scale dMajor{2, ScaleType::Major};
    VSM_ASSERT(isNoteInScale(66, dMajor));
    VSM_ASSERT(!isNoteInScale(65, dMajor));
}

VSM_TEST(scale_minor_and_pentatonic_differ) {
    Scale aMinor{9, ScaleType::NaturalMinor};
    Scale aPenta{9, ScaleType::PentatonicMinor};
    VSM_ASSERT(isNoteInScale(71, aMinor));   // si est dans la mineure naturelle
    VSM_ASSERT(!isNoteInScale(71, aPenta));  // mais pas dans la pentatonique mineure
}

VSM_TEST(snap_to_scale_moves_only_out_of_scale_notes) {
    Scale cMajor{0, ScaleType::Major};
    VSM_ASSERT_EQ(static_cast<int>(snapNoteToScale(60, cMajor)), 60); // déjà dans la gamme
    VSM_ASSERT_EQ(static_cast<int>(snapNoteToScale(61, cMajor)), 60); // do# -> do (grave à égalité)
    VSM_ASSERT_EQ(static_cast<int>(snapNoteToScale(66, cMajor)), 65); // fa# -> fa
}

VSM_TEST(note_names_follow_c4_is_60_convention) {
    VSM_ASSERT_EQ(noteNumberToName(60), std::string("C4"));
    VSM_ASSERT_EQ(noteNumberToName(61), std::string("C#4"));
    VSM_ASSERT_EQ(noteNumberToName(69), std::string("A4"));
    VSM_ASSERT_EQ(noteNumberToName(0), std::string("C-1"));
}

// --- Opérations de base ----------------------------------------------------

VSM_TEST(transpose_shifts_only_selected_notes) {
    auto notes = makeNotes();
    NoteSelection sel{1, 2};
    transposeNotes(notes, sel, 12);
    VSM_ASSERT_EQ(static_cast<int>(findById(notes, 1)->number), 72);
    VSM_ASSERT_EQ(static_cast<int>(findById(notes, 2)->number), 76);
    VSM_ASSERT_EQ(static_cast<int>(findById(notes, 3)->number), 67); // non sélectionnée, intacte
}

VSM_TEST(transpose_clamps_at_midi_range) {
    auto notes = makeNotes();
    transposeNotes(notes, allIds(notes), 120);
    for (const auto& n : notes) VSM_ASSERT(n.number <= 127);
}

VSM_TEST(empty_selection_is_a_no_op) {
    auto notes = makeNotes();
    auto before = notes;
    NoteSelection empty;
    transposeNotes(notes, empty, 5);
    nudgeNotes(notes, empty, 100);
    setVelocity(notes, empty, 10);
    for (size_t i = 0; i < notes.size(); ++i) {
        VSM_ASSERT_EQ(static_cast<int>(notes[i].number), static_cast<int>(before[i].number));
        VSM_ASSERT_EQ(static_cast<long long>(notes[i].startTick), static_cast<long long>(before[i].startTick));
        VSM_ASSERT_EQ(static_cast<int>(notes[i].velocity), static_cast<int>(before[i].velocity));
    }
}

VSM_TEST(nudge_preserves_duration_and_never_goes_negative) {
    auto notes = makeNotes();
    nudgeNotes(notes, allIds(notes), -10000);
    for (const auto& n : notes) {
        VSM_ASSERT(n.startTick >= 0);
        VSM_ASSERT_EQ(static_cast<long long>(n.durationTicks()), 480LL);
    }
}

VSM_TEST(set_and_scale_note_lengths) {
    auto notes = makeNotes();
    setNoteLengths(notes, allIds(notes), 240);
    for (const auto& n : notes) VSM_ASSERT_EQ(static_cast<long long>(n.durationTicks()), 240LL);
    scaleNoteLengths(notes, allIds(notes), 2.0f);
    for (const auto& n : notes) VSM_ASSERT_EQ(static_cast<long long>(n.durationTicks()), 480LL);
    scaleNoteLengths(notes, allIds(notes), 0.0001f); // jamais de durée nulle ou négative
    for (const auto& n : notes) VSM_ASSERT(n.durationTicks() >= 1);
}

// --- Legato, chevauchements, découpe, fusion -------------------------------

VSM_TEST(legato_extends_each_note_to_the_next_one) {
    auto notes = makeNotes();
    setNoteLengths(notes, allIds(notes), 100); // notes courtes et détachées
    applyLegato(notes, allIds(notes));
    VSM_ASSERT_EQ(static_cast<long long>(findById(notes, 1)->endTick), 480LL);
    VSM_ASSERT_EQ(static_cast<long long>(findById(notes, 2)->endTick), 960LL);
    VSM_ASSERT_EQ(static_cast<long long>(findById(notes, 3)->endTick), 1440LL);
    // La dernière note n'a pas de suivante : sa durée est conservée.
    VSM_ASSERT_EQ(static_cast<long long>(findById(notes, 4)->durationTicks()), 100LL);
}

VSM_TEST(remove_overlaps_only_trims_same_pitch_collisions) {
    std::vector<Note> notes;
    Note a; a.startTick = 0;   a.endTick = 1000; a.number = 60; a.id = 1; notes.push_back(a);
    Note b; b.startTick = 480; b.endTick = 1000; b.number = 60; b.id = 2; notes.push_back(b);
    Note c; c.startTick = 480; c.endTick = 1000; c.number = 67; c.id = 3; notes.push_back(c);

    removeOverlaps(notes, allIds(notes));
    VSM_ASSERT_EQ(static_cast<long long>(findById(notes, 1)->endTick), 480LL); // tronquée par la note de même hauteur
    VSM_ASSERT_EQ(static_cast<long long>(findById(notes, 3)->endTick), 1000LL); // hauteur différente : intacte
}

VSM_TEST(split_cuts_only_notes_crossing_the_point) {
    auto notes = makeNotes();
    uint64_t idCounter = 100;
    NoteSelection created;
    const size_t n = splitNotes(notes, allIds(notes), 240, idCounter, &created);
    VSM_ASSERT_EQ(n, size_t{1});          // seule la première note traverse le tick 240
    VSM_ASSERT_EQ(created.size(), size_t{1});
    VSM_ASSERT_EQ(static_cast<long long>(findById(notes, 1)->endTick), 240LL);
    const Note* second = findById(notes, *created.begin());
    VSM_ASSERT(second != nullptr);
    VSM_ASSERT_EQ(static_cast<long long>(second->startTick), 240LL);
    VSM_ASSERT_EQ(static_cast<long long>(second->endTick), 480LL);
    VSM_ASSERT_EQ(static_cast<int>(second->number), 60); // hauteur et vélocité conservées
}

VSM_TEST(split_at_a_note_boundary_does_nothing) {
    auto notes = makeNotes();
    uint64_t idCounter = 100;
    VSM_ASSERT_EQ(splitNotes(notes, allIds(notes), 480, idCounter, nullptr), size_t{0});
    VSM_ASSERT_EQ(notes.size(), size_t{4});
}

VSM_TEST(join_merges_same_pitch_notes_only) {
    std::vector<Note> notes;
    Note a; a.startTick = 0;   a.endTick = 240; a.number = 60; a.id = 1; notes.push_back(a);
    Note b; b.startTick = 480; b.endTick = 720; b.number = 60; b.id = 2; notes.push_back(b);
    Note c; c.startTick = 0;   c.endTick = 240; c.number = 67; c.id = 3; notes.push_back(c);

    NoteSelection sel = allIds(notes);
    const size_t removed = joinNotes(notes, sel);
    VSM_ASSERT_EQ(removed, size_t{1});
    VSM_ASSERT_EQ(notes.size(), size_t{2});
    VSM_ASSERT_EQ(static_cast<long long>(findById(notes, 1)->endTick), 720LL); // étendue jusqu'à la fin de la 2e
    VSM_ASSERT(findById(notes, 3) != nullptr);                                  // autre hauteur : préservée
    VSM_ASSERT(sel.count(2) == 0);                                              // la sélection ne référence plus une note morte
}

// --- Transformations musicales --------------------------------------------

VSM_TEST(reverse_in_time_keeps_window_and_durations) {
    auto notes = makeNotes();
    reverseNotesInTime(notes, allIds(notes));
    // La première note (do) doit être arrivée en dernier et vice versa.
    VSM_ASSERT_EQ(static_cast<long long>(findById(notes, 1)->startTick), 1440LL);
    VSM_ASSERT_EQ(static_cast<long long>(findById(notes, 4)->startTick), 0LL);
    for (const auto& n : notes) VSM_ASSERT_EQ(static_cast<long long>(n.durationTicks()), 480LL);
}

VSM_TEST(mirror_pitch_reflects_around_selection_centre) {
    auto notes = makeNotes(); // 60, 64, 67, 72 -> axe = (60+72)
    mirrorNotesPitch(notes, allIds(notes));
    VSM_ASSERT_EQ(static_cast<int>(findById(notes, 1)->number), 72);
    VSM_ASSERT_EQ(static_cast<int>(findById(notes, 4)->number), 60);
    VSM_ASSERT_EQ(static_cast<int>(findById(notes, 2)->number), 68);
}

VSM_TEST(constrain_to_scale_moves_out_of_scale_notes_only) {
    std::vector<Note> notes;
    Note a; a.number = 61; a.startTick = 0; a.endTick = 100; a.id = 1; notes.push_back(a); // do#
    Note b; b.number = 62; b.startTick = 0; b.endTick = 100; b.id = 2; notes.push_back(b); // ré
    constrainNotesToScale(notes, allIds(notes), Scale{0, ScaleType::Major});
    VSM_ASSERT_EQ(static_cast<int>(findById(notes, 1)->number), 60);
    VSM_ASSERT_EQ(static_cast<int>(findById(notes, 2)->number), 62);
}

// --- Vélocité --------------------------------------------------------------

VSM_TEST(velocity_ramp_follows_time_not_note_rank) {
    // Trois notes : deux collées au début, une très loin. Une rampe basée sur
    // le RANG donnerait 0/64/127 ; basée sur le TEMPS (le comportement voulu),
    // les deux premières restent presque au minimum.
    std::vector<Note> notes;
    Note a; a.startTick = 0;    a.endTick = 100;  a.id = 1; notes.push_back(a);
    Note b; b.startTick = 10;   b.endTick = 110;  b.id = 2; notes.push_back(b);
    Note c; c.startTick = 1000; c.endTick = 1100; c.id = 3; notes.push_back(c);

    rampVelocity(notes, allIds(notes), 10, 110);
    VSM_ASSERT_EQ(static_cast<int>(findById(notes, 1)->velocity), 10);
    VSM_ASSERT(findById(notes, 2)->velocity < 15); // proche du début, donc proche de 10
    VSM_ASSERT_EQ(static_cast<int>(findById(notes, 3)->velocity), 110);
}

VSM_TEST(velocity_operations_stay_in_midi_range) {
    auto notes = makeNotes();
    scaleVelocity(notes, allIds(notes), 100.0f);
    for (const auto& n : notes) VSM_ASSERT(n.velocity >= 1 && n.velocity <= 127);
    scaleVelocity(notes, allIds(notes), 0.0f);
    for (const auto& n : notes) VSM_ASSERT(n.velocity >= 1); // jamais 0 (= note off en MIDI)
}

VSM_TEST(randomize_velocity_is_reproducible) {
    auto a = makeNotes(), b = makeNotes();
    randomizeVelocity(a, allIds(a), 20, 12345);
    randomizeVelocity(b, allIds(b), 20, 12345);
    for (size_t i = 0; i < a.size(); ++i)
        VSM_ASSERT_EQ(static_cast<int>(a[i].velocity), static_cast<int>(b[i].velocity));

    auto c = makeNotes();
    randomizeVelocity(c, allIds(c), 20, 999);
    bool anyDifferent = false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].velocity != c[i].velocity) anyDifferent = true;
    VSM_ASSERT(anyDifferent); // une graine différente donne un résultat différent
}

// --- Muet, duplication, arpèges, accords ----------------------------------

VSM_TEST(mute_toggle_affects_only_selection) {
    auto notes = makeNotes();
    setNotesMuted(notes, NoteSelection{1}, true);
    VSM_ASSERT(findById(notes, 1)->muted);
    VSM_ASSERT(!findById(notes, 2)->muted);
    toggleNotesMuted(notes, NoteSelection{1, 2});
    VSM_ASSERT(!findById(notes, 1)->muted);
    VSM_ASSERT(findById(notes, 2)->muted);
}

VSM_TEST(duplicate_offsets_copies_and_returns_new_ids) {
    auto notes = makeNotes();
    uint64_t idCounter = 100;
    NoteSelection created = duplicateNotes(notes, NoteSelection{1}, 1920, idCounter);
    VSM_ASSERT_EQ(created.size(), size_t{1});
    VSM_ASSERT_EQ(notes.size(), size_t{5});
    const Note* copy = findById(notes, *created.begin());
    VSM_ASSERT_EQ(static_cast<long long>(copy->startTick), 1920LL);
    VSM_ASSERT_EQ(static_cast<int>(copy->number), 60);
    VSM_ASSERT(copy->id != 1); // identifiant distinct : la sélection reste sans ambiguïté
}

VSM_TEST(arpeggiate_spreads_a_chord_over_time) {
    std::vector<Note> notes;
    for (int i = 0; i < 3; ++i) {
        Note n; n.startTick = 0; n.endTick = 480;
        n.number = static_cast<uint8_t>(60 + 4 * i);
        n.id = static_cast<uint64_t>(i + 1);
        notes.push_back(n);
    }
    const size_t moved = arpeggiateNotes(notes, allIds(notes), 120, ArpeggioMode::Up);
    VSM_ASSERT_EQ(moved, size_t{3});
    VSM_ASSERT_EQ(static_cast<long long>(findById(notes, 1)->startTick), 0LL);
    VSM_ASSERT_EQ(static_cast<long long>(findById(notes, 2)->startTick), 120LL);
    VSM_ASSERT_EQ(static_cast<long long>(findById(notes, 3)->startTick), 240LL);
    for (const auto& n : notes) VSM_ASSERT_EQ(static_cast<long long>(n.durationTicks()), 480LL);
}

VSM_TEST(arpeggiate_down_reverses_the_order) {
    std::vector<Note> notes;
    for (int i = 0; i < 3; ++i) {
        Note n; n.startTick = 0; n.endTick = 480;
        n.number = static_cast<uint8_t>(60 + 4 * i);
        n.id = static_cast<uint64_t>(i + 1);
        notes.push_back(n);
    }
    arpeggiateNotes(notes, allIds(notes), 120, ArpeggioMode::Down);
    VSM_ASSERT_EQ(static_cast<long long>(findById(notes, 3)->startTick), 0LL);   // la plus aiguë d'abord
    VSM_ASSERT_EQ(static_cast<long long>(findById(notes, 1)->startTick), 240LL);
}

VSM_TEST(arpeggiate_leaves_single_notes_alone) {
    auto notes = makeNotes(); // aucune note simultanée
    VSM_ASSERT_EQ(arpeggiateNotes(notes, allIds(notes), 120, ArpeggioMode::Up), size_t{0});
}

VSM_TEST(insert_chord_creates_the_right_intervals) {
    std::vector<Note> notes;
    uint64_t idCounter = 0;
    NoteSelection created = insertChord(notes, 480, 960, 60, ChordType::Minor7, 0, 100, idCounter);
    VSM_ASSERT_EQ(created.size(), size_t{4});
    VSM_ASSERT_EQ(notes.size(), size_t{4});
    std::vector<int> numbers;
    for (const auto& n : notes) {
        numbers.push_back(static_cast<int>(n.number));
        VSM_ASSERT_EQ(static_cast<long long>(n.startTick), 480LL);
        VSM_ASSERT_EQ(static_cast<long long>(n.durationTicks()), 960LL);
    }
    std::sort(numbers.begin(), numbers.end());
    VSM_ASSERT_EQ(numbers[0], 60);
    VSM_ASSERT_EQ(numbers[1], 63);
    VSM_ASSERT_EQ(numbers[2], 67);
    VSM_ASSERT_EQ(numbers[3], 70);
}

// --- Sélection et statistiques --------------------------------------------

VSM_TEST(selection_helpers) {
    auto notes = makeNotes();
    VSM_ASSERT_EQ(selectAllNotes(notes).size(), size_t{4});
    VSM_ASSERT_EQ(invertNoteSelection(notes, NoteSelection{1, 2}).size(), size_t{2});
    VSM_ASSERT_EQ(selectNotesInTimeRange(notes, 0, 960).size(), size_t{2});

    // Deux notes de même hauteur : sélectionner l'une doit proposer les deux.
    Note extra; extra.startTick = 5000; extra.endTick = 5100; extra.number = 60; extra.id = 42;
    notes.push_back(extra);
    VSM_ASSERT_EQ(selectNotesWithSamePitch(notes, NoteSelection{1}).size(), size_t{2});
}

VSM_TEST(selection_stats_summarise_the_selection) {
    auto notes = makeNotes();
    SelectionStats stats = computeSelectionStats(notes, allIds(notes));
    VSM_ASSERT_EQ(stats.count, size_t{4});
    VSM_ASSERT_EQ(static_cast<long long>(stats.startTick), 0LL);
    VSM_ASSERT_EQ(static_cast<long long>(stats.endTick), 1920LL);
    VSM_ASSERT_EQ(static_cast<int>(stats.lowestNote), 60);
    VSM_ASSERT_EQ(static_cast<int>(stats.highestNote), 72);
    VSM_ASSERT_NEAR(stats.averageVelocity, 81.5f, 0.01f);

    SelectionStats empty = computeSelectionStats(notes, NoteSelection{});
    VSM_ASSERT_EQ(empty.count, size_t{0});
}
