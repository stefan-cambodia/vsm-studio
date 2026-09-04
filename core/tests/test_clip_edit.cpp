#include "TestFramework.h"
#include "vsm/sequencer/AutomationEdit.h"
#include "vsm/sequencer/ClipEdit.h"
#include "vsm/sequencer/PlaybackScheduler.h"
#include "vsm/sequencer/Project.h"
#include <algorithm>
#include <vector>

using namespace vsm::midi;
using namespace vsm::sequencer;

// D5.1 de docs/ROADMAP-daw.md — LES GESTES DE LA VUE D'ARRANGEMENT.
//
// Ce qui les rend particuliers : un clip est une FENÊTRE sur le matériau de la
// piste, pas un conteneur qui l'emporte. Déplacer un clip ne déplace aucune
// note ; tirer son bord gauche ne le pousse pas, cela révèle ou masque du
// matériau par la tête. C'est ce que fait un éditeur de régions, et c'est ce
// qu'on attend quand on rogne le début d'une prise.

namespace {
/// Un clip d'une mesure posé à `start`, dont la fenêtre commence à `source`.
Clip clip(uint64_t id, Tick start, Tick longueur, Tick source = 0) {
    Clip c;
    c.id = id;
    c.startTick = start;
    c.length = longueur;
    c.sourceLength = longueur;
    c.sourceStart = source;
    return c;
}

/// 480 ticks par noire, 120 BPM : un tick vaut 1/960 s.
double enSecondes(Tick tick) { return static_cast<double>(tick) / 960.0; }
}

VSM_TEST(the_played_length_falls_back_from_length_to_window_to_material) {
    // Trois conventions se rencontrent ici plutôt que dans chaque appelant :
    // durée nulle = celle de la fenêtre, fenêtre nulle = jusqu'au bout.
    Clip c;
    c.length = 500;
    c.sourceLength = 300;
    VSM_ASSERT_EQ(clipPlayedLength(c, 10000), Tick(500));
    c.length = 0;
    VSM_ASSERT_EQ(clipPlayedLength(c, 10000), Tick(300));
    c.sourceLength = 0;
    c.sourceStart = 200;
    VSM_ASSERT_EQ(clipPlayedLength(c, 10000), Tick(9800));
}

VSM_TEST(moving_a_clip_moves_the_window_and_not_the_material) {
    std::vector<Clip> clips{clip(1, 1920, 1920, 480)};
    moveClips(clips, {1}, 960);
    VSM_ASSERT_EQ(clips[0].startTick, Tick(2880));
    // LA FENÊTRE N'A PAS BOUGÉ : le clip joue toujours le même passage du
    // matériau, posé ailleurs. C'est tout le modèle de la région.
    VSM_ASSERT_EQ(clips[0].sourceStart, Tick(480));
    VSM_ASSERT_EQ(clips[0].length, Tick(1920));
}

VSM_TEST(a_selection_that_hits_zero_keeps_its_shape) {
    // Si un seul clip butait sur le début du morceau pendant que les autres
    // continuaient, la sélection se déformerait -- ce ne serait plus la figure
    // qu'on a saisie.
    std::vector<Clip> clips{clip(1, 480, 480), clip(2, 2880, 480)};
    moveClips(clips, {1, 2}, -1920);
    VSM_ASSERT_EQ(clips[0].startTick, Tick(0));      // butée
    VSM_ASSERT_EQ(clips[1].startTick, Tick(2400));   // le MÊME décalage, -480
}

VSM_TEST(dragging_the_right_edge_reveals_material_and_never_reaches_zero) {
    std::vector<Clip> clips{clip(1, 0, 960)};
    resizeClipsEnd(clips, {1}, 480, 100000);
    VSM_ASSERT_EQ(clips[0].length, Tick(1440));
    VSM_ASSERT_EQ(clips[0].sourceLength, Tick(1440));   // la fenêtre suit

    resizeClipsEnd(clips, {1}, -100000, 100000);
    VSM_ASSERT_EQ(clips[0].length, Tick(1));            // un clip nul serait invisible
}

VSM_TEST(dragging_the_left_edge_trims_the_head_without_moving_what_remains) {
    // LE GESTE QUI DISTINGUE UNE RÉGION D'UNE BOÎTE : ce qui reste doit être
    // exactement là où il était sur la ligne de temps.
    std::vector<Clip> clips{clip(1, 1920, 1920, 0)};
    resizeClipsStart(clips, {1}, 480, 100000, enSecondes);

    VSM_ASSERT_EQ(clips[0].startTick, Tick(2400));      // le bord a avancé
    VSM_ASSERT_EQ(clips[0].sourceStart, Tick(480));     // la fenêtre aussi
    VSM_ASSERT_EQ(clips[0].length, Tick(1440));         // et la fin n'a pas bougé
    VSM_ASSERT_EQ(clips[0].startTick + clips[0].length, Tick(3840));
}

VSM_TEST(trimming_the_head_of_an_audio_clip_moves_its_window_in_seconds) {
    // Sans cela, rogner le début d'une prise DÉCALERAIT le son au lieu de le
    // rogner : le clip commencerait plus tard en jouant la même chose.
    std::vector<Clip> clips{clip(1, 0, 1920)};
    clips[0].sourceStartSeconds = 2.0;
    resizeClipsStart(clips, {1}, 960, 100000, enSecondes);
    // 960 ticks valent une seconde à 120 BPM.
    VSM_ASSERT_NEAR(clips[0].sourceStartSeconds, 3.0, 1e-9);
}

VSM_TEST(the_left_edge_stops_at_the_clip_and_never_crosses_it) {
    std::vector<Clip> clips{clip(1, 480, 960)};
    resizeClipsStart(clips, {1}, 100000, 100000, enSecondes);
    VSM_ASSERT_EQ(clips[0].length, Tick(1));
    resizeClipsStart(clips, {1}, -100000, 100000, enSecondes);
    VSM_ASSERT(clips[0].startTick >= 0);
}

VSM_TEST(splitting_gives_two_halves_that_play_exactly_the_original) {
    std::vector<Clip> clips{clip(1, 1920, 1920, 240)};
    uint64_t compteur = 100;
    VSM_ASSERT_EQ(splitClips(clips, {1}, 2880, 100000, compteur, enSecondes), size_t(1));
    VSM_ASSERT_EQ(clips.size(), size_t(2));

    // Première moitié : même début, même fenêtre, longueur jusqu'à la coupe.
    VSM_ASSERT_EQ(clips[0].startTick, Tick(1920));
    VSM_ASSERT_EQ(clips[0].sourceStart, Tick(240));
    VSM_ASSERT_EQ(clips[0].length, Tick(960));
    // Seconde : elle REPREND la fenêtre là où la première l'a laissée.
    VSM_ASSERT_EQ(clips[1].startTick, Tick(2880));
    VSM_ASSERT_EQ(clips[1].sourceStart, Tick(1200));
    VSM_ASSERT_EQ(clips[1].length, Tick(960));
    VSM_ASSERT_EQ(clips[1].id, uint64_t(100));
    // Bout à bout, elles couvrent exactement l'original.
    VSM_ASSERT_EQ(clips[0].startTick + clips[0].length, clips[1].startTick);
    VSM_ASSERT_EQ(clips[1].startTick + clips[1].length, Tick(3840));
}

VSM_TEST(splitting_an_audio_clip_carries_the_window_in_seconds_too) {
    std::vector<Clip> clips{clip(1, 0, 1920)};
    clips[0].sourceStartSeconds = 5.0;
    uint64_t compteur = 1;
    splitClips(clips, {1}, 960, 100000, compteur, enSecondes);
    VSM_ASSERT_EQ(clips.size(), size_t(2));
    VSM_ASSERT_NEAR(clips[0].sourceStartSeconds, 5.0, 1e-9);
    VSM_ASSERT_NEAR(clips[1].sourceStartSeconds, 6.0, 1e-9);   // une seconde plus loin
}

