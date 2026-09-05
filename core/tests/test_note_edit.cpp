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

// --- Notes douteuses -------------------------------------------------------

namespace {
/// Six notes, dont trois douteuses (ids 2, 4, 5) ; les ids 4 et 5 commencent
/// au même tick, pour vérifier que l'ordre est total et stable.
std::vector<Note> makeDoubtfulNotes() {
    auto notes = makeNotes();                      // ids 1..4, ticks 0/480/960/1440
    notes[1].confidence = 0.30f;                   // id 2 : douteuse
    notes[3].confidence = 0.10f;                   // id 4 : douteuse
    Note simultanee; simultanee.startTick = 1440; simultanee.endTick = 1900;
    simultanee.number = 60; simultanee.id = 5; simultanee.confidence = 0.50f;   // douteuse, même tick que 4, plus grave
    Note franche; franche.startTick = 2400; franche.endTick = 2800;
    franche.number = 65; franche.id = 6; franche.confidence = 0.90f;
    notes.push_back(simultanee);
    notes.push_back(franche);
    return notes;
}
} // namespace

VSM_TEST(doubtful_notes_are_counted_and_selected_by_threshold) {
    auto notes = makeDoubtfulNotes();
    VSM_ASSERT_EQ(countDoubtfulNotes(notes), size_t{3});
    VSM_ASSERT(selectDoubtfulNotes(notes) == (NoteSelection{2, 4, 5}));
    // Une note pile au seuil n'est PAS douteuse (« en dessous », pas « au plus »).
    Note limite; limite.id = 7; limite.confidence = kDoubtfulNoteThreshold;
    VSM_ASSERT(!isNoteDoubtful(limite));
    // Le seuil est un paramètre : à 0,95, la note franche à 0,90 devient douteuse.
    VSM_ASSERT_EQ(countDoubtfulNotes(notes, 0.95f), size_t{4});
    // Une piste saisie à la main (confiance 1 partout) n'a rien de douteux.
    VSM_ASSERT_EQ(countDoubtfulNotes(makeNotes()), size_t{0});
    VSM_ASSERT_EQ(nextDoubtfulNote(makeNotes(), {}, 0, true), uint64_t{0});
}

VSM_TEST(next_doubtful_note_walks_the_song_in_order_and_wraps) {
    auto notes = makeDoubtfulNotes();
    // Sans sélection, depuis le début : la première douteuse (id 2, tick 480).
    VSM_ASSERT_EQ(nextDoubtfulNote(notes, {}, 0, true), uint64_t{2});
    // Depuis une sélection : la suivante dans l'ordre. À 1440, la plus grave
    // (id 5, do) vient avant la plus aiguë (id 4, do aigu).
    VSM_ASSERT_EQ(nextDoubtfulNote(notes, {2}, 0, true), uint64_t{5});
    VSM_ASSERT_EQ(nextDoubtfulNote(notes, {5}, 0, true), uint64_t{4});
    // Au bout : on repart du début au lieu de rester bloqué.
    VSM_ASSERT_EQ(nextDoubtfulNote(notes, {4}, 0, true), uint64_t{2});
    // Une note franche sélectionnée sert aussi de point de départ.
    VSM_ASSERT_EQ(nextDoubtfulNote(notes, {1}, 0, true), uint64_t{2});
    VSM_ASSERT_EQ(nextDoubtfulNote(notes, {6}, 0, true), uint64_t{2});
    // Une sélection multiple part de sa borne dans le sens du parcours.
    VSM_ASSERT_EQ(nextDoubtfulNote(notes, {2, 5}, 0, true), uint64_t{4});
    VSM_ASSERT_EQ(nextDoubtfulNote(notes, {2, 5}, 0, false), uint64_t{4});
}

