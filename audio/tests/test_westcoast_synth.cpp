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

SynthPluginPtr makeWc(double sr = kSampleRate) {
    auto p = PluginRegistry::instance().create("vsm.westcoast");
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

/// Centroïde spectral grossier sur une fenêtre, par somme pondérée de
/// magnitudes aux multiples du fondamental. Assez pour dire « ça s'assombrit ».
double centroid(const std::vector<float>& x, size_t from, size_t count, double f0, int rangs = 12) {
    double num = 0.0, den = 0.0;
    for (int k = 1; k <= rangs; ++k) {
        const double m = magnitudeAt(x, from, count, k * f0);
        num += m * k * f0;
        den += m;
    }
    return den > 1e-12 ? num / den : 0.0;
}

/// Règle la voix pour qu'elle TIENNE : c'est la condition pour mesurer un
/// spectre. Une porte qui se referme donnerait un spectre différent à chaque
/// instant de la mesure.
void tenue(ISynthPlugin& p) {
    setByName(p, "Amp Attack", 0.002f);
    setByName(p, "Amp Decay", 0.01f);
    setByName(p, "Amp Sustain", 1.0f);
    setByName(p, "Gate Lag", 0.005f);
    setByName(p, "Gate Cutoff", 16000.0f);
    setByName(p, "Analog Character", 0.0f);
    setByName(p, "Mod Depth", 0.0f);
    setByName(p, "Velocity to Fold", 0.0f);
}

} // namespace

VSM_TEST(westcoast_registered) {
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.westcoast"));
}

VSM_TEST(westcoast_silent_with_no_events) {
    auto p = makeWc();
    std::vector<float> l(512, 0.0f), r(512, 0.0f);
    p->process(nullptr, 0, l.data(), r.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0, 1e-6);
    VSM_ASSERT_EQ(p->activeVoiceCount(), 0);
}

VSM_TEST(westcoast_note_produces_sound) {
    auto p = makeWc();
    tenue(*p);
    const auto out = play(p, 45, 24000);
    VSM_ASSERT(peakAbs(out) > 0.02f);
}

VSM_TEST(westcoast_folding_ADDS_harmonics_a_filter_could_not) {
    // LE TRAIT DISTINCTIF, ET LA RAISON D'ÊTRE DE LA MACHINE.
    //
    // À pliage nul, l'oscillateur est un SINUS : il n'y a rien d'autre que le
    // fondamental. Aucun filtre du monde ne peut en tirer une harmonique --
    // filtrer est linéaire, ça ne crée pas de fréquence. Le plieur, lui, en
    // fabrique, et c'est toute l'école de la côte ouest.
    //
    // On vérifie donc les deux états : pauvre à zéro, riche à fond, mêmes
    // réglages par ailleurs.
    const int note = 45;
    const double f0 = midiToHz(note);
    const size_t depart = 6000, longueur = 32768;

    auto pur = makeWc();
    tenue(*pur);
    setByName(*pur, "Fold", 0.0f);
    const auto a = play(pur, static_cast<uint8_t>(note), 65536);
    const double a1 = magnitudeAt(a, depart, longueur, f0);
    const double a3 = magnitudeAt(a, depart, longueur, 3.0 * f0);
    const double a5 = magnitudeAt(a, depart, longueur, 5.0 * f0);
    VSM_ASSERT(a1 > 1e-4);              // ça sonne
    VSM_ASSERT(a3 < a1 * 0.01);         // et c'est un sinus : rien au rang 3
    VSM_ASSERT(a5 < a1 * 0.01);

    auto plie = makeWc();
    tenue(*plie);
    setByName(*plie, "Fold", 1.0f);
    const auto b = play(plie, static_cast<uint8_t>(note), 65536);
    const double b1 = magnitudeAt(b, depart, longueur, f0);
    const double b3 = magnitudeAt(b, depart, longueur, 3.0 * f0);
    const double b5 = magnitudeAt(b, depart, longueur, 5.0 * f0);
    VSM_ASSERT(b1 > 1e-5);
    VSM_ASSERT(b3 > b1 * 0.10);         // le rang 3 EXISTE maintenant
    VSM_ASSERT(b5 > b1 * 0.02);
    // Et il n'était pas là avant : c'est la comparaison qui fait le test.
    VSM_ASSERT(b3 / b1 > (a3 / a1) * 10.0);
}