VSM_TEST(splitting_does_not_duplicate_the_fades) {
    // Recopier les fondus sur les deux moitiés ferait apparaître un TROU au
    // point de coupe : la première s'éteindrait pendant que la seconde monte.
    std::vector<Clip> clips{clip(1, 0, 1920)};
    clips[0].fadeInSeconds = 0.1;
    clips[0].fadeOutSeconds = 0.2;
    uint64_t compteur = 1;
    splitClips(clips, {1}, 960, 100000, compteur, enSecondes);
    VSM_ASSERT_NEAR(clips[0].fadeInSeconds, 0.1, 1e-9);   // l'entrée reste au début
    VSM_ASSERT_NEAR(clips[0].fadeOutSeconds, 0.0, 1e-9);
    VSM_ASSERT_NEAR(clips[1].fadeInSeconds, 0.0, 1e-9);
    VSM_ASSERT_NEAR(clips[1].fadeOutSeconds, 0.2, 1e-9);  // la sortie reste à la fin
}

VSM_TEST(cutting_on_an_edge_or_outside_does_nothing) {
    // Couper sur un bord ne produirait qu'un clip vide et un clip identique :
    // rien d'utile, et une annulation à faire.
    std::vector<Clip> clips{clip(1, 960, 960)};
    uint64_t compteur = 1;
    VSM_ASSERT_EQ(splitClips(clips, {1}, 960, 100000, compteur, enSecondes), size_t(0));
    VSM_ASSERT_EQ(splitClips(clips, {1}, 1920, 100000, compteur, enSecondes), size_t(0));
    VSM_ASSERT_EQ(splitClips(clips, {1}, 100, 100000, compteur, enSecondes), size_t(0));
    VSM_ASSERT_EQ(clips.size(), size_t(1));
}

VSM_TEST(an_empty_selection_does_nothing_at_all) {
    // Un raccourci déclenché par erreur sans sélection doit être sans effet,
    // pas « tout traiter » -- la même convention que pour les notes.
    std::vector<Clip> clips{clip(1, 960, 960)};
    const Clip avant = clips[0];
    uint64_t compteur = 1;
    moveClips(clips, {}, 480);
    resizeClipsEnd(clips, {}, 480, 100000);
    resizeClipsStart(clips, {}, 480, 100000, enSecondes);
    VSM_ASSERT_EQ(splitClips(clips, {}, 1200, 100000, compteur, enSecondes), size_t(0));
    VSM_ASSERT_EQ(clips.size(), size_t(1));
    VSM_ASSERT_EQ(clips[0].startTick, avant.startTick);
    VSM_ASSERT_EQ(clips[0].length, avant.length);
}

// --- D5.2 : dupliquer, et boucler par étirement -----------------------------

VSM_TEST(stretching_past_the_material_loops_instead_of_reading_further) {
    // « Boucle de clip par ÉTIREMENT » : tant qu'il reste du matériau, tirer le
    // bord droit en révèle davantage ; une fois au bout, la fenêtre ne peut
    // plus grandir et c'est la durée jouée qui continue. Un modificateur pour
    // « boucler » demanderait de savoir à l'avance où finit le matériau, ce que
    // personne ne sait en tirant.
    std::vector<Clip> clips{clip(1, 0, 960)};
    const Tick materiau = 1920;          // deux mesures de matériau seulement

    resizeClipsEnd(clips, {1}, 960, materiau);
    VSM_ASSERT_EQ(clips[0].length, Tick(1920));
    VSM_ASSERT_EQ(clips[0].sourceLength, Tick(1920));   // encore du matériau : on révèle

    resizeClipsEnd(clips, {1}, 1920, materiau);
    VSM_ASSERT_EQ(clips[0].length, Tick(3840));         // ce qui est joué double
    VSM_ASSERT_EQ(clips[0].sourceLength, Tick(1920));   // la fenêtre est au bout
    // Durée jouée > fenêtre : le clip RÉPÈTE, sans qu'une note ait été copiée.
    VSM_ASSERT(clips[0].length > clips[0].sourceLength);
}

VSM_TEST(a_window_that_starts_late_runs_out_of_material_sooner) {
    // La fenêtre disponible se compte DEPUIS le début de la fenêtre, pas depuis
    // zéro : un clip qui commence à la moitié du matériau ne peut en révéler
    // que la moitié.
    std::vector<Clip> clips{clip(1, 0, 480, 1440)};
    resizeClipsEnd(clips, {1}, 4800, 1920);
    VSM_ASSERT_EQ(clips[0].sourceLength, Tick(480));    // 1920 - 1440
    VSM_ASSERT_EQ(clips[0].length, Tick(5280));
}

VSM_TEST(duplicating_returns_the_copies_so_the_next_gesture_lands_on_them) {
    std::vector<Clip> clips{clip(1, 0, 960), clip(2, 1920, 960)};
    uint64_t compteur = 50;
    const ClipSelection copies = duplicateClips(clips, {1, 2}, 3840, compteur);

    VSM_ASSERT_EQ(clips.size(), size_t(4));
    VSM_ASSERT_EQ(copies.size(), size_t(2));
    // La sélection rendue est celle des COPIES : le geste suivant porte sur ce
    // qu'on vient de créer, comme dans le piano roll.
    for (uint64_t id : copies) VSM_ASSERT(id >= 50);

    Tick debut = 0, fin = 0;
    VSM_ASSERT(clipSelectionBounds(clips, copies, 100000, debut, fin));
    VSM_ASSERT_EQ(debut, Tick(3840));
    VSM_ASSERT_EQ(fin, Tick(6720));
}

VSM_TEST(a_duplicate_that_would_land_before_zero_is_clamped) {
    std::vector<Clip> clips{clip(1, 480, 960)};
    uint64_t compteur = 10;
    duplicateClips(clips, {1}, -4800, compteur);
    VSM_ASSERT_EQ(clips.size(), size_t(2));
    VSM_ASSERT(clips[0].startTick >= 0);
}

VSM_TEST(the_bounds_of_an_empty_selection_are_reported_as_absent) {
    std::vector<Clip> clips{clip(1, 480, 960)};
    Tick a = 0, b = 0;
    VSM_ASSERT(!clipSelectionBounds(clips, {}, 100000, a, b));
    uint64_t compteur = 1;
    VSM_ASSERT(duplicateClips(clips, {}, 960, compteur).empty());
    VSM_ASSERT_EQ(clips.size(), size_t(1));
}

// --- D5.6 : fondus, gain et phase sur le clip -------------------------------

VSM_TEST(a_fade_is_measured_in_seconds_and_pulled_from_the_corner) {
    // Le fondu suit le SON, pas le tempo : accélérer un morceau ne doit pas
    // raccourcir ses fondus. D'où des secondes dans le modèle, et une
    // conversion à l'entrée.
    std::vector<Clip> clips{clip(1, 960, 1920)};
    setClipFadeIn(clips, 1, 1920, 100000, enSecondes);   // 960 ticks = 1 s
    VSM_ASSERT_NEAR(clips[0].fadeInSeconds, 1.0, 1e-9);

    setClipFadeOut(clips, 1, 2400, 100000, enSecondes);  // 480 ticks avant la fin
    VSM_ASSERT_NEAR(clips[0].fadeOutSeconds, 0.5, 1e-9);
}

VSM_TEST(a_fade_never_runs_past_the_clip) {
    // Au-delà, il mangerait ce qui vient après et ne s'entendrait plus comme un
    // fondu.
    std::vector<Clip> clips{clip(1, 0, 960)};
    setClipFadeIn(clips, 1, 99999, 100000, enSecondes);
    VSM_ASSERT_NEAR(clips[0].fadeInSeconds, 1.0, 1e-9);   // le clip entier, pas plus
    setClipFadeIn(clips, 1, -5000, 100000, enSecondes);
    VSM_ASSERT_NEAR(clips[0].fadeInSeconds, 0.0, 1e-9);
    setClipFadeOut(clips, 1, -5000, 100000, enSecondes);
    VSM_ASSERT_NEAR(clips[0].fadeOutSeconds, 1.0, 1e-9);
}

VSM_TEST(a_clip_gain_is_never_negative) {
    // Une inversion de phase est un réglage à part : la confondre avec un gain
    // négatif rendrait le bouton illisible -- on ne saurait plus si un clip est
    // faible ou inversé.
    std::vector<Clip> clips{clip(1, 0, 960)};
    setClipGain(clips, {1}, 1.8f);
    VSM_ASSERT_NEAR(clips[0].gain, 1.8f, 1e-6f);
    setClipGain(clips, {1}, -2.0f);
    VSM_ASSERT_NEAR(clips[0].gain, 0.0f, 1e-6f);
}