VSM_TEST(next_doubtful_note_starts_from_the_playhead_and_goes_backwards) {
    auto notes = makeDoubtfulNotes();
    // La tête de lecture posée SUR une douteuse la désigne (« à partir d'ici »).
    VSM_ASSERT_EQ(nextDoubtfulNote(notes, {}, 480, true), uint64_t{2});
    VSM_ASSERT_EQ(nextDoubtfulNote(notes, {}, 481, true), uint64_t{5});
    // Vers l'arrière depuis la tête : la dernière qui commence AVANT.
    VSM_ASSERT_EQ(nextDoubtfulNote(notes, {}, 1440, false), uint64_t{2});
    VSM_ASSERT_EQ(nextDoubtfulNote(notes, {}, 3000, false), uint64_t{4});
    // Vers l'arrière depuis une sélection, et le tour par la fin.
    VSM_ASSERT_EQ(nextDoubtfulNote(notes, {4}, 0, false), uint64_t{5});
    VSM_ASSERT_EQ(nextDoubtfulNote(notes, {5}, 0, false), uint64_t{2});
    VSM_ASSERT_EQ(nextDoubtfulNote(notes, {2}, 0, false), uint64_t{4});
    // Avant la première, sans sélection : le tour aussi.
    VSM_ASSERT_EQ(nextDoubtfulNote(notes, {}, 0, false), uint64_t{4});
}

// D19.1 — COMPRIMER LES VÉLOCITÉS VERS LEUR MOYENNE.
//
// Sur une transcription, le même coup de caisse claire ressort à 71, 96 et 58
// parce que l'estimation dépend de ce qui sonnait en même temps. Resserrer
// rend à l'instrument la frappe régulière que le jeu avait et que l'analyse a
// perdue. `scaleVelocity` ne peut pas faire cela : elle écarte les valeurs
// autant qu'elle les monte.
VSM_TEST(compressing_velocities_to_zero_puts_them_all_on_their_mean) {
    std::vector<Note> notes;
    const int valeurs[3] = {71, 96, 58};
    for (int i = 0; i < 3; ++i) {
        Note n;
        n.startTick = i * 100;
        n.endTick = i * 100 + 50;
        n.id = static_cast<uint64_t>(i + 1);
        n.velocity = static_cast<uint8_t>(valeurs[i]);
        notes.push_back(n);
    }
    // (71 + 96 + 58) / 3 = 225 / 3 = 75, exactement.
    compressVelocity(notes, allIds(notes), 0.0f);
    for (const Note& n : notes) VSM_ASSERT_EQ(static_cast<int>(n.velocity), 75);
}

VSM_TEST(compressing_velocities_by_one_changes_nothing_at_all) {
    // LE CAS NEUTRE EST EXACT, et pas seulement « très proche » : la vélocité
    // n'est pas recalculée puis réécrite identique, elle est laissée en place.
    // Faire reposer l'exactitude d'un cas neutre sur un arrondi qui tombe juste
    // est la façon dont on découvre, six mois plus tard, qu'une note sur mille
    // a bougé d'un cran.
    std::vector<Note> notes;
    const int valeurs[5] = {1, 37, 64, 100, 127};
    for (int i = 0; i < 5; ++i) {
        Note n;
        n.startTick = i * 10;
        n.endTick = i * 10 + 5;
        n.id = static_cast<uint64_t>(i + 1);
        n.velocity = static_cast<uint8_t>(valeurs[i]);
        notes.push_back(n);
    }
    const std::vector<Note> avant = notes;
    compressVelocity(notes, allIds(notes), 1.0f);
    for (size_t i = 0; i < notes.size(); ++i)
        VSM_ASSERT_EQ(static_cast<int>(notes[i].velocity), static_cast<int>(avant[i].velocity));
}

