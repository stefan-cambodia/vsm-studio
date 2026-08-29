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

SynthPluginPtr makePd(double sr = kSampleRate) {
    auto p = PluginRegistry::instance().create("vsm.phasedist");
    p->initialize(sr, 512);
    return p;
}

float peakAbs(const std::vector<float>& b) {
    float p = 0.0f; for (float s : b) p = std::max(p, std::abs(s)); return p;
}

double rmsOf(const std::vector<float>& b, size_t from, size_t count) {
    const size_t fin = std::min(b.size(), from + count);
    double acc = 0.0;
    for (size_t i = from; i < fin; ++i) acc += static_cast<double>(b[i]) * b[i];
    return std::sqrt(acc / std::max<size_t>(fin - from, 1));
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

/// Centroïde spectral calculé sur les seize premiers rangs : « où se trouve
/// l'énergie », en rangs harmoniques.
double centroidRang(const std::vector<float>& x, size_t from, size_t count, double f0) {
    double num = 0.0, den = 0.0;
    for (int k = 1; k <= 16; ++k) {
        const double m = magnitudeAt(x, from, count, k * f0);
        num += m * k;
        den += m;
    }
    return den > 1e-12 ? num / den : 0.0;
}

/// Réglages qui tiennent la note à timbre FIGÉ : les deux enveloppes ouvertes,
/// aucune modulation, aucune dérive. Sans quoi on mesurerait un geste et non un
/// spectre.
void tenue(ISynthPlugin& p) {
    setByName(p, "Amp Attack", 0.002f);
    setByName(p, "Amp Decay", 0.01f);
    setByName(p, "Amp Sustain", 1.0f);
    setByName(p, "Env to Distortion", 0.0f);
    setByName(p, "Velocity to Distortion", 0.0f);
    setByName(p, "Analog Character", 0.0f);
    setByName(p, "Resonance", 0.0f);
}

} // namespace

VSM_TEST(phasedist_registered) {
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.phasedist"));
}

VSM_TEST(phasedist_silent_with_no_events) {
    auto p = makePd();
    std::vector<float> l(512, 0.0f), r(512, 0.0f);
    p->process(nullptr, 0, l.data(), r.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0, 1e-6);
    VSM_ASSERT_EQ(p->activeVoiceCount(), 0);
}

VSM_TEST(phasedist_at_rest_it_is_a_pure_sine) {
    // À distorsion nulle, la fonction de relecture est l'IDENTITÉ : il ne doit
    // rien se passer du tout. C'est la moitié de la démonstration -- sans elle,
    // on ne saurait pas que ce qui apparaît ensuite vient bien de la
    // déformation.
    auto p = makePd();
    tenue(*p);
    setByName(*p, "Distortion", 0.0f);
    const int note = 45;
    const double f0 = midiToHz(note);
    const auto out = play(p, static_cast<uint8_t>(note), 65536);
    const double h1 = magnitudeAt(out, 6000, 32768, f0);
    VSM_ASSERT(h1 > 1e-4);
    VSM_ASSERT(magnitudeAt(out, 6000, 32768, 2.0 * f0) < h1 * 0.02);
    VSM_ASSERT(magnitudeAt(out, 6000, 32768, 3.0 * f0) < h1 * 0.02);
}

VSM_TEST(phasedist_opens_the_timbre_at_constant_level) {
    // LE TRAIT DISTINCTIF, ET C'EST LUI QUI SÉPARE CETTE MACHINE DU PLIEUR.
    //
    // Déformer la PHASE ne touche pas à l'amplitude : la lecture parcourt
    // toujours toute la table de sinus, donc la valeur efficace ne bouge
    // presque pas. Le plieur de `vsm.westcoast`, lui, gagne ses harmoniques en
    // poussant plus fort dans les replis -- son niveau bouge nécessairement.
    //
    // On mesure donc les DEUX ensemble : le spectre doit s'ouvrir franchement
    // pendant que le niveau reste où il est.
    const int note = 45;
    const double f0 = midiToHz(note);

    auto ferme = makePd();
    tenue(*ferme);
    setByName(*ferme, "Distortion", 0.0f);
    const auto a = play(ferme, static_cast<uint8_t>(note), 65536);

    auto ouvert = makePd();
    tenue(*ouvert);
    setByName(*ouvert, "Distortion", 1.0f);
    const auto b = play(ouvert, static_cast<uint8_t>(note), 65536);

    const double centreFerme = centroidRang(a, 6000, 32768, f0);
    const double centreOuvert = centroidRang(b, 6000, 32768, f0);
    VSM_ASSERT(centreFerme > 0.9 && centreFerme < 1.3);   // un sinus : rang 1
    VSM_ASSERT(centreOuvert > centreFerme * 3.0);          // franchement ouvert

    const double niveauFerme = rmsOf(a, 6000, 32768);
    const double niveauOuvert = rmsOf(b, 6000, 32768);
    VSM_ASSERT(niveauOuvert > niveauFerme * 0.85);
    VSM_ASSERT(niveauOuvert < niveauFerme * 1.15);
}