VSM_TEST(inverting_the_phase_toggles_each_clip_rather_than_aligning_them) {
    // Inverser une sélection dont la moitié l'est déjà doit rendre l'autre
    // moitié, pas tout aligner.
    std::vector<Clip> clips{clip(1, 0, 480), clip(2, 480, 480)};
    clips[1].invertPhase = true;
    toggleClipPhase(clips, {1, 2});
    VSM_ASSERT(clips[0].invertPhase);
    VSM_ASSERT(!clips[1].invertPhase);
}

VSM_TEST(these_gestures_ignore_a_clip_that_is_not_there) {
    std::vector<Clip> clips{clip(1, 0, 960)};
    setClipFadeIn(clips, 999, 480, 100000, enSecondes);
    setClipFadeOut(clips, 999, 480, 100000, enSecondes);
    setClipGain(clips, {}, 0.5f);
    toggleClipPhase(clips, {});
    VSM_ASSERT_NEAR(clips[0].fadeInSeconds, 0.0, 1e-9);
    VSM_ASSERT_NEAR(clips[0].gain, 1.0f, 1e-6f);
    VSM_ASSERT(!clips[0].invertPhase);
}

// D11.1 — LE CLIP CHANGE DE PISTE, ET IL EMPORTE CE QUE SA FENÊTRE COUVRE.

namespace {
Note note(Tick debut, uint8_t hauteur, uint64_t id) {
    Note n;
    n.startTick = debut;
    n.endTick = debut + 240;
    n.number = hauteur;
    n.id = id;
    return n;
}
}

VSM_TEST(moving_a_clip_to_another_track_takes_the_notes_its_window_covers) {
    std::vector<Track> pistes(3);
    // Piste 0 : deux clips, l'un sur les mesures 1-2 (0..1920), l'autre sur
    // 2-3 (1920..3840) ; trois notes, une par mesure sur les trois premières.
    pistes[0].clips = {clip(1, 0, 1920, 0), clip(2, 1920, 1920, 1920)};
    pistes[0].notes = {note(0, 60, 10), note(1920, 62, 11), note(3840, 64, 12)};

    const auto rapport = moveClipsAcrossTracks(pistes, {2}, 2);
    VSM_ASSERT_EQ(rapport.moved, size_t{1});
    VSM_ASSERT_EQ(rapport.refused, size_t{0});
    VSM_ASSERT_EQ(rapport.applied, 2);
    // Le clip 2 est sur la piste 2, avec LA note que sa fenêtre couvrait, aux
    // mêmes ticks ; la piste 0 garde le clip 1 et ses deux autres notes.
    VSM_ASSERT_EQ(pistes[0].clips.size(), size_t{1});
    VSM_ASSERT_EQ(pistes[2].clips.size(), size_t{1});
    VSM_ASSERT_EQ(pistes[2].clips[0].id, uint64_t{2});
    VSM_ASSERT_EQ(pistes[2].clips[0].startTick, Tick{1920});
    VSM_ASSERT_EQ(pistes[2].notes.size(), size_t{1});
    VSM_ASSERT_EQ(pistes[2].notes[0].id, uint64_t{11});
    VSM_ASSERT_EQ(pistes[2].notes[0].startTick, Tick{1920});
    VSM_ASSERT_EQ(pistes[0].notes.size(), size_t{2});
    VSM_ASSERT_EQ(pistes[0].notes[1].id, uint64_t{12});
}

VSM_TEST(a_selection_that_hits_the_last_track_keeps_its_shape_across_tracks) {
    std::vector<Track> pistes(4);
    pistes[1].clips = {clip(1, 0, 960)};
    pistes[2].clips = {clip(2, 0, 960)};
    // Demander +5 quand le plus bas est en 2 sur quatre pistes : +1 pour tous.
    const auto rapport = moveClipsAcrossTracks(pistes, {1, 2}, 5);
    VSM_ASSERT_EQ(rapport.applied, 1);
    VSM_ASSERT_EQ(rapport.moved, size_t{2});
    VSM_ASSERT_EQ(pistes[2].clips.size(), size_t{1});
    VSM_ASSERT_EQ(pistes[2].clips[0].id, uint64_t{1});
    VSM_ASSERT_EQ(pistes[3].clips[0].id, uint64_t{2});
    // Et vers le haut, le clip en 2 (désormais en 3) ne dépasse pas zéro.
    const auto retour = moveClipsAcrossTracks(pistes, {1, 2}, -9);
    VSM_ASSERT_EQ(retour.applied, -2);
    VSM_ASSERT_EQ(pistes[0].clips[0].id, uint64_t{1});
    VSM_ASSERT_EQ(pistes[1].clips[0].id, uint64_t{2});
}

VSM_TEST(an_audio_clip_refuses_a_track_with_another_file_and_adopts_an_empty_one) {
    std::vector<Track> pistes(3);
    pistes[0].kind = Track::Kind::Audio;
    pistes[0].audio.path = "prise-1.wav";
    pistes[0].clips = {clip(1, 0, 960)};
    pistes[1].kind = Track::Kind::Audio;
    pistes[1].audio.path = "prise-2.wav";
    pistes[2].kind = Track::Kind::Audio;

    const auto refus = moveClipsAcrossTracks(pistes, {1}, 1);
    VSM_ASSERT_EQ(refus.refused, size_t{1});
    VSM_ASSERT_EQ(refus.moved, size_t{0});
    VSM_ASSERT_EQ(pistes[0].clips.size(), size_t{1});

    const auto adopte = moveClipsAcrossTracks(pistes, {1}, 2);
    VSM_ASSERT_EQ(adopte.moved, size_t{1});
    VSM_ASSERT_EQ(pistes[2].clips.size(), size_t{1});
    VSM_ASSERT_EQ(pistes[2].audio.path, std::string("prise-1.wav"));

    // Une piste MIDI ne reçoit pas un clip audio, ni un groupe quoi que ce soit.
    std::vector<Track> mixte(2);
    mixte[0].kind = Track::Kind::Audio;
    mixte[0].audio.path = "prise-1.wav";
    mixte[0].clips = {clip(1, 0, 960)};
    VSM_ASSERT_EQ(moveClipsAcrossTracks(mixte, {1}, 1).refused, size_t{1});
    mixte[1].kind = Track::Kind::Group;
    VSM_ASSERT_EQ(moveClipsAcrossTracks(mixte, {1}, 1).refused, size_t{1});
}

// ---------------------------------------------------------------------------
// D12.4 — LE SUIVI DE TEMPO (docs/CDC-etirement-temporel.md, § 2 et § 4).
// Les marqueurs sont relatifs au clip ; l'allumer est neutre ; « N mesures »
// pose la paire ; couper et rogner transportent la carte, tick pour tick.
// ---------------------------------------------------------------------------

VSM_TEST(turning_warp_on_places_the_neutral_pair_and_changes_no_sound) {
    std::vector<Clip> clips{clip(1, 1920, 1920)};
    clips[0].sourceStartSeconds = 5.0;
    VSM_ASSERT(!clipIsWarped(clips[0]));
    setClipWarpMode(clips, {1}, WarpMode::KeepPitch, 100000, enSecondes);
    VSM_ASSERT(clipIsWarped(clips[0]));
    VSM_ASSERT_EQ(clips[0].warpMarkers.size(), size_t(2));
    VSM_ASSERT_NEAR(clips[0].warpMarkers[0].sourceSeconds, 5.0, 1e-9);
    VSM_ASSERT_EQ(clips[0].warpMarkers[0].tick, Tick(0));
    VSM_ASSERT_NEAR(clips[0].warpMarkers[1].sourceSeconds, 7.0, 1e-9);   // 1920 ticks = 2 s
    VSM_ASSERT_EQ(clips[0].warpMarkers[1].tick, Tick(1920));
    // La carte est celle du tempo : rapport un, partout, prolongé au-delà.
    VSM_ASSERT_NEAR(warpSourceSecondsAt(clips[0], 960), 6.0, 1e-9);
    VSM_ASSERT_NEAR(warpSourceSecondsAt(clips[0], 3840), 9.0, 1e-9);
    VSM_ASSERT_EQ(warpTickAtSeconds(clips[0], 6.5), Tick(1440));
    // Éteindre garde les marqueurs (pour rallumer sans les perdre) mais le
    // clip ne suit plus.
    setClipWarpMode(clips, {1}, WarpMode::Off, 100000, enSecondes);
    VSM_ASSERT(!clipIsWarped(clips[0]));
    VSM_ASSERT_EQ(clips[0].warpMarkers.size(), size_t(2));
}

