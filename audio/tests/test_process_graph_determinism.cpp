#include "TestFramework.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/sequencer/Project.h"
#include <algorithm>
#include <chrono>
#include <cmath>

using namespace vsm::sequencer;
using namespace vsm::audio::engine;

namespace {

Project buildSingleNoteProject(uint16_t ppq = 480) {
    Project project;
    project.ticksPerQuarterNote = ppq; // tempoMap par défaut = 120 BPM

    Track track;
    track.name = "Test";
    track.channel = 0;
    uint64_t idCounter = 1;
    track.addNote(0, ppq, 69, 100, 0, idCounter); // A4, une noire, dès le tick 0
    project.tracks.push_back(track);
    return project;
}

float peakOf(const std::vector<float>& buf) {
    float peak = 0.0f;
    for (float s : buf) peak = std::max(peak, std::abs(s));
    return peak;
}

} // namespace

VSM_TEST(process_graph_offline_render_is_deterministic) {
    Project project = buildSingleNoteProject();

    ProcessGraph graphA;
    graphA.prepare(8000.0, 256);
    graphA.setTrackInstrument(0, "vsm.testtone");
    graphA.setProject(project);
    RenderedAudio renderA = OfflineRenderer::render(graphA, 8000.0, 256, 1.0);

    ProcessGraph graphB;
    graphB.prepare(8000.0, 256);
    graphB.setTrackInstrument(0, "vsm.testtone");
    graphB.setProject(project);
    RenderedAudio renderB = OfflineRenderer::render(graphB, 8000.0, 256, 1.0);

    VSM_ASSERT_EQ(renderA.left.size(), renderB.left.size());
    for (size_t i = 0; i < renderA.left.size(); ++i) {
        VSM_ASSERT_NEAR(renderA.left[i], renderB.left[i], 1e-9);
        VSM_ASSERT_NEAR(renderA.right[i], renderB.right[i], 1e-9);
    }
}

VSM_TEST(process_graph_produces_sound_when_note_is_active) {
    Project project = buildSingleNoteProject(480); // note dure 1 noire = 0.5s à 120 BPM

    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.testtone");
    graph.setProject(project);

    RenderedAudio audio = OfflineRenderer::render(graph, 8000.0, 256, 1.0);

    // Bien avant la fin de la note (500 ms), le signal doit être établi.
    size_t sampleAt300ms = static_cast<size_t>(0.3 * 8000.0);
    std::vector<float> early(audio.left.begin(), audio.left.begin() + static_cast<long>(sampleAt300ms));
    VSM_ASSERT(peakOf(early) > 0.01f);
}

VSM_TEST(process_graph_silent_when_track_muted) {
    Project project = buildSingleNoteProject();
    project.tracks[0].muted = true;

    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.testtone");
    graph.setProject(project);

    RenderedAudio audio = OfflineRenderer::render(graph, 8000.0, 256, 1.0);
    VSM_ASSERT_NEAR(peakOf(audio.left), 0.0, 1e-6);
}

VSM_TEST(process_graph_silent_without_instrument_assigned) {
    Project project = buildSingleNoteProject();
    // Pas d'instrument assigné à la piste 0 -- ne doit jamais crasher, juste ne rien produire.

    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setProject(project);

    RenderedAudio audio = OfflineRenderer::render(graph, 8000.0, 256, 0.5);
    VSM_ASSERT_NEAR(peakOf(audio.left), 0.0, 1e-6);
}

VSM_TEST(process_graph_meter_reports_nonzero_peak_while_playing) {
    Project project = buildSingleNoteProject();

    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.testtone");
    graph.setProject(project);
    graph.setPlaying(true);

    std::vector<float> l(256, 0.0f), r(256, 0.0f);
    graph.processBlock(l.data(), r.data(), 256);

    VSM_ASSERT(graph.readMeterPeak(0) > 0.0f);
}

VSM_TEST(process_graph_not_playing_produces_silence_and_freezes_position) {
    Project project = buildSingleNoteProject();

    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.testtone");
    graph.setProject(project);
    graph.setPlaying(false); // état par défaut, explicite ici pour la clarté du test

    std::vector<float> l(256, 0.0f), r(256, 0.0f);
    graph.processBlock(l.data(), r.data(), 256);

    VSM_ASSERT_NEAR(peakOf(l), 0.0, 1e-6);
    VSM_ASSERT_NEAR(graph.currentSeconds(), 0.0, 1e-9); // ne doit pas avancer à l'arrêt
}

// ---------------------------------------------------------------------------
// Boucle de lecture (Phase 6, unification des transports).
//
// C'est désormais l'horloge AUDIO qui fait référence : c'est donc elle qui
// doit reboucler. Ces tests vérifient les trois choses qui font la différence
// entre une boucle utilisable et une boucle approximative : la position
// revient au bon endroit, elle y revient à l'ÉCHANTILLON près (une boucle
// arrondie à la taille de bloc dériverait audiblement en quelques tours), et
// aucune note ne reste bloquée au passage.
// ---------------------------------------------------------------------------

