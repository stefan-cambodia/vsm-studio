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

SynthPluginPtr makeFm(double sr = kSampleRate) {
    auto p = PluginRegistry::instance().create("vsm.fmdrums");
    p->initialize(sr, 512);
    return p;
}

float peakAbs(const std::vector<float>& b) {
    float p = 0.0f; for (float s : b) p = std::max(p, std::abs(s)); return p;
}

double energy(const std::vector<float>& b, size_t from = 0) {
    double e = 0.0;
    for (size_t i = from; i < b.size(); ++i) e += static_cast<double>(b[i]) * b[i];
    return e;
}

MidiNoteEvent hit(int offset, uint8_t note, uint8_t vel = 110) {
    return MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, offset, 9, note, vel};
}

ParamId paramByName(const ISynthPlugin& p, const std::string& n) {
    for (const auto& info : p.parameterList()) if (info.name == n) return info.id;
    return 0;
}

void setByName(ISynthPlugin& p, const std::string& n, float v) {
    p.setParameter(paramByName(p, n), v);
}

std::vector<float> strike(SynthPluginPtr& p, uint8_t note, int numSamples, uint8_t vel = 110) {
    std::vector<float> l(static_cast<size_t>(numSamples), 0.0f);
    std::vector<float> r(static_cast<size_t>(numSamples), 0.0f);
    const MidiNoteEvent ev = hit(0, note, vel);
    p->process(&ev, 1, l.data(), r.data(), numSamples);
    return l;
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

/// Part de l'énergie qui tombe SUR la série harmonique du fondamental, contre
/// l'énergie totale relevée aux mêmes points plus les points intercalaires.
/// Un spectre harmonique donne un rapport proche de 1 ; un spectre inharmonique
/// répand son énergie ailleurs et le fait chuter.
double harmonicity(const std::vector<float>& x, size_t from, size_t count, double f0) {
    double surLaSerie = 0.0, entreLesRangs = 0.0;
    for (int k = 1; k <= 12; ++k) {
        surLaSerie += magnitudeAt(x, from, count, k * f0);
        // Points intercalaires : là où une série harmonique n'a RIEN.
        entreLesRangs += magnitudeAt(x, from, count, (k + 0.5) * f0);
    }
    return surLaSerie / std::max(surLaSerie + entreLesRangs, 1e-12);
}

} // namespace

VSM_TEST(fmdrums_registered) {
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.fmdrums"));
}

VSM_TEST(fmdrums_silent_with_no_events) {
    auto p = makeFm();
    std::vector<float> l(512, 0.0f), r(512, 0.0f);
    p->process(nullptr, 0, l.data(), r.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0, 1e-6);
    VSM_ASSERT_EQ(p->activeVoiceCount(), 0);
}

VSM_TEST(fmdrums_every_piece_sounds) {
    const uint8_t notes[] = {36, 38, 39, 42, 45, 46, 49};
    for (uint8_t note : notes) {
        auto p = makeFm();
        const auto out = strike(p, note, 24000);
        VSM_ASSERT(peakAbs(out) > 0.01f);
    }
}

VSM_TEST(fmdrums_ignores_notes_it_does_not_declare) {
    auto p = makeFm();
    const auto out = strike(p, 60, 12000);   // 60 : aucune pièce ici
    VSM_ASSERT_NEAR(peakAbs(out), 0.0, 1e-6);
}

VSM_TEST(fmdrums_a_non_integer_ratio_leaves_the_harmonic_series) {
    // LE TRAIT DISTINCTIF, ET LA RAISON D'ÊTRE DE LA MACHINE.
    //
    // En FM, les composantes tombent à |porteuse ± n·modulante|. À rapport
    // ENTIER, toutes ces fréquences sont des multiples du fondamental : le
    // spectre est harmonique, comme celui d'une boîte analogique. À rapport NON
    // ENTIER, elles n'y sont plus -- et c'est ce qui donne le « clang ».
    //
    // On mesure donc la part d'énergie qui tombe SUR la série harmonique, aux
    // deux réglages. Le test compare : c'est le rapport qui décide.
    auto entier = makeFm();
    setByName(*entier, "Tom Ratio", 2.0f);
    setByName(*entier, "Tom Clang", 6.0f);
    setByName(*entier, "Tom Tune", 150.0f);
    setByName(*entier, "Tom Decay", 1.0f);
    const auto a = strike(entier, 45, 48000);

    auto irrationnel = makeFm();
    setByName(*irrationnel, "Tom Ratio", 1.414f);
    setByName(*irrationnel, "Tom Clang", 6.0f);
    setByName(*irrationnel, "Tom Tune", 150.0f);
    setByName(*irrationnel, "Tom Decay", 1.0f);
    const auto b = strike(irrationnel, 45, 48000);

    // Après le balayage d'attaque, sur la partie tenue.
    const size_t depart = 6000, longueur = 16384;
    const double hEntier = harmonicity(a, depart, longueur, 150.0);
    const double hIrrationnel = harmonicity(b, depart, longueur, 150.0);

    VSM_ASSERT(hEntier > 0.75);                    // rapport entier : harmonique
    VSM_ASSERT(hIrrationnel < hEntier * 0.85);     // rapport irrationnel : ailleurs
}