VSM_TEST(the_clip_is_n_bars_sets_the_length_and_the_pair_and_tells_the_tempo) {
    // Une boucle de 2,0 s (1920 ticks à 120 BPM) qu'on déclare faire UNE
    // mesure : elle doit désormais durer 1920 ticks... c'est déjà le cas ; on
    // la déclare faire DEUX mesures, et sa carte s'étire d'un facteur deux.
    std::vector<Clip> clips{clip(1, 0, 1920)};
    clips[0].sourceStartSeconds = 10.0;
    const double bpm = setClipBars(clips, 1, 2, 1920, 100000, enSecondes);
    VSM_ASSERT(clipIsWarped(clips[0]));
    VSM_ASSERT(clips[0].warpMode == WarpMode::KeepPitch);
    VSM_ASSERT_EQ(clips[0].length, Tick(3840));
    VSM_ASSERT_EQ(clips[0].warpMarkers.size(), size_t(2));
    VSM_ASSERT_NEAR(clips[0].warpMarkers[1].sourceSeconds, 12.0, 1e-9);   // le même matériau
    VSM_ASSERT_EQ(clips[0].warpMarkers[1].tick, Tick(3840));             // sur deux fois plus de ticks
    // Deux mesures de quatre temps en deux secondes : 240 BPM d'origine.
    VSM_ASSERT_NEAR(bpm, 240.0, 1e-9);
    VSM_ASSERT_NEAR(warpSourceSecondsAt(clips[0], 1920), 11.0, 1e-9);
}

VSM_TEST(splitting_a_warped_clip_cuts_its_map_and_the_halves_replay_it_tick_for_tick) {
    std::vector<Clip> clips{clip(1, 0, 3840)};
    clips[0].sourceStartSeconds = 0.0;
    clips[0].warpMode = WarpMode::KeepPitch;
    // Trois marqueurs, deux rapports : 0 → 0 s, 1920 → 1 s (×2), 3840 → 4 s (×0,67).
    clips[0].warpMarkers = {{0.0, 0}, {1.0, 1920}, {4.0, 3840}};
    const Clip original = clips[0];
    uint64_t compteur = 10;
    VSM_ASSERT_EQ(splitClips(clips, {1}, 2880, 100000, compteur, enSecondes), size_t(1));
    VSM_ASSERT_EQ(clips.size(), size_t(2));
    // La première garde 0 et 1920, et reçoit la coupe (2880 → 2,5 s).
    VSM_ASSERT_EQ(clips[0].warpMarkers.size(), size_t(3));
    VSM_ASSERT_EQ(clips[0].warpMarkers.back().tick, Tick(2880));
    VSM_ASSERT_NEAR(clips[0].warpMarkers.back().sourceSeconds, 2.5, 1e-9);
    // La seconde commence à la coupe (0 → 2,5 s) et garde 3840 → 4 s, décalé.
    VSM_ASSERT_EQ(clips[1].warpMarkers.size(), size_t(2));
    VSM_ASSERT_NEAR(clips[1].sourceStartSeconds, 2.5, 1e-9);
    VSM_ASSERT_EQ(clips[1].warpMarkers[0].tick, Tick(0));
    VSM_ASSERT_EQ(clips[1].warpMarkers[1].tick, Tick(960));
    VSM_ASSERT_NEAR(clips[1].warpMarkers[1].sourceSeconds, 4.0, 1e-9);
    // Bout à bout, les deux cartes SONT l'ancienne.
    for (Tick t = 0; t < 3840; t += 120) {
        const double attendu = warpSourceSecondsAt(original, t);
        const double obtenu = t < 2880 ? warpSourceSecondsAt(clips[0], t)
                                       : warpSourceSecondsAt(clips[1], t - 2880);
        VSM_ASSERT_NEAR(obtenu, attendu, 1e-9);
    }
}

VSM_TEST(trimming_the_head_of_a_warped_clip_follows_its_map_not_the_tempo) {
    std::vector<Clip> clips{clip(1, 0, 3840)};
    clips[0].warpMode = WarpMode::KeepPitch;
    clips[0].warpMarkers = {{0.0, 0}, {1.0, 1920}, {4.0, 3840}};   // ×2 puis ×0,67
    resizeClipsStart(clips, {1}, 960, 100000, enSecondes);
    // Au tempo, 960 ticks font 1 s ; dans la carte, 0,5 s.
    VSM_ASSERT_NEAR(clips[0].sourceStartSeconds, 0.5, 1e-9);
    VSM_ASSERT_EQ(clips[0].startTick, Tick(960));
    VSM_ASSERT_EQ(clips[0].length, Tick(2880));
    VSM_ASSERT_EQ(clips[0].warpMarkers.size(), size_t(3));
    VSM_ASSERT_EQ(clips[0].warpMarkers[0].tick, Tick(0));
    VSM_ASSERT_EQ(clips[0].warpMarkers[1].tick, Tick(960));    // l'ancien 1920, glissé
    VSM_ASSERT_EQ(clips[0].warpMarkers[2].tick, Tick(2880));
    VSM_ASSERT_NEAR(warpSourceSecondsAt(clips[0], 960), 1.0, 1e-9);
}

VSM_TEST(warp_markers_are_added_where_the_map_already_is_moved_between_neighbours_and_never_below_two) {
    std::vector<Clip> clips{clip(1, 0, 3840)};
    clips[0].warpMode = WarpMode::KeepPitch;
    clips[0].warpMarkers = {{0.0, 0}, {4.0, 3840}};
    // Ajouter au milieu : le son ne change pas (2 s à 1920).
    VSM_ASSERT_EQ(addWarpMarker(clips, 1, 1920), 1);
    VSM_ASSERT_NEAR(clips[0].warpMarkers[1].sourceSeconds, 2.0, 1e-9);
    VSM_ASSERT_EQ(addWarpMarker(clips, 1, 1920), -1);     // déjà là
    VSM_ASSERT_EQ(addWarpMarker(clips, 1, 0), -1);        // pas sur les bords
    VSM_ASSERT_EQ(addWarpMarker(clips, 1, 5000), -1);
    // Le déplacer en musique : c'est le calage. La source ne bouge pas.
    VSM_ASSERT(moveWarpMarker(clips, 1, 1, 1440));
    VSM_ASSERT_EQ(clips[0].warpMarkers[1].tick, Tick(1440));
    VSM_ASSERT_NEAR(clips[0].warpMarkers[1].sourceSeconds, 2.0, 1e-9);
    VSM_ASSERT_NEAR(warpSourceSecondsAt(clips[0], 720), 1.0, 1e-9);     // ×1,33 avant
    VSM_ASSERT_NEAR(warpSourceSecondsAt(clips[0], 2640), 3.0, 1e-9);    // ×0,8 après
    // Jamais sur un voisin, jamais le premier.
    VSM_ASSERT(moveWarpMarker(clips, 1, 1, 5000));
    VSM_ASSERT_EQ(clips[0].warpMarkers[1].tick, Tick(3839));
    VSM_ASSERT(!moveWarpMarker(clips, 1, 0, 100));
    // Retirer : pas le premier, pas sous deux.
    VSM_ASSERT(!removeWarpMarker(clips, 1, 0));
    VSM_ASSERT(removeWarpMarker(clips, 1, 1));
    VSM_ASSERT_EQ(clips[0].warpMarkers.size(), size_t(2));
    VSM_ASSERT(!removeWarpMarker(clips, 1, 1));
}

