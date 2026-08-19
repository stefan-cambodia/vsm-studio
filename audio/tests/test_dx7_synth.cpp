#include "TestFramework.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

namespace {
SynthPluginPtr makeDx7(double sr = 48000.0) {
    auto p = PluginRegistry::instance().create("vsm.dx7");
    p->initialize(sr, 512);
    return p;
}
float peakAbs(const std::vector<float>& b) { float p = 0.0f; for (float s : b) p = std::max(p, std::abs(s)); return p; }
MidiNoteEvent noteOn(int off, uint8_t note, uint8_t vel = 100) { return {MidiNoteEvent::Kind::NoteOn, off, 0, note, vel}; }
ParamId byName(const ISynthPlugin& p, const std::string& n) { for (const auto& i : p.parameterList()) if (i.name == n) return i.id; return 0; }

std::vector<float> renderNote(SynthPluginPtr& s, uint8_t note, uint8_t vel = 100, int n = 6000) {
    MidiNoteEvent on = noteOn(0, note, vel);
    std::vector<float> l(static_cast<size_t>(n), 0.0f), r(static_cast<size_t>(n), 0.0f);
    s->process(&on, 1, l.data(), r.data(), n);
    return l;
}
} // namespace

VSM_TEST(dx7_registered) {
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.dx7"));
}

VSM_TEST(dx7_silent_with_no_events) {
    auto s = makeDx7();
    std::vector<float> l(512, 0.0f), r(512, 0.0f);
    s->process(nullptr, 0, l.data(), r.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0, 1e-6);
    VSM_ASSERT_EQ(s->activeVoiceCount(), 0);
}

VSM_TEST(dx7_note_produces_sound) {
    auto s = makeDx7();
    auto l = renderNote(s, 48);
    VSM_ASSERT(peakAbs(l) > 0.02f);
    for (float x : l) VSM_ASSERT(std::isfinite(x));
    VSM_ASSERT_EQ(s->activeVoiceCount(), 1);
}

VSM_TEST(dx7_is_polyphonic_and_steals) {
    auto s = makeDx7();
    std::vector<MidiNoteEvent> chord;
    for (int i = 0; i < 8; ++i) chord.push_back(noteOn(0, static_cast<uint8_t>(48 + i), 100));
    std::vector<float> l(2000, 0.0f), r(2000, 0.0f);
    s->process(chord.data(), 8, l.data(), r.data(), 2000);
    VSM_ASSERT_EQ(s->activeVoiceCount(), 8);

    auto s2 = makeDx7();
    std::vector<MidiNoteEvent> many;
    for (int i = 0; i < 11; ++i) many.push_back(noteOn(i * 10, static_cast<uint8_t>(48 + i), 100));
    std::vector<float> l2(2000, 0.0f), r2(2000, 0.0f);
    s2->process(many.data(), 11, l2.data(), r2.data(), 2000);
    VSM_ASSERT_EQ(s2->activeVoiceCount(), 8);
}

VSM_TEST(dx7_algorithm_changes_routing) {
    // Deux algorithmes différents (3 = pile unique 1 porteuse ; 8 = additif
    // 6 porteuses) doivent produire des rendus nettement différents à
    // réglages d'opérateurs identiques -> le sélecteur d'algorithme route
    // réellement le signal.
    auto stack = makeDx7();
    stack->setParameter(byName(*stack, "Algorithm"), 3.0f);
    auto a = renderNote(stack, 48);

    auto additive = makeDx7();
    additive->setParameter(byName(*additive, "Algorithm"), 8.0f);
    auto b = renderNote(additive, 48);

    double diff = 0.0;
    for (size_t i = 500; i < a.size(); ++i) diff += std::abs(a[i] - b[i]);
    VSM_ASSERT(diff > 20.0);
}

VSM_TEST(dx7_modulation_depth_changes_timbre) {
    // Algorithme 3 (pile unique, op1 porteuse modulée par op2). Sans
    // modulateur -> sinus pur ; avec op2 fort -> spectre FM riche.
    auto pure = makeDx7();
    pure->setParameter(byName(*pure, "Algorithm"), 3.0f);
    pure->setParameter(byName(*pure, "Op2 Level"), 0.0f);
    pure->setParameter(byName(*pure, "Op3 Level"), 0.0f);
    pure->setParameter(byName(*pure, "Op4 Level"), 0.0f);
    pure->setParameter(byName(*pure, "Op5 Level"), 0.0f);
    pure->setParameter(byName(*pure, "Op6 Level"), 0.0f);
    auto a = renderNote(pure, 48);

    auto fm = makeDx7();
    fm->setParameter(byName(*fm, "Algorithm"), 3.0f);
    fm->setParameter(byName(*fm, "Op2 Level"), 1.0f);
    auto b = renderNote(fm, 48);

    double diff = 0.0;
    for (size_t i = 500; i < a.size(); ++i) diff += std::abs(a[i] - b[i]);
    VSM_ASSERT(diff > 20.0);
}

VSM_TEST(dx7_feedback_changes_timbre) {
    auto noFb = makeDx7();
    noFb->setParameter(byName(*noFb, "Algorithm"), 3.0f);
    noFb->setParameter(byName(*noFb, "Feedback"), 0.0f);
    auto a = renderNote(noFb, 45);

    auto fb = makeDx7();
    fb->setParameter(byName(*fb, "Algorithm"), 3.0f);
    fb->setParameter(byName(*fb, "Feedback"), 1.0f);
    auto b = renderNote(fb, 45);

    double diff = 0.0;
    for (size_t i = 500; i < a.size(); ++i) diff += std::abs(a[i] - b[i]);
    VSM_ASSERT(diff > 5.0);
}