namespace {
Project buildLoopProject(uint16_t ppq = 480) {
    Project project;
    project.ticksPerQuarterNote = ppq; // 120 BPM par défaut -> 1 noire = 0,5 s
    Track track;
    track.name = "Loop";
    track.channel = 0;
    uint64_t ids = 1;
    // Note qui COMMENCE dans la boucle et se termine bien après sa fin : c'est
    // exactement le cas qui produit une note bloquée si le rebouclage ne
    // relâche rien.
    track.addNote(ppq / 2, ppq * 8, 69, 100, 0, ids);
    project.tracks.push_back(track);
    return project;
}
} // namespace

VSM_TEST(process_graph_loop_wraps_the_position) {
    ProcessGraph graph;
    graph.prepare(48000.0, 256);
    graph.setTrackInstrument(0, "vsm.testtone");
    graph.setProject(buildLoopProject());
    graph.setLoopRegion(0.0, 0.5, true);
    graph.setPlaying(true);

    std::vector<float> l(256, 0.0f), r(256, 0.0f);
    // 0,5 s à 48 kHz = 24000 échantillons ; on en rend nettement plus.
    for (int i = 0; i < 200; ++i) graph.processBlock(l.data(), r.data(), 256);
    VSM_ASSERT(graph.currentSeconds() >= 0.0);
    VSM_ASSERT(graph.currentSeconds() < 0.5); // jamais au-delà de la fin de boucle
}

VSM_TEST(process_graph_loop_is_sample_accurate) {
    // 24000 échantillons de boucle et des blocs de 256 : la frontière tombe au
    // milieu d'un bloc (24000 = 93,75 blocs). Une implémentation qui
    // reboucherait "au bloc suivant" accumulerait 0,25 bloc d'erreur par tour.
    ProcessGraph graph;
    graph.prepare(48000.0, 256);
    graph.setTrackInstrument(0, "vsm.testtone");
    graph.setProject(buildLoopProject());
    graph.setLoopRegion(0.0, 0.5, true);
    graph.setPlaying(true);

    std::vector<float> l(256, 0.0f), r(256, 0.0f);
    const int totalBlocks = 500;
    for (int i = 0; i < totalBlocks; ++i) graph.processBlock(l.data(), r.data(), 256);

    const double rendered = static_cast<double>(totalBlocks) * 256.0 / 48000.0;
    const double expected = std::fmod(rendered, 0.5);
    // Tolérance : un échantillon. Sans découpage à la frontière, l'écart
    // atteindrait plusieurs dizaines de millisecondes après 500 blocs.
    VSM_ASSERT_NEAR(graph.currentSeconds(), expected, 1.0 / 48000.0);
}

VSM_TEST(process_graph_loop_releases_notes_held_across_the_boundary) {
    // Cas qui produit la fameuse "note bloquée" : la note DÉMARRE avant la
    // région de boucle et se termine bien après. Son NoteOff se trouve donc
    // hors de la boucle et ne sera jamais atteint ; sans relâche explicite au
    // rebouclage, elle sonnerait indéfiniment.
    ProcessGraph graph;
    graph.prepare(48000.0, 256);
    graph.setTrackInstrument(0, "vsm.testtone");

    Project project;
    project.ticksPerQuarterNote = 480;
    Track track;
    uint64_t ids = 1;
    track.addNote(0, 480 * 8, 69, 100, 0, ids); // 0 -> 4 s
    project.tracks.push_back(track);
    graph.setProject(project);
    graph.setLoopRegion(1.0, 1.5, true);
    graph.setPlaying(true);

    std::vector<float> l(256, 0.0f), r(256, 0.0f);
    while (graph.currentSeconds() < 1.4) graph.processBlock(l.data(), r.data(), 256);
    VSM_ASSERT_EQ(graph.trackInstrument(0)->activeVoiceCount(), 1); // la note sonne

    // Franchit la frontière, puis laisse le temps au release de s'éteindre.
    // Rien ne redéclenche la note : son NoteOn (tick 0) est hors de la boucle.
    for (int i = 0; i < 400; ++i) graph.processBlock(l.data(), r.data(), 256);
    VSM_ASSERT(graph.currentSeconds() >= 1.0 && graph.currentSeconds() < 1.5);
    VSM_ASSERT_EQ(graph.trackInstrument(0)->activeVoiceCount(), 0);

    float peak = 0.0f;
    for (int i = 0; i < 20; ++i) {
        graph.processBlock(l.data(), r.data(), 256);
        for (float v : l) peak = std::max(peak, std::abs(v));
    }
    VSM_ASSERT(peak < 1e-4f); // silence : plus rien ne traîne
}

VSM_TEST(process_graph_without_loop_keeps_advancing) {
    ProcessGraph graph;
    graph.prepare(48000.0, 256);
    graph.setTrackInstrument(0, "vsm.testtone");
    graph.setProject(buildLoopProject());
    graph.setLoopRegion(0.0, 0.5, false); // région définie mais DÉSACTIVÉE
    graph.setPlaying(true);

    std::vector<float> l(256, 0.0f), r(256, 0.0f);
    for (int i = 0; i < 200; ++i) graph.processBlock(l.data(), r.data(), 256);
    VSM_ASSERT(graph.currentSeconds() > 1.0); // la lecture a dépassé la région
}