VSM_TEST(stretching_the_right_edge_scales_the_map_and_the_last_marker_follows_the_edge) {
    // D13.2 : un clip non étiré de 1920 ticks (2 s) tiré de 960 ticks devient
    // un clip étiré de 2880 ticks qui joue les MÊMES deux secondes.
    std::vector<Clip> clips{clip(1, 0, 1920)};
    clips[0].sourceStartSeconds = 3.0;
    VSM_ASSERT(stretchClipsEnd(clips, {1}, 960, 100000, enSecondes));
    VSM_ASSERT(clipIsWarped(clips[0]));
    VSM_ASSERT(clips[0].warpMode == WarpMode::KeepPitch);
    VSM_ASSERT_EQ(clips[0].length, Tick(2880));
    VSM_ASSERT_EQ(clips[0].warpMarkers.back().tick, Tick(2880));
    VSM_ASSERT_NEAR(clips[0].warpMarkers.back().sourceSeconds, 5.0, 1e-9);   // le même matériau
    VSM_ASSERT_NEAR(warpSourceSecondsAt(clips[0], 1440), 4.0, 1e-9);          // au milieu, la moitié
    // Un clip déjà calé : les marqueurs glissent en proportion.
    clips[0].warpMarkers = {{3.0, 0}, {3.5, 960}, {5.0, 2880}};
    VSM_ASSERT(stretchClipsEnd(clips, {1}, -1440, 100000, enSecondes));      // 2880 -> 1440
    VSM_ASSERT_EQ(clips[0].length, Tick(1440));
    VSM_ASSERT_EQ(clips[0].warpMarkers[1].tick, Tick(480));
    VSM_ASSERT_EQ(clips[0].warpMarkers[2].tick, Tick(1440));
    VSM_ASSERT_NEAR(clips[0].warpMarkers[1].sourceSeconds, 3.5, 1e-9);        // la source ne bouge pas
    // Jamais sous un tick, et deux marqueurs ne se confondent pas.
    VSM_ASSERT(stretchClipsEnd(clips, {1}, -100000, 100000, enSecondes));
    VSM_ASSERT(clips[0].length >= 1);
    VSM_ASSERT(clips[0].warpMarkers[1].tick > clips[0].warpMarkers[0].tick);
    VSM_ASSERT(clips[0].warpMarkers[2].tick > clips[0].warpMarkers[1].tick);
}

VSM_TEST(reversing_toggles_each_selected_clip_on_its_own) {
    std::vector<Clip> clips{clip(1, 0, 960), clip(2, 960, 960)};
    clips[1].reversed = true;
    toggleClipReverse(clips, {1, 2});
    VSM_ASSERT(clips[0].reversed);
    VSM_ASSERT(!clips[1].reversed);   // chacun le sien, pas un alignement
}

VSM_TEST(moving_a_warped_clip_leaves_its_markers_alone_they_are_relative) {
    std::vector<Clip> clips{clip(1, 0, 3840)};
    clips[0].warpMode = WarpMode::KeepPitch;
    clips[0].warpMarkers = {{0.0, 0}, {1.0, 1920}, {4.0, 3840}};
    moveClips(clips, {1}, 7680);
    VSM_ASSERT_EQ(clips[0].startTick, Tick(7680));
    VSM_ASSERT_EQ(clips[0].warpMarkers[1].tick, Tick(1920));
    VSM_ASSERT_NEAR(clips[0].warpMarkers[1].sourceSeconds, 1.0, 1e-9);
}

// --------------------------------------------------------------------------
// D16.1 — CRÉER UN CLIP DANS L'ARRANGEMENT.
// --------------------------------------------------------------------------

VSM_TEST(creating_a_clip_makes_the_identity_window_on_the_material_already_there) {
    // Le critère de l'étape : un clip créé d'une mesure sur une piste qui
    // porte des notes rejoue EXACTEMENT les notes de cette mesure -- ni celles
    // d'avant, ni celles d'après, et aucune déplacée.
    Project project;
    project.ticksPerQuarterNote = 480;
    project.tempoMap.addTempoChange(0, 500000);          // 120 BPM, la noire = 0,5 s
    Track piste;
    uint64_t ids = 1;
    for (int i = 0; i < 8; ++i)                          // deux mesures de quatre noires
        piste.addNote(480 * i, 480 * i + 240, static_cast<uint8_t>(60 + i), 100, 0, ids);
    project.tracks.push_back(piste);

    uint64_t compteur = 1;
    const auto faite = createClip(project.tracks[0].clips, 0, 1920, compteur, 100000);
    VSM_ASSERT(faite.id != 0);
    VSM_ASSERT_EQ(faite.length, Tick(1920));
    VSM_ASSERT(!faite.truncated);
    VSM_ASSERT_EQ(compteur, uint64_t(2));                 // le compteur a avancé d'un
    VSM_ASSERT_EQ(project.tracks[0].clips.size(), size_t(1));
    // La fenêtre est l'identité : ce qui sonnait là sonne toujours là.
    VSM_ASSERT_EQ(project.tracks[0].clips[0].sourceStart, Tick(0));
    VSM_ASSERT_EQ(project.tracks[0].clips[0].startTick, Tick(0));

    std::vector<double> departs;
    for (const auto& e : PlaybackScheduler::build(project, 0, 100000))
        if (std::holds_alternative<vsm::midi::NoteOnEvent>(e.data))
            departs.push_back(e.timeSeconds);
    std::sort(departs.begin(), departs.end());
    VSM_ASSERT_EQ(departs.size(), size_t(4));             // la première mesure, pas la seconde
    for (int i = 0; i < 4; ++i) VSM_ASSERT_NEAR(departs[static_cast<size_t>(i)], 0.5 * i, 1e-9);
}

VSM_TEST(creating_a_clip_on_an_empty_track_gives_a_clip_and_no_event) {
    Project project;
    project.ticksPerQuarterNote = 480;
    project.tempoMap.addTempoChange(0, 500000);
    project.tracks.push_back(Track{});

    uint64_t compteur = 1;
    const auto faite = createClip(project.tracks[0].clips, 1920, 1920, compteur, 100000);
    VSM_ASSERT(faite.id != 0);
    VSM_ASSERT_EQ(project.tracks[0].clips.size(), size_t(1));
    VSM_ASSERT_EQ(project.tracks[0].clips[0].startTick, Tick(1920));
    VSM_ASSERT(PlaybackScheduler::build(project, 0, 100000).empty());
}

VSM_TEST(creating_a_clip_never_bites_into_the_one_that_is_already_there) {
    // LA RÈGLE DU CHEVAUCHEMENT. Deux fenêtres qui se recouvrent joueraient le
    // même matériau deux fois, sans qu'aucune note soit en double : la
    // création s'arrête donc au clip suivant, et refuse un début déjà pris.
    std::vector<Clip> clips{clip(1, 1920, 960)};
    uint64_t compteur = 2;

    // Une mesure demandée à partir de 960 : il n'y a que 960 ticks de libre.
    const auto raccourcie = createClip(clips, 960, 1920, compteur, 100000);
    VSM_ASSERT(raccourcie.id != 0);
    VSM_ASSERT(raccourcie.truncated);
    VSM_ASSERT_EQ(raccourcie.length, Tick(960));
    VSM_ASSERT_EQ(clips.size(), size_t(2));

    // Un début déjà couvert : RIEN n'est créé, et le compteur ne bouge pas --
    // un refus se dit, il ne se déplace pas ailleurs en silence.
    const uint64_t avant = compteur;
    const auto refusee = createClip(clips, 2400, 960, compteur, 100000);
    VSM_ASSERT_EQ(refusee.id, uint64_t(0));
    VSM_ASSERT_EQ(compteur, avant);
    VSM_ASSERT_EQ(clips.size(), size_t(2));

    // Un clip BOUCLÉ couvre toute sa répétition, pas seulement sa fenêtre.
    std::vector<Clip> boucle{clip(1, 0, 960)};
    boucle[0].length = 3840;                              // la fenêtre de 960 répétée
    uint64_t c2 = 2;
    VSM_ASSERT_EQ(createClip(boucle, 1920, 960, c2, 100000).id, uint64_t(0));
}

// --------------------------------------------------------------------------
// D16.3 — JOINDRE DES CLIPS (la Colle de Cubase, le Consolidate de Live).
// --------------------------------------------------------------------------

