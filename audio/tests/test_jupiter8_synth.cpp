#include "TestFramework.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

namespace {
SynthPluginPtr makeJupiter(double sr = 48000.0) {
    auto p = PluginRegistry::instance().create("vsm.jupiter8");
    p->initialize(sr, 512);
    return p;
}
float peakAbs(const std::vector<float>& b) { float p = 0.0f; for (float s : b) p = std::max(p, std::abs(s)); return p; }
double energy(const std::vector<float>& b) { double e = 0.0; for (float s : b) e += static_cast<double>(s) * s; return e; }
MidiNoteEvent noteOn(int off, uint8_t note, uint8_t vel = 100) { return {MidiNoteEvent::Kind::NoteOn, off, 0, note, vel}; }
ParamId byName(const ISynthPlugin& p, const std::string& n) { for (const auto& i : p.parameterList()) if (i.name == n) return i.id; return 0; }

std::vector<float> renderNoteL(SynthPluginPtr& s, uint8_t note, int n = 6000) {
    MidiNoteEvent on = noteOn(0, note, 100);
    std::vector<float> l(static_cast<size_t>(n), 0.0f), r(static_cast<size_t>(n), 0.0f);
    s->process(&on, 1, l.data(), r.data(), n);
    return l;
}
} // namespace

VSM_TEST(jupiter8_registered) {
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.jupiter8"));
}

VSM_TEST(jupiter8_silent_with_no_events) {
    auto s = makeJupiter();
    std::vector<float> l(512, 0.0f), r(512, 0.0f);
    s->process(nullptr, 0, l.data(), r.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0, 1e-6);
    VSM_ASSERT_EQ(s->activeVoiceCount(), 0);
}

VSM_TEST(jupiter8_note_produces_sound) {
    auto s = makeJupiter();
    auto l = renderNoteL(s, 48);
    VSM_ASSERT(peakAbs(l) > 0.02f);
    for (float x : l) VSM_ASSERT(std::isfinite(x));
    VSM_ASSERT_EQ(s->activeVoiceCount(), 1);
}

VSM_TEST(jupiter8_is_polyphonic_eight_voices) {
    auto s = makeJupiter();
    std::vector<MidiNoteEvent> chord;
    for (int i = 0; i < 8; ++i) chord.push_back(noteOn(0, static_cast<uint8_t>(48 + i), 100));
    std::vector<float> l(2000, 0.0f), r(2000, 0.0f);
    s->process(chord.data(), 8, l.data(), r.data(), 2000);
    VSM_ASSERT_EQ(s->activeVoiceCount(), 8);
}

VSM_TEST(jupiter8_steals_beyond_eight_voices) {
    auto s = makeJupiter();
    std::vector<MidiNoteEvent> notes;
    for (int i = 0; i < 10; ++i) notes.push_back(noteOn(i * 10, static_cast<uint8_t>(48 + i), 100));
    std::vector<float> l(2000, 0.0f), r(2000, 0.0f);
    s->process(notes.data(), 10, l.data(), r.data(), 2000);
    VSM_ASSERT_EQ(s->activeVoiceCount(), 8);
}

VSM_TEST(jupiter8_crossmod_changes_timbre) {
    // La cross-modulation VCO-2 -> fréquence VCO-1 doit modifier le timbre.
    auto plainS = makeJupiter();
    plainS->setParameter(byName(*plainS, "Chorus Mode"), 0.0f);
    plainS->setParameter(byName(*plainS, "VCO-2 Detune"), 7.0f);
    auto plain = renderNoteL(plainS, 45);

    auto modS = makeJupiter();
    modS->setParameter(byName(*modS, "Chorus Mode"), 0.0f);
    modS->setParameter(byName(*modS, "VCO-2 Detune"), 7.0f);
    modS->setParameter(byName(*modS, "Cross Mod"), 1.0f);
    auto crossmod = renderNoteL(modS, 45);

    double diff = 0.0;
    for (size_t i = 1000; i < plain.size(); ++i) diff += std::abs(plain[i] - crossmod[i]);
    VSM_ASSERT(diff > 5.0);
}

VSM_TEST(jupiter8_sync_changes_timbre) {
    auto noSync = makeJupiter();
    noSync->setParameter(byName(*noSync, "Chorus Mode"), 0.0f);
    noSync->setParameter(byName(*noSync, "VCO-2 Detune"), 5.0f);
    auto a = renderNoteL(noSync, 45);

    auto withSync = makeJupiter();
    withSync->setParameter(byName(*withSync, "Chorus Mode"), 0.0f);
    withSync->setParameter(byName(*withSync, "Sync"), 1.0f);
    withSync->setParameter(byName(*withSync, "VCO-2 Detune"), 5.0f);
    auto b = renderNoteL(withSync, 45);

    double diff = 0.0;
    for (size_t i = 500; i < a.size(); ++i) diff += std::abs(a[i] - b[i]);
    VSM_ASSERT(diff > 1.0);
}

