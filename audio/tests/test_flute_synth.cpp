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

/// Fréquence fondamentale par AUTOCORRÉLATION, précise au cent près -- une
/// transformée de Fourier ne l'est pas assez pour juger d'une justesse.
double frequenceFondamentale(const std::vector<float>& y, size_t depuis) {
    const size_t n = std::min<size_t>(y.size() - depuis, static_cast<size_t>(0.8 * kSampleRate));
    if (n < 4096) return 0.0;
    std::vector<double> x(n);
    double moy = 0.0;
    for (size_t i = 0; i < n; ++i) { x[i] = y[depuis + i]; moy += x[i]; }
    moy /= static_cast<double>(n);
    for (auto& v : x) v -= moy;

    const size_t lo = static_cast<size_t>(kSampleRate / 1400.0);
    const size_t hi = std::min(n / 2, static_cast<size_t>(kSampleRate / 60.0));
    double record = -1e30; size_t meilleur = lo;
    std::vector<double> ac(hi + 2, 0.0);
    for (size_t k = lo; k <= hi + 1; ++k) {
        double s = 0.0;
        for (size_t i = 0; i + k < n; ++i) s += x[i] * x[i + k];
        ac[k] = s;
        if (k <= hi && s > record) { record = s; meilleur = k; }
    }
    double k = static_cast<double>(meilleur);
    if (meilleur > lo && meilleur < hi) {
        const double a = ac[meilleur - 1], b = ac[meilleur], c = ac[meilleur + 1];
        k += 0.5 * (a - c) / (a - 2.0 * b + c + 1e-12);
    }
    return kSampleRate / k;
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
    const int note = 60;
    const double f0 = midiToHz(note);
    const auto out = play(p, static_cast<uint8_t>(note), 72000);

    // Elle sonne franchement.
    VSM_ASSERT(peakAbs(out) > 0.05f);
    // Elle tient : autant d'énergie à la fin qu'au milieu.
    double milieu = 0.0, fin = 0.0;
    for (size_t i = 24000; i < 36000; ++i) milieu += static_cast<double>(out[i]) * out[i];
    for (size_t i = 54000; i < 66000; ++i) fin += static_cast<double>(out[i]) * out[i];
    VSM_ASSERT(fin > milieu * 0.5);

    // ET CE N'EST PAS UN SINUS. Les rangs se mesurent sur la fréquence
    // RÉELLEMENT jouée, pas sur la fréquence tempérée : la machine sonne
    // quelques cents en dessous, et sur une fenêtre longue ce décalage suffit à
    // annuler une corrélation calculée au mauvais endroit. Mesuré en cherchant
    // au bon endroit, les rangs sont là ; au mauvais, tout paraissait nul --
    // c'est un piège de MESURE, pas un défaut de machine.
    const double joue = frequenceFondamentale(out, 24000);
    VSM_ASSERT(joue > 0.5 * f0 && joue < 2.0 * f0);
    const double h1 = magnitudeAt(out, 24000, 8192, joue);
    const double h3 = magnitudeAt(out, 24000, 8192, 3.0 * joue);
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
    const int note = 60;
    const double f0 = midiToHz(note);

    auto flute = makeFlute();
    tenue(*flute);
    const auto a = play(flute, static_cast<uint8_t>(note), 72000);

    auto wind = makeWind();
    setByName(*wind, "Vibrato Depth", 0.0f);
    setByName(*wind, "Analog Character", 0.0f);
    const auto b = play(wind, static_cast<uint8_t>(note), 72000);

    // Chaque machine est mesurée sur SA fréquence jouée : la flûte sonne
    // quelques cents sous le tempérament, le cylindre à anche non, et comparer
    // les deux au même endroit théorique fausserait le verdict.
    const size_t depart = 24000, longueur = 8192;
    const double fFlute = frequenceFondamentale(a, depart);
    const double fWind = frequenceFondamentale(b, depart);
    VSM_ASSERT(fFlute > 0.5 * f0 && fFlute < 2.0 * f0);
    VSM_ASSERT(fWind > 0.5 * f0 && fWind < 2.0 * f0);
    const double fluteH1 = magnitudeAt(a, depart, longueur, fFlute);
    const double fluteH2 = magnitudeAt(a, depart, longueur, 2.0 * fFlute);
    const double windH1 = magnitudeAt(b, depart, longueur, fWind);
    const double windH2 = magnitudeAt(b, depart, longueur, 2.0 * fWind);

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
    const auto out = play(p, 60, 48000);
    VSM_ASSERT(peakAbs(out) < 0.01f);
}