VSM_TEST(joining_two_contiguous_clips_gives_one_that_plays_note_for_note_the_same) {
    // LE CRITÈRE : le clip joint rejoue EXACTEMENT ce que jouaient les deux.
    // Le témoin est le projet non découpé, et la comparaison se fait sur les
    // événements du planificateur, pas sur la géométrie des clips.
    Project temoin;
    temoin.ticksPerQuarterNote = 480;
    temoin.tempoMap.addTempoChange(0, 500000);
    Track piste;
    uint64_t ids = 1;
    for (int i = 0; i < 4; ++i)
        piste.addNote(480 * i, 480 * i + 240, static_cast<uint8_t>(60 + i), 100, 0, ids);
    temoin.tracks.push_back(piste);
    Clip entier;
    entier.id = 1; entier.sourceStart = 0; entier.sourceLength = 1920;
    entier.startTick = 0; entier.length = 1920;
    temoin.tracks[0].clips.push_back(entier);

    Project coupe = temoin;
    coupe.tracks[0].clips = {clip(1, 0, 960), clip(2, 960, 960, 960)};

    Project joint = coupe;
    const auto bilan = joinClips(joint.tracks[0].clips, {1, 2}, 1920, false,
                                  [&joint](Tick t) { return joint.ticksToSeconds(t); });
    VSM_ASSERT_EQ(bilan.joined, size_t(1));
    VSM_ASSERT_EQ(bilan.refused, size_t(0));
    VSM_ASSERT_EQ(joint.tracks[0].clips.size(), size_t(1));
    VSM_ASSERT_EQ(joint.tracks[0].clips[0].startTick, Tick(0));
    VSM_ASSERT_EQ(joint.tracks[0].clips[0].length, Tick(1920));

    const auto attendus = PlaybackScheduler::build(temoin, 0, 100000);
    const auto obtenus = PlaybackScheduler::build(joint, 0, 100000);
    VSM_ASSERT_EQ(obtenus.size(), attendus.size());
    for (size_t i = 0; i < attendus.size(); ++i)
        VSM_ASSERT_NEAR(obtenus[i].timeSeconds, attendus[i].timeSeconds, 1e-12);
}

VSM_TEST(clips_whose_windows_do_not_continue_are_refused_and_nothing_moves) {
    // Contigus SUR LA LIGNE DE TEMPS mais lisant deux endroits différents du
    // matériau : les joindre changerait ce qu'on entend. Refusé, et compté.
    std::vector<Clip> clips{clip(1, 0, 960), clip(2, 960, 960, 2880)};
    const auto avant = clips;
    const auto bilan = joinClips(clips, {1, 2}, 100000, false, {});
    VSM_ASSERT_EQ(bilan.joined, size_t(0));
    VSM_ASSERT_EQ(bilan.refused, size_t(1));
    VSM_ASSERT_EQ(clips.size(), size_t(2));
    VSM_ASSERT_EQ(clips[0].sourceStart, avant[0].sourceStart);
    VSM_ASSERT_EQ(clips[1].sourceStart, avant[1].sourceStart);
    VSM_ASSERT_EQ(clips[1].startTick, avant[1].startTick);

    // Un trou sur la ligne de temps : refusé aussi.
    std::vector<Clip> troues{clip(1, 0, 960), clip(2, 1440, 960, 960)};
    VSM_ASSERT_EQ(joinClips(troues, {1, 2}, 100000, false, {}).joined, size_t(0));
    VSM_ASSERT_EQ(troues.size(), size_t(2));
}

VSM_TEST(a_looping_clip_a_warped_one_and_a_differently_set_one_never_join) {
    // Une boucle : la joindre donnerait une fenêtre qui n'est plus celle qu'on
    // répétait.
    std::vector<Clip> boucle{clip(1, 0, 960), clip(2, 960, 960, 960)};
    boucle[0].length = 1920;                    // la fenêtre de 960 répétée deux fois
    VSM_ASSERT_EQ(joinClips(boucle, {1, 2}, 100000, false, {}).joined, size_t(0));

    // Un clip qui suit le tempo : deux cartes bout à bout ne font pas une carte.
    std::vector<Clip> warpe{clip(1, 0, 960), clip(2, 960, 960, 960)};
    warpe[0].warpMode = WarpMode::KeepPitch;
    warpe[0].warpMarkers = {{0.0, 0}, {2.0, 960}};
    VSM_ASSERT_EQ(joinClips(warpe, {1, 2}, 100000, false, {}).joined, size_t(0));

    // Deux gains différents : un clip joint ne peut pas porter les deux.
    std::vector<Clip> gains{clip(1, 0, 960), clip(2, 960, 960, 960)};
    gains[1].gain = 0.5f;
    VSM_ASSERT_EQ(joinClips(gains, {1, 2}, 100000, false, {}).joined, size_t(0));
}

VSM_TEST(joining_is_the_exact_inverse_of_splitting) {
    // Couper puis joindre rend le clip de départ, fondus compris : le fondu
    // d'entrée du premier et celui de sortie du dernier sont les deux bords
    // qui restent des bords.
    std::vector<Clip> clips{clip(1, 0, 1920)};
    clips[0].fadeInSeconds = 0.25;
    clips[0].fadeOutSeconds = 0.5;
    const Clip depart = clips[0];

    uint64_t compteur = 2;
    auto enSecondes = [](Tick t) { return static_cast<double>(t) / 960.0; };
    VSM_ASSERT_EQ(splitClips(clips, {1}, 960, 100000, compteur, enSecondes), size_t(1));
    VSM_ASSERT_EQ(clips.size(), size_t(2));

    const auto bilan = joinClips(clips, {1, 2}, 100000, false, enSecondes);
    VSM_ASSERT_EQ(bilan.joined, size_t(1));
    VSM_ASSERT_EQ(clips.size(), size_t(1));
    VSM_ASSERT_EQ(clips[0].startTick, depart.startTick);
    VSM_ASSERT_EQ(clips[0].length, depart.length);
    VSM_ASSERT_EQ(clips[0].sourceStart, depart.sourceStart);
    VSM_ASSERT_EQ(clips[0].sourceLength, depart.sourceLength);
    VSM_ASSERT_NEAR(clips[0].fadeInSeconds, depart.fadeInSeconds, 1e-12);
    VSM_ASSERT_NEAR(clips[0].fadeOutSeconds, depart.fadeOutSeconds, 1e-12);
}

VSM_TEST(three_in_a_row_become_one_and_a_stranger_in_the_middle_is_counted) {
    std::vector<Clip> trois{clip(1, 0, 960), clip(2, 960, 960, 960), clip(3, 1920, 960, 1920)};
    VSM_ASSERT_EQ(joinClips(trois, {1, 2, 3}, 100000, false, {}).joined, size_t(2));
    VSM_ASSERT_EQ(trois.size(), size_t(1));
    VSM_ASSERT_EQ(trois[0].length, Tick(2880));

    // Deux paires joignables séparées par une rupture : deux clips, un refus.
    std::vector<Clip> deux{clip(1, 0, 960), clip(2, 960, 960, 960),
                            clip(3, 3840, 960), clip(4, 4800, 960, 960)};
    const auto bilan = joinClips(deux, {1, 2, 3, 4}, 100000, false, {});
    VSM_ASSERT_EQ(bilan.joined, size_t(2));
    VSM_ASSERT_EQ(bilan.refused, size_t(1));
    VSM_ASSERT_EQ(deux.size(), size_t(2));
}

