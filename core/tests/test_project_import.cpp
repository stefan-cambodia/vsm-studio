#include "TestFramework.h"
#include "vsm/sequencer/ProjectImport.h"

using namespace vsm::midi;
using namespace vsm::sequencer;

// D14.3 — IMPORTER UN MIDI SUR DE NOUVELLES PISTES : les pistes s'ajoutent à
// la suite, converties à la résolution du projet, posées à la tête de lecture,
// avec des identifiants neufs ; le tempo de la source est ignoré et compté.

VSM_TEST(imported_tracks_are_appended_rescaled_shifted_and_renumbered) {
    Project dest;
    dest.ticksPerQuarterNote = 480;
    Track existante;
    existante.name = "Basse";
    existante.notes.push_back({0, 480, 0, 40, 100, 64, dest.nextNoteId()});
    dest.tracks.push_back(existante);

    Project source;
    source.ticksPerQuarterNote = 960;            // deux fois plus fin
    Track a;
    a.name = "Mélodie";
    a.notes.push_back({1920, 2880, 0, 60, 90, 64, 1});   // à 1920/960 = une noire… en ticks source
    a.notes.push_back({2880, 3840, 0, 62, 90, 64, 2});
    a.controlChanges.push_back({1920, 0, 7, 100});
    source.tracks.push_back(a);
    Track b;
    b.name = "Accords";
    b.notes.push_back({3840, 7680, 0, 48, 80, 64, 3});
    source.tracks.push_back(b);
    source.tempoMap.addTempoChange(1000, 400000);

    const ImportOutcome bilan = appendTracksFrom(dest, source, 3840);   // à la mesure 3 du projet
    VSM_ASSERT_EQ(bilan.tracksAdded, size_t(2));
    VSM_ASSERT_EQ(bilan.notesAdded, size_t(3));
    VSM_ASSERT_EQ(bilan.tempoChangesIgnored, size_t(1));
    VSM_ASSERT_EQ(dest.tracks.size(), size_t(3));
    // Le premier événement de la source (1920 ticks source = 960 ticks
    // projet) est posé à 3840 ; la suite garde ses écarts, divisés par deux.
    const auto& m = dest.tracks[1].notes;
    VSM_ASSERT_EQ(m[0].startTick, Tick(3840));
    VSM_ASSERT_EQ(m[0].endTick, Tick(3840 + 480));
    VSM_ASSERT_EQ(m[1].startTick, Tick(3840 + 480));
    VSM_ASSERT_EQ(dest.tracks[1].controlChanges[0].tick, Tick(3840));
    VSM_ASSERT_EQ(dest.tracks[2].notes[0].startTick, Tick(3840 + 960));
    VSM_ASSERT_EQ(dest.tracks[2].notes[0].endTick, Tick(3840 + 960 + 1920));
    // Les identifiants sont neufs et uniques, et le projet le sait.
    VSM_ASSERT(m[0].id != dest.tracks[0].notes[0].id);
    VSM_ASSERT(m[0].id != m[1].id && m[1].id != dest.tracks[2].notes[0].id);
    VSM_ASSERT(dest.peekNextNoteId() > dest.tracks[2].notes[0].id);
    // Le tempo du projet n'a pas bougé.
    VSM_ASSERT_EQ(dest.tempoMap.changes().size(), size_t(1));
}

VSM_TEST(importing_an_empty_source_adds_nothing) {
    Project dest, source;
    const ImportOutcome bilan = appendTracksFrom(dest, source, 0);
    VSM_ASSERT_EQ(bilan.tracksAdded, size_t(0));
    VSM_ASSERT(dest.tracks.empty());
}
