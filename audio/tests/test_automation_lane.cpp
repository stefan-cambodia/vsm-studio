#include "TestFramework.h"
#include "vsm/audio/effect/IAudioEffect.h"
#include "vsm/audio/engine/AutomationLane.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/sequencer/Project.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace vsm::audio::engine;

namespace {
/// Un gain, avec un paramètre pilotable : de quoi vérifier qu'une courbe
/// atteint le BON insert d'une chaîne, et pas son voisin.
class GainProbe : public vsm::audio::effect::IAudioEffect {
public:
    explicit GainProbe(float gain) {
        parameterList_ = {{0, "Gain", 0.0f, 4.0f, gain, ""}};
        gain_ = gain;
    }
    void prepare(double, int) override {}
    void reset() override {}
    void process(float* l, float* r, int n) override {
        for (int i = 0; i < n; ++i) { l[i] *= gain_; r[i] *= gain_; }
    }
    void setParameter(vsm::audio::plugin::ParamId id, float v) override { if (id == 0) gain_ = v; }
    float getParameter(vsm::audio::plugin::ParamId id) const override { return id == 0 ? gain_ : 0.0f; }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    const char* effectName() const override { return "GainProbe"; }
private:
    float gain_ = 1.0f;
    vsm::audio::plugin::ParameterList parameterList_;
};
} // namespace

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

// --- D4.6 : automation de TOUT ---------------------------------------------
//
// Jusqu'ici une courbe ne pouvait piloter qu'un réglage de machine. Le fondu --
// le geste d'automation le plus courant qui soit -- était donc impossible à
// écrire, et tout le mixage échappait à l'automation alors que le format savait
// déjà l'écrire.

namespace {
using vsm::sequencer::Project;
using vsm::sequencer::Track;

Project noteProject(int pistes = 1) {
    Project projet;
    projet.ticksPerQuarterNote = 480;
    uint64_t ids = 1;
    for (int i = 0; i < pistes; ++i) {
        Track t;
        t.addNote(0, 1920, 60, 100, 0, ids);
        projet.tracks.push_back(t);
    }
    return projet;
}

/// Crête d'une TRANCHE du rendu, en secondes.
float peakBetween(const RenderedAudio& rendu, double sr, double debut, double fin) {
    const size_t a = static_cast<size_t>(debut * sr);
    const size_t b = std::min(rendu.left.size(), static_cast<size_t>(fin * sr));
    float m = 0.0f;
    for (size_t i = a; i < b; ++i) m = std::max(m, std::abs(rendu.left[i]));
    return m;
}
} // namespace

VSM_TEST(a_fade_written_in_automation_is_heard) {
    // LE CRITÈRE DE L'ÉTAPE, littéralement : « un fondu écrit en automation
    // s'entend ». Le fader descend de 1 à 0 sur une seconde ; on mesure le
    // début et la fin.
    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.minimoog");
    graph.setProject(noteProject());

    AutomationLane fondu;
    fondu.target = AutomationTarget::TrackVolume;
    fondu.targetTrackIndex = 0;
    fondu.addPoint(0, 1.0f);
    fondu.addPoint(960, 0.0f);      // une seconde à 120 BPM avec ppq=480
    graph.setAutomationLanes({fondu});

    const auto rendu = OfflineRenderer::render(graph, 8000.0, 256, 1.0);
    const float debut = peakBetween(rendu, 8000.0, 0.05, 0.20);
    const float fin = peakBetween(rendu, 8000.0, 0.85, 0.99);
    VSM_ASSERT(debut > 0.02f);
    VSM_ASSERT(fin < debut * 0.25f);
}

VSM_TEST(automating_the_pan_moves_the_sound_across_the_stereo_field) {
    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.minimoog");
    graph.setProject(noteProject());

    AutomationLane balayage;
    balayage.target = AutomationTarget::TrackPan;
    balayage.targetTrackIndex = 0;
    balayage.addPoint(0, -1.0f);    // tout à gauche
    balayage.addPoint(960, 1.0f);   // tout à droite
    graph.setAutomationLanes({balayage});

    const auto rendu = OfflineRenderer::render(graph, 8000.0, 256, 1.0);
    float gaucheDebut = 0.0f, droiteDebut = 0.0f, gaucheFin = 0.0f, droiteFin = 0.0f;
    for (size_t i = 400; i < 1600; ++i) {
        gaucheDebut = std::max(gaucheDebut, std::abs(rendu.left[i]));
        droiteDebut = std::max(droiteDebut, std::abs(rendu.right[i]));
    }
    for (size_t i = 6800; i < 7900 && i < rendu.left.size(); ++i) {
        gaucheFin = std::max(gaucheFin, std::abs(rendu.left[i]));
        droiteFin = std::max(droiteFin, std::abs(rendu.right[i]));
    }
    VSM_ASSERT(gaucheDebut > droiteDebut * 4.0f);   // à gauche au départ
    VSM_ASSERT(droiteFin > gaucheFin * 4.0f);       // à droite à l'arrivée
}

