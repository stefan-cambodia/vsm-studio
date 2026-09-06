#include "TestFramework.h"
#include "vsm/audio/engine/MasterBus.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/sequencer/Project.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace vsm::audio::engine;
using vsm::sequencer::Project;
using vsm::sequencer::Track;

namespace {
constexpr double kTwoPiD = 6.28318530717958647692;

float peakOf(const std::vector<float>& b) {
    float p = 0.0f;
    for (float s : b) p = std::max(p, std::abs(s));
    return p;
}

void fillSine(std::vector<float>& l, std::vector<float>& r, float amp, float freq, double sr) {
    for (size_t i = 0; i < l.size(); ++i) {
        l[i] = amp * std::sin(static_cast<float>(kTwoPiD * freq * static_cast<double>(i) / sr));
        r[i] = l[i];
    }
}
} // namespace

VSM_TEST(master_bus_bypassed_is_transparent) {
    MasterBus bus;
    bus.prepare(48000.0, 512);
    // enabled = false par défaut -> process() ne doit RIEN changer.
    std::vector<float> l(1000), r(1000);
    fillSine(l, r, 0.8f, 440.0f, 48000.0);
    std::vector<float> refL = l, refR = r;
    bus.process(l.data(), r.data(), static_cast<int>(l.size()));
    for (size_t i = 0; i < l.size(); ++i) {
        VSM_ASSERT_EQ(l[i], refL[i]);
        VSM_ASSERT_EQ(r[i], refR[i]);
    }
}

VSM_TEST(master_bus_limiter_caps_output) {
    MasterBus bus;
    bus.prepare(48000.0, 4096);
    bus.setEnabled(true);
    bus.setParameter(MasterBus::kLimiterCeilingDb, -6.0f); // ~0.501 linéaire

    std::vector<float> l(48000), r(48000);
    fillSine(l, r, 1.0f, 220.0f, 48000.0); // 0 dBFS, au-dessus du plafond
    bus.process(l.data(), r.data(), static_cast<int>(l.size()));

    const float ceiling = std::pow(10.0f, -6.0f / 20.0f);
    VSM_ASSERT(peakOf(l) <= ceiling + 1e-4f);
    VSM_ASSERT(peakOf(r) <= ceiling + 1e-4f);
}

VSM_TEST(master_bus_reports_lufs_when_active) {
    MasterBus bus;
    bus.prepare(48000.0, 4096);
    bus.setEnabled(true);
    std::vector<float> l(48000), r(48000);
    fillSine(l, r, 0.5f, 1000.0f, 48000.0);
    bus.process(l.data(), r.data(), static_cast<int>(l.size()));

    const double lufs = bus.integratedLufs();
    VSM_ASSERT(lufs > vsm::audio::dsp::LufsMeter::kSilence);
    VSM_ASSERT(lufs < 6.0); // borne de sécurité, jamais absurde
    VSM_ASSERT(bus.outputPeak() > 0.0f);
}

VSM_TEST(master_bus_low_shelf_boosts_low_end) {
    auto lowRms = [](float shelfGainDb) {
        MasterBus bus;
        bus.prepare(48000.0, 4096);
        bus.setEnabled(true);
        bus.setParameter(MasterBus::kLowShelfGainDb, shelfGainDb);
        bus.setParameter(MasterBus::kLimiterCeilingDb, 0.0f); // ne pas écrêter la mesure
        std::vector<float> l(24000), r(24000);
        fillSine(l, r, 0.2f, 50.0f, 48000.0); // 50 Hz, dans la bande du low shelf
        bus.process(l.data(), r.data(), static_cast<int>(l.size()));
        double acc = 0.0;
        for (size_t i = l.size() / 3; i < l.size(); ++i) acc += static_cast<double>(l[i]) * l[i];
        return std::sqrt(acc / (l.size() - l.size() / 3));
    };
    VSM_ASSERT(lowRms(12.0f) > lowRms(0.0f) * 1.5); // +12 dB doit clairement remonter le grave
}

// --- Intégration dans le chemin de rendu ---------------------------------

namespace {
Project buildSingleNoteProject(uint16_t ppq = 480) {
    Project project;
    project.ticksPerQuarterNote = ppq;
    Track track;
    track.name = "Test";
    track.channel = 0;
    uint64_t id = 1;
    track.addNote(0, ppq * 2, 69, 120, 0, id); // A4, deux noires, vélocité forte
    project.tracks.push_back(track);
    return project;
}
} // namespace

