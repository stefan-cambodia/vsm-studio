#include "TestFramework.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

namespace {
SynthPluginPtr makeMs20(double sr = 48000.0) {
    auto p = PluginRegistry::instance().create("vsm.ms20");
    p->initialize(sr, 512);
    return p;
}
float peakAbs(const std::vector<float>& b) { float p = 0.0f; for (float s : b) p = std::max(p, std::abs(s)); return p; }
double energy(const std::vector<float>& b) { double e = 0.0; for (float s : b) e += static_cast<double>(s) * s; return e; }
MidiNoteEvent noteOn(int off, uint8_t note, uint8_t vel = 100) { return {MidiNoteEvent::Kind::NoteOn, off, 0, note, vel}; }
ParamId byName(const ISynthPlugin& p, const std::string& n) { for (const auto& i : p.parameterList()) if (i.name == n) return i.id; return 0; }

std::vector<float> renderNote(SynthPluginPtr& s, uint8_t note, int n = 6000) {
    MidiNoteEvent on = noteOn(0, note, 100);
    std::vector<float> l(static_cast<size_t>(n), 0.0f), r(static_cast<size_t>(n), 0.0f);
    s->process(&on, 1, l.data(), r.data(), n);
    return l;
}
} // namespace

VSM_TEST(ms20_registered) {
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.ms20"));
}

VSM_TEST(ms20_silent_with_no_events) {
    auto s = makeMs20();
    std::vector<float> l(512, 0.0f), r(512, 0.0f);
    s->process(nullptr, 0, l.data(), r.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0, 1e-6);
    VSM_ASSERT_EQ(s->activeVoiceCount(), 0);
}

VSM_TEST(ms20_note_produces_sound) {
    auto s = makeMs20();
    auto l = renderNote(s, 48);
    VSM_ASSERT(peakAbs(l) > 0.02f);
    for (float x : l) VSM_ASSERT(std::isfinite(x));
    VSM_ASSERT_EQ(s->activeVoiceCount(), 1);
}

VSM_TEST(ms20_dual_filter_hpf_removes_low_end) {
    auto open = makeMs20();
    open->setParameter(byName(*open, "HPF Cutoff"), 20.0f);
    const double eOpen = energy(renderNote(open, 30, 4000));

    auto hp = makeMs20();
    hp->setParameter(byName(*hp, "HPF Cutoff"), 8000.0f);
    const double eHp = energy(renderNote(hp, 30, 4000));

    VSM_ASSERT(eHp < eOpen * 0.7);
}

VSM_TEST(ms20_resonance_changes_timbre) {
    auto low = makeMs20();
    low->setParameter(byName(*low, "LPF Resonance"), 0.1f);
    auto a = renderNote(low, 45);

    auto high = makeMs20();
    high->setParameter(byName(*high, "LPF Resonance"), 0.95f);
    auto b = renderNote(high, 45);

    double diff = 0.0;
    for (size_t i = 500; i < a.size(); ++i) diff += std::abs(a[i] - b[i]);
    VSM_ASSERT(diff > 5.0);
}

VSM_TEST(ms20_extreme_resonance_stays_bounded) {
    // Coeur du modèle MS-20 : à résonance maximale sur les DEUX filtres et
    // drive fort, l'auto-oscillation crie mais reste FINIE et bornée (grâce
    // à la saturation d'états de MS20Filter), jamais divergente.
    auto s = makeMs20();
    s->setParameter(byName(*s, "HPF Resonance"), 1.0f);
    s->setParameter(byName(*s, "LPF Resonance"), 1.0f);
    s->setParameter(byName(*s, "Filter Drive"), 4.0f);
    s->setParameter(byName(*s, "LPF Cutoff"), 800.0f);
    auto l = renderNote(s, 40, 12000);
    for (float x : l) VSM_ASSERT(std::isfinite(x));
    VSM_ASSERT(peakAbs(l) < 8.0f); // borné, pas de divergence
}

VSM_TEST(ms20_ring_mod_changes_timbre) {
    auto saw = makeMs20();
    saw->setParameter(byName(*saw, "VCO-2 Level"), 0.9f);
    saw->setParameter(byName(*saw, "VCO-2 Shape"), 0.0f); // saw
    saw->setParameter(byName(*saw, "VCO-2 Pitch"), 7.0f);
    auto a = renderNote(saw, 45);

    auto ring = makeMs20();
    ring->setParameter(byName(*ring, "VCO-2 Level"), 0.9f);
    ring->setParameter(byName(*ring, "VCO-2 Shape"), 2.0f); // ring
    ring->setParameter(byName(*ring, "VCO-2 Pitch"), 7.0f);
    auto b = renderNote(ring, 45);

    double diff = 0.0;
    for (size_t i = 500; i < a.size(); ++i) diff += std::abs(a[i] - b[i]);
    VSM_ASSERT(diff > 5.0);
}

VSM_TEST(ms20_not_velocity_sensitive) {
    auto v1 = makeMs20(); MidiNoteEvent e1 = noteOn(0, 50, 30);
    std::vector<float> a(3000, 0.0f), ar(3000, 0.0f); v1->process(&e1, 1, a.data(), ar.data(), 3000);
    auto v2 = makeMs20(); MidiNoteEvent e2 = noteOn(0, 50, 120);
    std::vector<float> bb(3000, 0.0f), br(3000, 0.0f); v2->process(&e2, 1, bb.data(), br.data(), 3000);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], bb[i], 1e-9);
}

VSM_TEST(ms20_is_deterministic) {
    auto render = [] {
        auto s = makeMs20();
        s->setParameter(byName(*s, "Analog Character"), 0.7f);
        s->setParameter(byName(*s, "Noise Level"), 0.5f);
        s->setParameter(byName(*s, "LPF Resonance"), 0.8f);
        return renderNote(s, 48, 4000);
    };
    auto a = render(), b = render();
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], b[i], 1e-9);
}

VSM_TEST(ms20_save_load_roundtrip) {
    auto a = makeMs20();
    ParamId res = byName(*a, "LPF Resonance");
    ParamId hp = byName(*a, "HPF Cutoff");
    a->setParameter(res, 0.9f);
    a->setParameter(hp, 400.0f);
    PresetState st = a->saveState();
    VSM_ASSERT_EQ(st.pluginTypeId, std::string("vsm.ms20"));
    auto b = makeMs20();
    b->loadState(st);
    VSM_ASSERT_NEAR(b->getParameter(res), 0.9, 1e-3);
    VSM_ASSERT_NEAR(b->getParameter(hp), 400.0, 1e-1);
}

VSM_TEST(ms20_parameter_list_size) {
    auto s = makeMs20();
    VSM_ASSERT_EQ(s->parameterList().size(), static_cast<size_t>(23));
}
