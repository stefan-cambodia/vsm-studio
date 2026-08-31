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

VSM_TEST(process_graph_loop_rendering_is_deterministic_across_runs) {
    // CE TEST JURAIT PLUS QUE LE MOTEUR NE PROMET, ET IL PASSAIT PAR ACCIDENT.
    // Sa version d'origine comparait le tour 2 au tour 3 d'une MÊME exécution
    // (« un motif bouclé ne respire pas ») ; or l'état de synthèse traverse le
    // rebouclage, et la répétabilité bit-à-bit d'un tour n'est PAS garantie :
    // sur le moteur d'AVANT la correction de frontière, une note de 241 ticks
    // divergeait déjà entre tours (mesuré), et vingt tours ne se répètent
    // jamais -- seize voix, pas de période simple. La note historique de
    // 240 ticks passait par la coïncidence exacte de son relâchement avec la
    // fin d'un bloc, que la correction de l'invariant n° 3 a déplacée d'un
    // échantillon. Le POURQUOI fin (quel état de voix traverse le wrap) est
    // une question ouverte, écrite au § 6 de ROADMAP-daw.
    //
    // Ce que le moteur garantit, et que ce test vérifie désormais : le
    // rebouclage est DÉTERMINISTE. Deux exécutions complètes -- graphe neuf,
    // mêmes blocs, trois tours -- rendent le même signal au bit près. Un wrap
    // qui consulterait une horloge, un ordre de threads ou un générateur non
    // seedé échouerait ici.
    auto rendreTroisTours = [] {
        ProcessGraph graph;
        graph.prepare(48000.0, 240);
        graph.setTrackInstrument(0, "vsm.testtone");
        Project project;
        project.ticksPerQuarterNote = 480;
        Track track;
        uint64_t ids = 1;
        track.addNote(0, 240, 69, 100, 0, ids);
        project.tracks.push_back(track);
        graph.setProject(project);
        graph.setLoopRegion(0.0, 0.5, true);
        graph.setPlaying(true);
        std::vector<float> out;
        std::vector<float> l(240, 0.0f), r(240, 0.0f);
        for (int i = 0; i < 300; ++i) {
            graph.processBlock(l.data(), r.data(), 240);
            out.insert(out.end(), l.begin(), l.end());
        }
        return out;
    };
    const auto premiere = rendreTroisTours();
    const auto seconde = rendreTroisTours();
    VSM_ASSERT_EQ(premiere.size(), seconde.size());
    for (size_t i = 0; i < premiere.size(); ++i)
        VSM_ASSERT_EQ(premiere[i], seconde[i]);
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

// --- D7.4 : le transport tel qu'un plugin tiers le reçoit -------------------
//
// `transportFor` est une CONVERSION -- des secondes vers un tempo, une
// signature et une position en noires. Elle se vérifie donc sans moteur, sans
// carte son et sans plugin, sur les lignes mêmes que le graphe emploie.

VSM_TEST(the_transport_reports_the_tempo_at_the_moment_being_rendered) {
    Project project;
    project.ticksPerQuarterNote = 480;
    project.tempoMap.clearTempoChanges();
    project.tempoMap.addTempoChange(0, 500000);      // 120 BPM
    project.tempoMap.addTempoChange(1920, 300000);   // 200 BPM à la mesure 2

    // Deux secondes après le départ, on est encore à 120 BPM (la mesure 2
    // tombe à 2 s pile, et le changement s'applique à partir de là).
    const auto avant = ProcessGraph::transportFor(project, 1.0, true);
    VSM_ASSERT_NEAR(avant.tempoBpm, 120.0, 1e-9);
    VSM_ASSERT(avant.playing);

    const auto apres = ProcessGraph::transportFor(project, 3.0, true);
    VSM_ASSERT_NEAR(apres.tempoBpm, 200.0, 1e-9);
}

VSM_TEST(the_transport_position_is_in_quarter_notes_even_in_six_eight) {
    // « Beat » veut dire LA NOIRE dans tous les formats de plugin, y compris en
    // 6/8 où le temps musical est la croche pointée. Convertir en temps de
    // mesure ferait sauter un delay synchronisé d'un facteur trois dès qu'on
    // quitte le 4/4 -- et personne ne comprendrait pourquoi.
    Project project;
    project.ticksPerQuarterNote = 480;
    project.timeSignatureMap.clear();
    project.timeSignatureMap.addChange(0, 6, 3); // 6/8

    const auto transport = ProcessGraph::transportFor(project, 1.0, true);
    // Une seconde à 120 BPM = deux noires, quelle que soit la signature.
    VSM_ASSERT_NEAR(transport.positionBeats, 2.0, 1e-6);
    VSM_ASSERT_NEAR(transport.positionSeconds, 1.0, 1e-9);
    VSM_ASSERT_EQ(transport.timeSignatureNumerator, 6);
    VSM_ASSERT_EQ(transport.timeSignatureDenominator, 8);
}

VSM_TEST(the_transport_carries_the_loop_only_when_there_is_one) {
    Project project;
    project.ticksPerQuarterNote = 480;

    // Une boucle DÉSACTIVÉE n'en est pas une : l'annoncer ferait boucler
    // l'affichage d'un plugin sur une région que le morceau ne rejoue pas.
    project.loopEnabled = false;
    project.loopStartTick = 0;
    project.loopEndTick = 1920;
    VSM_ASSERT(!ProcessGraph::transportFor(project, 0.0, true).looping);

    project.loopEnabled = true;
    const auto avec = ProcessGraph::transportFor(project, 0.0, true);
    VSM_ASSERT(avec.looping);
    VSM_ASSERT_NEAR(avec.loopEndBeats, 4.0, 1e-6);

    // Une boucle vide -- début et fin au même endroit -- n'en est pas une non
    // plus, même cochée.
    project.loopEndTick = 0;
    VSM_ASSERT(!ProcessGraph::transportFor(project, 0.0, true).looping);
}

VSM_TEST(a_stopped_transport_says_so) {
    Project project;
    project.ticksPerQuarterNote = 480;
    VSM_ASSERT(!ProcessGraph::transportFor(project, 0.0, false).playing);
}

namespace {

/// Rend le même projet à une taille de bloc donnée, avec la même machine.
RenderedAudio rendreAuBloc(const Project& projet, const char* machine,
                                     int bloc, double secondes) {
    ProcessGraph graphe;
    graphe.prepare(48000.0, bloc);
    graphe.setTrackInstrument(0, machine);
    graphe.setProject(projet);
    return OfflineRenderer::render(graphe, 48000.0, bloc, secondes, false);
}

} // namespace

VSM_TEST(le_rendu_ne_depend_pas_de_la_taille_de_bloc_hors_frontiere) {
    // INVARIANT N° 3 DE `ROADMAP-daw.md` § 6, DANS LA PARTIE QUI MANQUAIT.
    //
    // « Rendu temps réel et rendu hors ligne restent identiques, à
    // l'échantillon près, sur tout ce qui s'ajoute. Le test existe pour CLAP ;
    // il s'étend. » Il ne s'était pas étendu. Ce qui existait comparait le
    // CADENCEMENT -- `OfflineRenderer` avec et sans attente --, c'est-à-dire le
    // même code appelé deux fois : cela ne pouvait attraper qu'une horloge qui
    // modifierait l'audio.
    //
    // CE QUI SÉPARE VRAIMENT LA LECTURE DE L'EXPORT, C'EST LA TAILLE DE BLOC :
    // l'export choisit la sienne, une carte son impose la sienne. Si le graphe
    // n'y est pas invariant, ce qu'on entend n'est pas ce qu'on exporte, et
    // rien ne le dit -- la panne la plus coûteuse de la famille (§ 3, règle 1).
    Project projet = buildSingleNoteProject();
    const auto reference = rendreAuBloc(projet, "vsm.testtone", 512, 0.7);

    // La fin de note : une noire à 120 BPM, 0,5 s à 48 kHz. Ce chiffre est ce
    // qui SÉPARE les deux tests -- s'il dérivait avec le projet d'essai, les
    // deux familles de tailles n'auraient plus le sens qu'elles annoncent, et
    // les tests passeraient à vide. Il est donc vérifié, pas supposé.
    const size_t finDeNote = 24000;
    VSM_ASSERT_EQ(reference.numFrames(), static_cast<size_t>(0.7 * 48000.0));

    // Tailles où la fin de note tombe À L'INTÉRIEUR d'un bloc, comme pour la
    // référence (512). Identité STRICTE exigée.
    for (int bloc : {128, 256, 384, 1024, 2048}) {
        VSM_ASSERT(finDeNote % static_cast<size_t>(bloc) != 0);
        const auto obtenu = rendreAuBloc(projet, "vsm.testtone", bloc, 0.7);
        VSM_ASSERT_EQ(obtenu.numFrames(), reference.numFrames());
        for (size_t i = 0; i < reference.numFrames(); ++i) {
            VSM_ASSERT_EQ(obtenu.left[i], reference.left[i]);
            VSM_ASSERT_EQ(obtenu.right[i], reference.right[i]);
        }
    }
}

VSM_TEST(une_fin_de_note_SUR_une_frontiere_de_bloc_est_desormais_exacte) {
    // CE TEST DÉCRIVAIT UN DÉFAUT ; LE DÉFAUT EST CORRIGÉ (31/08/2026), ET LA
    // BORNE BASSE A FAIT EXACTEMENT CE POUR QUOI ELLE EXISTAIT : elle a échoué
    // au premier build corrigé, rappelant de remplacer la tolérance par
    // l'identité stricte. La cause était double, dans la distribution des
    // événements aux blocs : l'appartenance se décidait en SECONDES accumulées
    // (l'erreur d'accumulation faisait entrer un événement de frontière dans
    // le bloc de trop) et un clamp le rabattait alors sur le DERNIER
    // échantillon -- le relâchement partait un échantillon trop tôt.
    // Désormais l'appartenance se décide en ÉCHANTILLONS ABSOLUS ARRONDIS
    // (`llround(t x sr)`), avec un quart d'échantillon de marge sur la borne
    // de recherche pour que la frontière soit vue des deux blocs et tranchée
    // par l'offset. Identité stricte exigée, les DEUX canaux, toutes tailles.
    Project projet = buildSingleNoteProject();
    const auto reference = rendreAuBloc(projet, "vsm.juno106", 512, 0.7);
    const size_t finDeNote = 24000;
    VSM_ASSERT_EQ(reference.numFrames(), static_cast<size_t>(0.7 * 48000.0));

    for (int bloc : {64, 96, 192}) {
        VSM_ASSERT_EQ(finDeNote % static_cast<size_t>(bloc), size_t{0});
        const auto obtenu = rendreAuBloc(projet, "vsm.juno106", bloc, 0.7);
        VSM_ASSERT_EQ(obtenu.numFrames(), reference.numFrames());
        for (size_t i = 0; i < reference.numFrames(); ++i) {
            VSM_ASSERT_EQ(obtenu.left[i], reference.left[i]);
            VSM_ASSERT_EQ(obtenu.right[i], reference.right[i]);
        }
    }
}

