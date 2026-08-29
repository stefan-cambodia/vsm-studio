#include "TestFramework.h"
#include "vsm/sequencer/MidiRecorder.h"
#include "vsm/sequencer/Project.h"
#include <vector>

using namespace vsm::midi;
using namespace vsm::sequencer;

// D3.3 de docs/ROADMAP-daw.md — L'ENREGISTREMENT MIDI TEMPS RÉEL.
//
// Jusqu'ici `Track::armed` était écrit par un bouton et lu par personne : on
// pouvait armer une piste, et rien n'arrivait. Ce qui suit couvre le seul vrai
// travail de l'enregistrement -- transformer des touches en notes -- et tous
// les cas où l'appariement se casse.

namespace {
/// 480 ticks par noire, 120 BPM : une noire = 0,5 s, un tick = 1/960 s.
Project projetDeReference() {
    Project projet;
    projet.ticksPerQuarterNote = 480;
    return projet;
}

std::function<Tick(double)> conversion(const Project& projet) {
    return [&projet](double secondes) { return projet.secondsToTicks(secondes); };
}
}

VSM_TEST(a_pressed_and_released_key_becomes_one_note) {
    Project projet = projetDeReference();
    MidiRecorder enregistreur;
    enregistreur.begin(0.0);
    enregistreur.push({0.5, 60, 100, 0, true});
    enregistreur.push({1.0, 60, 64, 0, false});

    uint64_t compteur = 1;
    auto notes = enregistreur.finish(2.0, conversion(projet), compteur);
    VSM_ASSERT_EQ(notes.size(), size_t(1));
    VSM_ASSERT_EQ(notes[0].startTick, Tick(480));
    VSM_ASSERT_EQ(notes[0].endTick, Tick(960));
    VSM_ASSERT_EQ(int(notes[0].number), 60);
    VSM_ASSERT_EQ(int(notes[0].velocity), 100);
    VSM_ASSERT_EQ(int(notes[0].releaseVelocity), 64);
    VSM_ASSERT_EQ(notes[0].id, uint64_t(1));
}

VSM_TEST(a_key_still_held_at_stop_is_kept_and_closed_there) {
    // La note tenue de la fin est presque toujours la dernière du morceau :
    // la jeter serait perdre ce qu'on venait de jouer.
    Project projet = projetDeReference();
    MidiRecorder enregistreur;
    enregistreur.begin(0.0);
    enregistreur.push({0.0, 48, 90, 0, true});

    uint64_t compteur = 1;
    auto notes = enregistreur.finish(1.5, conversion(projet), compteur);
    VSM_ASSERT_EQ(notes.size(), size_t(1));
    VSM_ASSERT_EQ(notes[0].startTick, Tick(0));
    VSM_ASSERT_EQ(notes[0].endTick, Tick(1440));
}

VSM_TEST(a_release_without_a_press_is_ignored) {
    // Le vrai cas : la touche était déjà enfoncée quand on a appuyé sur Rec.
    // Inventer une note qui commencerait au point d'entrée serait écrire ce
    // qu'on n'a pas joué.
    Project projet = projetDeReference();
    MidiRecorder enregistreur;
    enregistreur.begin(0.0);
    enregistreur.push({0.2, 72, 0, 0, false});

    uint64_t compteur = 1;
    auto notes = enregistreur.finish(1.0, conversion(projet), compteur);
    VSM_ASSERT(notes.empty());
}

VSM_TEST(the_same_pitch_struck_twice_closes_the_oldest_first) {
    // Fermer le plus RÉCENT laisserait la première note traîner jusqu'à la fin
    // de la prise -- la « note bloquée » classique.
    Project projet = projetDeReference();
    MidiRecorder enregistreur;
    enregistreur.begin(0.0);
    enregistreur.push({0.0, 60, 100, 0, true});
    enregistreur.push({0.25, 60, 110, 0, true});
    enregistreur.push({0.5, 60, 64, 0, false});
    enregistreur.push({0.75, 60, 64, 0, false});

    uint64_t compteur = 1;
    auto notes = enregistreur.finish(1.0, conversion(projet), compteur);
    VSM_ASSERT_EQ(notes.size(), size_t(2));
    VSM_ASSERT_EQ(notes[0].startTick, Tick(0));
    VSM_ASSERT_EQ(notes[0].endTick, Tick(480));    // fermée par le PREMIER relâchement
    VSM_ASSERT_EQ(notes[1].startTick, Tick(240));
    VSM_ASSERT_EQ(notes[1].endTick, Tick(720));
}

VSM_TEST(what_is_played_before_the_punch_point_never_enters_the_take) {
    // C'est la définition du décompte : on compte pour se caler, on ne joue pas
    // encore. Les positions négatives sont celles du décompte, avant le zéro du
    // morceau.
    Project projet = projetDeReference();
    MidiRecorder enregistreur;
    enregistreur.begin(0.0);
    enregistreur.push({-1.0, 60, 100, 0, true});   // pendant le décompte
    enregistreur.push({-0.5, 60, 64, 0, false});
    enregistreur.push({0.5, 62, 100, 0, true});
    enregistreur.push({1.0, 62, 64, 0, false});

    uint64_t compteur = 1;
    auto notes = enregistreur.finish(2.0, conversion(projet), compteur);
    VSM_ASSERT_EQ(notes.size(), size_t(1));
    VSM_ASSERT_EQ(int(notes[0].number), 62);
}