VSM_TEST(dx7_velocity_sensitive_when_enabled) {
    // Sensibilité activée -> deux vélocités donnent des rendus différents.
    auto s = makeDx7();
    s->setParameter(byName(*s, "Algorithm"), 3.0f);
    s->setParameter(byName(*s, "Velocity Sens"), 0.8f);
    auto soft = renderNote(s, 50, 20);
    auto hardHit = makeDx7();
    hardHit->setParameter(byName(*hardHit, "Algorithm"), 3.0f);
    hardHit->setParameter(byName(*hardHit, "Velocity Sens"), 0.8f);
    auto loud = renderNote(hardHit, 50, 120);
    VSM_ASSERT(peakAbs(loud) > peakAbs(soft) * 1.2f);
}

VSM_TEST(dx7_velocity_insensitive_when_zero) {
    // Sensibilité nulle -> vélocité sans effet (rendu identique).
    auto s1 = makeDx7();
    s1->setParameter(byName(*s1, "Velocity Sens"), 0.0f);
    auto a = renderNote(s1, 50, 20, 3000);
    auto s2 = makeDx7();
    s2->setParameter(byName(*s2, "Velocity Sens"), 0.0f);
    auto b = renderNote(s2, 50, 120, 3000);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], b[i], 1e-9);
}

VSM_TEST(dx7_is_deterministic) {
    auto render = [] {
        auto s = makeDx7();
        s->setParameter(byName(*s, "Analog Character"), 0.6f);
        s->setParameter(byName(*s, "Algorithm"), 5.0f);
        s->setParameter(byName(*s, "Feedback"), 0.5f);
        std::vector<MidiNoteEvent> chord = {noteOn(0,48,90),noteOn(0,55,110)};
        std::vector<float> l(4000, 0.0f), r(4000, 0.0f);
        s->process(chord.data(), 2, l.data(), r.data(), 4000);
        return l;
    };
    auto a = render(), b = render();
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], b[i], 1e-9);
}

VSM_TEST(dx7_save_load_roundtrip) {
    auto a = makeDx7();
    ParamId algo = byName(*a, "Algorithm");
    ParamId op2 = byName(*a, "Op2 Ratio");
    a->setParameter(algo, 6.0f);
    a->setParameter(op2, 3.5f);
    PresetState st = a->saveState();
    VSM_ASSERT_EQ(st.pluginTypeId, std::string("vsm.dx7"));
    auto b = makeDx7();
    b->loadState(st);
    VSM_ASSERT_NEAR(b->getParameter(algo), 6.0, 1e-3);
    VSM_ASSERT_NEAR(b->getParameter(op2), 3.5, 1e-3);
}

VSM_TEST(dx7_parameter_list_size) {
    auto s = makeDx7();
    VSM_ASSERT_EQ(s->parameterList().size(), static_cast<size_t>(51));
}

VSM_TEST(dx7_pitch_envelope_changes_attack) {
    // §11 : une enveloppe de pitch non nulle modifie l'attaque (le pitch
    // "chute" vers la note tenue) -> les premiers échantillons diffèrent.
    auto flat = makeDx7();
    flat->setParameter(byName(*flat, "Pitch Env Amount"), 0.0f);
    auto a = renderNote(flat, 48, 100, 4000);

    auto swept = makeDx7();
    swept->setParameter(byName(*swept, "Pitch Env Amount"), 12.0f);
    swept->setParameter(byName(*swept, "Pitch Env Time"), 0.2f);
    auto b = renderNote(swept, 48, 100, 4000);

    double diff = 0.0;
    for (size_t i = 0; i < 2000; ++i) diff += std::abs(a[i] - b[i]);
    VSM_ASSERT(diff > 5.0);
}

VSM_TEST(dx7_keyboard_scaling_attenuates_high_notes) {
    // §11 : le keyboard level scaling atténue le niveau dans l'aigu.
    auto noScale = makeDx7();
    noScale->setParameter(byName(*noScale, "Key Level Scaling"), 0.0f);
    const float highNoScale = peakAbs(renderNote(noScale, 84));

    auto scaled = makeDx7();
    scaled->setParameter(byName(*scaled, "Key Level Scaling"), 1.0f);
    const float highScaled = peakAbs(renderNote(scaled, 84));

    VSM_ASSERT(highScaled < highNoScale * 0.9f);
}

VSM_TEST(dx7_fixed_frequency_ignores_note) {
    // §11 : un opérateur en fréquence FIXE ne suit plus la note jouée.
    // Algorithme 8 (additif), seul op1 audible et fixe -> deux notes
    // différentes produisent un rendu identique (même hauteur fixe).
    auto make = [](uint8_t note) {
        auto s = makeDx7();
        s->setParameter(byName(*s, "Algorithm"), 8.0f);
        for (int op = 2; op <= 6; ++op)
            s->setParameter(byName(*s, "Op" + std::to_string(op) + " Level"), 0.0f);
        s->setParameter(byName(*s, "Op1 Fixed"), 1.0f);
        s->setParameter(byName(*s, "Analog Character"), 0.0f);
        return renderNote(s, note, 100, 3000);
    };
    auto low = make(40), high = make(72);
    for (size_t i = 0; i < low.size(); ++i) VSM_ASSERT_NEAR(low[i], high[i], 1e-6);
}
