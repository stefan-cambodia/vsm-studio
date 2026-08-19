#include "TestFramework.h"
#include "vsm/audio/engine/AutomationLane.h"

using namespace vsm::audio::engine;

VSM_TEST(automation_lane_empty_returns_zero) {
    AutomationLane lane;
    VSM_ASSERT_NEAR(lane.valueAt(0), 0.0, 1e-6);
}

VSM_TEST(automation_lane_exact_point_values) {
    AutomationLane lane;
    lane.addPoint(0, 300.0f);
    lane.addPoint(1920, 1200.0f);
    lane.addPoint(3840, 400.0f);

    VSM_ASSERT_NEAR(lane.valueAt(0), 300.0, 1e-4);
    VSM_ASSERT_NEAR(lane.valueAt(1920), 1200.0, 1e-4);
    VSM_ASSERT_NEAR(lane.valueAt(3840), 400.0, 1e-4);
}

VSM_TEST(automation_lane_linear_interpolation_midpoint) {
    AutomationLane lane;
    lane.addPoint(0, 0.0f);
    lane.addPoint(1000, 100.0f);
    VSM_ASSERT_NEAR(lane.valueAt(500), 50.0, 0.5);
    VSM_ASSERT_NEAR(lane.valueAt(250), 25.0, 0.5);
}

VSM_TEST(automation_lane_step_curve_holds_left_value) {
    AutomationLane lane;
    lane.addPoint(0, 10.0f, AutomationCurve::Step);
    lane.addPoint(1000, 90.0f);

    VSM_ASSERT_NEAR(lane.valueAt(0), 10.0, 1e-4);
    VSM_ASSERT_NEAR(lane.valueAt(500), 10.0, 1e-4); // maintenu, pas d'interpolation
    VSM_ASSERT_NEAR(lane.valueAt(999), 10.0, 1e-4);
    VSM_ASSERT_NEAR(lane.valueAt(1000), 90.0, 1e-4); // saute exactement au point suivant
}

VSM_TEST(automation_lane_clamps_outside_range) {
    AutomationLane lane;
    lane.addPoint(1000, 5.0f);
    lane.addPoint(2000, 15.0f);

    VSM_ASSERT_NEAR(lane.valueAt(0), 5.0, 1e-4);     // avant le premier point
    VSM_ASSERT_NEAR(lane.valueAt(5000), 15.0, 1e-4); // après le dernier point
}

VSM_TEST(automation_lane_add_point_at_existing_tick_replaces_value) {
    AutomationLane lane;
    lane.addPoint(0, 1.0f);
    lane.addPoint(0, 2.0f); // même tick : remplace, n'ajoute pas de doublon
    VSM_ASSERT_EQ(lane.points().size(), static_cast<size_t>(1));
    VSM_ASSERT_NEAR(lane.valueAt(0), 2.0, 1e-4);
}

VSM_TEST(automation_lane_points_stay_sorted_regardless_of_insertion_order) {
    AutomationLane lane;
    lane.addPoint(2000, 3.0f);
    lane.addPoint(0, 1.0f);
    lane.addPoint(1000, 2.0f);

    const auto& pts = lane.points();
    VSM_ASSERT_EQ(pts.size(), static_cast<size_t>(3));
    VSM_ASSERT(pts[0].tick < pts[1].tick);
    VSM_ASSERT(pts[1].tick < pts[2].tick);
}

// --- Application via ProcessGraph (chemin RT-safe : setAutomationLanes) ---
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/sequencer/Project.h"
#include <string>

VSM_TEST(process_graph_applies_automation_via_snapshot) {
    using namespace vsm::sequencer;
    Project project;
    project.ticksPerQuarterNote = 480;
    Track track; track.name = "Bass"; track.channel = 0;
    uint64_t id = 1;
    track.addNote(0, 480 * 4, 48, 100, 0, id); // une note tenue pour que ça joue
    project.tracks.push_back(track);

    vsm::audio::engine::ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.minimoog");
    graph.setProject(project);

    auto* inst = graph.trackInstrument(0);
    VSM_ASSERT(inst != nullptr);
    vsm::audio::plugin::ParamId cutoff = 0;
    for (const auto& info : inst->parameterList())
        if (info.name.find("Cutoff") != std::string::npos) cutoff = info.id;

    vsm::audio::engine::AutomationLane lane;
    lane.targetTrackIndex = 0;
    lane.targetParam = cutoff;
    lane.addPoint(0, 200.0f);
    lane.addPoint(600, 5000.0f); // ~0.5 s à 480ppq/120bpm -> ensuite maintenu
    graph.setAutomationLanes({lane});

    // Rendu ~1 s : la position avance, l'automation est appliquée par bloc.
    (void)vsm::audio::engine::OfflineRenderer::render(graph, 8000.0, 256, 1.0);

    // En fin de rendu, la coupure doit avoir grimpé vers 5000 (bien au-dessus
    // de la valeur initiale 200).
    VSM_ASSERT(inst->getParameter(cutoff) > 3000.0f);
}

VSM_TEST(process_graph_automation_is_subblock_accurate) {
    using namespace vsm::sequencer;
    Project project;
    project.ticksPerQuarterNote = 480;
    Track track; track.channel = 0;
    uint64_t id = 1;
    track.addNote(0, 480 * 4, 48, 100, 0, id);
    project.tracks.push_back(track);

    vsm::audio::engine::ProcessGraph graph;
    graph.prepare(48000.0, 1024);
    graph.setTrackInstrument(0, "vsm.minimoog");
    graph.setProject(project);

    auto* inst = graph.trackInstrument(0);
    vsm::audio::plugin::ParamId cutoff = 0;
    for (const auto& info : inst->parameterList())
        if (info.name.find("Cutoff") != std::string::npos) cutoff = info.id;

    vsm::audio::engine::AutomationLane lane;
    lane.targetTrackIndex = 0; lane.targetParam = cutoff;
    lane.addPoint(0, 200.0f);
    lane.addPoint(10, 5000.0f); // atteint 5000 en ~10 ticks (~0.01 s)
    graph.setAutomationLanes({lane});

    // Un SEUL bloc de 960 échantillons (0,02 s) : ~19 ticks. En block-granular
    // (valeur figée en tête de bloc = 200) la coupure resterait à 200. Le
    // découpage sous-bloc doit la faire monter vers 5000.
    (void)vsm::audio::engine::OfflineRenderer::render(graph, 48000.0, 1024, 0.02);
    VSM_ASSERT(inst->getParameter(cutoff) > 3000.0f);
}
