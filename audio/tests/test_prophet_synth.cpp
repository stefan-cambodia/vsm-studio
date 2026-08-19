#include "TestFramework.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

namespace {
SynthPluginPtr makeProphet(double sr = 48000.0) {
    auto p = PluginRegistry::instance().create("vsm.prophet");
    p->initialize(sr, 512);
    return p;
}
float peakAbs(const std::vector<float>& b) { float p = 0.0f; for (float s : b) p = std::max(p, std::abs(s)); return p; }
MidiNoteEvent noteOn(int off, uint8_t note, uint8_t vel = 100) { return {MidiNoteEvent::Kind::NoteOn, off, 0, note, vel}; }
ParamId byName(const ISynthPlugin& p, const std::string& n) { for (const auto& i : p.parameterList()) if (i.name == n) return i.id; return 0; }

std::vector<float> renderNote(SynthPluginPtr& s, uint8_t note, int n = 6000) {
    MidiNoteEvent on = noteOn(0, note, 100);
    std::vector<float> l(static_cast<size_t>(n), 0.0f), r(static_cast<size_t>(n), 0.0f);
    s->process(&on, 1, l.data(), r.data(), n);
    return l;
}
} // namespace

VSM_TEST(prophet_registered) {
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.prophet"));
}

VSM_TEST(prophet_silent_with_no_events) {
    auto s = makeProphet();
    std::vector<float> l(512, 0.0f), r(512, 0.0f);
    s->process(nullptr, 0, l.data(), r.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0, 1e-6);
    VSM_ASSERT_EQ(s->activeVoiceCount(), 0);
}

VSM_TEST(prophet_note_produces_sound) {
    auto s = makeProphet();
    auto l = renderNote(s, 48);
    VSM_ASSERT(peakAbs(l) > 0.02f);
    for (float x : l) VSM_ASSERT(std::isfinite(x));
    VSM_ASSERT_EQ(s->activeVoiceCount(), 1);
}

VSM_TEST(prophet_is_polyphonic_five_voices) {
    auto s = makeProphet();
    std::vector<MidiNoteEvent> chord = {noteOn(0,48,100),noteOn(0,52,100),noteOn(0,55,100),
                                        noteOn(0,59,100),noteOn(0,62,100)};
    std::vector<float> l(2000, 0.0f), r(2000, 0.0f);
    s->process(chord.data(), 5, l.data(), r.data(), 2000);
    VSM_ASSERT_EQ(s->activeVoiceCount(), 5);
}

VSM_TEST(prophet_steals_beyond_five_voices) {
    auto s = makeProphet();
    std::vector<MidiNoteEvent> notes;
    for (int i = 0; i < 7; ++i) notes.push_back(noteOn(i * 10, static_cast<uint8_t>(48 + i), 100));
    std::vector<float> l(2000, 0.0f), r(2000, 0.0f);
    s->process(notes.data(), 7, l.data(), r.data(), 2000);
    VSM_ASSERT_EQ(s->activeVoiceCount(), 5);
}

VSM_TEST(prophet_polymod_oscb_changes_timbre) {
    // Poly-Mod osc B -> Freq A doit modifier le timbre (cross-mod audio).
    auto base = makeProphet();
    base->setParameter(byName(*base, "Osc B Level"), 0.0f); // B muet dans le mix
    auto plain = renderNote(base, 45);

    auto modded = makeProphet();
    modded->setParameter(byName(*modded, "Osc B Level"), 0.0f);
    modded->setParameter(byName(*modded, "PolyMod Osc B"), 1.0f);
    modded->setParameter(byName(*modded, "PolyMod to Freq A"), 1.0f);
    modded->setParameter(byName(*modded, "Osc B Detune"), 7.0f);
    auto crossmod = renderNote(modded, 45);

    double diff = 0.0;
    for (size_t i = 1000; i < plain.size(); ++i) diff += std::abs(plain[i] - crossmod[i]);
    VSM_ASSERT(diff > 5.0); // timbre nettement différent
}

