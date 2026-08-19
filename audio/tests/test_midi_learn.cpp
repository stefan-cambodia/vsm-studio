#include "TestFramework.h"
#include "vsm/audio/engine/MidiLearnMap.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/sequencer/Project.h"
#include <string>
#include <cmath>

using namespace vsm::audio::engine;

namespace {
MidiLearnTarget makeTarget(size_t track, vsm::audio::plugin::ParamId id, float mn, float mx) {
    MidiLearnTarget t; t.trackIndex = track; t.paramId = id; t.min = mn; t.max = mx; t.valid = true;
    return t;
}
} // namespace

VSM_TEST(midi_learn_bind_and_resolve) {
    MidiLearnMap map;
    map.bind(74, makeTarget(0, 5, 20.0f, 20000.0f)); // ex : cutoff

    MidiLearnTarget out; float value = 0.0f;
    VSM_ASSERT(map.resolve(74, 127, out, value));
    VSM_ASSERT_EQ(out.paramId, static_cast<vsm::audio::plugin::ParamId>(5));
    VSM_ASSERT_NEAR(value, 20000.0, 1.0);       // 127 -> max
    VSM_ASSERT(map.resolve(74, 0, out, value));
    VSM_ASSERT_NEAR(value, 20.0, 1e-3);         // 0 -> min
    VSM_ASSERT(map.resolve(74, 64, out, value));
    VSM_ASSERT_NEAR(value, 20.0 + (64.0 / 127.0) * (20000.0 - 20.0), 1.0);
}

VSM_TEST(midi_learn_unbound_controller_returns_false) {
    MidiLearnMap map;
    map.bind(1, makeTarget(0, 0, 0.0f, 1.0f));
    MidiLearnTarget out; float value = 0.0f;
    VSM_ASSERT(!map.resolve(2, 100, out, value));
}

VSM_TEST(midi_learn_rebind_replaces) {
    MidiLearnMap map;
    map.bind(10, makeTarget(0, 1, 0.0f, 1.0f));
    map.bind(10, makeTarget(3, 7, -1.0f, 1.0f)); // même CC, nouvelle cible
    VSM_ASSERT_EQ(map.size(), static_cast<size_t>(1));
    MidiLearnTarget out; float value = 0.0f;
    map.resolve(10, 127, out, value);
    VSM_ASSERT_EQ(out.trackIndex, static_cast<size_t>(3));
    VSM_ASSERT_EQ(out.paramId, static_cast<vsm::audio::plugin::ParamId>(7));
    VSM_ASSERT_NEAR(value, 1.0, 1e-4);
}

VSM_TEST(midi_learn_clear) {
    MidiLearnMap map;
    map.bind(1, makeTarget(0, 0, 0.0f, 1.0f));
    map.bind(2, makeTarget(0, 1, 0.0f, 1.0f));
    map.clearController(1);
    VSM_ASSERT(!map.hasController(1));
    VSM_ASSERT(map.hasController(2));
    map.clearAll();
    VSM_ASSERT_EQ(map.size(), static_cast<size_t>(0));
}

VSM_TEST(process_graph_set_instrument_parameter) {
    // setInstrumentParameter doit atteindre le vrai instrument de la piste.
    ProcessGraph graph;
    graph.prepare(48000.0, 256);
    graph.setTrackInstrument(0, "vsm.minimoog");
    auto* inst = graph.trackInstrument(0);
    VSM_ASSERT(inst != nullptr);

    // Trouver un paramètre (Filter Cutoff) et le régler via le graphe.
    vsm::audio::plugin::ParamId cutoff = 0;
    float mn = 0.0f, mx = 1.0f;
    for (const auto& info : inst->parameterList()) {
        if (info.name.find("Cutoff") != std::string::npos) { cutoff = info.id; mn = info.minValue; mx = info.maxValue; }
    }
    (void)mn; (void)mx;
    graph.setInstrumentParameter(0, cutoff, 1234.0f);
    VSM_ASSERT_NEAR(inst->getParameter(cutoff), 1234.0, 1e-2);

    // Piste sans instrument -> no-op, pas de crash.
    graph.setInstrumentParameter(5, 0, 1.0f);
}