VSM_TEST(flute_plays_the_note_it_is_given) {
    // LA QUESTION QUI DÉCIDE SI LA MACHINE EST LIVRABLE, et elle a d'abord
    // reçu la mauvaise réponse : une première version partait +41 demi-tons
    // au-dessus de la note demandée dans le grave. Une boucle à retard résonne
    // sur TOUS les multiples de sa fondamentale, et sans rien pour trancher,
    // elle choisit son mode toute seule.
    //
    // Ce test mesure la justesse au CENT près, par autocorrélation -- une
    // transformée n'y suffirait pas --, sur toute l'étendue déclarée.
    double pire = 0.0;
    for (int note : {52, 60, 69, 79, 84}) {
        auto p = makeFlute();
        tenue(*p);
        const auto out = play(p, static_cast<uint8_t>(note), 96000);
        const double f = frequenceFondamentale(out, static_cast<size_t>(0.6 * kSampleRate));
        VSM_ASSERT(f > 0.0);
        const double cents = 1200.0 * std::log2(f / midiToHz(note));
        pire = std::max(pire, std::abs(cents));
    }
    // Vingt-cinq cents, c'est un quart de demi-ton : au-delà, un instrument
    // mélodique sonne faux à l'oreille. Mesuré aux réglages d'usine : moyenne
    // -4,6 cents, dispersion 3,7, pire cas 12,2.
    VSM_ASSERT(pire < 25.0);
}

VSM_TEST(flute_does_not_speak_below_its_bore) {
    // UNE FLÛTE A UNE PERCE, DONC UNE NOTE LA PLUS GRAVE. Sous mi3 la boucle ne
    // s'installe plus sur son premier mode -- mesuré, note 51 : +3 134 cents.
    // Une vraie flûte à qui l'on demande une note hors de sa perce ne crie pas
    // trois octaves plus haut : elle NE PARLE PAS. C'est ce que fait la
    // machine, et le compteur de voix le dit.
    auto p = makeFlute();
    tenue(*p);
    const auto out = play(p, 48, 48000);
    VSM_ASSERT_NEAR(peakAbs(out), 0.0, 1e-6);
    VSM_ASSERT_EQ(p->activeVoiceCount(), 0);

    // Et juste au-dessus de la limite, elle parle.
    auto q = makeFlute();
    tenue(*q);
    const auto ok = play(q, 52, 48000);
    VSM_ASSERT(peakAbs(ok) > 0.02f);
}

VSM_TEST(flute_is_polyphonic_as_a_section) {
    auto p = makeFlute();
    tenue(*p);
    std::vector<MidiNoteEvent> ev{noteOn(0, 60), noteOn(0, 64), noteOn(0, 67)};
    std::vector<float> l(48000, 0.0f), r(48000, 0.0f);
    p->process(ev.data(), static_cast<int>(ev.size()), l.data(), r.data(), 48000);
    VSM_ASSERT_EQ(p->activeVoiceCount(), 3);
    VSM_ASSERT(peakAbs(l) > 0.05f);
    VSM_ASSERT(peakAbs(l) < 1.0f);
}

VSM_TEST(flute_stays_finite_under_extreme_settings) {
    auto p = makeFlute();
    for (const auto& info : p->parameterList()) p->setParameter(info.id, info.maxValue);
    const auto a = play(p, 55, 48000, 127);
    for (float s : a) VSM_ASSERT(std::isfinite(s));
    auto q = makeFlute();
    for (const auto& info : q->parameterList()) q->setParameter(info.id, info.minValue);
    const auto b = play(q, 96, 48000, 127);
    for (float s : b) VSM_ASSERT(std::isfinite(s));
}

VSM_TEST(flute_is_deterministic) {
    auto a = makeFlute();
    auto b = makeFlute();
    const auto x = play(a, 60, 48000);
    const auto y = play(b, 60, 48000);
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