VSM_TEST(prophet_polymod_filter_env_opens_filter) {
    // Poly-Mod enveloppe de filtre -> coupure doit ouvrir le filtre (plus
    // d'énergie haute) vs sans poly-mod.
    auto closed = makeProphet();
    closed->setParameter(byName(*closed, "Filter Cutoff"), 300.0f);
    closed->setParameter(byName(*closed, "Filter Env Amount"), 0.0f);
    auto lowRms = [](SynthPluginPtr& s) {
        auto l = renderNote(s, 40, 4000);
        double e = 0.0; for (float x : l) e += static_cast<double>(x) * x; return e;
    };
    const double eClosed = lowRms(closed);

    auto opened = makeProphet();
    opened->setParameter(byName(*opened, "Filter Cutoff"), 300.0f);
    opened->setParameter(byName(*opened, "Filter Env Amount"), 0.0f);
    opened->setParameter(byName(*opened, "PolyMod Filt Env"), 1.0f);
    opened->setParameter(byName(*opened, "PolyMod to Filter"), 1.0f);
    const double eOpened = lowRms(opened);

    VSM_ASSERT(eOpened > eClosed * 1.2);
}

VSM_TEST(prophet_sync_changes_timbre) {
    auto noSync = makeProphet();
    noSync->setParameter(byName(*noSync, "Osc B Detune"), 5.0f);
    auto a = renderNote(noSync, 45);

    auto withSync = makeProphet();
    withSync->setParameter(byName(*withSync, "Sync"), 1.0f);
    withSync->setParameter(byName(*withSync, "Osc B Detune"), 5.0f);
    auto b = renderNote(withSync, 45);

    double diff = 0.0;
    for (size_t i = 500; i < a.size(); ++i) diff += std::abs(a[i] - b[i]);
    VSM_ASSERT(diff > 1.0);
}

VSM_TEST(prophet_not_velocity_sensitive) {
    // Clavier Prophet-5 non vélocité-sensible : même note à deux vélocités
    // -> rendu strictement identique.
    auto v1 = makeProphet(); MidiNoteEvent e1 = noteOn(0, 50, 30);
    std::vector<float> a(3000, 0.0f), ar(3000, 0.0f); v1->process(&e1, 1, a.data(), ar.data(), 3000);
    auto v2 = makeProphet(); MidiNoteEvent e2 = noteOn(0, 50, 120);
    std::vector<float> bb(3000, 0.0f), br(3000, 0.0f); v2->process(&e2, 1, bb.data(), br.data(), 3000);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], bb[i], 1e-9);
}

VSM_TEST(prophet_is_deterministic) {
    auto render = [] {
        auto s = makeProphet();
        s->setParameter(byName(*s, "Analog Character"), 0.6f);
        s->setParameter(byName(*s, "PolyMod Osc B"), 0.5f);
        s->setParameter(byName(*s, "PolyMod to Freq A"), 1.0f);
        std::vector<MidiNoteEvent> chord = {noteOn(0,48,100),noteOn(0,55,100)};
        std::vector<float> l(4000, 0.0f), r(4000, 0.0f);
        s->process(chord.data(), 2, l.data(), r.data(), 4000);
        return l;
    };
    auto a = render(), b = render();
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], b[i], 1e-9);
}

VSM_TEST(prophet_save_load_roundtrip) {
    auto a = makeProphet();
    ParamId res = byName(*a, "Filter Resonance");
    ParamId pm = byName(*a, "PolyMod Osc B");
    a->setParameter(res, 3.5f);
    a->setParameter(pm, 0.66f);
    PresetState st = a->saveState();
    VSM_ASSERT_EQ(st.pluginTypeId, std::string("vsm.prophet"));
    auto b = makeProphet();
    b->loadState(st);
    VSM_ASSERT_NEAR(b->getParameter(res), 3.5, 1e-2);
    VSM_ASSERT_NEAR(b->getParameter(pm), 0.66, 1e-3);
}

VSM_TEST(prophet_parameter_list_size) {
    auto s = makeProphet();
    VSM_ASSERT_EQ(s->parameterList().size(), static_cast<size_t>(29));
}
