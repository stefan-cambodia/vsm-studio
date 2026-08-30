#include "TestFramework.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/audio/engine/Transport.h"
#include "vsm/sequencer/Project.h"
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

using namespace vsm::audio::engine;
using namespace vsm::sequencer;

// D8.3 de docs/ROADMAP-daw.md — UN SEUL CHEMIN DE TRANSPORT.
//
// Deux notions de position coexistaient : celle d'un thread MIDI à l'horloge
// système, et celle du moteur audio comptant les échantillons sortis de la
// carte. La première a disparu. Ce que ces tests gardent, ce n'est pas
// « le transport marche » -- c'est ce qui rendait la coexistence intenable, et
// que la suppression devait réparer.

namespace {

Project projetAvecNotes() {
    Project projet;
    projet.ticksPerQuarterNote = 480;   // 120 BPM par défaut
    Track piste;
    uint64_t ids = 1;
    piste.addNote(0, 480, 60, 100, 0, ids);     // (start, END) : une noire
    piste.addNote(480, 960, 64, 100, 0, ids);   // la noire suivante
    projet.tracks.push_back(piste);
    return projet;
}

/// Un projet SANS LA MOINDRE NOTE, et une prise audio de trente secondes. C'est
/// le cas qui ne pouvait pas jouer du tout : sans note, le planning était vide,
/// la passe se terminait « naturellement » aussitôt, et le transport
/// s'arrêtait avant d'avoir commencé.
Project projetUniquementAudio() {
    Project projet;
    projet.ticksPerQuarterNote = 480;
    Track piste;
    piste.kind = Track::Kind::Audio;
    piste.audio.path = "prise.wav";
    piste.audio.sampleRate = 48000.0;
    piste.audio.frames = 48000 * 30;
    Clip clip;
    clip.startTick = 0;
    clip.length = 480 * 60;   // trente secondes à 120 BPM
    piste.clips.push_back(clip);
    projet.tracks.push_back(piste);
    return projet;
}

/// Fait avancer le graphe comme le ferait une carte son, sans en avoir une.
void rendre(ProcessGraph& graphe, int blocs, int taille = 512) {
    std::vector<float> gauche(static_cast<size_t>(taille), 0.0f);
    std::vector<float> droite(static_cast<size_t>(taille), 0.0f);
    for (int i = 0; i < blocs; ++i) graphe.processBlock(gauche.data(), droite.data(), taille);
}

} // namespace

VSM_TEST(the_transport_position_is_the_graph_position) {
    // IL N'Y A PLUS DEUX POSITIONS À RAPPROCHER : il n'y en a qu'une, et le
    // transport la LIT. Ce test échouerait à la seconde où quelqu'un
    // rétablirait un compteur parallèle.
    ProcessGraph graphe;
    graphe.prepare(48000.0, 512);
    graphe.setTrackInstrument(0, "vsm.testtone");
    graphe.setProject(projetAvecNotes());

    Transport transport(graphe);
    transport.setProject(projetAvecNotes());
    transport.play();

    rendre(graphe, 20);
    VSM_ASSERT_NEAR(transport.currentSeconds(), graphe.currentSeconds(), 0.0);
    VSM_ASSERT(transport.currentSeconds() > 0.2);
    VSM_ASSERT(transport.state() == TransportState::Playing);
}

VSM_TEST(the_position_advances_between_two_notes) {
    // LE DÉFAUT LE PLUS VISIBLE DE L'ANCIEN TRANSPORT : sa position n'avançait
    // qu'aux ÉVÉNEMENTS. Entre deux notes espacées, le curseur restait figé, et
    // sur une nappe tenue il ne bougeait plus du tout. Ici, une seule note très
    // longue, et la position doit avancer entre son début et sa fin.
    Project projet;
    projet.ticksPerQuarterNote = 480;
    Track piste;
    uint64_t ids = 1;
    piste.addNote(0, 480 * 20, 60, 100, 0, ids);   // dix secondes tenues
    projet.tracks.push_back(piste);

    ProcessGraph graphe;
    graphe.prepare(48000.0, 512);
    graphe.setTrackInstrument(0, "vsm.testtone");
    graphe.setProject(projet);
    Transport transport(graphe);
    transport.setProject(projet);
    transport.play();

    rendre(graphe, 10);
    const double apresDix = transport.currentSeconds();
    rendre(graphe, 10);
    const double apresVingt = transport.currentSeconds();
    VSM_ASSERT(apresVingt > apresDix);
    // Et l'avancée est exactement celle du temps rendu : dix blocs de 512 à
    // 48 kHz font 106,67 ms, au bit près.
    VSM_ASSERT_NEAR(apresVingt - apresDix, 10.0 * 512.0 / 48000.0, 1e-9);
}