VSM_TEST(compressing_velocities_halfway_moves_them_halfway) {
    std::vector<Note> notes;
    const int valeurs[2] = {40, 80};   // moyenne 60
    for (int i = 0; i < 2; ++i) {
        Note n;
        n.startTick = i * 10;
        n.endTick = i * 10 + 5;
        n.id = static_cast<uint64_t>(i + 1);
        n.velocity = static_cast<uint8_t>(valeurs[i]);
        notes.push_back(n);
    }
    compressVelocity(notes, allIds(notes), 0.5f);
    VSM_ASSERT_EQ(static_cast<int>(findById(notes, 1)->velocity), 50);
    VSM_ASSERT_EQ(static_cast<int>(findById(notes, 2)->velocity), 70);
}

VSM_TEST(compressing_uses_the_mean_of_the_selection_and_not_of_everything) {
    // La moyenne se calcule sur ce qu'on a CHOISI. Sinon deux compressions
    // successives ne donneraient pas ce que la sélection réunie donne, et le
    // résultat dépendrait de notes qu'on n'a pas touchées.
    std::vector<Note> notes;
    const int valeurs[4] = {10, 30, 200, 200};   // les deux dernières hors sélection
    for (int i = 0; i < 4; ++i) {
        Note n;
        n.startTick = i * 10;
        n.endTick = i * 10 + 5;
        n.id = static_cast<uint64_t>(i + 1);
        n.velocity = static_cast<uint8_t>(std::min(valeurs[i], 127));
        notes.push_back(n);
    }
    NoteSelection choix{1, 2};
    compressVelocity(notes, choix, 0.0f);
    VSM_ASSERT_EQ(static_cast<int>(findById(notes, 1)->velocity), 20);
    VSM_ASSERT_EQ(static_cast<int>(findById(notes, 2)->velocity), 20);
    // Les non choisies n'ont pas bougé.
    VSM_ASSERT_EQ(static_cast<int>(findById(notes, 3)->velocity), 127);
}

// D19.1 — LIMITER : on RAMÈNE dans l'intervalle, on n'y remet pas à l'échelle.
VSM_TEST(limiting_velocities_is_idempotent_and_leaves_those_already_inside) {
    std::vector<Note> notes;
    const int valeurs[4] = {5, 60, 90, 127};
    for (int i = 0; i < 4; ++i) {
        Note n;
        n.startTick = i * 10;
        n.endTick = i * 10 + 5;
        n.id = static_cast<uint64_t>(i + 1);
        n.velocity = static_cast<uint8_t>(valeurs[i]);
        notes.push_back(n);
    }
    limitVelocity(notes, allIds(notes), 20, 100);
    VSM_ASSERT_EQ(static_cast<int>(findById(notes, 1)->velocity), 20);   // remontée
    VSM_ASSERT_EQ(static_cast<int>(findById(notes, 2)->velocity), 60);   // intacte
    VSM_ASSERT_EQ(static_cast<int>(findById(notes, 3)->velocity), 90);   // intacte
    VSM_ASSERT_EQ(static_cast<int>(findById(notes, 4)->velocity), 100);  // rabaissée

    // IDEMPOTENTE : c'est la propriété qu'on attend d'une limite, et ce qui la
    // distingue d'une mise à l'échelle vers l'intervalle.
    const std::vector<Note> uneFois = notes;
    limitVelocity(notes, allIds(notes), 20, 100);
    for (size_t i = 0; i < notes.size(); ++i)
        VSM_ASSERT_EQ(static_cast<int>(notes[i].velocity), static_cast<int>(uneFois[i].velocity));
}

VSM_TEST(limiting_with_the_bounds_swapped_reads_them_the_right_way_round) {
    // Deux bornes saisies à l'envers sont une faute de frappe, pas une demande
    // d'ignorer le geste en silence.
    std::vector<Note> notes;
    Note n; n.startTick = 0; n.endTick = 10; n.id = 1; n.velocity = 5;
    notes.push_back(n);
    limitVelocity(notes, allIds(notes), 100, 20);
    VSM_ASSERT_EQ(static_cast<int>(findById(notes, 1)->velocity), 20);
}
