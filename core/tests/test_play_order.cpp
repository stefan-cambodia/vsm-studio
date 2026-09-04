#include "TestFramework.h"
#include "vsm/sequencer/AutomationEdit.h"
#include "vsm/sequencer/PlaybackScheduler.h"
#include "vsm/sequencer/PlayOrder.h"
#include <algorithm>
#include <vector>

using namespace vsm::sequencer;
using vsm::midi::Tick;

// D18.4 de docs/ROADMAP-daw.md — L'ORDRE DE JEU.
//
// Une section n'est pas un objet de plus : elle se DÉDUIT des repères, de
// celui-ci jusqu'au suivant, parce que c'est déjà ainsi qu'on s'en sert. Seul
// l'ORDRE est nouveau, et il tient dans une liste d'entiers.

namespace {

/// Deux sections d'une mesure : « A » avec un do, « B » avec un sol.
Project projetAB() {
    Project project;
    project.ticksPerQuarterNote = 480;
    project.tempoMap.addTempoChange(0, 500000);          // 120 BPM
    Track piste;
    uint64_t ids = 1;
    piste.addNote(0, 480, 60, 100, 0, ids);              // dans A
    piste.addNote(1920, 2400, 67, 100, 0, ids);          // dans B
    project.tracks.push_back(piste);
    project.markers.push_back({0, "A"});
    project.markers.push_back({1920, "B"});
    return project;
}

std::vector<int> hauteursJouees(const Project& project) {
    std::vector<std::pair<double, int>> evenements;
    for (const auto& e : PlaybackScheduler::build(project, 0, 1000000))
        if (const auto* on = std::get_if<vsm::midi::NoteOnEvent>(&e.data))
            evenements.emplace_back(e.timeSeconds, static_cast<int>(on->note));
    std::stable_sort(evenements.begin(), evenements.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<int> hauteurs;
    for (const auto& [t, n] : evenements) hauteurs.push_back(n);
    return hauteurs;
}

} // namespace

VSM_TEST(sections_are_read_from_the_markers_and_the_last_runs_to_the_material) {
    const auto sections = sectionsFromMarkers(projetAB());
    VSM_ASSERT_EQ(sections.size(), size_t(2));
    VSM_ASSERT_EQ(sections[0].name, std::string("A"));
    VSM_ASSERT_EQ(sections[0].startTick, Tick(0));
    VSM_ASSERT_EQ(sections[0].endTick, Tick(1920));
    VSM_ASSERT_EQ(sections[1].name, std::string("B"));
    VSM_ASSERT_EQ(sections[1].endTick, Tick(2400));      // jusqu'au bout du matériau

    // Un repère posé APRÈS tout le matériau ne fait pas de section : elle
    // serait vide, et une section vide ne se voit qu'à ce qu'elle ne fait rien.
    Project tardif = projetAB();
    tardif.markers.push_back({9600, "Coda"});
    VSM_ASSERT_EQ(sectionsFromMarkers(tardif).size(), size_t(2));

    // Sans repère, aucune section : « aplatir » sur un projet sans repère se
    // contenterait sinon de le recopier en donnant l'impression de travailler.
    Project nu = projetAB();
    nu.markers.clear();
    VSM_ASSERT(sectionsFromMarkers(nu).empty());
}

VSM_TEST(flattening_A_A_B_gives_a_project_whose_schedule_is_what_one_would_hear) {
    // LE CRITÈRE DE L'ÉTAPE : le résultat n'est pas « un ordre de jeu », c'est
    // un projet ordinaire dont le PLANNING est celui qu'on entendrait.
    Project project = projetAB();
    VSM_ASSERT(flattenPlayOrder(project, {0, 0, 1}));

    const auto hauteurs = hauteursJouees(project);
    VSM_ASSERT_EQ(hauteurs.size(), size_t(3));
    VSM_ASSERT_EQ(hauteurs[0], 60);      // A
    VSM_ASSERT_EQ(hauteurs[1], 60);      // A encore
    VSM_ASSERT_EQ(hauteurs[2], 67);      // puis B

    // Et les notes sont là où l'ordre les met : A dure une mesure, donc le
    // second A commence à 1920 et B à 3840.
    const auto& notes = project.tracks[0].notes;
    VSM_ASSERT_EQ(notes.size(), size_t(3));
    VSM_ASSERT_EQ(notes[0].startTick, Tick(0));
    VSM_ASSERT_EQ(notes[1].startTick, Tick(1920));
    VSM_ASSERT_EQ(notes[2].startTick, Tick(3840));
    // DES IDENTIFIANTS NEUFS : deux copies de la même note ne peuvent pas
    // partager le sien.
    VSM_ASSERT(notes[0].id != notes[1].id);
}

VSM_TEST(the_markers_follow_so_the_flattened_song_can_still_be_read) {
    Project project = projetAB();
    VSM_ASSERT(flattenPlayOrder(project, {1, 0}));
    VSM_ASSERT_EQ(project.markers.size(), size_t(2));
    VSM_ASSERT_EQ(project.markers[0].name, std::string("B"));
    VSM_ASSERT_EQ(project.markers[0].tick, Tick(0));
    VSM_ASSERT_EQ(project.markers[1].name, std::string("A"));
    VSM_ASSERT_EQ(project.markers[1].tick, Tick(480));   // B fait 480 ticks
}

VSM_TEST(a_note_that_would_overrun_its_section_is_cut_at_its_end) {
    // Laissée entière, elle empiéterait sur la section suivante, que personne
    // n'a arrangée ainsi.
    Project project;
    project.ticksPerQuarterNote = 480;
    project.tempoMap.addTempoChange(0, 500000);
    Track piste;
    uint64_t ids = 1;
    piste.addNote(1440, 2880, 60, 100, 0, ids);   // commence dans A, finit dans B
    project.tracks.push_back(piste);
    project.markers.push_back({0, "A"});
    project.markers.push_back({1920, "B"});

    VSM_ASSERT(flattenPlayOrder(project, {0}));
    VSM_ASSERT_EQ(project.tracks[0].notes.size(), size_t(1));
    VSM_ASSERT_EQ(project.tracks[0].notes[0].endTick, Tick(1920));   // coupée au bord
}

VSM_TEST(an_empty_or_invalid_order_leaves_the_project_completely_alone) {
    Project temoin = projetAB();
    Project essai = projetAB();
    VSM_ASSERT(!flattenPlayOrder(essai, {}));
    VSM_ASSERT(!flattenPlayOrder(essai, {7, -3}));
    VSM_ASSERT_EQ(essai.tracks[0].notes.size(), temoin.tracks[0].notes.size());
    VSM_ASSERT_EQ(essai.markers.size(), temoin.markers.size());
    VSM_ASSERT_EQ(essai.tracks[0].notes[1].startTick, temoin.tracks[0].notes[1].startTick);
}

VSM_TEST(the_automation_follows_each_slot_and_gets_its_value_at_the_join) {
    // Sans un point au début du créneau, un créneau qui commence au milieu
    // d'un fondu hériterait de la valeur du créneau précédent, et le paramètre
    // sauterait au raccord.
    Project project = projetAB();
    AutomationCurve courbe;
    courbe.parameter = "mix.volume";
    courbe.points = {{0, 0.0f, false, 0.0f}, {1920, 1.0f, false, 0.0f}, {2400, 0.5f, false, 0.0f}};
    project.tracks[0].automation.push_back(courbe);

    VSM_ASSERT(flattenPlayOrder(project, {1, 0}));
    const auto& c = project.tracks[0].automation[0];
    VSM_ASSERT(!c.points.empty());
    // Le créneau 0 est la section B, qui commençait à 1920 où la courbe valait
    // 1,0 : le raccord doit donc valoir 1,0 au tick 0.
    VSM_ASSERT_NEAR(automationValueAt(c, 0), 1.0f, 1e-6f);
}

VSM_TEST(a_song_with_two_tempi_says_that_flattening_will_leave_the_map_behind) {
    Project simple = projetAB();
    VSM_ASSERT(!flattenChangesTempoMeaning(simple));
    simple.tempoMap.addTempoChange(1920, 1000000);
    VSM_ASSERT(flattenChangesTempoMeaning(simple));
}

VSM_TEST(a_project_made_only_of_audio_clips_still_has_sections) {
    // LA LEÇON DE D8.3, REPAYÉE ICI : `lastUsedTick()` ne connaît que le
    // matériau MIDI. S'en servir pour borner la dernière section faisait
    // qu'une reconstruction faite de clips AUDIO n'avait aucune section
    // au-delà de son dernier repère — c'est-à-dire, le plus souvent, aucune.
    Project project;
    project.ticksPerQuarterNote = 480;
    project.tempoMap.addTempoChange(0, 500000);
    Track piste;
    piste.kind = Track::Kind::Audio;
    piste.audio.path = "prise.wav";
    piste.audio.sampleRate = 48000.0;
    piste.audio.frames = 480000;          // dix secondes
    piste.audio.channels = 2;
    Clip clip;
    clip.id = 1;
    clip.sourceStart = 0; clip.sourceLength = 7680;
    clip.startTick = 0;   clip.length = 7680;
    piste.clips.push_back(clip);
    project.tracks.push_back(piste);      // AUCUNE note
    project.markers.push_back({0, "A"});
    project.markers.push_back({1920, "B"});

    const auto sections = sectionsFromMarkers(project);
    VSM_ASSERT_EQ(sections.size(), size_t(2));
    VSM_ASSERT_EQ(sections[0].endTick, Tick(1920));
    VSM_ASSERT(sections[1].endTick >= Tick(7680));   // jusqu'au bout de ce qui SONNE

    // Et l'aplatissement transporte bien les clips.
    VSM_ASSERT(flattenPlayOrder(project, {1, 0}));
    VSM_ASSERT_EQ(project.tracks[0].clips.size(), size_t(2));
    VSM_ASSERT_EQ(project.tracks[0].clips[0].startTick, Tick(0));
}
