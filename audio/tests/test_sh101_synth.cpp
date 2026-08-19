#include "TestFramework.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

namespace {
SynthPluginPtr makeSh101(double sr = 48000.0) {
    auto p = PluginRegistry::instance().create("vsm.sh101");
    p->initialize(sr, 512);
    return p;
}
float peakAbs(const std::vector<float>& b) { float p = 0.0f; for (float s : b) p = std::max(p, std::abs(s)); return p; }
MidiNoteEvent noteOn(int off, uint8_t note, uint8_t vel = 100) { return {MidiNoteEvent::Kind::NoteOn, off, 0, note, vel}; }
MidiNoteEvent noteOff(int off, uint8_t note) { return {MidiNoteEvent::Kind::NoteOff, off, 0, note, 64}; }
ParamId byName(const ISynthPlugin& p, const std::string& n) { for (const auto& i : p.parameterList()) if (i.name == n) return i.id; return 0; }
} // namespace

VSM_TEST(sh101_registered) {
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.sh101"));
}

VSM_TEST(sh101_silent_with_no_events) {
    auto s = makeSh101();
    std::vector<float> l(512, 0.0f), r(512, 0.0f);
    s->process(nullptr, 0, l.data(), r.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0, 1e-6);
    VSM_ASSERT_EQ(s->activeVoiceCount(), 0);
}

VSM_TEST(sh101_note_produces_sound) {
    auto s = makeSh101();
    MidiNoteEvent on = noteOn(0, 45, 100);
    std::vector<float> l(4000, 0.0f), r(4000, 0.0f);
    s->process(&on, 1, l.data(), r.data(), 4000);
    VSM_ASSERT(peakAbs(l) > 0.02f);
    for (float x : l) VSM_ASSERT(std::isfinite(x));
    VSM_ASSERT_EQ(s->activeVoiceCount(), 1);
}

VSM_TEST(sh101_is_monophonic) {
    // Deux notes tenues -> une seule voix (mono).
    auto s = makeSh101();
    std::vector<MidiNoteEvent> evs = {noteOn(0, 48, 100), noteOn(100, 55, 100)};
    std::vector<float> l(2000, 0.0f), r(2000, 0.0f);
    s->process(evs.data(), 2, l.data(), r.data(), 2000);
    VSM_ASSERT_EQ(s->activeVoiceCount(), 1);
}

VSM_TEST(sh101_sub_oscillator_adds_low_energy) {
    // Le sous-oscillateur doit ajouter de l'énergie (grave) vs sub à zéro.
    auto render = [](float subLevel) {
        auto s = makeSh101();
        s->setParameter(byName(*s, "Saw Level"), 1.0f);
        s->setParameter(byName(*s, "Sub Level"), subLevel);
        s->setParameter(byName(*s, "Filter Cutoff"), 6000.0f);
        MidiNoteEvent on = noteOn(0, 40, 100);
        std::vector<float> l(4000, 0.0f), r(4000, 0.0f);
        s->process(&on, 1, l.data(), r.data(), 4000);
        double e = 0.0; for (float x : l) e += static_cast<double>(x) * x; return e;
    };
    VSM_ASSERT(render(1.0f) > render(0.0f) * 1.1);
}

VSM_TEST(sh101_not_velocity_sensitive) {
    // Clavier SH-101 non vélocité-sensible : même note, vélocités différentes
    // -> rendu identique.
    auto render = [](uint8_t vel) {
        auto s = makeSh101();
        MidiNoteEvent on = noteOn(0, 50, vel);
        std::vector<float> l(3000, 0.0f), r(3000, 0.0f);
        s->process(&on, 1, l.data(), r.data(), 3000);
        return l;
    };
    auto soft = render(25), hard = render(120);
    for (size_t i = 0; i < soft.size(); ++i) VSM_ASSERT_NEAR(soft[i], hard[i], 1e-9);
}

VSM_TEST(sh101_gate_mode_holds_while_note_down) {
    // En mode VCA=gate, le niveau doit rester soutenu tant que la note est
    // tenue, indépendamment de l'enveloppe (sustain 0).
    auto s = makeSh101();
    s->setParameter(byName(*s, "VCA Mode"), 1.0f);      // gate
    s->setParameter(byName(*s, "Env Sustain"), 0.0f);   // env retombe à 0
    s->setParameter(byName(*s, "Env Decay"), 0.02f);
    s->setParameter(byName(*s, "Filter Env Amount"), 0.0f);
    s->setParameter(byName(*s, "Filter Cutoff"), 8000.0f);
    MidiNoteEvent on = noteOn(0, 45, 100);
    std::vector<float> l(8000, 0.0f), r(8000, 0.0f);
    s->process(&on, 1, l.data(), r.data(), 8000);
    // Après que l'enveloppe soit retombée, il reste du son (gate maintient).
    double lateEnergy = 0.0;
    for (size_t i = 4000; i < l.size(); ++i) lateEnergy += static_cast<double>(l[i]) * l[i];
    VSM_ASSERT(lateEnergy > 0.001);
}

VSM_TEST(sh101_glide_changes_transition) {
    // Avec glide, la transition de pitch entre deux notes diffère du saut sec.
    auto render = [](float glide) {
        auto s = makeSh101();
        s->setParameter(byName(*s, "Glide Time"), glide);
        std::vector<MidiNoteEvent> evs = {noteOn(0, 40, 100), noteOn(1000, 64, 100)};
        std::vector<float> l(4000, 0.0f), r(4000, 0.0f);
        s->process(evs.data(), 2, l.data(), r.data(), 4000);
        return l;
    };
    auto noGlide = render(0.0f), withGlide = render(0.4f);
    double diff = 0.0;
    for (size_t i = 1000; i < 2000; ++i) diff += std::abs(noGlide[i] - withGlide[i]);
    VSM_ASSERT(diff > 1.0); // le glide modifie clairement la transition
}

VSM_TEST(sh101_is_deterministic) {
    auto render = [] {
        auto s = makeSh101();
        s->setParameter(byName(*s, "Analog Character"), 0.6f);
        s->setParameter(byName(*s, "LFO Waveform"), 2.0f); // aléatoire S&H
        s->setParameter(byName(*s, "LFO Pitch Amount"), 0.5f);
        std::vector<MidiNoteEvent> evs = {noteOn(0, 45, 100), noteOff(2000, 45)};
        std::vector<float> l(4000, 0.0f), r(4000, 0.0f);
        s->process(evs.data(), 2, l.data(), r.data(), 4000);
        return l;
    };
    auto a = render(), b = render();
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], b[i], 1e-9);
}

VSM_TEST(sh101_save_load_roundtrip) {
    auto a = makeSh101();
    ParamId cutoff = byName(*a, "Filter Cutoff");
    ParamId sub = byName(*a, "Sub Level");
    a->setParameter(cutoff, 2500.0f);
    a->setParameter(sub, 0.9f);
    PresetState st = a->saveState();
    VSM_ASSERT_EQ(st.pluginTypeId, std::string("vsm.sh101"));
    auto b = makeSh101();
    b->loadState(st);
    VSM_ASSERT_NEAR(b->getParameter(cutoff), 2500.0, 1e-2);
    VSM_ASSERT_NEAR(b->getParameter(sub), 0.9, 1e-3);
}

VSM_TEST(sh101_parameter_list_size) {
    auto s = makeSh101();
    VSM_ASSERT_EQ(s->parameterList().size(), static_cast<size_t>(22));
}