VSM_TEST(a_project_made_only_of_audio_can_actually_play) {
    // LE BUG QUE CETTE PHASE RÉPARE, et il était total : un projet sans note
    // s'arrêtait au premier tour, quelle que soit la durée de son audio. Le DAW
    // savait charger une prise de neuf minutes et refusait de la lire.
    const Project projet = projetUniquementAudio();
    VSM_ASSERT_EQ(projet.lastUsedTick(), static_cast<vsm::midi::Tick>(0));
    VSM_ASSERT(projet.lastSoundingTick() > 0);

    ProcessGraph graphe;
    graphe.prepare(48000.0, 512);
    graphe.setProject(projet);
    Transport transport(graphe);
    transport.setProject(projet);

    // Le morceau dure trente secondes, plus la noire de marge.
    VSM_ASSERT(transport.endOfSongSeconds() > 30.0);
    transport.play();
    rendre(graphe, 100);   // ~1,07 s
    transport.poll();
    VSM_ASSERT(transport.state() == TransportState::Playing);
}

VSM_TEST(the_transport_stops_at_the_end_of_the_song) {
    const Project projet = projetAvecNotes();   // deux noires = 1 s, plus une noire de marge
    ProcessGraph graphe;
    graphe.prepare(48000.0, 512);
    graphe.setTrackInstrument(0, "vsm.testtone");
    graphe.setProject(projet);
    Transport transport(graphe);
    transport.setProject(projet);

    VSM_ASSERT_NEAR(transport.endOfSongSeconds(), 1.5, 1e-9);
    transport.play();
    rendre(graphe, 100);          // 1,07 s : pas encore fini
    transport.poll();
    VSM_ASSERT(transport.state() == TransportState::Playing);

    rendre(graphe, 60);           // 1,71 s : au-delà de la fin
    transport.poll();
    VSM_ASSERT(transport.state() == TransportState::Stopped);
    // ET L'ARRÊT REVIENT À ZÉRO, ce qui est toute la différence avec la pause.
    VSM_ASSERT_NEAR(transport.currentSeconds(), 0.0, 1e-12);
}

VSM_TEST(a_loop_never_reaches_the_end_of_the_song) {
    const Project projet = projetAvecNotes();
    ProcessGraph graphe;
    graphe.prepare(48000.0, 512);
    graphe.setTrackInstrument(0, "vsm.testtone");
    graphe.setProject(projet);
    Transport transport(graphe);
    transport.setProject(projet);
    transport.setLoopRegion(0, 480, true);   // une noire, une demi-seconde

    transport.play();
    rendre(graphe, 400);   // 4,3 s : bien au-delà de la fin du morceau
    transport.poll();
    VSM_ASSERT(transport.state() == TransportState::Playing);
    VSM_ASSERT(transport.currentSeconds() < 0.51);
}

VSM_TEST(pause_keeps_the_position_and_stop_gives_it_back) {
    const Project projet = projetAvecNotes();
    ProcessGraph graphe;
    graphe.prepare(48000.0, 512);
    graphe.setTrackInstrument(0, "vsm.testtone");
    graphe.setProject(projet);
    Transport transport(graphe);
    transport.setProject(projet);

    transport.play();
    rendre(graphe, 20);
    const double avant = transport.currentSeconds();
    transport.pause();
    VSM_ASSERT(transport.state() == TransportState::Paused);
    rendre(graphe, 20);   // à l'arrêt, le graphe n'avance plus la position
    VSM_ASSERT_NEAR(transport.currentSeconds(), avant, 1e-12);

    transport.stop();
    VSM_ASSERT(transport.state() == TransportState::Stopped);
    VSM_ASSERT_NEAR(transport.currentSeconds(), 0.0, 1e-12);
}

VSM_TEST(without_a_sound_card_the_same_clock_keeps_running) {
    // SANS CARTE SON, LE TEMPS DOIT AVANCER QUAND MÊME : l'application reste
    // utilisable pour éditer, faire défiler et exporter. Et il doit avancer par
    // le MÊME chemin -- un thread de secours qui appelle `processBlock` dans un
    // tampon qu'on jette --, parce qu'une seconde façon de faire avancer le
    // temps serait une seconde façon de se tromper.
    const Project projet = projetAvecNotes();
    ProcessGraph graphe;
    graphe.prepare(48000.0, 256);
    graphe.setTrackInstrument(0, "vsm.testtone");
    graphe.setProject(projet);
    Transport transport(graphe);
    transport.setProject(projet);

    transport.setAudioDeviceOpen(false, 48000.0, 256);
    VSM_ASSERT(transport.fallbackClockRunning());
    transport.play();

    const double depart = transport.currentSeconds();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const double arrivee = transport.currentSeconds();
    // Il a avancé, et pas n'importe comment : à peu près à la vitesse du temps.
    // La marge est large à dessein -- on éprouve l'existence de l'horloge, pas
    // l'ordonnanceur du système.
    VSM_ASSERT(arrivee - depart > 0.05);
    VSM_ASSERT(arrivee - depart < 0.5);

    // ET ELLE REND LA MAIN quand la carte revient : deux moteurs qui
    // avanceraient le même graphe le feraient avancer deux fois plus vite.
    transport.setAudioDeviceOpen(true);
    VSM_ASSERT(!transport.fallbackClockRunning());
    const double fige = transport.currentSeconds();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    VSM_ASSERT_NEAR(transport.currentSeconds(), fige, 1e-12);
}
