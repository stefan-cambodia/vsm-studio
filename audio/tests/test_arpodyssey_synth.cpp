#include "TestFramework.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

namespace {
SynthPluginPtr makeArp(double sr = 48000.0) {
    auto p = PluginRegistry::instance().create("vsm.arpodyssey");
    p->initialize(sr, 512);
    return p;
}
float peakAbs(const std::vector<float>& b) { float p = 0.0f; for (float s : b) p = std::max(p, std::abs(s)); return p; }
MidiNoteEvent noteOn(int off, uint8_t note, uint8_t vel = 100) { return {MidiNoteEvent::Kind::NoteOn, off, 0, note, vel}; }
ParamId byName(const ISynthPlugin& p, const std::string& n) { for (const auto& i : p.parameterList()) if (i.name == n) return i.id; return 0; }

std::vector<float> render(SynthPluginPtr& s, const std::vector<MidiNoteEvent>& ev, int n = 6000) {
    std::vector<float> l(static_cast<size_t>(n), 0.0f), r(static_cast<size_t>(n), 0.0f);
    s->process(ev.data(), static_cast<int>(ev.size()), l.data(), r.data(), n);
    return l;
}
} // namespace

VSM_TEST(arpodyssey_registered) {
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.arpodyssey"));
}

VSM_TEST(arpodyssey_silent_with_no_events) {
    auto s = makeArp();
    std::vector<float> l(512, 0.0f), r(512, 0.0f);
    s->process(nullptr, 0, l.data(), r.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0, 1e-6);
    VSM_ASSERT_EQ(s->activeVoiceCount(), 0);
}

VSM_TEST(arpodyssey_note_produces_sound) {
    auto s = makeArp();
    auto l = render(s, {noteOn(0, 48)});
    VSM_ASSERT(peakAbs(l) > 0.02f);
    for (float x : l) VSM_ASSERT(std::isfinite(x));
    VSM_ASSERT_EQ(s->activeVoiceCount(), 1);
}

VSM_TEST(arpodyssey_is_duophonic) {
    // Une touche -> 1 ; deux touches -> 2 ; trois touches -> plafonne à 2
    // (allocation DUOPHONIQUE, pas polyphonique).
    auto one = makeArp();
    render(one, {noteOn(0, 48)}, 1000);
    VSM_ASSERT_EQ(one->activeVoiceCount(), 1);

    auto two = makeArp();
    render(two, {noteOn(0, 48), noteOn(0, 60)}, 1000);
    VSM_ASSERT_EQ(two->activeVoiceCount(), 2);

    auto three = makeArp();
    render(three, {noteOn(0, 48), noteOn(0, 55), noteOn(0, 60)}, 1000);
    VSM_ASSERT_EQ(three->activeVoiceCount(), 2);
}

VSM_TEST(arpodyssey_second_voice_tracks_highest_key) {
    // Deux touches distinctes -> VCO-2 suit l'aiguë : le rendu diffère
    // nettement de l'unisson (une seule touche, les deux VCO au même pitch).
    auto uni = makeArp();
    uni->setParameter(byName(*uni, "VCO-2 Level"), 0.9f);
    auto unison = render(uni, {noteOn(0, 48)});

    auto duo = makeArp();
    duo->setParameter(byName(*duo, "VCO-2 Level"), 0.9f);
    auto duophonic = render(duo, {noteOn(0, 48), noteOn(0, 60)});

    double diff = 0.0;
    for (size_t i = 500; i < unison.size(); ++i) diff += std::abs(unison[i] - duophonic[i]);
    VSM_ASSERT(diff > 20.0);
}

VSM_TEST(arpodyssey_ring_mod_changes_timbre) {
    auto dry = makeArp();
    dry->setParameter(byName(*dry, "VCO-2 Detune"), 7.0f);
    dry->setParameter(byName(*dry, "Ring Mod Level"), 0.0f);
    auto a = render(dry, {noteOn(0, 45)});

    auto ring = makeArp();
    ring->setParameter(byName(*ring, "VCO-2 Detune"), 7.0f);
    ring->setParameter(byName(*ring, "Ring Mod Level"), 1.0f);
    auto b = render(ring, {noteOn(0, 45)});

    double diff = 0.0;
    for (size_t i = 500; i < a.size(); ++i) diff += std::abs(a[i] - b[i]);
    VSM_ASSERT(diff > 5.0);
}

VSM_TEST(arpodyssey_not_velocity_sensitive) {
    auto v1 = makeArp();
    auto a = render(v1, {noteOn(0, 50, 30)}, 3000);
    auto v2 = makeArp();
    auto b = render(v2, {noteOn(0, 50, 120)}, 3000);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], b[i], 1e-9);
}

VSM_TEST(arpodyssey_is_deterministic) {
    auto renderIt = [] {
        auto s = makeArp();
        s->setParameter(byName(*s, "Analog Character"), 0.7f);
        s->setParameter(byName(*s, "LFO Waveform"), 2.0f); // S&H : couvre le drift seedé
        s->setParameter(byName(*s, "LFO to Filter"), 0.8f);
        s->setParameter(byName(*s, "Noise Level"), 0.5f);
        return render(s, {noteOn(0, 48), noteOn(0, 55)}, 4000);
    };
    auto a = renderIt(), b = renderIt();
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], b[i], 1e-9);
}

VSM_TEST(arpodyssey_save_load_roundtrip) {
    auto a = makeArp();
    ParamId res = byName(*a, "Filter Resonance");
    ParamId ring = byName(*a, "Ring Mod Level");
    a->setParameter(res, 3.5f);
    a->setParameter(ring, 0.66f);
    PresetState st = a->saveState();
    VSM_ASSERT_EQ(st.pluginTypeId, std::string("vsm.arpodyssey"));
    auto b = makeArp();
    b->loadState(st);
    VSM_ASSERT_NEAR(b->getParameter(res), 3.5, 1e-2);
    VSM_ASSERT_NEAR(b->getParameter(ring), 0.66, 1e-3);
}

VSM_TEST(arpodyssey_parameter_list_size) {
    auto s = makeArp();
    VSM_ASSERT_EQ(s->parameterList().size(), static_cast<size_t>(25));
}