VSM_TEST(on_an_audio_track_the_window_in_the_file_must_continue_too) {
    // La même exigence que la fenêtre en ticks, dans l'unité du matériau : un
    // clip audio lit des SECONDES du fichier, pas des ticks. Deux moitiés dont
    // les secondes ne s'enchaînent pas joueraient un saut à l'endroit du joint.
    auto enSecondes = [](Tick t) { return static_cast<double>(t) / 960.0; };   // 960 ticks = 1 s

    std::vector<Clip> suite{clip(1, 0, 960), clip(2, 960, 960, 960)};
    suite[0].sourceStartSeconds = 3.0;
    suite[1].sourceStartSeconds = 4.0;                    // 3 s + la seconde du premier
    VSM_ASSERT_EQ(joinClips(suite, {1, 2}, 100000, true, enSecondes).joined, size_t(1));

    std::vector<Clip> saut{clip(1, 0, 960), clip(2, 960, 960, 960)};
    saut[0].sourceStartSeconds = 3.0;
    saut[1].sourceStartSeconds = 9.0;                     // six secondes plus loin
    const auto bilan = joinClips(saut, {1, 2}, 100000, true, enSecondes);
    VSM_ASSERT_EQ(bilan.joined, size_t(0));
    VSM_ASSERT_EQ(bilan.refused, size_t(1));
    VSM_ASSERT_EQ(saut.size(), size_t(2));
    // Et la MÊME paire sur une piste MIDI se joint : le critère des secondes ne
    // la concerne pas, et c'est pourquoi le genre est dit et non deviné.
    std::vector<Clip> midi{clip(1, 0, 960), clip(2, 960, 960, 960)};
    midi[0].sourceStartSeconds = 3.0;
    midi[1].sourceStartSeconds = 9.0;
    VSM_ASSERT_EQ(joinClips(midi, {1, 2}, 100000, false, enSecondes).joined, size_t(1));
}

// --------------------------------------------------------------------------
// D16.5 — LE VERROU. Une piste verrouillée se joue, s'entend et se mixe comme
// avant ; c'est son MONTAGE qui est refusé. Le refus est dans ces surcharges,
// pas dans les quarante gestes des vues.
// --------------------------------------------------------------------------

namespace {
/// Une piste qui porte deux clips d'une mesure et les notes qui vont avec.
Track pisteDeuxClips(uint64_t premierId) {
    Track piste;
    uint64_t ids = 1;
    for (int i = 0; i < 4; ++i)
        piste.addNote(480 * i, 480 * i + 240, static_cast<uint8_t>(60 + i), 100, 0, ids);
    piste.clips = {clip(premierId, 0, 960), clip(premierId + 1, 960, 960, 960)};
    return piste;
}
} // namespace

VSM_TEST(a_locked_track_refuses_every_edit_and_not_a_tick_moves) {
    Track piste = pisteDeuxClips(1);
    piste.locked = true;
    const auto avant = piste.clips;

    VSM_ASSERT_EQ(moveClips(piste, {1, 2}, 480, false, 1920), size_t(0));
    VSM_ASSERT_EQ(resizeClipsEnd(piste, {1}, 480, 1920), size_t(0));
    VSM_ASSERT_EQ(resizeClipsStart(piste, {1}, 480, 1920, {}), size_t(0));
    VSM_ASSERT_EQ(stretchClipsEnd(piste, {1}, 480, 1920, {}), size_t(0));
    uint64_t compteur = 3;
    VSM_ASSERT_EQ(splitClips(piste, {1}, 480, 1920, compteur, {}), size_t(0));
    VSM_ASSERT(duplicateClips(piste, {1}, 1920, compteur).empty());
    VSM_ASSERT_EQ(createClip(piste, 3840, 960, compteur, 1920).id, uint64_t(0));
    VSM_ASSERT_EQ(joinClips(piste, {1, 2}, 1920, {}).joined, size_t(0));

    VSM_ASSERT_EQ(compteur, uint64_t(3));            // pas un identifiant distribué
    VSM_ASSERT_EQ(piste.clips.size(), avant.size());
    for (size_t i = 0; i < avant.size(); ++i) {
        VSM_ASSERT_EQ(piste.clips[i].startTick, avant[i].startTick);
        VSM_ASSERT_EQ(piste.clips[i].length, avant[i].length);
        VSM_ASSERT_EQ(piste.clips[i].sourceStart, avant[i].sourceStart);
    }

    // Déverrouillée, le MÊME appel passe : c'est le cadenas qu'on mesure, pas
    // une sélection vide ou un geste impossible.
    piste.locked = false;
    VSM_ASSERT_EQ(moveClips(piste, {1, 2}, 480, false, 1920), size_t(2));
    VSM_ASSERT_EQ(piste.clips[0].startTick, Tick(480));
}

VSM_TEST(a_selection_across_a_locked_and_a_free_track_only_moves_the_free_one) {
    std::vector<Track> pistes{pisteDeuxClips(1), pisteDeuxClips(3)};
    pistes[0].locked = true;
    const auto avantVerrouillee = pistes[0].clips;

    // Ce que l'appelant doit DIRE : deux clips sur quatre sont verrouillés.
    VSM_ASSERT_EQ(lockedClipsInSelection(pistes, {1, 2, 3, 4}), size_t(2));

    size_t deplaces = 0;
    for (auto& piste : pistes) deplaces += moveClips(piste, {1, 2, 3, 4}, 960, false, 1920);
    VSM_ASSERT_EQ(deplaces, size_t(2));                       // seuls ceux de la piste libre
    VSM_ASSERT_EQ(pistes[0].clips[0].startTick, avantVerrouillee[0].startTick);
    VSM_ASSERT_EQ(pistes[0].clips[1].startTick, avantVerrouillee[1].startTick);
    VSM_ASSERT_EQ(pistes[1].clips[0].startTick, Tick(960));
    VSM_ASSERT_EQ(pistes[1].clips[1].startTick, Tick(1920));
}

VSM_TEST(a_locked_track_neither_gives_a_clip_nor_receives_one) {
    // Des deux côtés : on ne prend rien à une piste verrouillée, et on ne lui
    // pose rien. Sinon le verrou se contournerait en poussant depuis la
    // voisine.
    std::vector<Track> versLeBas{pisteDeuxClips(1), pisteDeuxClips(3)};
    versLeBas[1].locked = true;
    auto rapport = moveClipsAcrossTracks(versLeBas, {1, 2}, 1);
    VSM_ASSERT_EQ(rapport.moved, size_t(0));
    VSM_ASSERT_EQ(rapport.refused, size_t(2));
    VSM_ASSERT_EQ(versLeBas[0].clips.size(), size_t(2));

    std::vector<Track> depuis{pisteDeuxClips(1), pisteDeuxClips(3)};
    depuis[0].locked = true;
    rapport = moveClipsAcrossTracks(depuis, {1, 2}, 1);
    VSM_ASSERT_EQ(rapport.moved, size_t(0));
    VSM_ASSERT_EQ(rapport.refused, size_t(2));
    VSM_ASSERT_EQ(depuis[0].clips.size(), size_t(2));
}

VSM_TEST(locking_a_track_changes_nothing_to_what_it_plays) {
    // VERROUILLER N'EST PAS TAIRE : c'est la moitié de la définition, et elle
    // se vérifie au planificateur, pas à la géométrie.
    Project libre;
    libre.ticksPerQuarterNote = 480;
    libre.tempoMap.addTempoChange(0, 500000);
    libre.tracks.push_back(pisteDeuxClips(1));
    Project verrouille = libre;
    verrouille.tracks[0].locked = true;

    const auto a = PlaybackScheduler::build(libre, 0, 100000);
    const auto b = PlaybackScheduler::build(verrouille, 0, 100000);
    VSM_ASSERT_EQ(b.size(), a.size());
    VSM_ASSERT(!a.empty());
    for (size_t i = 0; i < a.size(); ++i)
        VSM_ASSERT_NEAR(b[i].timeSeconds, a[i].timeSeconds, 1e-12);
}

// --------------------------------------------------------------------------
// D17.2 — L'AUTOMATION SUIT LES CLIPS.
//
// `ClipEdit` ne touchait à `Track::automation` nulle part : déplacer un clip
// d'une mesure laissait sa courbe de volume où elle était, et le projet ne
// jouait plus ce qu'il montrait.
// --------------------------------------------------------------------------