VSM_TEST(phasedist_resonance_locks_to_an_integer_harmonic) {
    // SECOND TRAIT. La forme « résonante » est un sinus rapide fenêtré par une
    // dent de scie à la fondamentale : son pic ne peut se placer QU'À un rang
    // ENTIER, et il saute d'un rang à l'autre au lieu de glisser. C'est ce qui
    // la distingue d'une résonance de filtre -- et c'est l'exact complément de
    // `vsm.vocal`, dont les formants restent à des fréquences absolues.
    const int note = 45;
    const double f0 = midiToHz(note);

    auto mesurer = [&](float rang) {
        auto p = makePd();
        tenue(*p);
        setByName(*p, "Distortion", 0.0f);
        setByName(*p, "Resonance", 1.0f);
        setByName(*p, "Resonance Harmonic", rang);
        const auto out = play(p, static_cast<uint8_t>(note), 65536);
        // Le rang le plus fort parmi les seize premiers.
        int meilleur = 1; double record = -1.0;
        for (int k = 1; k <= 16; ++k) {
            const double m = magnitudeAt(out, 6000, 32768, k * f0);
            if (m > record) { record = m; meilleur = k; }
        }
        return meilleur;
    };

    VSM_ASSERT_EQ(mesurer(3.0f), 3);
    VSM_ASSERT_EQ(mesurer(7.0f), 7);
}

VSM_TEST(phasedist_envelope_opens_the_timbre_over_time) {
    // Ces machines imitaient le geste d'un filtre qui s'ouvre : c'est la
    // seconde enveloppe, et son effet doit se voir dans le TEMPS.
    auto p = makePd();
    setByName(*p, "Distortion", 0.0f);
    setByName(*p, "Env to Distortion", 1.0f);
    setByName(*p, "Mod Attack", 0.002f);
    setByName(*p, "Mod Decay", 0.30f);
    setByName(*p, "Mod Sustain", 0.0f);
    setByName(*p, "Amp Attack", 0.002f);
    setByName(*p, "Amp Decay", 0.01f);
    setByName(*p, "Amp Sustain", 1.0f);
    setByName(*p, "Velocity to Distortion", 0.0f);
    setByName(*p, "Analog Character", 0.0f);
    setByName(*p, "Resonance", 0.0f);

    const int note = 45;
    const double f0 = midiToHz(note);
    const auto out = play(p, static_cast<uint8_t>(note), 96000);
    const double tot = centroidRang(out, 2000, 8192, f0);
    const double tard = centroidRang(out, 60000, 8192, f0);
    VSM_ASSERT(tot > tard * 1.5);   // riche au départ, refermé ensuite
}

VSM_TEST(phasedist_is_polyphonic) {
    auto p = makePd();
    tenue(*p);
    std::vector<MidiNoteEvent> ev{noteOn(0, 48), noteOn(0, 52), noteOn(0, 55)};
    std::vector<float> l(24000, 0.0f), r(24000, 0.0f);
    p->process(ev.data(), static_cast<int>(ev.size()), l.data(), r.data(), 24000);
    VSM_ASSERT_EQ(p->activeVoiceCount(), 3);
}

VSM_TEST(phasedist_a_full_chord_does_not_clip) {
    auto p = makePd();
    tenue(*p);
    setByName(*p, "Distortion", 1.0f);
    std::vector<MidiNoteEvent> ev;
    for (int k = 0; k < 8; ++k) ev.push_back(noteOn(0, static_cast<uint8_t>(48 + 2 * k), 110));
    std::vector<float> l(48000, 0.0f), r(48000, 0.0f);
    p->process(ev.data(), static_cast<int>(ev.size()), l.data(), r.data(), 48000);
    VSM_ASSERT(peakAbs(l) > 0.05f);
    VSM_ASSERT(peakAbs(l) < 1.0f);
}

VSM_TEST(phasedist_stays_finite_under_extreme_settings) {
    auto p = makePd();
    for (const auto& info : p->parameterList()) p->setParameter(info.id, info.maxValue);
    const auto a = play(p, 36, 24000, 127);
    for (float s : a) VSM_ASSERT(std::isfinite(s));
    auto q = makePd();
    for (const auto& info : q->parameterList()) q->setParameter(info.id, info.minValue);
    const auto b = play(q, 96, 24000, 127);
    for (float s : b) VSM_ASSERT(std::isfinite(s));
}

VSM_TEST(phasedist_is_deterministic) {
    auto a = makePd();
    auto b = makePd();
    const auto x = play(a, 45, 24000);
    const auto y = play(b, 45, 24000);
    for (size_t i = 0; i < x.size(); ++i) VSM_ASSERT_EQ(x[i], y[i]);
}

VSM_TEST(phasedist_save_load_roundtrip) {
    auto p = makePd();
    setByName(*p, "Distortion", 0.62f);
    setByName(*p, "Resonance Harmonic", 9.0f);
    const auto state = p->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.phasedist"));
    auto autre = makePd();
    autre->loadState(state);
    VSM_ASSERT_NEAR(autre->getParameter(paramByName(*autre, "Distortion")), 0.62f, 1e-6);
    VSM_ASSERT_NEAR(autre->getParameter(paramByName(*autre, "Resonance Harmonic")), 9.0f, 1e-6);
}

VSM_TEST(phasedist_parameter_list_is_complete) {
    auto p = makePd();
    VSM_ASSERT_EQ(static_cast<int>(p->parameterList().size()), 15);
}
