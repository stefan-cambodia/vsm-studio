#include "TestFramework.h"
#include "vsm/audio/engine/MasterBus.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/sequencer/Project.h"
#include <cmath>
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
