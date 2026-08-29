#include "TestFramework.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

SynthPluginPtr makeFlute(double sr = kSampleRate) {
    auto p = PluginRegistry::instance().create("vsm.flute");
    p->initialize(sr, 512);
    return p;
}

SynthPluginPtr makeWind(double sr = kSampleRate) {
    auto p = PluginRegistry::instance().create("vsm.wind");
    p->initialize(sr, 512);
    return p;
}

float peakAbs(const std::vector<float>& b) {
    float p = 0.0f; for (float s : b) p = std::max(p, std::abs(s)); return p;
}

ParamId paramByName(const ISynthPlugin& p, const std::string& n) {
    for (const auto& info : p.parameterList()) if (info.name == n) return info.id;
    return 0;
}

void setByName(ISynthPlugin& p, const std::string& n, float v) {
    p.setParameter(paramByName(p, n), v);
}

MidiNoteEvent noteOn(int offset, uint8_t note, uint8_t vel = 110) {
    return MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, offset, 0, note, vel};
}

std::vector<float> play(SynthPluginPtr& p, uint8_t note, int numSamples, uint8_t vel = 110) {
    std::vector<float> l(static_cast<size_t>(numSamples), 0.0f);
    std::vector<float> r(static_cast<size_t>(numSamples), 0.0f);
    const MidiNoteEvent ev = noteOn(0, note, vel);
    p->process(&ev, 1, l.data(), r.data(), numSamples);
    return l;
}

double midiToHz(int note) {
    return 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0);
}

double magnitudeAt(const std::vector<float>& x, size_t from, size_t count, double hz) {
    const size_t fin = std::min(x.size(), from + count);
    if (from >= fin) return 0.0;
    std::complex<double> acc{0.0, 0.0};
    const double w = 2.0 * kPi * hz / kSampleRate;
    const double n = static_cast<double>(fin - from);
    size_t i2 = 0;
    for (size_t i = from; i < fin; ++i, ++i2) {
        const double fen = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i2) / n);
        acc += std::complex<double>(x[i] * fen, 0.0)
             * std::exp(std::complex<double>(0.0, -w * static_cast<double>(i2)));
    }
    return std::abs(acc) / n;
}

/// Réglages qui tiennent une note franche : pas de vibrato, pas de dérive.
void tenue(ISynthPlugin& p) {
    setByName(p, "Vibrato Depth", 0.0f);
    setByName(p, "Analog Character", 0.0f);
    setByName(p, "Attack", 0.01f);
}

} // namespace

VSM_TEST(flute_registered) {
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.flute"));
}

VSM_TEST(flute_silent_with_no_events) {
    auto p = makeFlute();
    std::vector<float> l(512, 0.0f), r(512, 0.0f);
    p->process(nullptr, 0, l.data(), r.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0, 1e-6);
    VSM_ASSERT_EQ(p->activeVoiceCount(), 0);
}

VSM_TEST(flute_self_oscillates) {
    // LA PREMIÈRE QUESTION, ET ELLE N'ÉTAIT PAS ACQUISE. Le prototype conique
    // du parc n'a jamais réussi à s'auto-osciller par quatre routes : il
    // rendait une sinusoïde pure, c'est-à-dire un résonateur qui sonne et non
    // un instrument qui joue. Ce test-là est donc le premier à passer, et le
    // plus important : la note doit TENIR, et ne pas être un simple sinus.
    auto p = makeFlute();
    tenue(*p);
    const int note = 57;
    const double f0 = midiToHz(note);
    const auto out = play(p, static_cast<uint8_t>(note), 72000);

    // Elle sonne franchement.
    VSM_ASSERT(peakAbs(out) > 0.05f);
    // Elle tient : autant d'énergie à la fin qu'au milieu.
    double milieu = 0.0, fin = 0.0;
    for (size_t i = 24000; i < 36000; ++i) milieu += static_cast<double>(out[i]) * out[i];
    for (size_t i = 54000; i < 66000; ++i) fin += static_cast<double>(out[i]) * out[i];
    VSM_ASSERT(fin > milieu * 0.5);
    // Et ce n'est PAS un sinus : le rang 3 existe.
    const double h1 = magnitudeAt(out, 24000, 32768, f0);
    const double h3 = magnitudeAt(out, 24000, 32768, 3.0 * f0);
    VSM_ASSERT(h1 > 1e-4);
    VSM_ASSERT(h3 > h1 * 0.05);
}

