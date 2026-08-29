#include "TestFramework.h"
#include "vsm/sequencer/PlaybackScheduler.h"
#include "vsm/sequencer/Project.h"
#include <algorithm>

using namespace vsm::sequencer;
using vsm::midi::Tick;

// D1 de docs/ROADMAP-daw.md — LE CLIP DANS LE MODÈLE.
//
// Un clip est une RÉGION du matériau de la piste, posée sur la ligne de temps ;
// il ne l'emporte pas. Voir `Clip` dans Track.h pour le raisonnement complet et
// pour le modèle écarté (le clip-conteneur, façon Ableton).

namespace {

/// Quatre noires, une par temps, sur une piste.
Project quatreNotes() {
    Project project;
    project.ticksPerQuarterNote = 480;
    project.tempoMap.addTempoChange(0, 500000);   // 120 BPM : une noire = 0,5 s
    Track track;
    track.name = "Essai";
    uint64_t ids = 1;
    for (int i = 0; i < 4; ++i)
        track.addNote(480 * i, 480 * i + 240, static_cast<uint8_t>(60 + i), 100, 0, ids);
    project.tracks.push_back(track);
    return project;
}

std::vector<double> departsDeNotes(const Project& project) {
    std::vector<double> departs;
    for (const auto& event : PlaybackScheduler::build(project, 0, 100000))
        if (std::holds_alternative<vsm::midi::NoteOnEvent>(event.data))
            departs.push_back(event.timeSeconds);
    std::sort(departs.begin(), departs.end());
    return departs;
}

} // namespace

VSM_TEST(a_track_without_a_clip_plays_exactly_what_it_always_played) {
    // LE CRITÈRE DE LA PHASE, à l'échelle du planificateur : introduire les
    // clips ne doit pas déplacer un seul événement. Le témoin est une piste
    // découpée par un clip IDENTITÉ -- même fenêtre, même position : les deux
    // doivent produire exactement la même chose, événement par événement.
    const Project nu = quatreNotes();

    Project decoupe = nu;
    Clip identite;
    identite.sourceStart = 0;
    identite.sourceLength = 1920;
    identite.startTick = 0;
    identite.length = 1920;
    decoupe.tracks[0].clips.push_back(identite);

    const auto a = PlaybackScheduler::build(nu, 0, 100000);
    const auto b = PlaybackScheduler::build(decoupe, 0, 100000);
    VSM_ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        VSM_ASSERT_NEAR(a[i].timeSeconds, b[i].timeSeconds, 1e-12);
        VSM_ASSERT_EQ(a[i].trackIndex, b[i].trackIndex);
        VSM_ASSERT_EQ(a[i].data.index(), b[i].data.index());
    }
}

VSM_TEST(a_clip_moves_the_music_without_moving_a_note) {
    Project project = quatreNotes();
    Clip clip;
    clip.sourceStart = 0;
    clip.sourceLength = 1920;
    clip.startTick = 1920;      // décalé d'une mesure
    project.tracks[0].clips.push_back(clip);

    const auto departs = departsDeNotes(project);
    VSM_ASSERT_EQ(departs.size(), size_t(4));
    VSM_ASSERT_NEAR(departs[0], 2.0, 1e-9);   // une mesure à 120 BPM = 2 s
    VSM_ASSERT_NEAR(departs[3], 3.5, 1e-9);

    // ET LE MATÉRIAU N'A PAS BOUGÉ : c'est tout l'intérêt du modèle de région.
    // Le piano roll, l'historique d'annulation et les identifiants de notes
    // continuent de voir exactement les mêmes ticks qu'avant.
    VSM_ASSERT_EQ(project.tracks[0].notes[0].startTick, Tick(0));
    VSM_ASSERT_EQ(project.tracks[0].notes[3].startTick, Tick(1440));
}

VSM_TEST(a_clip_longer_than_its_window_repeats_it_without_copying_a_note) {
    Project project = quatreNotes();
    Clip boucle;
    boucle.sourceStart = 0;
    boucle.sourceLength = 960;   // les deux premières noires
    boucle.startTick = 0;
    boucle.length = 2880;        // trois fois la fenêtre
    project.tracks[0].clips.push_back(boucle);

    const auto departs = departsDeNotes(project);
    VSM_ASSERT_EQ(departs.size(), size_t(6));       // 2 notes x 3 tours
    VSM_ASSERT_NEAR(departs[0], 0.0, 1e-9);
    VSM_ASSERT_NEAR(departs[2], 1.0, 1e-9);         // début du deuxième tour
    VSM_ASSERT_NEAR(departs[4], 2.0, 1e-9);         // début du troisième

    // Aucune note n'a été dupliquée dans le modèle : la répétition est un
    // décalage, pas une copie.
    VSM_ASSERT_EQ(project.tracks[0].notes.size(), size_t(4));
}