VSM_TEST(jupiter8_hpf_removes_low_end) {
    // Monter le passe-haut sur une note grave doit réduire l'énergie totale.
    auto open = makeJupiter();
    open->setParameter(byName(*open, "Chorus Mode"), 0.0f);
    open->setParameter(byName(*open, "HPF Cutoff"), 20.0f);
    const double eOpen = energy(renderNoteL(open, 30, 4000));

    auto hp = makeJupiter();
    hp->setParameter(byName(*hp, "Chorus Mode"), 0.0f);
    hp->setParameter(byName(*hp, "HPF Cutoff"), 2000.0f);
    const double eHp = energy(renderNoteL(hp, 30, 4000));

    VSM_ASSERT(eHp < eOpen * 0.8);
}

VSM_TEST(jupiter8_chorus_creates_stereo_width) {
    // Chorus off -> L == R (mono). Chorus on -> les deux canaux diffèrent.
    auto monoS = makeJupiter();
    monoS->setParameter(byName(*monoS, "Chorus Mode"), 0.0f);
    MidiNoteEvent on = noteOn(0, 50, 100);
    std::vector<float> ml(6000, 0.0f), mr(6000, 0.0f);
    monoS->process(&on, 1, ml.data(), mr.data(), 6000);
    for (size_t i = 0; i < ml.size(); ++i) VSM_ASSERT_NEAR(ml[i], mr[i], 1e-9);

    auto stereoS = makeJupiter();
    stereoS->setParameter(byName(*stereoS, "Chorus Mode"), 2.0f);
    std::vector<float> sl(6000, 0.0f), sr(6000, 0.0f);
    MidiNoteEvent on2 = noteOn(0, 50, 100);
    stereoS->process(&on2, 1, sl.data(), sr.data(), 6000);
    double lr = 0.0;
    for (size_t i = 2000; i < sl.size(); ++i) lr += std::abs(sl[i] - sr[i]);
    VSM_ASSERT(lr > 1.0);
}

VSM_TEST(jupiter8_not_velocity_sensitive) {
    // Clavier Jupiter-8 non vélocité-sensible : même note à deux vélocités
    // -> rendu strictement identique.
    auto v1 = makeJupiter(); MidiNoteEvent e1 = noteOn(0, 50, 30);
    std::vector<float> a(3000, 0.0f), ar(3000, 0.0f); v1->process(&e1, 1, a.data(), ar.data(), 3000);
    auto v2 = makeJupiter(); MidiNoteEvent e2 = noteOn(0, 50, 120);
    std::vector<float> bb(3000, 0.0f), br(3000, 0.0f); v2->process(&e2, 1, bb.data(), br.data(), 3000);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], bb[i], 1e-9);
}

VSM_TEST(jupiter8_is_deterministic) {
    auto render = [] {
        auto s = makeJupiter();
        s->setParameter(byName(*s, "Analog Character"), 0.7f);
        s->setParameter(byName(*s, "Cross Mod"), 0.4f);
        s->setParameter(byName(*s, "Chorus Mode"), 2.0f);
        std::vector<MidiNoteEvent> chord = {noteOn(0,48,100),noteOn(0,55,100)};
        std::vector<float> l(4000, 0.0f), r(4000, 0.0f);
        s->process(chord.data(), 2, l.data(), r.data(), 4000);
        l.insert(l.end(), r.begin(), r.end());
        return l;
    };
    auto a = render(), b = render();
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], b[i], 1e-9);
}

VSM_TEST(jupiter8_save_load_roundtrip) {
    auto a = makeJupiter();
    ParamId res = byName(*a, "Filter Resonance");
    ParamId cm = byName(*a, "Cross Mod");
    a->setParameter(res, 3.5f);
    a->setParameter(cm, 0.66f);
    PresetState st = a->saveState();
    VSM_ASSERT_EQ(st.pluginTypeId, std::string("vsm.jupiter8"));
    auto b = makeJupiter();
    b->loadState(st);
    VSM_ASSERT_NEAR(b->getParameter(res), 3.5, 1e-2);
    VSM_ASSERT_NEAR(b->getParameter(cm), 0.66, 1e-3);
}

VSM_TEST(jupiter8_parameter_list_size) {
    auto s = makeJupiter();
    VSM_ASSERT_EQ(s->parameterList().size(), static_cast<size_t>(28));
}