VSM_TEST(flute_carries_even_harmonics_where_the_reed_cannot) {
    // LE TRAIT DISTINCTIF, ET C'EST L'EXACT MIROIR DE
    // `wind_bore_supports_only_odd_harmonics`.
    //
    // Un cylindre à anche impose `x(t + T/2) = -x(t)` : cette symétrie
    // demi-onde annule mathématiquement les rangs PAIRS, quel que soit le
    // réglage. Un tuyau ouvert aux deux bouts n'a pas cette contrainte, et sa
    // boucle non inversante à retard complet porte la série complète.
    //
    // On compare donc les deux machines sur la même note, et le test mesure les
    // deux moitiés : le rang 2 de la flûte est présent, celui de `vsm.wind` ne
    // l'est pas.
    const int note = 57;
    const double f0 = midiToHz(note);

    auto flute = makeFlute();
    tenue(*flute);
    const auto a = play(flute, static_cast<uint8_t>(note), 72000);

    auto wind = makeWind();
    setByName(*wind, "Vibrato Depth", 0.0f);
    setByName(*wind, "Analog Character", 0.0f);
    const auto b = play(wind, static_cast<uint8_t>(note), 72000);

    const size_t depart = 24000, longueur = 32768;
    const double fluteH1 = magnitudeAt(a, depart, longueur, f0);
    const double fluteH2 = magnitudeAt(a, depart, longueur, 2.0 * f0);
    const double windH1 = magnitudeAt(b, depart, longueur, f0);
    const double windH2 = magnitudeAt(b, depart, longueur, 2.0 * f0);

    VSM_ASSERT(fluteH1 > 1e-4);
    VSM_ASSERT(windH1 > 1e-4);
    // La flûte porte son rang 2...
    VSM_ASSERT(fluteH2 > fluteH1 * 0.10);
    // ...et l'anche cylindrique, non.
    VSM_ASSERT(windH2 < windH1 * 0.02);
}

VSM_TEST(flute_needs_breath_to_sound) {
    // L'oscillation naît de la boucle jet/tuyau : sans souffle, rien. Ce n'est
    // pas un oscillateur qu'on module par une enveloppe.
    auto p = makeFlute();
    tenue(*p);
    setByName(*p, "Breath Pressure", 0.0f);
    setByName(*p, "Breath Noise", 0.0f);
    setByName(*p, "Velocity Sensitivity", 0.0f);
    const auto out = play(p, 57, 48000);
    VSM_ASSERT(peakAbs(out) < 0.01f);
}

VSM_TEST(flute_jet_delay_changes_the_regime) {
    // UN RÉGLAGE QUI NE FAIT RIEN EST PIRE QU'ABSENT. Celui-ci décide du régime
    // d'oscillation : mesuré, à retard court et souffle poussé, la note bascule
    // à l'OCTAVE -- c'est le surbouffle d'une vraie flûte. Le test vérifie que
    // le rapport entre le rang 2 et le fondamental change franchement sur la
    // course du réglage.
    const int note = 57;
    const double f0 = midiToHz(note);
    auto rapport = [&](float jet) {
        auto p = makeFlute();
        tenue(*p);
        setByName(*p, "Jet Delay", jet);
        setByName(*p, "Breath Pressure", 0.8f);
        const auto out = play(p, static_cast<uint8_t>(note), 72000);
        const double h1 = magnitudeAt(out, 24000, 32768, f0);
        const double h2 = magnitudeAt(out, 24000, 32768, 2.0 * f0);
        return h2 / std::max(h1, 1e-12);
    };
    VSM_ASSERT(rapport(0.3f) > rapport(0.5f) * 3.0);
}

VSM_TEST(flute_is_polyphonic_as_a_section) {
    auto p = makeFlute();
    tenue(*p);
    std::vector<MidiNoteEvent> ev{noteOn(0, 55), noteOn(0, 59), noteOn(0, 62)};
    std::vector<float> l(48000, 0.0f), r(48000, 0.0f);
    p->process(ev.data(), static_cast<int>(ev.size()), l.data(), r.data(), 48000);
    VSM_ASSERT_EQ(p->activeVoiceCount(), 3);
    VSM_ASSERT(peakAbs(l) > 0.05f);
    VSM_ASSERT(peakAbs(l) < 1.0f);
}

VSM_TEST(flute_stays_finite_under_extreme_settings) {
    auto p = makeFlute();
    for (const auto& info : p->parameterList()) p->setParameter(info.id, info.maxValue);
    const auto a = play(p, 36, 48000, 127);
    for (float s : a) VSM_ASSERT(std::isfinite(s));
    auto q = makeFlute();
    for (const auto& info : q->parameterList()) q->setParameter(info.id, info.minValue);
    const auto b = play(q, 96, 48000, 127);
    for (float s : b) VSM_ASSERT(std::isfinite(s));
}

VSM_TEST(flute_is_deterministic) {
    auto a = makeFlute();
    auto b = makeFlute();
    const auto x = play(a, 57, 48000);
    const auto y = play(b, 57, 48000);
    for (size_t i = 0; i < x.size(); ++i) VSM_ASSERT_EQ(x[i], y[i]);
}

VSM_TEST(flute_save_load_roundtrip) {
    auto p = makeFlute();
    setByName(*p, "Jet Delay", 0.63f);
    setByName(*p, "Bell Damping", 0.21f);
    const auto state = p->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.flute"));
    auto autre = makeFlute();
    autre->loadState(state);
    VSM_ASSERT_NEAR(autre->getParameter(paramByName(*autre, "Jet Delay")), 0.63f, 1e-6);
    VSM_ASSERT_NEAR(autre->getParameter(paramByName(*autre, "Bell Damping")), 0.21f, 1e-6);
}

VSM_TEST(flute_parameter_list_is_complete) {
    auto p = makeFlute();
    VSM_ASSERT_EQ(static_cast<int>(p->parameterList().size()), 13);
}