VSM_TEST(two_clips_on_the_same_material_share_it_by_construction) {
    // D1.2 : « un même clip placé deux fois ne duplique pas ses notes ; éditer
    // l'un modifie l'autre ». Ici il n'y a rien à partager explicitement --
    // deux fenêtres sur le même matériau LISENT les mêmes notes.
    Project project = quatreNotes();
    Clip premier;
    premier.sourceStart = 0; premier.sourceLength = 960; premier.startTick = 0;
    Clip second = premier;
    second.startTick = 1920;
    project.tracks[0].clips = {premier, second};

    VSM_ASSERT_EQ(departsDeNotes(project).size(), size_t(4));   // 2 notes, 2 endroits

    // On modifie LE MATÉRIAU : les deux clips changent ensemble.
    project.tracks[0].notes[0].muted = true;
    VSM_ASSERT_EQ(departsDeNotes(project).size(), size_t(2));
}

VSM_TEST(a_note_crossing_the_end_of_a_clip_is_cut_there_not_left_hanging) {
    Project project = quatreNotes();
    // Une note longue, qui dépasse la fenêtre.
    uint64_t ids = project.peekNextNoteId();
    project.tracks[0].notes.clear();
    project.tracks[0].addNote(0, 1920, 60, 100, 0, ids);
    project.ensureNoteIdAbove(ids);

    Clip court;
    court.sourceStart = 0; court.sourceLength = 480; court.startTick = 0; court.length = 480;
    project.tracks[0].clips.push_back(court);

    const auto events = PlaybackScheduler::build(project, 0, 100000);
    double debut = -1.0, fin = -1.0;
    for (const auto& e : events) {
        if (std::holds_alternative<vsm::midi::NoteOnEvent>(e.data)) debut = e.timeSeconds;
        if (std::holds_alternative<vsm::midi::NoteOffEvent>(e.data)) fin = e.timeSeconds;
    }
    VSM_ASSERT_NEAR(debut, 0.0, 1e-9);
    // Sans cette coupe, le NoteOff tomberait hors du clip et ne serait jamais
    // émis : la note resterait tenue pour toujours.
    VSM_ASSERT(fin > 0.0);
    VSM_ASSERT_NEAR(fin, 0.5, 1e-9);
}

VSM_TEST(a_muted_clip_is_silent_while_its_material_stays) {
    Project project = quatreNotes();
    Clip clip;
    clip.sourceStart = 0; clip.sourceLength = 1920; clip.startTick = 0;
    clip.muted = true;
    project.tracks[0].clips.push_back(clip);

    VSM_ASSERT(departsDeNotes(project).empty());
    VSM_ASSERT_EQ(project.tracks[0].notes.size(), size_t(4));
}

VSM_TEST(an_empty_window_is_skipped_instead_of_looping_forever) {
    // Un clip dont la fenêtre est vide et la durée non nulle ferait tourner la
    // boucle de répétition indéfiniment. Le cas est écarté explicitement.
    Project project = quatreNotes();
    Clip vide;
    vide.sourceStart = 5000;    // au-delà du matériau
    vide.sourceLength = 0;      // « jusqu'à la fin » : donc rien
    vide.startTick = 0;
    vide.length = 1920;
    project.tracks[0].clips.push_back(vide);

    VSM_ASSERT(departsDeNotes(project).empty());
}

// --- D1.4 : les repères, promus au rang d'entités --------------------------

VSM_TEST(a_marker_read_from_a_midi_file_becomes_an_entity_of_the_project) {
    // Ils étaient conservés en octets opaques dans `Track::miscEvents` : lus,
    // réexportés fidèlement, et invisibles pour le logiciel. Un fichier passait
    // donc à travers en gardant ses repères, sans que personne ne puisse les
    // voir ni en poser un.
    Project project = quatreNotes();
    project.markers.push_back({0, "Intro"});
    project.markers.push_back({960, "Refrain"});

    const Project relu = Project::fromParsedFile(project.toParsedFile());
    VSM_ASSERT_EQ(relu.markers.size(), size_t(2));
    VSM_ASSERT_EQ(relu.markers[0].tick, Tick(0));
    VSM_ASSERT_EQ(relu.markers[0].name, std::string("Intro"));
    VSM_ASSERT_EQ(relu.markers[1].tick, Tick(960));
    VSM_ASSERT_EQ(relu.markers[1].name, std::string("Refrain"));

    // ET ILS NE SE MULTIPLIENT PAS. Écrits sur chaque piste, ils seraient
    // multipliés par le nombre de pistes à chaque aller-retour -- une erreur
    // qui ne se voit qu'au troisième ou quatrième enregistrement.
    const Project deuxTours = Project::fromParsedFile(relu.toParsedFile());
    VSM_ASSERT_EQ(deuxTours.markers.size(), size_t(2));
}

VSM_TEST(markers_do_not_multiply_across_several_tracks) {
    Project project = quatreNotes();
    project.tracks.push_back(project.tracks[0]);   // une deuxième piste
    project.tracks.push_back(project.tracks[0]);   // une troisième
    project.markers.push_back({480, "Pont"});

    const Project relu = Project::fromParsedFile(project.toParsedFile());
    VSM_ASSERT_EQ(relu.markers.size(), size_t(1));
    VSM_ASSERT_EQ(relu.markers[0].name, std::string("Pont"));
}