VSM_TEST(process_graph_master_limiter_reduces_peak) {
    Project project = buildSingleNoteProject();

    // Rendu SANS master (référence).
    ProcessGraph graphRef;
    graphRef.prepare(8000.0, 256);
    graphRef.setTrackInstrument(0, "vsm.minimoog");
    graphRef.setProject(project);
    RenderedAudio ref = OfflineRenderer::render(graphRef, 8000.0, 256, 1.0);
    const float refPeak = peakOf(ref.left);

    // Rendu AVEC master + limiteur bas.
    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.minimoog");
    graph.setProject(project);
    graph.masterBus().setEnabled(true);
    graph.masterBus().setParameter(MasterBus::kLimiterCeilingDb, -18.0f); // ~0.126
    RenderedAudio out = OfflineRenderer::render(graph, 8000.0, 256, 1.0);
    const float outPeak = peakOf(out.left);

    const float ceiling = std::pow(10.0f, -18.0f / 20.0f);
    VSM_ASSERT(refPeak > ceiling);            // la référence dépassait bien le plafond
    VSM_ASSERT(outPeak <= ceiling + 1e-3f);   // le master l'a ramenée sous le plafond
}

// --- D23.5 : l'écoute en mono ---------------------------------------------

VSM_TEST(mono_listen_folds_a_hard_left_signal_to_both_sides_even_when_bypassed) {
    MasterBus bus;
    bus.prepare(48000.0, 512);
    std::vector<float> l(600), r(600, 0.0f);
    for (size_t i = 0; i < l.size(); ++i)
        l[i] = 0.8f * std::sin(static_cast<float>(kTwoPiD * 440.0 * static_cast<double>(i) / 48000.0));
    // Tranche contournée : sans écoute mono, rien ne bouge (le test au-dessus) ;
    // avec, les deux côtés reçoivent la demi-somme.
    bus.setMonoListen(true);
    bus.process(l.data(), r.data(), static_cast<int>(l.size()));
    for (size_t i = 0; i < l.size(); ++i) {
        VSM_ASSERT_NEAR(l[i], r[i], 1e-7);
        const float attendu = 0.4f * std::sin(static_cast<float>(kTwoPiD * 440.0 * static_cast<double>(i) / 48000.0));
        VSM_ASSERT_NEAR(l[i], attendu, 1e-6);
    }
    // Tranche active : la corrélation MESURÉE est celle de ce qu'on entend, 1.
    bus.setEnabled(true);
    std::vector<float> l2(600), r2(600);
    for (size_t i = 0; i < l2.size(); ++i) { l2[i] = 0.5f * std::sin(0.05f * static_cast<float>(i)); r2[i] = -l2[i]; }
    bus.process(l2.data(), r2.data(), static_cast<int>(l2.size()));
    VSM_ASSERT_NEAR(bus.outputCorrelation(), 1.0, 1e-3);
    // Éteinte, elle redevient transparente.
    bus.setMonoListen(false);
    bus.setEnabled(false);
    std::vector<float> l3(4, 1.0f), r3(4, -1.0f);
    bus.process(l3.data(), r3.data(), 4);
    VSM_ASSERT_NEAR(l3[0], 1.0, 1e-7);
    VSM_ASSERT_NEAR(r3[0], -1.0, 1e-7);
}

// --- D23.1 : la polarité d'une piste ---------------------------------------

VSM_TEST(a_track_with_inverted_polarity_cancels_its_twin) {
    Project project = buildSingleNoteProject();
    project.tracks.push_back(project.tracks[0]);   // la jumelle, même notes
    // Témoin : les deux en phase, le rendu est le double d'une piste.
    ProcessGraph deux;
    deux.prepare(8000.0, 256);
    deux.setTrackInstrument(0, "vsm.minimoog");
    deux.setTrackInstrument(1, "vsm.minimoog");
    deux.setProject(project);
    const float crete2 = peakOf(OfflineRenderer::render(deux, 8000.0, 256, 1.0).left);
    VSM_ASSERT(crete2 > 0.05f);

    project.tracks[1].invertPhase = true;
    ProcessGraph oppose;
    oppose.prepare(8000.0, 256);
    oppose.setTrackInstrument(0, "vsm.minimoog");
    oppose.setTrackInstrument(1, "vsm.minimoog");
    oppose.setProject(project);
    const RenderedAudio rendu = OfflineRenderer::render(oppose, 8000.0, 256, 1.0);
    VSM_ASSERT(peakOf(rendu.left) < 1e-4f);
    VSM_ASSERT(peakOf(rendu.right) < 1e-4f);
}

// --- D24.1 : un contrôleur en direct atteint la machine ---------------------