VSM_TEST(a_moved_clip_takes_its_automation_with_it_and_leaves_the_rest_alone) {
    Track piste;
    piste.clips = {clip(1, 0, 960)};
    AutomationCurve courbe;
    courbe.parameter = "mix.volume";
    courbe.points = {{0, 0.2f, false}, {480, 0.6f, false},        // sous le clip
                      {2880, 1.0f, false}};                        // ailleurs, et qui ne bouge pas
    piste.automation.push_back(courbe);

    VSM_ASSERT_EQ(moveClips(piste, {1}, 1920, true, 3840), size_t(1));
    VSM_ASSERT_EQ(piste.clips[0].startTick, Tick(1920));

    const auto& c = piste.automation[0];
    // Ce que la courbe disait à 0 et à 480, elle le dit maintenant à 1920 et
    // à 2400 : le clip a emporté sa courbe.
    VSM_ASSERT_NEAR(automationValueAt(c, 1920), 0.2f, 1e-6f);
    VSM_ASSERT_NEAR(automationValueAt(c, 2400), 0.6f, 1e-6f);
    // Et le point de 2880, hors de la plage du clip, n'a pas bougé.
    bool trouve = false;
    for (const auto& p : c.points)
        if (p.tick == 2880) { trouve = true; VSM_ASSERT_NEAR(p.value, 1.0f, 1e-6f); }
    VSM_ASSERT(trouve);
}

VSM_TEST(the_switch_off_leaves_the_curve_exactly_where_it_was) {
    // Cubase rend le suivi débrayable, et le débrayer sert : quand on remonte
    // une prise SOUS une courbe qu'on veut garder, c'est la courbe qui a
    // raison, pas le clip.
    Track piste;
    piste.clips = {clip(1, 0, 960)};
    AutomationCurve courbe;
    courbe.points = {{0, 0.2f, false}, {480, 0.6f, false}};
    piste.automation.push_back(courbe);

    VSM_ASSERT_EQ(moveClips(piste, {1}, 1920, false, 3840), size_t(1));
    VSM_ASSERT_EQ(piste.automation[0].points.size(), size_t(2));
    VSM_ASSERT_EQ(piste.automation[0].points[0].tick, Tick(0));
    VSM_ASSERT_EQ(piste.automation[0].points[1].tick, Tick(480));
}

VSM_TEST(the_curve_follows_the_distance_actually_travelled_not_the_one_asked_for) {
    // `moveClips` réduit le décalage POUR TOUS quand l'un des clips buterait
    // sur zéro. Décaler la courbe de ce qu'on a demandé au lieu de ce qui
    // s'est fait la désaccorderait du clip qu'elle suit.
    Track piste;
    piste.clips = {clip(1, 480, 960)};
    AutomationCurve courbe;
    courbe.points = {{480, 0.4f, false}};
    piste.automation.push_back(courbe);

    moveClips(piste, {1}, -2000, true, 3840);       // réduit à -480 : le clip bute sur zéro
    VSM_ASSERT_EQ(piste.clips[0].startTick, Tick(0));
    VSM_ASSERT_EQ(piste.automation[0].points.size(), size_t(1));
    VSM_ASSERT_EQ(piste.automation[0].points[0].tick, Tick(0));
}

VSM_TEST(a_clip_that_changes_track_hands_its_automation_to_the_new_one) {
    // La courbe ne se DÉPLACE pas dans le temps -- le clip garde sa position,
    // il change de piste --, elle DÉMÉNAGE.
    std::vector<Track> pistes(2);
    pistes[0].clips = {clip(1, 960, 960)};
    AutomationCurve courbe;
    courbe.parameter = "mix.pan";
    courbe.points = {{960, -0.5f, false}, {1440, 0.5f, false}, {2880, 0.0f, false}};
    pistes[0].automation.push_back(courbe);

    const auto rapport = moveClipsAcrossTracks(pistes, {1}, 1, true, 3840);
    VSM_ASSERT_EQ(rapport.moved, size_t(1));
    VSM_ASSERT_EQ(pistes[1].clips.size(), size_t(1));
    VSM_ASSERT_EQ(pistes[1].automation.size(), size_t(1));
    VSM_ASSERT_EQ(pistes[1].automation[0].parameter, std::string("mix.pan"));
    VSM_ASSERT_EQ(pistes[1].automation[0].points.size(), size_t(2));
    VSM_ASSERT_EQ(pistes[1].automation[0].points[0].tick, Tick(960));
    // Ce qui n'était pas sous le clip reste sur la piste d'origine.
    VSM_ASSERT_EQ(pistes[0].automation[0].points.size(), size_t(1));
    VSM_ASSERT_EQ(pistes[0].automation[0].points[0].tick, Tick(2880));
}

// --------------------------------------------------------------------------
// D18.3 — LES GROUPES D'ÉDITION.
//
// Couper une reconstruction multipiste à la mesure 33 demandait douze gestes,
// et un tick d'écart entre deux micros d'une même batterie casse leur phase,
// c'est-à-dire le son.
// --------------------------------------------------------------------------

VSM_TEST(a_selection_grows_to_the_other_tracks_of_the_same_edit_group) {
    std::vector<Track> pistes(4);
    for (int i = 0; i < 4; ++i)
        pistes[static_cast<size_t>(i)].clips = {clip(static_cast<uint64_t>(i + 1), 0, 1920)};
    pistes[0].editGroup = 1;
    pistes[1].editGroup = 1;
    pistes[2].editGroup = 2;      // un autre groupe : il ne suit pas
    // pistes[3] n'a aucun groupe : elle ne suit pas non plus.

    const auto elargie = expandSelectionToEditGroups(pistes, {1}, 1920);
    VSM_ASSERT_EQ(elargie.size(), size_t(2));
    VSM_ASSERT(elargie.count(1) == 1);
    VSM_ASSERT(elargie.count(2) == 1);
    VSM_ASSERT(elargie.count(3) == 0);
    VSM_ASSERT(elargie.count(4) == 0);
}

VSM_TEST(cutting_one_track_of_a_group_of_three_cuts_the_three_at_the_same_tick) {
    // LE CRITÈRE DE L'ÉTAPE. Le geste n'a pas changé d'une ligne : c'est la
    // SÉLECTION qui a grandi, et `splitClips` coupe ce qui est choisi.
    std::vector<Track> pistes(4);
    for (int i = 0; i < 4; ++i)
        pistes[static_cast<size_t>(i)].clips = {clip(static_cast<uint64_t>(i + 1), 0, 1920)};
    for (int i = 0; i < 3; ++i) pistes[static_cast<size_t>(i)].editGroup = 7;

    const auto choisis = expandSelectionToEditGroups(pistes, {1}, 1920);
    uint64_t compteur = 100;
    size_t coupes = 0;
    for (auto& piste : pistes)
        coupes += splitClips(piste, choisis, 960, 1920, compteur, {});

    VSM_ASSERT_EQ(coupes, size_t(3));
    for (int i = 0; i < 3; ++i) {
        const auto& c = pistes[static_cast<size_t>(i)].clips;
        VSM_ASSERT_EQ(c.size(), size_t(2));
        VSM_ASSERT_EQ(c[1].startTick, Tick(960));      // AU MÊME TICK, les trois
    }
    // Et la piste hors groupe n'a pas été touchée.
    VSM_ASSERT_EQ(pistes[3].clips.size(), size_t(1));
}

VSM_TEST(the_group_follows_by_overlap_and_not_by_identical_edges) {
    // Deux micros d'une même batterie sont découpés pareil, mais un clip a pu
    // être rogné : c'est encore le même passage, et il doit suivre.
    std::vector<Track> pistes(2);
    pistes[0].clips = {clip(1, 0, 1920)};
    pistes[1].clips = {clip(2, 480, 960), clip(3, 3840, 960)};   // l'un rogné, l'autre ailleurs
    pistes[0].editGroup = pistes[1].editGroup = 3;

    const auto elargie = expandSelectionToEditGroups(pistes, {1}, 4800);
    VSM_ASSERT(elargie.count(2) == 1);    // il recouvre
    VSM_ASSERT(elargie.count(3) == 0);    // il ne recouvre pas
}

VSM_TEST(without_a_group_the_selection_is_returned_untouched) {
    std::vector<Track> pistes(2);
    pistes[0].clips = {clip(1, 0, 960)};
    pistes[1].clips = {clip(2, 0, 960)};
    const auto elargie = expandSelectionToEditGroups(pistes, {1}, 960);
    VSM_ASSERT_EQ(elargie.size(), size_t(1));
    VSM_ASSERT(elargie.count(1) == 1);
    // Et une sélection vide reste vide : élargir le vide donnerait tout.
    VSM_ASSERT(expandSelectionToEditGroups(pistes, {}, 960).empty());
}
