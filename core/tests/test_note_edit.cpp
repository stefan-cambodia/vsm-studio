#include "TestFramework.h"
#include <functional>
#include "vsm/sequencer/NoteEdit.h"
#include <algorithm>
#include <map>
#include <tuple>
#include <set>
#include <string>

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

// D536 : TOUT OU RIEN — ce test remplace `transpose_clamps_at_midi_range`, qui épinglait le
// BORNAGE (« toutes ≤ 127 ») : une note à 120 montée d'une octave sonnait à 127, une hauteur
// que personne n'a demandée, et deux notes s'y confondaient. La transposition de piste avait
// déjà tranché l'inverse (`transposeDroppedNotes`) ; l'édition s'y range. Une seule note qui
// sortirait de 0..127 et RIEN ne bouge — un accord ne se casse pas en deux octaves —, et le
// nombre est rendu pour être dit.
VSM_TEST(transpose_moves_nothing_when_one_chosen_note_would_leave_the_midi_range) {
    const std::vector<Note> depart = {{0, 480, 0, 60, 100, 64, 1}, {0, 480, 0, 64, 100, 64, 2},
                                      {0, 480, 0, 120, 100, 64, 3}, {0, 480, 0, 127, 100, 64, 4}};
    auto notes = depart;
    VSM_ASSERT_EQ(transposeNotes(notes, {1, 2, 3}, 12), static_cast<size_t>(1));
    for (size_t i = 0; i < notes.size(); ++i) VSM_ASSERT_EQ(notes[i].number, depart[i].number);
    // La note 4 (127) n'est pas choisie : elle ne compte pas, et les autres montent.
    VSM_ASSERT_EQ(transposeNotes(notes, {1, 2}, 12), static_cast<size_t>(0));
    VSM_ASSERT_EQ(static_cast<int>(findById(notes, 1)->number), 72);
    VSM_ASSERT_EQ(static_cast<int>(findById(notes, 2)->number), 76);
    VSM_ASSERT_EQ(static_cast<int>(findById(notes, 4)->number), 127);
    // Vers le bas aussi : −80 sur {72, 76} sortirait deux fois.
    VSM_ASSERT_EQ(transposeNotes(notes, {1, 2}, -80), static_cast<size_t>(2));
    VSM_ASSERT_EQ(static_cast<int>(findById(notes, 1)->number), 72);
    VSM_ASSERT_EQ(notesLeavingMidiRange(notes, {1, 2, 3, 4}, 8), static_cast<size_t>(2));   // 120 et 127
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

// D576.2, D576.3 — LE CHEVAUCHEMENT ET LE MODE « ENTRE LES NOTES CHOISIES ». L'attendu écrit avant (ROADMAP-daw, D576) :
// quatre notes de 100 ticks à 0, 480, 960, 1 440 ; +10 → 490, 970, 1 450 ; −10 → 470, 950, 1 430 ; la dernière garde sa
// durée (1 540). Les notes 1 et 3 seules choisies : le mode « entre les notes choisies » rend 960 et 1 060, le témoin
// (toutes les notes) 480 et 1 440, les autres intactes. Un blanc plus grand que l'écart laisse un tick au moins.
VSM_TEST(legato_overlap_and_selected_only_mode) {
    auto fins = [](LegatoOptions o, const std::function<NoteSelection(const std::vector<Note>&)>& choix) {
        auto notes = makeNotes();
        setNoteLengths(notes, allIds(notes), 100);
        applyLegato(notes, choix(notes), o);
        std::vector<long long> f;
        for (uint64_t id = 1; id <= 4; ++id) f.push_back(static_cast<long long>(findById(notes, id)->endTick));
        return f;
    };
    const auto toutes = [](const std::vector<Note>& n) { return allIds(n); };
    const auto impaires = [](const std::vector<Note>&) { return NoteSelection{1, 3}; };
    using V = std::vector<long long>;
    VSM_ASSERT(fins({0, false}, toutes) == (V{480, 960, 1440, 1540}));     // le chemin d'avant
    VSM_ASSERT(fins({10, false}, toutes) == (V{490, 970, 1450, 1540}));
    VSM_ASSERT(fins({-10, false}, toutes) == (V{470, 950, 1430, 1540}));
    VSM_ASSERT(fins({0, false}, impaires) == (V{480, 580, 1440, 1540}));    // le témoin : toutes les notes en référence
    VSM_ASSERT(fins({0, true}, impaires) == (V{960, 580, 1060, 1540}));     // entre les notes choisies
    VSM_ASSERT(fins({-1000, false}, toutes) == (V{1, 481, 961, 1540}));    // un tick au moins
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

// D223 (A39) : LES MOINS SÛRES, EN PROPORTION. Le seuil absolu désigne 53 à 91 %
// des notes des reconstructions réelles ; ce geste-ci en désigne toujours une
// minorité, choisie par le bas.
VSM_TEST(least_confident_notes_are_a_share_not_a_threshold) {
    auto notes = makeDoubtfulNotes();              // six notes : 1, 0.30, 1, 0.10, 0.50, 0.90
    // 10 % de six notes fait 0,6 -> une note, la plus basse (id 4, confiance 0,10).
    VSM_ASSERT(selectLeastConfident(notes, 0.10) == (NoteSelection{4}));
    // La moitié : les trois plus basses (0,10 ; 0,30 ; 0,50) -- exactement les
    // douteuses ici, mais par leur RANG et non par le seuil.
    VSM_ASSERT(selectLeastConfident(notes, 0.50) == (NoteSelection{2, 4, 5}));
    // Tout : toutes les notes, sans exception.
    VSM_ASSERT_EQ(selectLeastConfident(notes, 1.0).size(), notes.size());
    // Au moins une dès qu'il y en a, et rien sur une piste vide.
    VSM_ASSERT_EQ(selectLeastConfident(notes, 0.0001).size(), size_t{1});
    VSM_ASSERT(selectLeastConfident(std::vector<Note>{}, 0.10).empty());
    // Une piste saisie à la main (confiance 1 partout) : le geste rend quand même
    // une note -- « les 10 % les moins sûres » d'un ensemble sûr existe, et c'est
    // à l'appelant de savoir qu'il n'y a rien à revoir.
    VSM_ASSERT_EQ(selectLeastConfident(makeNotes(), 0.25).size(), size_t{1});
    // DEUX APPELS, LA MÊME SÉLECTION : à confiance égale, l'ordre des notes tranche.
    VSM_ASSERT(selectLeastConfident(makeNotes(), 0.50) == selectLeastConfident(makeNotes(), 0.50));
}

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

// D21.1 — CHOISIR PAR VÉLOCITÉ OU PAR DURÉE. Les bornes sont strictes, et la
// sélection rendue ne contient que les notes visées.

VSM_TEST(selecting_below_a_velocity_and_shorter_than_a_length_is_strict) {
    std::vector<Note> notes;
    auto note = [&](uint64_t id, uint8_t velocity, Tick longueur) {
        Note n; n.id = id; n.number = 60; n.velocity = velocity; n.startTick = 0; n.endTick = longueur;
        notes.push_back(n);
    };
    note(1, 10, 480);
    note(2, 32, 120);
    note(3, 31, 119);
    note(4, 100, 30);

    const NoteSelection faibles = selectNotesBelowVelocity(notes, 32);
    VSM_ASSERT_EQ(faibles.size(), size_t(2));
    VSM_ASSERT(faibles.count(1) == 1 && faibles.count(3) == 1);   // 32 n'est pas « plus faible que 32 »

    const NoteSelection courtes = selectNotesShorterThan(notes, 120);
    VSM_ASSERT_EQ(courtes.size(), size_t(2));
    VSM_ASSERT(courtes.count(3) == 1 && courtes.count(4) == 1);   // 120 n'est pas « plus courte que 120 »

    VSM_ASSERT(selectNotesBelowVelocity(notes, 1).empty());
    VSM_ASSERT(selectNotesShorterThan(notes, 1).empty());
    VSM_ASSERT_EQ(selectNotesBelowVelocity(notes, 128).size(), size_t(4));
}

// --- D22.3 : deux fois plus lent, deux fois plus vite ----------------------

VSM_TEST(scale_note_times_halves_the_speed_from_the_first_start_without_overlaps) {
    auto notes = makeNotes(); // quatre noires à 0, 480, 960, 1440
    scaleNoteTimes(notes, allIds(notes), 2.0);
    const Tick departs[4] = {0, 960, 1920, 2880};
    for (int i = 0; i < 4; ++i) {
        const Note* n = findById(notes, static_cast<uint64_t>(i + 1));
        VSM_ASSERT(n != nullptr);
        VSM_ASSERT_EQ(n->startTick, departs[i]);
        VSM_ASSERT_EQ(n->durationTicks(), static_cast<Tick>(960));
    }
}

VSM_TEST(scale_note_times_keeps_the_anchor_and_leaves_unselected_notes_alone) {
    auto notes = makeNotes();
    NoteSelection deuxDernieres{3, 4}; // à 960 et 1440
    scaleNoteTimes(notes, deuxDernieres, 0.5);
    // L'ancre est le premier départ DE LA SÉLECTION (960), pas du morceau.
    VSM_ASSERT_EQ(findById(notes, 3)->startTick, static_cast<Tick>(960));
    VSM_ASSERT_EQ(findById(notes, 3)->durationTicks(), static_cast<Tick>(240));
    VSM_ASSERT_EQ(findById(notes, 4)->startTick, static_cast<Tick>(960 + 240));
    VSM_ASSERT_EQ(findById(notes, 4)->durationTicks(), static_cast<Tick>(240));
    VSM_ASSERT_EQ(findById(notes, 1)->startTick, static_cast<Tick>(0));
    VSM_ASSERT_EQ(findById(notes, 2)->startTick, static_cast<Tick>(480));
    VSM_ASSERT_EQ(findById(notes, 2)->durationTicks(), static_cast<Tick>(480));
}

VSM_TEST(scale_note_times_never_loses_a_note_when_halving_repeatedly) {
    auto notes = makeNotes();
    for (int i = 0; i < 12; ++i) scaleNoteTimes(notes, allIds(notes), 0.5);
    VSM_ASSERT_EQ(notes.size(), static_cast<size_t>(4));
    for (const auto& n : notes) VSM_ASSERT(n.durationTicks() >= 1);
    // Un facteur nul ou négatif ne fait rien.
    auto temoin = makeNotes();
    scaleNoteTimes(temoin, allIds(temoin), 0.0);
    VSM_ASSERT_EQ(findById(temoin, 4)->startTick, static_cast<Tick>(1440));
}

VSM_TEST(every_scale_type_has_a_stable_id_that_reads_back) {
    // D495 : LE TYPE DE GAMME S'ÉCRIT DANS UN PROJET PAR UN IDENTIFIANT STABLE.
    // Chacun des quatorze se relit en lui-même, et deux types n'en partagent pas.
    std::set<std::string> vus;
    for (const auto type : allScaleTypes()) {
        const std::string id = scaleTypeId(type);
        VSM_ASSERT(vus.insert(id).second);
        ScaleType relu = ScaleType::Chromatic;
        VSM_ASSERT(scaleTypeFromId(id, relu));
        VSM_ASSERT(relu == type);
    }
    VSM_ASSERT_EQ(vus.size(), size_t{14});
    // Un identifiant inconnu est REFUSÉ et ne touche pas la sortie : c'est à
    // l'appelant de dire qu'il l'écarte.
    ScaleType sortie = ScaleType::Dorian;
    VSM_ASSERT(!scaleTypeFromId("inconnu", sortie));
    VSM_ASSERT(sortie == ScaleType::Dorian);
    // Le NOM affiché n'est pas un identifiant.
    VSM_ASSERT(!scaleTypeFromId("Dorien", sortie));
}

// --- D546.1 : supprimer les doublons ---------------------------------------
namespace {
Note doublon(uint64_t id, Tick debut, uint8_t hauteur, uint8_t velocite, Tick duree = 120, uint8_t canal = 0,
             bool muette = false) {
    Note n;
    n.id = id; n.startTick = debut; n.endTick = debut + duree; n.number = hauteur;
    n.velocity = velocite; n.channel = canal; n.muted = muette;
    return n;
}
NoteSelection toutes(const std::vector<Note>& notes) {
    NoteSelection s;
    for (const auto& n : notes) s.insert(n.id);
    return s;
}
std::set<uint64_t> restantes(const std::vector<Note>& notes) {
    std::set<uint64_t> ids;
    for (const auto& n : notes) ids.insert(n.id);
    return ids;
}
}

VSM_TEST(delete_doubles_keeps_the_loudest_of_notes_that_start_together) {
    std::vector<Note> notes{doublon(1, 0, 60, 80), doublon(2, 0, 60, 100)};
    VSM_ASSERT_EQ(deleteDoubleNotes(notes, toutes(notes), 30), size_t(1));
    VSM_ASSERT(restantes(notes) == std::set<uint64_t>{2});
    // À 20 ticks : un doublon ; à 40 : deux notes (hors tolérance).
    notes = {doublon(1, 0, 60, 90), doublon(2, 20, 60, 80)};
    VSM_ASSERT_EQ(deleteDoubleNotes(notes, toutes(notes), 30), size_t(1));
    VSM_ASSERT(restantes(notes) == std::set<uint64_t>{1});
    notes = {doublon(1, 0, 60, 90), doublon(2, 40, 60, 80)};
    VSM_ASSERT_EQ(deleteDoubleNotes(notes, toutes(notes), 30), size_t(0));
}

VSM_TEST(different_pitches_or_channels_are_not_doubles) {
    std::vector<Note> notes{doublon(1, 0, 60, 90), doublon(2, 0, 62, 90), doublon(3, 0, 60, 90, 120, 1)};
    VSM_ASSERT_EQ(deleteDoubleNotes(notes, toutes(notes), 30), size_t(0));
    VSM_ASSERT_EQ(notes.size(), size_t(3));
}

VSM_TEST(at_equal_velocity_the_longest_stays_and_a_muted_note_never_wins) {
    std::vector<Note> notes{doublon(1, 0, 60, 90, 60), doublon(2, 5, 60, 90, 240)};
    deleteDoubleNotes(notes, toutes(notes), 30);
    VSM_ASSERT(restantes(notes) == std::set<uint64_t>{2});
    notes = {doublon(1, 0, 60, 127, 480, 0, true), doublon(2, 0, 60, 40)};
    deleteDoubleNotes(notes, toutes(notes), 30);
    VSM_ASSERT(restantes(notes) == std::set<uint64_t>{2});
}

VSM_TEST(notes_outside_the_selection_are_neither_removed_nor_remove) {
    std::vector<Note> notes{doublon(1, 0, 60, 80), doublon(2, 0, 60, 100), doublon(3, 0, 60, 120)};
    VSM_ASSERT_EQ(deleteDoubleNotes(notes, NoteSelection{1, 2}, 30), size_t(1));
    VSM_ASSERT(restantes(notes) == (std::set<uint64_t>{2, 3}));   // la 3, plus forte, n'était pas choisie
}

VSM_TEST(a_run_of_close_notes_does_not_melt_into_one) {
    // De proche en proche, 0 · 25 · 50 · 75 seraient UN groupe ; depuis la première, deux groupes de deux.
    std::vector<Note> notes{doublon(1, 0, 60, 90), doublon(2, 25, 60, 80), doublon(3, 50, 60, 90), doublon(4, 75, 60, 80)};
    VSM_ASSERT_EQ(deleteDoubleNotes(notes, toutes(notes), 30), size_t(2));
    VSM_ASSERT(restantes(notes) == (std::set<uint64_t>{1, 3}));
}

// D552.2 — LES CHEVAUCHEMENTS EN MONO : une ligne qui ne joue jamais deux notes à la fois, toutes hauteurs confondues.
VSM_TEST(remove_overlaps_mono_makes_a_line_that_never_plays_two_notes) {
    std::vector<Note> notes;
    auto poser = [&](Tick d, Tick f, uint8_t h, uint64_t id) {
        Note n; n.startTick = d; n.endTick = f; n.number = h; n.id = id; notes.push_back(n);
    };
    poser(0, 1000, 40, 1);      // déborde sur la suivante, d'une AUTRE hauteur
    poser(480, 900, 45, 2);     // commence avec 3, plus grave : retirée
    poser(480, 1200, 52, 3);    // la plus aiguë du départ 480 : gardée, déborde sur 4
    poser(960, 1400, 40, 4);
    poser(0, 3000, 30, 5);      // NON choisie : ne bouge pas, ne compte pas
    const auto r = removeOverlapsMono(notes, {1, 2, 3, 4});
    VSM_ASSERT_EQ(r.removed, size_t(1));
    VSM_ASSERT_EQ(r.shortened, size_t(2));
    VSM_ASSERT(findById(notes, 2) == nullptr);
    VSM_ASSERT_EQ(static_cast<long long>(findById(notes, 1)->endTick), 480LL);
    VSM_ASSERT_EQ(static_cast<long long>(findById(notes, 3)->endTick), 960LL);
    VSM_ASSERT_EQ(static_cast<long long>(findById(notes, 4)->endTick), 1400LL);
    VSM_ASSERT_EQ(static_cast<long long>(findById(notes, 5)->endTick), 3000LL);
    // Le même geste, rejoué : plus rien à faire.
    const auto r2 = removeOverlapsMono(notes, {1, 3, 4});
    VSM_ASSERT_EQ(r2.removed + r2.shortened, size_t(0));
}

// D554.2 — RESTREINDRE LA POLYPHONIE : un accord de quatre et une note qui entre au milieu, à trois voix.
VSM_TEST(restricting_polyphony_keeps_the_highest_and_shortens_the_oldest) {
    std::vector<Note> notes;
    uint64_t ids = 1;
    const auto ajoute = [&](Tick d, Tick f, uint8_t h) {
        Note n; n.startTick = d; n.endTick = f; n.number = h; n.id = ids++; notes.push_back(n); return n.id; };
    NoteSelection choix;
    for (uint8_t h : {60, 64, 67, 71}) choix.insert(ajoute(0, 1920, h));
    choix.insert(ajoute(960, 1440, 62));
    const uint64_t horsChoix = ajoute(0, 3000, 48);   // non choisie : ne bouge pas et ne compte pas
    const auto r = restrictPolyphony(notes, choix, 3);
    VSM_ASSERT_EQ(r.removed, size_t(1));     // le 60, le plus grave de l'accord de quatre
    VSM_ASSERT_EQ(r.shortened, size_t(1));   // le 64 : le plus ancien, et à départ égal le plus grave
    std::map<int, std::pair<Tick, Tick>> v;
    for (const auto& n : notes) v[n.number] = {n.startTick, n.endTick};
    VSM_ASSERT(v.count(60) == 0);
    VSM_ASSERT(v[64] == std::make_pair(Tick(0), Tick(960)));
    VSM_ASSERT(v[67] == std::make_pair(Tick(0), Tick(1920)));
    VSM_ASSERT(v[71] == std::make_pair(Tick(0), Tick(1920)));
    VSM_ASSERT(v[62] == std::make_pair(Tick(960), Tick(1440)));
    VSM_ASSERT(v[48] == std::make_pair(Tick(0), Tick(3000)));
    VSM_ASSERT(std::any_of(notes.begin(), notes.end(), [&](const Note& n) { return n.id == horsChoix; }));
    // Rejoué : plus rien à faire.
    const auto rien = restrictPolyphony(notes, choix, 3);
    VSM_ASSERT_EQ(rien.removed + rien.shortened, size_t(0));
}

// D554.2 — LA PLUS ANCIENNE, PAS LA PLUS RÉCENTE : à deux voix, 60 [0, …) puis 64 [480, …) sonnent quand 67 entre à
// 960 — c'est 60 qui s'arrête. Le premier test ne le distinguait pas (toutes ses notes partaient ensemble), et le
// mutant qui coupe la plus récente y passait.
VSM_TEST(restricting_polyphony_cuts_the_oldest_sounding_note_not_the_newest) {
    std::vector<Note> notes;
    NoteSelection choix;
    uint64_t id = 1;
    for (auto [d, h] : std::vector<std::pair<Tick, uint8_t>>{{0, 60}, {480, 64}, {960, 67}}) {
        Note n; n.startTick = d; n.endTick = 1920; n.number = h; n.id = id; choix.insert(id++); notes.push_back(n);
    }
    const auto r = restrictPolyphony(notes, choix, 2);
    VSM_ASSERT_EQ(r.shortened, size_t(1));
    VSM_ASSERT_EQ(notes[0].endTick, Tick(960));    // 60, la plus ancienne
    VSM_ASSERT_EQ(notes[1].endTick, Tick(1920));   // 64 continue
    VSM_ASSERT_EQ(notes[2].endTick, Tick(1920));
}

// D554.2 — À UNE VOIX, c'est exactement `removeOverlapsMono` (D552.2), sur le cas de son banc.
VSM_TEST(restricting_polyphony_to_one_voice_is_removing_overlaps_mono) {
    std::vector<Note> base;
    NoteSelection choix;
    uint64_t id = 1;
    for (auto [d, f, h] : std::vector<std::tuple<Tick, Tick, uint8_t>>{{0, 1000, 41}, {480, 900, 45}, {480, 1200, 52},
                                                                      {960, 1400, 40}}) {
        Note n; n.startTick = d; n.endTick = f; n.number = h; n.id = id; choix.insert(id++); base.push_back(n);
    }
    auto a = base, b = base;
    const auto ra = restrictPolyphony(a, choix, 1);
    const auto rb = removeOverlapsMono(b, choix);
    VSM_ASSERT_EQ(ra.removed, rb.removed);
    VSM_ASSERT_EQ(ra.shortened, rb.shortened);
    VSM_ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        VSM_ASSERT_EQ(a[i].id, b[i].id);
        VSM_ASSERT_EQ(a[i].endTick, b[i].endTick);
    }
}

// D554.3 — LES PÉDALES EN DURÉES : tenue jusqu'au relâchement, bornée par la frappe suivante de même hauteur ; une
// note finie pédale levée intacte ; une pédale jamais relâchée tient jusqu'à la fin du matériau, comptée à part.
VSM_TEST(pedals_become_note_lengths_and_leave) {
    std::vector<Note> notes;
    uint64_t ids = 1;
    const auto ajoute = [&](Tick d, Tick f, uint8_t h) {
        Note n; n.startTick = d; n.endTick = f; n.number = h; n.id = ids++; notes.push_back(n); };
    ajoute(0, 240, 60);       // sous la pédale [0, 1440) : jusqu'à la re-frappe du 60 à 960
    ajoute(480, 720, 64);     // jusqu'au relâchement, 1440
    ajoute(960, 1200, 60);    // jusqu'au relâchement, 1440
    ajoute(1920, 2160, 67);   // pédale levée : intacte
    ajoute(2880, 3000, 69);   // pédale enfoncée à 2880, jamais relâchée : jusqu'à la fin du matériau
    ajoute(3600, 3700, 71);   // idem
    std::vector<CcPoint> ccs = {{0, 0, 64, 127}, {100, 0, 7, 90}, {1440, 0, 64, 0}, {2880, 0, 64, 100}};
    VSM_ASSERT_EQ(countPedalPresses(ccs), size_t(2));
    const auto r = pedalsToNoteLength(notes, ccs, 3840);
    std::vector<std::pair<Tick, Tick>> v;
    for (const auto& n : notes) v.emplace_back(n.startTick, n.endTick);
    VSM_ASSERT(v == (std::vector<std::pair<Tick, Tick>>{{0, 960}, {480, 1440}, {960, 1440}, {1920, 2160}, {2880, 3840},
                                                         {3600, 3840}}));
    VSM_ASSERT_EQ(r.lengthened, size_t(5));
    VSM_ASSERT_EQ(r.heldToEnd, size_t(2));
    VSM_ASSERT_EQ(r.pedalEvents, size_t(3));
    VSM_ASSERT_EQ(ccs.size(), size_t(1));   // le CC 7 reste
    VSM_ASSERT_EQ(static_cast<int>(ccs[0].controller), 7);
    VSM_ASSERT_EQ(countPedalPresses(ccs), size_t(0));
}

// D554.3 — À TICK ÉGAL, la fin de note passe AVANT le CC 64 (le planning pousse les notes avant les contrôleurs, et
// trie de façon stable) : une pédale enfoncée au tick même où une note finit ne la tient pas.
VSM_TEST(a_pedal_pressed_on_the_very_tick_a_note_ends_does_not_hold_it) {
    std::vector<Note> notes(1);
    notes[0].startTick = 0; notes[0].endTick = 480; notes[0].number = 60; notes[0].id = 1;
    std::vector<CcPoint> ccs = {{480, 0, 64, 127}, {960, 0, 64, 0}};
    const auto r = pedalsToNoteLength(notes, ccs, 1920);
    VSM_ASSERT_EQ(r.lengthened, size_t(0));
    VSM_ASSERT_EQ(notes[0].endTick, Tick(480));
    VSM_ASSERT_EQ(r.pedalEvents, size_t(2));
}

// D556.3 — LES NOTES DE MÊME NOM : un do choisi appelle tous les do, à toutes les octaves, et rien d'autre ; la
// sélection s'étend (un mi déjà choisi reste, et appelle ses mi).
VSM_TEST(selecting_equal_pitch_in_all_octaves_takes_every_note_of_that_name) {
    std::vector<Note> notes;
    uint64_t id = 1;
    for (uint8_t h : {60, 72, 48, 61, 64, 76, 67}) {
        Note n; n.startTick = 0; n.endTick = 480; n.number = h; n.id = id++; notes.push_back(n);
    }
    const auto dos = selectNotesWithSamePitchClass(notes, NoteSelection{1});
    VSM_ASSERT((dos == NoteSelection{1, 2, 3}));   // 60, 72, 48 — pas 61
    const auto etendue = selectNotesWithSamePitchClass(notes, NoteSelection{1, 5});
    VSM_ASSERT((etendue == NoteSelection{1, 2, 3, 5, 6}));   // les do et les mi
    VSM_ASSERT(selectNotesWithSamePitchClass(notes, NoteSelection{}).empty());
}

// D570.3 — LA COULEUR DES NOTES : une teinte par nom de note (toutes octaves), par canal, ou celle de la piste.
VSM_TEST(note_hue_follows_the_pitch_class_or_the_channel) {
    Note do4; do4.number = 60; do4.channel = 0;
    Note do5; do5.number = 72; do5.channel = 9;
    Note sol4; sol4.number = 67; sol4.channel = 0;
    VSM_ASSERT_NEAR(noteHue(NoteColorMode::Pitch, do4), 0.0f, 1e-9);
    VSM_ASSERT_NEAR(noteHue(NoteColorMode::Pitch, do5), noteHue(NoteColorMode::Pitch, do4), 1e-9);   // même nom
    VSM_ASSERT_NEAR(noteHue(NoteColorMode::Pitch, sol4), 7.0f / 12.0f, 1e-6);
    VSM_ASSERT_NEAR(noteHue(NoteColorMode::Channel, do5), 9.0f / 16.0f, 1e-6);
    VSM_ASSERT_NEAR(noteHue(NoteColorMode::Channel, do4), noteHue(NoteColorMode::Channel, sol4), 1e-9);   // même canal
    VSM_ASSERT_NEAR(noteHue(NoteColorMode::Track, sol4), -1.0f, 1e-9);
}