VSM_TEST(a_live_pitch_bend_is_delivered_to_the_track_instrument) {
    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.minimoog");
    graph.setProject(buildSingleNoteProject());
    vsm::audio::plugin::MidiControlEvent molette;
    molette.kind = vsm::audio::plugin::MidiControlEvent::Kind::PitchBend;
    molette.value = 1.0f;   // un demi-ton
    VSM_ASSERT(graph.sendLiveControl(ProcessGraph::LiveNoteSource::MidiInput, 0, molette));
    const uint64_t ignoresAvant = graph.ignoredControlEvents();
    std::vector<float> l(256), r(256);
    graph.processBlock(l.data(), r.data(), 256);   // transport à l'arrêt : le bloc est rendu pour la livrer
    VSM_ASSERT_EQ(graph.liveControlsDelivered(), static_cast<uint64_t>(1));
    VSM_ASSERT_EQ(graph.ignoredControlEvents(), ignoresAvant);
    // Une piste hors bornes est refusée, jamais bloquante.
    VSM_ASSERT(!graph.sendLiveControl(ProcessGraph::LiveNoteSource::Ui, ProcessGraph::kMaxTracks, molette));
}

// --- D24.4 : couper toutes les notes ---------------------------------------

VSM_TEST(panic_silences_a_held_note) {
    Project project;
    project.ticksPerQuarterNote = 480;
    Track track;
    uint64_t id = 1;
    track.addNote(0, 480 * 8, 57, 120, 0, id);   // quatre secondes à 120 BPM
    project.tracks.push_back(track);
    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.minimoog");
    graph.setProject(project);
    graph.seekSeconds(0.0);
    graph.setPlaying(true);
    std::vector<float> l(256), r(256);
    auto rendre = [&](int blocs) {
        float crete = 0.0f;
        for (int b = 0; b < blocs; ++b) {
            graph.processBlock(l.data(), r.data(), 256);
            crete = std::max(crete, std::max(peakOf(l), peakOf(r)));
        }
        return crete;
    };
    const float tenue = rendre(16);            // 0,5 s : la note sonne
    VSM_ASSERT(tenue > 0.05f);
    graph.requestPanic();
    (void)rendre(31);                          // une seconde après le panic
    const float apres = rendre(1);
    VSM_ASSERT(apres < tenue * 0.25f);
    // Sans panic, la même note tenue sonnerait encore : le témoin.
    ProcessGraph temoin;
    temoin.prepare(8000.0, 256);
    temoin.setTrackInstrument(0, "vsm.minimoog");
    temoin.setProject(project);
    temoin.seekSeconds(0.0);
    temoin.setPlaying(true);
    float creteTemoin = 0.0f;
    for (int b = 0; b < 48; ++b) {
        temoin.processBlock(l.data(), r.data(), 256);
        if (b == 47) creteTemoin = std::max(peakOf(l), peakOf(r));
    }
    VSM_ASSERT(creteTemoin > tenue * 0.25f);
}

// --- D25.1 : la pédale de sustain, tenue par le graphe -----------------------

VSM_TEST(sustain_pedal_holds_a_released_note_until_the_pedal_is_released) {
    // Une croche (0,25 s) à 120 BPM ; pédale enfoncée au départ, relâchée à 2 s.
    auto construire = [](bool pedale) {
        Project project;
        project.ticksPerQuarterNote = 480;
        Track track;
        uint64_t id = 1;
        track.addNote(0, 240, 57, 120, 0, id);
        if (pedale) {
            track.controlChanges.push_back({0, 0, 64, 127});
            track.controlChanges.push_back({1920, 0, 64, 0});
        }
        project.tracks.push_back(track);
        return project;
    };
    auto creteA = [](ProcessGraph& graph, int blocsAvant, int blocs) {
        std::vector<float> l(256), r(256);
        for (int b = 0; b < blocsAvant; ++b) graph.processBlock(l.data(), r.data(), 256);
        float crete = 0.0f;
        for (int b = 0; b < blocs; ++b) {
            graph.processBlock(l.data(), r.data(), 256);
            crete = std::max(crete, std::max(peakOf(l), peakOf(r)));
        }
        return crete;
    };
    // 8000 Hz, 256 échantillons : 31,25 blocs par seconde.
    ProcessGraph avec;
    avec.prepare(8000.0, 256);
    avec.setTrackInstrument(0, "vsm.minimoog");
    avec.setProject(construire(true));
    avec.seekSeconds(0.0);
    avec.setPlaying(true);
    const float sousPedale = creteA(avec, 31, 4);      // à 1,0 s : le NoteOff (0,25 s) est retenu
    const float apresRelache = creteA(avec, 40, 4);    // vers 2,4 s : relâchée à 2 s

    ProcessGraph sans;
    sans.prepare(8000.0, 256);
    sans.setTrackInstrument(0, "vsm.minimoog");
    sans.setProject(construire(false));
    sans.seekSeconds(0.0);
    sans.setPlaying(true);
    const float temoin = creteA(sans, 31, 4);          // à 1,0 s, sans pédale : la note est finie

    VSM_ASSERT(sousPedale > 0.05f);
    VSM_ASSERT(temoin < sousPedale * 0.25f);
    VSM_ASSERT(apresRelache < sousPedale * 0.25f);
    // La pédale n'est pas comptée « ignorée » : le graphe l'a prise.
    VSM_ASSERT_EQ(avec.ignoredControlEvents(), static_cast<uint64_t>(0));
}

