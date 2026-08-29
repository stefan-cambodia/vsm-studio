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
