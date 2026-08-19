#include "TestFramework.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

namespace {
SynthPluginPtr makeTr909(double sr = 48000.0) {
    auto p = PluginRegistry::instance().create("vsm.tr909");
    p->initialize(sr, 512);
    return p;
}
float peakAbs(const std::vector<float>& b) {
    float p = 0.0f; for (float s : b) p = std::max(p, std::abs(s)); return p;
}
double energy(const std::vector<float>& b, size_t from = 0) {
    double e = 0.0; for (size_t i = from; i < b.size(); ++i) e += static_cast<double>(b[i]) * b[i]; return e;
}
MidiNoteEvent hit(int offset, uint8_t note, uint8_t vel = 110) {
    return MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, offset, 9, note, vel};
}
ParamId paramByName(const ISynthPlugin& p, const std::string& n) {
    for (const auto& info : p.parameterList()) if (info.name == n) return info.id; return 0;
}
} // namespace

VSM_TEST(tr909_registered) {
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.tr909"));
}

VSM_TEST(tr909_silent_with_no_events) {
    auto d = makeTr909();
    std::vector<float> l(512, 0.0f), r(512, 0.0f);
    d->process(nullptr, 0, l.data(), r.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0, 1e-6);
    VSM_ASSERT_EQ(d->activeVoiceCount(), 0);
}

VSM_TEST(tr909_each_piece_triggers) {
    const uint8_t notes[] = {36, 38, 42, 46, 39, 49, 45, 47, 50};
    for (uint8_t note : notes) {
        auto d = makeTr909();
        MidiNoteEvent e = hit(0, note);
        std::vector<float> l(8000, 0.0f), r(8000, 0.0f);
        d->process(&e, 1, l.data(), r.data(), 8000);
        VSM_ASSERT(peakAbs(l) > 0.02f);
        for (float s : l) VSM_ASSERT(std::isfinite(s));
    }
}

VSM_TEST(tr909_toms_have_distinct_pitch) {
    // Low/mid/hi toms doivent produire des sons différents (accords distincts).
    auto render = [](uint8_t note) {
        auto d = makeTr909();
        MidiNoteEvent e = hit(0, note);
        std::vector<float> l(4000, 0.0f), r(4000, 0.0f);
        d->process(&e, 1, l.data(), r.data(), 4000);
        return l;
    };
    auto low = render(45);
    auto hi = render(50);
    double diff = 0.0;
    for (size_t i = 0; i < low.size(); ++i) diff += std::abs(low[i] - hi[i]);
    VSM_ASSERT(diff > 1.0); // clairement différents
}

VSM_TEST(tr909_crash_has_long_tail) {
    // Le crash doit encore sonner après 1 seconde (longue traîne).
    auto d = makeTr909(48000.0);
    MidiNoteEvent e = hit(0, 49);
    std::vector<float> l(96000, 0.0f), r(96000, 0.0f); // 2 s
    d->process(&e, 1, l.data(), r.data(), 96000);
    VSM_ASSERT(energy(l, 48000) > 0.001); // énergie non négligeable après 1 s
}

VSM_TEST(tr909_closed_hat_chokes_open_hat) {
    auto d = makeTr909(48000.0);
    MidiNoteEvent openEv = hit(0, 46);
    std::vector<float> l(2000, 0.0f), r(2000, 0.0f);
    d->process(&openEv, 1, l.data(), r.data(), 2000);
    const double openAlone = energy(l);

    auto d2 = makeTr909(48000.0);
    d2->process(&openEv, 1, l.data(), r.data(), 500);
    MidiNoteEvent closedEv = hit(0, 42);
    std::vector<float> l2(2000, 0.0f), r2(2000, 0.0f);
    d2->process(&closedEv, 1, l2.data(), r2.data(), 2000);
    VSM_ASSERT(openAlone > 0.0);
    VSM_ASSERT(energy(l2, 1000) < openAlone * 0.25);
}

VSM_TEST(tr909_is_deterministic) {
    auto render = [] {
        auto d = makeTr909();
        std::vector<MidiNoteEvent> evs = {hit(0, 36), hit(1000, 38), hit(2000, 42), hit(3000, 49)};
        std::vector<float> l(8000, 0.0f), r(8000, 0.0f);
        d->process(evs.data(), static_cast<int>(evs.size()), l.data(), r.data(), 8000);
        return l;
    };
    auto a = render();
    auto b = render();
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], b[i], 1e-9);
}

VSM_TEST(tr909_save_load_roundtrip) {
    auto a = makeTr909();
    ParamId snappy = paramByName(*a, "Snare Snappy");
    ParamId attack = paramByName(*a, "Kick Attack");
    a->setParameter(snappy, 0.9f);
    a->setParameter(attack, 0.2f);
    PresetState st = a->saveState();
    VSM_ASSERT_EQ(st.pluginTypeId, std::string("vsm.tr909"));
    auto b = makeTr909();
    b->loadState(st);
    VSM_ASSERT_NEAR(b->getParameter(snappy), 0.9, 1e-3);
    VSM_ASSERT_NEAR(b->getParameter(attack), 0.2, 1e-3);
}

VSM_TEST(tr909_parameter_list_size) {
    auto d = makeTr909();
    VSM_ASSERT_EQ(d->parameterList().size(), static_cast<size_t>(20));
}