VSM_TEST(automating_a_send_level_changes_what_reaches_the_bus) {
    auto rendre = [](float niveau) {
        Project projet = noteProject();
        vsm::sequencer::SendBusDescription bus;
        bus.effectType = "reverb";
        projet.sends.push_back(bus);

        ProcessGraph graph;
        graph.prepare(8000.0, 256);
        graph.setTrackInstrument(0, "vsm.minimoog");
        graph.setProject(projet);
        graph.setSendEffect(0, std::make_shared<GainProbe>(1.0f));
        graph.setSendReturn(0, 1.0f);
        // La piste est muette au master : ce qui sort est UNIQUEMENT le retour
        // du bus, donc exactement ce que le départ y a versé.
        AutomationLane depart;
        depart.target = AutomationTarget::TrackSend;
        depart.targetTrackIndex = 0;
        depart.targetSlot = 0;
        depart.addPoint(0, niveau);
        AutomationLane silence;
        silence.target = AutomationTarget::TrackVolume;
        silence.targetTrackIndex = 0;
        silence.addPoint(0, 1.0f);
        graph.setAutomationLanes({depart, silence});
        const auto rendu = OfflineRenderer::render(graph, 8000.0, 256, 0.5);
        float m = 0.0f;
        for (float e : rendu.left) m = std::max(m, std::abs(e));
        return m;
    };
    const float ferme = rendre(0.0f);
    const float ouvert = rendre(1.0f);
    VSM_ASSERT(ouvert > ferme * 1.3f);
}

VSM_TEST(automating_a_master_parameter_reaches_the_master_strip) {
    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.minimoog");
    graph.setProject(noteProject());
    graph.masterBus().setParameter(MasterBus::kEnabled, 1.0f);

    AutomationLane plafond;
    plafond.target = AutomationTarget::MasterParam;
    plafond.targetParam = MasterBus::kLimiterCeilingDb;
    plafond.addPoint(0, -24.0f);     // un plafond très bas, tenu
    graph.setAutomationLanes({plafond});

    const auto rendu = OfflineRenderer::render(graph, 8000.0, 256, 0.5);
    float m = 0.0f;
    for (float e : rendu.left) m = std::max(m, std::abs(e));
    // -24 dB vaut 0,063 : le limiteur du master a bien reçu la consigne.
    VSM_ASSERT(m <= 0.07f);
    VSM_ASSERT(m > 0.0f);
}

VSM_TEST(automating_an_insert_parameter_reaches_that_insert_and_no_other) {
    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.minimoog");
    graph.setProject(noteProject());

    // Deux gains dans la chaîne : la courbe ne doit toucher que le second.
    auto premier = std::make_shared<GainProbe>(1.0f);
    auto second = std::make_shared<GainProbe>(1.0f);
    auto chain = std::make_shared<ProcessGraph::EffectChain>();
    chain->push_back(premier);
    chain->push_back(second);
    graph.setTrackEffectChain(0, chain);

    AutomationLane courbe;
    courbe.target = AutomationTarget::InsertParam;
    courbe.targetTrackIndex = 0;
    courbe.targetSlot = 1;              // le SECOND insert
    courbe.targetParam = 0;             // son gain
    courbe.addPoint(0, 0.0f);
    graph.setAutomationLanes({courbe});

    const auto rendu = OfflineRenderer::render(graph, 8000.0, 256, 0.4);
    float m = 0.0f;
    for (float e : rendu.left) m = std::max(m, std::abs(e));
    VSM_ASSERT(m < 1e-5f);                              // le second a bien fermé
    VSM_ASSERT_NEAR(premier->getParameter(0), 1.0f, 1e-6);   // le premier n'a pas bougé
}