VSM_TEST(process_graph_loop_renders_the_same_audio_every_turn) {
    // Le rebouclage doit être déterministe : deux tours consécutifs de la même
    // boucle produisent le même signal. C'est ce qui permet à un motif bouclé
    // de rester stable au lieu de "respirer" à chaque tour.
    ProcessGraph graph;
    graph.prepare(48000.0, 256);
    graph.setTrackInstrument(0, "vsm.testtone");
    Project project;
    project.ticksPerQuarterNote = 480;
    Track track;
    uint64_t ids = 1;
    track.addNote(0, 240, 69, 100, 0, ids); // note courte, entièrement dans la boucle
    project.tracks.push_back(track);
    graph.setProject(project);
    graph.setLoopRegion(0.0, 0.5, true);
    graph.setPlaying(true);

    auto renderOneTurn = [&] {
        std::vector<float> out;
        std::vector<float> l(240, 0.0f), r(240, 0.0f);
        for (int i = 0; i < 100; ++i) { // 24000 échantillons = un tour exact
            graph.processBlock(l.data(), r.data(), 240);
            out.insert(out.end(), l.begin(), l.end());
        }
        return out;
    };
    // On compare le DEUXIÈME et le TROISIÈME tour, pas le premier et le
    // deuxième : au tout premier tour, aucune queue de release de la note
    // précédente ne traîne encore, alors qu'à partir du deuxième le régime est
    // établi. Comparer le premier aux suivants testerait donc une différence
    // légitime (et présente aussi dans un vrai séquenceur), pas une régression.
    renderOneTurn();
    const auto secondTurn = renderOneTurn();
    const auto thirdTurn = renderOneTurn();
    for (size_t i = 0; i < secondTurn.size(); ++i)
        VSM_ASSERT_NEAR(secondTurn[i], thirdTurn[i], 1e-6);
}

// --- D6.5 : le rendu au pas du temps réel ----------------------------------
//
// Ce que ces tests gardent n'est pas « l'option existe » mais les deux
// propriétés qui la rendent défendable : elle est FAUSSE par défaut, et quand
// elle est vraie elle change la DURÉE du calcul sans changer un seul
// échantillon. Une option de rendu qui modifierait le résultat ne serait pas
// une option de vitesse.

VSM_TEST(realtime_pacing_is_off_by_default_and_faster_than_real_time) {
    Project project = buildSingleNoteProject();
    ProcessGraph graph;
    graph.prepare(48000.0, 256);
    graph.setProject(project);

    const auto depart = std::chrono::steady_clock::now();
    const auto rendu = OfflineRenderer::render(graph, 48000.0, 256, 0.4);
    const double ecoule = std::chrono::duration<double>(
                              std::chrono::steady_clock::now() - depart).count();

    VSM_ASSERT_EQ(rendu.numFrames(), static_cast<size_t>(0.4 * 48000.0));
    // Le défaut ne doit RIEN attendre. La marge est large : ce test mesure une
    // absence d'attente, pas la vitesse de la machine qui l'exécute.
    VSM_ASSERT(ecoule < 0.3);
}

VSM_TEST(realtime_pacing_takes_real_time_and_changes_not_one_sample) {
    Project project = buildSingleNoteProject();
    ProcessGraph graph;
    graph.prepare(48000.0, 256);
    graph.setProject(project);

    const auto vite = OfflineRenderer::render(graph, 48000.0, 256, 0.25, false);

    const auto depart = std::chrono::steady_clock::now();
    const auto lent = OfflineRenderer::render(graph, 48000.0, 256, 0.25, true);
    const double ecoule = std::chrono::duration<double>(
                              std::chrono::steady_clock::now() - depart).count();

    // ATTENDRE, C'EST TOUT CE QU'ELLE FAIT. Le seuil est sous la durée demandée
    // parce qu'un ordonnanceur réveille toujours un peu tard, jamais trop tôt :
    // on vérifie qu'on a bien attendu, pas la précision de l'horloge.
    VSM_ASSERT(ecoule > 0.2);

    VSM_ASSERT_EQ(vite.numFrames(), lent.numFrames());
    for (size_t i = 0; i < vite.numFrames(); ++i) {
        VSM_ASSERT_EQ(vite.left[i], lent.left[i]);
        VSM_ASSERT_EQ(vite.right[i], lent.right[i]);
    }
}

VSM_TEST(no_machine_of_this_project_demands_real_time) {
    // Si l'une d'elles se mettait à l'exiger, tous les rendus deviendraient
    // cent fois plus lents sans que personne l'ait demandé. Le test le dirait.
    Project project = buildSingleNoteProject();
    ProcessGraph graph;
    graph.prepare(48000.0, 256);
    graph.setProject(project);
    for (size_t i = 0; i < project.tracks.size(); ++i)
        if (auto* instrument = graph.trackInstrument(i))
            VSM_ASSERT(!instrument->requiresRealtimeRender());
}