// --- D27.2 : la file de sortie MIDI ----------------------------------------

VSM_TEST(a_track_with_a_midi_output_port_deposits_its_notes_in_order_even_without_a_machine) {
    Project project;
    project.ticksPerQuarterNote = 480;
    Track track;
    uint64_t id = 1;
    track.addNote(0, 480, 60, 100, 2, id);        // une noire, canal 3
    track.controlChanges.push_back({240, 2, 1, 64});   // la molette de modulation à mi-course
    track.midiOutputDevice = "VSM Studio";
    project.tracks.push_back(track);
    ProcessGraph graph;
    graph.prepare(8000.0, 256);           // pas de machine sur la piste 0
    graph.setProject(project);
    graph.seekSeconds(0.0);
    graph.setPlaying(true);
    std::vector<float> l(256), r(256);
    for (int b = 0; b < 40; ++b) graph.processBlock(l.data(), r.data(), 256);   // 1,28 s

    std::vector<ProcessGraph::MidiOutEvent> sortis;
    ProcessGraph::MidiOutEvent e;
    while (graph.popMidiOut(e)) sortis.push_back(e);
    // La suite des statuts en clair, pour qu'un échec DISE l'ordre reçu.
    std::string suite;
    for (const auto& s : sortis) {
        char h[8];
        std::snprintf(h, sizeof(h), "%02X:%d ", s.status, s.data1);
        suite += h;
    }
    VSM_ASSERT_EQ(suite, std::string("92:60 B2:1 82:60 "));   // NoteOn canal 3, CC 1, NoteOff
    VSM_ASSERT_EQ(sortis[0].data2, static_cast<uint8_t>(100));
    // Les heures ne se comparent pas ici : hors ligne, les blocs sont rendus
    // d'un coup et l'ancre d'horloge se repose (> 20 ms d'écart) ; l'ordre est
    // celui de la file, et c'est lui que le fil émetteur trie à l'envoi.
    VSM_ASSERT_EQ(graph.droppedMidiOut(), static_cast<uint64_t>(0));

    // Sans port : rien ne sort, et une piste sans machine n'est pas rendue.
    project.tracks[0].midiOutputDevice.clear();
    ProcessGraph muet;
    muet.prepare(8000.0, 256);
    muet.setProject(project);
    muet.seekSeconds(0.0);
    muet.setPlaying(true);
    for (int b = 0; b < 40; ++b) muet.processBlock(l.data(), r.data(), 256);
    VSM_ASSERT(!muet.popMidiOut(e));
}

VSM_TEST(a_midi_output_track_emits_each_note_once_at_48k) {
    Project project;
    project.ticksPerQuarterNote = 480;
    project.tempoMap.addTempoChange(0, 461538);   // 130 BPM, comme le projet d'exemple
    Track track;
    uint64_t id = 1;
    for (int i = 0; i < 8; ++i) track.addNote(i * 240, i * 240 + 200, static_cast<uint8_t>(36 + i), 100, 0, id);
    track.midiOutputDevice = "VSM Studio";
    project.tracks.push_back(track);
    ProcessGraph graph;
    graph.prepare(48000.0, 512);
    graph.setTrackInstrument(0, "vsm.tb303");
    graph.setProject(project);
    graph.seekSeconds(0.0);
    graph.setPlaying(true);
    std::vector<float> l(512), r(512);
    for (int b = 0; b < 300; ++b) graph.processBlock(l.data(), r.data(), 512);   // 3,2 s
    size_t on = 0, off = 0;
    ProcessGraph::MidiOutEvent e;
    while (graph.popMidiOut(e)) { if ((e.status & 0xF0) == 0x90) ++on; else if ((e.status & 0xF0) == 0x80) ++off; }
    VSM_ASSERT_EQ(on, static_cast<size_t>(8));
    VSM_ASSERT_EQ(off, static_cast<size_t>(8));
}
