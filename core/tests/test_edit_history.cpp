#include "TestFramework.h"
#include "vsm/sequencer/EditHistory.h"
#include "vsm/sequencer/NoteEdit.h"

using namespace vsm::sequencer;

namespace {
std::vector<Note> twoNotes() {
    std::vector<Note> notes;
    Note a; a.startTick = 0; a.endTick = 480; a.number = 60; a.id = 1; notes.push_back(a);
    Note b; b.startTick = 480; b.endTick = 960; b.number = 64; b.id = 2; notes.push_back(b);
    return notes;
}
} // namespace

VSM_TEST(history_starts_empty) {
    EditHistory history;
    VSM_ASSERT(!history.canUndo());
    VSM_ASSERT(!history.canRedo());
    std::vector<Note> notes = twoNotes();
    VSM_ASSERT(!history.undo(notes));
    VSM_ASSERT(!history.redo(notes));
}

VSM_TEST(history_undoes_and_redoes_an_edit) {
    EditHistory history;
    std::vector<Note> notes = twoNotes();

    history.beginEdit(notes, "Transposer");
    transposeNotes(notes, selectAllNotes(notes), 12);
    VSM_ASSERT_EQ(static_cast<int>(notes[0].number), 72);

    VSM_ASSERT(history.canUndo());
    VSM_ASSERT_EQ(history.undoLabel(), std::string("Transposer"));
    VSM_ASSERT(history.undo(notes));
    VSM_ASSERT_EQ(static_cast<int>(notes[0].number), 60);

    VSM_ASSERT(history.canRedo());
    VSM_ASSERT(history.redo(notes));
    VSM_ASSERT_EQ(static_cast<int>(notes[0].number), 72);
}

VSM_TEST(history_handles_many_successive_edits) {
    EditHistory history;
    std::vector<Note> notes = twoNotes();
    for (int i = 0; i < 5; ++i) {
        history.beginEdit(notes, "Transposer");
        transposeNotes(notes, selectAllNotes(notes), 1);
    }
    VSM_ASSERT_EQ(static_cast<int>(notes[0].number), 65);
    VSM_ASSERT_EQ(history.undoDepth(), size_t{5});
    for (int i = 0; i < 5; ++i) VSM_ASSERT(history.undo(notes));
    VSM_ASSERT_EQ(static_cast<int>(notes[0].number), 60);
    VSM_ASSERT(!history.canUndo());
}

VSM_TEST(new_edit_after_undo_clears_the_redo_branch) {
    // Comportement standard des éditeurs : après avoir annulé puis fait autre
    // chose, on ne peut plus "rétablir" l'ancienne branche -- elle n'existe
    // plus. Sans ça, le rétablir réappliquerait un état incohérent.
    EditHistory history;
    std::vector<Note> notes = twoNotes();

    history.beginEdit(notes, "Transposer");
    transposeNotes(notes, selectAllNotes(notes), 12);
    history.undo(notes);
    VSM_ASSERT(history.canRedo());

    history.beginEdit(notes, "Vélocité");
    setVelocity(notes, selectAllNotes(notes), 30);
    VSM_ASSERT(!history.canRedo());
}

VSM_TEST(history_respects_its_depth_limit) {
    EditHistory history(3);
    std::vector<Note> notes = twoNotes();
    for (int i = 0; i < 10; ++i) {
        history.beginEdit(notes, "Transposer");
        transposeNotes(notes, selectAllNotes(notes), 1);
    }
    VSM_ASSERT_EQ(history.undoDepth(), size_t{3});
    // On ne peut remonter que de 3 pas : 70 -> 67, pas jusqu'à 60.
    while (history.canUndo()) history.undo(notes);
    VSM_ASSERT_EQ(static_cast<int>(notes[0].number), 67);
}

VSM_TEST(history_restores_deletions_and_insertions_not_just_values) {
    EditHistory history;
    std::vector<Note> notes = twoNotes();

    history.beginEdit(notes, "Supprimer");
    notes.clear();
    VSM_ASSERT(notes.empty());
    history.undo(notes);
    VSM_ASSERT_EQ(notes.size(), size_t{2});
    VSM_ASSERT_EQ(static_cast<int>(notes[1].number), 64);

    history.beginEdit(notes, "Insérer accord");
    uint64_t idCounter = 100;
    insertChord(notes, 0, 480, 60, ChordType::Major, 0, 100, idCounter);
    VSM_ASSERT_EQ(notes.size(), size_t{5});
    history.undo(notes);
    VSM_ASSERT_EQ(notes.size(), size_t{2});
}

VSM_TEST(history_clear_drops_both_stacks) {
    EditHistory history;
    std::vector<Note> notes = twoNotes();
    history.beginEdit(notes, "Transposer");
    transposeNotes(notes, selectAllNotes(notes), 1);
    history.undo(notes);
    history.clear();
    VSM_ASSERT(!history.canUndo());
    VSM_ASSERT(!history.canRedo());
}