VSM_TEST(westcoast_symmetry_brings_out_even_harmonics) {
    // Un plieur SYMÉTRIQUE ne peut donner que des rangs impairs : sa fonction
    // de transfert est impaire, et une fonction impaire d'un sinus n'a pas de
    // rangs pairs. Décaler le signal avant le pliage brise cette symétrie, et
    // c'est le seul rôle de ce réglage. Un réglage qui ne ferait rien serait
    // pire qu'absent (§ 33 d'ARCHITECTURE.md).
    const int note = 45;
    const double f0 = midiToHz(note);
    const size_t depart = 6000, longueur = 32768;

    auto sym = makeWc();
    tenue(*sym);
    setByName(*sym, "Fold", 0.8f);
    setByName(*sym, "Fold Symmetry", 0.5f);
    const auto a = play(sym, static_cast<uint8_t>(note), 65536);

    auto asym = makeWc();
    tenue(*asym);
    setByName(*asym, "Fold", 0.8f);
    setByName(*asym, "Fold Symmetry", 1.0f);
    const auto b = play(asym, static_cast<uint8_t>(note), 65536);

    const double symPair = magnitudeAt(a, depart, longueur, 2.0 * f0)
                         / std::max(magnitudeAt(a, depart, longueur, f0), 1e-12);
    const double asymPair = magnitudeAt(b, depart, longueur, 2.0 * f0)
                          / std::max(magnitudeAt(b, depart, longueur, f0), 1e-12);
    VSM_ASSERT(symPair < 0.05);          // symétrique : pas de rang 2
    VSM_ASSERT(asymPair > symPair * 5.0); // décalé : il apparaît
}

VSM_TEST(westcoast_gate_darkens_as_it_closes) {
    // LE SECOND TRAIT : la porte passe-bas baisse le volume ET la brillance
    // ensemble. Une note doit donc s'éteindre en devenant SOURDE, ce qu'un
    // simple ampli ne ferait pas. On mesure le centroïde tôt puis tard dans
    // l'extinction.
    auto p = makeWc();
    setByName(*p, "Fold", 0.9f);           // de quoi avoir des harmoniques à perdre
    setByName(*p, "Amp Attack", 0.002f);
    setByName(*p, "Amp Decay", 0.35f);
    setByName(*p, "Amp Sustain", 0.0f);
    setByName(*p, "Gate Lag", 0.05f);
    setByName(*p, "Gate Cutoff", 16000.0f);
    setByName(*p, "Analog Character", 0.0f);
    setByName(*p, "Velocity to Fold", 0.0f);

    const int note = 45;
    const double f0 = midiToHz(note);
    const auto out = play(p, static_cast<uint8_t>(note), 48000);

    const double tot = centroid(out, 2000, 8192, f0);
    const double tard = centroid(out, 14000, 8192, f0);
    VSM_ASSERT(tot > 0.0);
    VSM_ASSERT(tard > 0.0);
    VSM_ASSERT(tard < tot * 0.85);   // le son s'assombrit en s'éteignant
}

VSM_TEST(westcoast_is_polyphonic) {
    auto p = makeWc();
    tenue(*p);
    std::vector<MidiNoteEvent> ev{noteOn(0, 48), noteOn(0, 52), noteOn(0, 55)};
    std::vector<float> l(24000, 0.0f), r(24000, 0.0f);
    p->process(ev.data(), static_cast<int>(ev.size()), l.data(), r.data(), 24000);
    VSM_ASSERT_EQ(p->activeVoiceCount(), 3);
}

VSM_TEST(westcoast_a_full_chord_does_not_clip) {
    auto p = makeWc();
    tenue(*p);
    setByName(*p, "Fold", 1.0f);
    std::vector<MidiNoteEvent> ev;
    for (int k = 0; k < 8; ++k) ev.push_back(noteOn(0, static_cast<uint8_t>(48 + 2 * k), 110));
    std::vector<float> l(48000, 0.0f), r(48000, 0.0f);
    p->process(ev.data(), static_cast<int>(ev.size()), l.data(), r.data(), 48000);
    VSM_ASSERT(peakAbs(l) > 0.05f);
    VSM_ASSERT(peakAbs(l) < 1.0f);
}

VSM_TEST(westcoast_stays_finite_under_extreme_settings) {
    auto p = makeWc();
    for (const auto& info : p->parameterList()) p->setParameter(info.id, info.maxValue);
    const auto a = play(p, 36, 24000, 127);
    for (float s : a) VSM_ASSERT(std::isfinite(s));
    auto q = makeWc();
    for (const auto& info : q->parameterList()) q->setParameter(info.id, info.minValue);
    const auto b = play(q, 96, 24000, 127);
    for (float s : b) VSM_ASSERT(std::isfinite(s));
}

VSM_TEST(westcoast_is_deterministic) {
    auto a = makeWc();
    auto b = makeWc();
    const auto x = play(a, 45, 24000);
    const auto y = play(b, 45, 24000);
    for (size_t i = 0; i < x.size(); ++i) VSM_ASSERT_EQ(x[i], y[i]);
}

VSM_TEST(westcoast_save_load_roundtrip) {
    auto p = makeWc();
    setByName(*p, "Fold", 0.71f);
    setByName(*p, "Gate Lag", 0.42f);
    const auto state = p->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.westcoast"));
    auto autre = makeWc();
    autre->loadState(state);
    VSM_ASSERT_NEAR(autre->getParameter(paramByName(*autre, "Fold")), 0.71f, 1e-6);
    VSM_ASSERT_NEAR(autre->getParameter(paramByName(*autre, "Gate Lag")), 0.42f, 1e-6);
}

VSM_TEST(westcoast_parameter_list_is_complete) {
    auto p = makeWc();
    VSM_ASSERT_EQ(static_cast<int>(p->parameterList().size()), 13);
}