VSM_TEST(a_note_too_short_to_measure_still_lasts_one_tick) {
    // Sans ce plancher, une frappe très brève donnerait une note de début et de
    // fin identiques : invisible dans le piano roll, muette au rendu.
    Project projet = projetDeReference();
    MidiRecorder enregistreur;
    enregistreur.begin(0.0);
    enregistreur.push({0.5, 64, 100, 0, true});
    enregistreur.push({0.5001, 64, 64, 0, false});

    uint64_t compteur = 1;
    auto notes = enregistreur.finish(1.0, conversion(projet), compteur);
    VSM_ASSERT_EQ(notes.size(), size_t(1));
    VSM_ASSERT(notes[0].durationTicks() >= 1);
}

VSM_TEST(the_same_take_can_be_written_to_several_tracks) {
    // Deux pistes armées reçoivent la MÊME prise, mais chacune ses propres
    // identifiants : sans quoi la sélection, l'automation liée et l'humanisation
    // confondraient les notes de l'une avec celles de l'autre.
    Project projet = projetDeReference();
    MidiRecorder enregistreur;
    enregistreur.begin(0.0);
    enregistreur.push({0.0, 60, 100, 0, true});
    enregistreur.push({0.5, 60, 64, 0, false});

    uint64_t compteur = 1;
    auto premiere = enregistreur.finish(1.0, conversion(projet), compteur);
    auto seconde = enregistreur.finish(1.0, conversion(projet), compteur);
    VSM_ASSERT_EQ(premiere.size(), size_t(1));
    VSM_ASSERT_EQ(seconde.size(), size_t(1));
    VSM_ASSERT(premiere[0].id != seconde[0].id);
    VSM_ASSERT_EQ(premiere[0].startTick, seconde[0].startTick);
}

VSM_TEST(overdub_adds_to_what_was_already_there) {
    Track piste;
    piste.channel = 3;
    uint64_t compteur = 1;
    piste.addNote(0, 240, 36, 100, 3, compteur);

    std::vector<Note> prise;
    Note ajoutee;
    ajoutee.startTick = 480;
    ajoutee.endTick = 720;
    ajoutee.number = 38;
    ajoutee.channel = 9;   // le clavier émettait sur un autre canal
    ajoutee.id = compteur++;
    prise.push_back(ajoutee);

    applyRecording(piste, prise, RecordMode::Overdub, 480, 960);
    VSM_ASSERT_EQ(piste.notes.size(), size_t(2));
    VSM_ASSERT_EQ(int(piste.notes[0].number), 36);
    VSM_ASSERT_EQ(int(piste.notes[1].number), 38);
    // LE CANAL DE LA PISTE L'EMPORTE : une prise gardant celui du clavier
    // ferait jouer la piste sur deux canaux à la fois.
    VSM_ASSERT_EQ(int(piste.notes[1].channel), 3);
}

VSM_TEST(replace_erases_only_what_starts_inside_the_take) {
    // Une note tenue commencée AVANT le point d'entrée appartient à ce qui
    // précède : l'effacer détruirait hors de la région désignée.
    Track piste;
    uint64_t compteur = 1;
    piste.addNote(0, 2000, 36, 100, 0, compteur);    // traverse toute la prise
    piste.addNote(500, 600, 38, 100, 0, compteur);   // dedans
    piste.addNote(1000, 1100, 40, 100, 0, compteur); // après

    std::vector<Note> prise;
    Note ajoutee;
    ajoutee.startTick = 520;
    ajoutee.endTick = 700;
    ajoutee.number = 42;
    ajoutee.id = compteur++;
    prise.push_back(ajoutee);

    applyRecording(piste, prise, RecordMode::Replace, 480, 960);
    VSM_ASSERT_EQ(piste.notes.size(), size_t(3));
    VSM_ASSERT_EQ(int(piste.notes[0].number), 36);  // traversante : conservée
    VSM_ASSERT_EQ(int(piste.notes[1].number), 42);  // la prise
    VSM_ASSERT_EQ(int(piste.notes[2].number), 40);  // après : conservée
}

VSM_TEST(a_take_recorded_over_a_tempo_change_lands_on_the_right_ticks) {
    // La conversion passe par la carte de tempo du projet : un enregistrement
    // fait après un changement de tempo doit tomber sur les ticks du morceau,
    // pas sur ceux qu'on aurait eus à tempo constant.
    Project projet = projetDeReference();
    projet.tempoMap.addTempoChange(960, 250000);   // 240 BPM à partir de la mesure 2

    MidiRecorder enregistreur;
    enregistreur.begin(0.0);
    // 960 ticks à 120 BPM = 1,0 s ; puis 480 ticks à 240 BPM = 0,25 s.
    enregistreur.push({1.25, 60, 100, 0, true});
    enregistreur.push({1.5, 60, 64, 0, false});

    uint64_t compteur = 1;
    auto notes = enregistreur.finish(2.0, conversion(projet), compteur);
    VSM_ASSERT_EQ(notes.size(), size_t(1));
    VSM_ASSERT_EQ(notes[0].startTick, Tick(1440));
    VSM_ASSERT_EQ(notes[0].endTick, Tick(1920));
}