VSM_TEST(fmdrums_the_clang_fades_before_the_note_does) {
    // SECOND TRAIT, et il distingue la FM d'un simple oscillateur inharmonique
    // (la voie modale de `vsm.perc`, par exemple) : l'indice de modulation est
    // SOUS ENVELOPPE, et il descend plus vite que l'amplitude. La frappe est
    // donc métallique à l'attaque et se referme sur son fondamental. Le spectre
    // CHANGE pendant la note, ce qu'un banc de modes fixes ne fait pas.
    auto p = makeFm();
    setByName(*p, "Bell Tune", 400.0f);
    setByName(*p, "Bell Ratio", 1.414f);
    setByName(*p, "Bell Clang", 9.0f);
    setByName(*p, "Bell Decay", 2.0f);
    const auto out = strike(p, 49, 96000);

    // Part de l'énergie hors série harmonique, tôt puis tard.
    const double tot = 1.0 - harmonicity(out, 2000, 16384, 400.0);
    const double tard = 1.0 - harmonicity(out, 50000, 16384, 400.0);
    VSM_ASSERT(tot > 0.05);            // il y a bien du métal au départ
    VSM_ASSERT(tard < tot * 0.8);      // et il s'en va avant la note
}

VSM_TEST(fmdrums_closed_hat_chokes_open_hat) {
    auto p = makeFm();
    std::vector<MidiNoteEvent> ev{hit(0, 46), hit(4800, 42)};
    std::vector<float> l(48000, 0.0f), r(48000, 0.0f);
    p->process(ev.data(), static_cast<int>(ev.size()), l.data(), r.data(), 48000);
    // La charleston ouverte devait durer 0,45 s ; étouffée à 0,1 s, il ne doit
    // presque rien rester après 0,3 s.
    VSM_ASSERT(energy(l, 14400) < energy(l, 0) * 0.02);
}

VSM_TEST(fmdrums_velocity_changes_level) {
    auto fort = makeFm();
    auto doux = makeFm();
    const auto a = strike(fort, 36, 24000, 127);
    const auto b = strike(doux, 36, 24000, 40);
    VSM_ASSERT(peakAbs(a) > peakAbs(b) * 1.4f);
}

VSM_TEST(fmdrums_a_full_kit_does_not_clip) {
    auto p = makeFm();
    const uint8_t notes[] = {36, 38, 39, 42, 45, 46, 49};
    std::vector<MidiNoteEvent> ev;
    for (uint8_t n : notes) ev.push_back(hit(0, n, 127));
    std::vector<float> l(48000, 0.0f), r(48000, 0.0f);
    p->process(ev.data(), static_cast<int>(ev.size()), l.data(), r.data(), 48000);
    VSM_ASSERT(peakAbs(l) > 0.05f);
    VSM_ASSERT(peakAbs(l) < 1.0f);
}

VSM_TEST(fmdrums_stays_finite_under_extreme_settings) {
    auto p = makeFm();
    for (const auto& info : p->parameterList()) p->setParameter(info.id, info.maxValue);
    const auto a = strike(p, 36, 24000, 127);
    for (float s : a) VSM_ASSERT(std::isfinite(s));
    auto q = makeFm();
    for (const auto& info : q->parameterList()) q->setParameter(info.id, info.minValue);
    const auto b = strike(q, 36, 24000, 127);
    for (float s : b) VSM_ASSERT(std::isfinite(s));
}

VSM_TEST(fmdrums_is_deterministic) {
    auto a = makeFm();
    auto b = makeFm();
    const auto x = strike(a, 42, 24000);   // la pièce bruitée
    const auto y = strike(b, 42, 24000);
    for (size_t i = 0; i < x.size(); ++i) VSM_ASSERT_EQ(x[i], y[i]);
}

VSM_TEST(fmdrums_save_load_roundtrip) {
    auto p = makeFm();
    setByName(*p, "Kick Ratio", 1.732f);
    setByName(*p, "Bell Clang", 8.25f);
    const auto state = p->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.fmdrums"));
    auto autre = makeFm();
    autre->loadState(state);
    VSM_ASSERT_NEAR(autre->getParameter(paramByName(*autre, "Kick Ratio")), 1.732f, 1e-6);
    VSM_ASSERT_NEAR(autre->getParameter(paramByName(*autre, "Bell Clang")), 8.25f, 1e-6);
}

VSM_TEST(fmdrums_parameter_list_is_complete) {
    auto p = makeFm();
    VSM_ASSERT_EQ(static_cast<int>(p->parameterList().size()), 27);
}
