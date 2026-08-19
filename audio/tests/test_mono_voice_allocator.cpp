#include "TestFramework.h"
#include "vsm/audio/engine/MonoVoiceAllocator.h"

using namespace vsm::audio::engine;

VSM_TEST(mono_allocator_first_note_plays_and_retriggers) {
    MonoVoiceAllocator alloc;
    auto r = alloc.noteOn(60, 100);
    VSM_ASSERT(r.shouldPlay);
    VSM_ASSERT_EQ(r.note, static_cast<uint8_t>(60));
    VSM_ASSERT(r.retrigger);
}

VSM_TEST(mono_allocator_second_note_takes_priority_default_mode) {
    MonoVoiceAllocator alloc; // legato désactivé par défaut
    alloc.noteOn(60, 100);
    auto r = alloc.noteOn(64, 110);

    VSM_ASSERT(r.shouldPlay);
    VSM_ASSERT_EQ(r.note, static_cast<uint8_t>(64));
    VSM_ASSERT(r.retrigger); // comportement Model D par défaut : toujours retrigger
}

VSM_TEST(mono_allocator_release_falls_back_to_previous_held_note) {
    MonoVoiceAllocator alloc;
    alloc.noteOn(60, 100);
    alloc.noteOn(64, 110);
    auto r = alloc.noteOff(64); // relâche la note active -> retombe sur 60

    VSM_ASSERT(r.shouldPlay);
    VSM_ASSERT_EQ(r.note, static_cast<uint8_t>(60));
}

VSM_TEST(mono_allocator_release_last_note_stops_playing) {
    MonoVoiceAllocator alloc;
    alloc.noteOn(60, 100);
    auto r = alloc.noteOff(60);
    VSM_ASSERT(!r.shouldPlay);
    VSM_ASSERT(!alloc.hasHeldNotes());
}

VSM_TEST(mono_allocator_trill_sequence) {
    // Joue un accord tenu, relâche dans le désordre : doit "triller" entre
    // les notes encore tenues plutôt que de couper le son prématurément.
    MonoVoiceAllocator alloc;
    alloc.noteOn(60, 100);
    alloc.noteOn(64, 100);
    alloc.noteOn(67, 100);
    VSM_ASSERT_EQ(alloc.heldNoteCount(), static_cast<size_t>(3));

    auto r1 = alloc.noteOff(67); // relâche la plus haute -> retombe sur 64
    VSM_ASSERT(r1.shouldPlay);
    VSM_ASSERT_EQ(r1.note, static_cast<uint8_t>(64));

    auto r2 = alloc.noteOff(60); // relâche 60 (pas la note active) -> reste sur 64
    VSM_ASSERT(r2.shouldPlay);
    VSM_ASSERT_EQ(r2.note, static_cast<uint8_t>(64));

    auto r3 = alloc.noteOff(64); // dernière note tenue -> silence
    VSM_ASSERT(!r3.shouldPlay);
}

VSM_TEST(mono_allocator_legato_mode_suppresses_retrigger_on_overlap) {
    MonoVoiceAllocator alloc;
    alloc.setLegatoMode(true);

    auto r1 = alloc.noteOn(60, 100);
    VSM_ASSERT(r1.retrigger); // première note : toujours un retrigger, rien n'était tenu avant

    auto r2 = alloc.noteOn(64, 100); // 60 encore tenue -> pas de retrigger en legato
    VSM_ASSERT(!r2.retrigger);
    VSM_ASSERT_EQ(r2.note, static_cast<uint8_t>(64));

    auto r3 = alloc.noteOff(64); // retombe sur 60, toujours pas de retrigger en legato
    VSM_ASSERT(r3.shouldPlay);
    VSM_ASSERT_EQ(r3.note, static_cast<uint8_t>(60));
    VSM_ASSERT(!r3.retrigger);
}

VSM_TEST(mono_allocator_repeated_note_on_same_pitch_does_not_duplicate_stack) {
    MonoVoiceAllocator alloc;
    alloc.noteOn(60, 100);
    alloc.noteOn(60, 120); // même hauteur, ré-enfoncée sans note-off MIDI intermédiaire
    VSM_ASSERT_EQ(alloc.heldNoteCount(), static_cast<size_t>(1));
}

VSM_TEST(mono_allocator_reset_clears_held_notes) {
    MonoVoiceAllocator alloc;
    alloc.noteOn(60, 100);
    alloc.noteOn(64, 100);
    alloc.reset();
    VSM_ASSERT(!alloc.hasHeldNotes());
}
