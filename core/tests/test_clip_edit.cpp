#include "TestFramework.h"
#include "vsm/sequencer/ClipEdit.h"
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
