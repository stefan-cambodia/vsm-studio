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

SynthPluginPtr makeAdditive(double sr = kSampleRate) {
    auto p = PluginRegistry::instance().create("vsm.additive");
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

/// Amplitude à une fréquence précise, par corrélation fenêtrée.
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

} // namespace

VSM_TEST(additive_registered) {
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.additive"));
}

VSM_TEST(additive_silent_with_no_events) {
    auto p = makeAdditive();
    std::vector<float> l(512, 0.0f), r(512, 0.0f);
    p->process(nullptr, 0, l.data(), r.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0, 1e-6);
    VSM_ASSERT_EQ(p->activeVoiceCount(), 0);
}

VSM_TEST(additive_note_produces_sound) {
    auto p = makeAdditive();
    const auto out = play(p, 57, 24000);
    VSM_ASSERT(peakAbs(out) > 0.02f);
    VSM_ASSERT_EQ(p->activeVoiceCount(), 1);
}

VSM_TEST(additive_reaches_a_spectrum_no_filter_can_make) {
    // LE TRAIT DISTINCTIF, ET LA RAISON D'ÊTRE DE LA MACHINE.
    //
    // Un filtre est une fonction de transfert CONTINUE : il ne peut pas
    // éteindre le rang 2 en laissant intacts les rangs 1 et 3, quelle que soit
    // sa résonance -- toute atténuation à 2·f0 mord sur ses voisins. Un additif
    // le fait par construction, puisqu'il POSE chaque rang.
    //
    // On vérifie les deux moitiés de l'affirmation, sans quoi le test ne dirait
    // rien : les rangs impairs sont là, ET les pairs n'y sont pas.
    auto p = makeAdditive();
    setByName(*p, "Odd/Even Balance", 0.0f);   // impairs seuls
    setByName(*p, "Spectral Tilt", -3.0f);
    setByName(*p, "Decay Tilt", 0.0f);
    setByName(*p, "Amp Sustain", 1.0f);
    setByName(*p, "Analog Character", 0.0f);   // pas de dérive : on mesure des rangs
    setByName(*p, "Velocity to Tilt", 0.0f);

    const int note = 45;                        // ~110 Hz : les rangs tiennent
    const double f0 = midiToHz(note);
    const auto out = play(p, static_cast<uint8_t>(note), 65536);

    const size_t depart = 8000, longueur = 32768;
    const double h1 = magnitudeAt(out, depart, longueur, f0);
    const double h2 = magnitudeAt(out, depart, longueur, 2.0 * f0);
    const double h3 = magnitudeAt(out, depart, longueur, 3.0 * f0);
    const double h4 = magnitudeAt(out, depart, longueur, 4.0 * f0);
    const double h5 = magnitudeAt(out, depart, longueur, 5.0 * f0);

    VSM_ASSERT(h1 > 1e-4);              // ça sonne
    VSM_ASSERT(h3 > h1 * 0.10);         // les impairs sont là
    VSM_ASSERT(h5 > h1 * 0.02);
    VSM_ASSERT(h2 < h1 * 0.01);         // et les pairs n'y sont PAS
    VSM_ASSERT(h4 < h1 * 0.01);

    // Et l'inverse, pour que le réglage soit mesuré sur sa course entière :
    // poussé à l'autre bout, ce sont les pairs qui sonnent seuls.
    auto q = makeAdditive();
    setByName(*q, "Odd/Even Balance", 1.0f);
    setByName(*q, "Spectral Tilt", -3.0f);
    setByName(*q, "Decay Tilt", 0.0f);
    setByName(*q, "Amp Sustain", 1.0f);
    setByName(*q, "Analog Character", 0.0f);
    setByName(*q, "Velocity to Tilt", 0.0f);
    const auto out2 = play(q, static_cast<uint8_t>(note), 65536);
    const double g1 = magnitudeAt(out2, depart, longueur, f0);
    const double g2 = magnitudeAt(out2, depart, longueur, 2.0 * f0);
    VSM_ASSERT(g2 > 1e-4);
    VSM_ASSERT(g1 < g2 * 0.05);
}

VSM_TEST(additive_stretches_its_partials) {
    // SECOND TRAIT DISTINCTIF. Une corde raide a ses partiels à
    // n·f0·sqrt(1 + B·n²), et non aux multiples entiers : c'est ce qui fait
    // qu'un piano s'accorde faux exprès. Aucune autre machine du parc ne sait
    // étirer un spectre ainsi.
    //
    // Le rang 8 est celui où l'écart devient franc ; on vérifie qu'il se
    // DÉPLACE, c'est-à-dire qu'il quitte 8·f0 pour aller là où la physique le
    // met.
    const int note = 45;
    const double f0 = midiToHz(note);

    auto droit = makeAdditive();
    setByName(*droit, "Inharmonicity", 0.0f);
    setByName(*droit, "Decay Tilt", 0.0f);
    setByName(*droit, "Amp Sustain", 1.0f);
    setByName(*droit, "Analog Character", 0.0f);
    setByName(*droit, "Spectral Tilt", -3.0f);
    const auto a = play(droit, static_cast<uint8_t>(note), 65536);

    auto etire = makeAdditive();
    setByName(*etire, "Inharmonicity", 1.0f);
    setByName(*etire, "Decay Tilt", 0.0f);
    setByName(*etire, "Amp Sustain", 1.0f);
    setByName(*etire, "Analog Character", 0.0f);
    setByName(*etire, "Spectral Tilt", -3.0f);
    const auto b = play(etire, static_cast<uint8_t>(note), 65536);

    const size_t depart = 8000, longueur = 32768;
    // B = 0,0008 au maximum : le rang 8 part à 8·f0·sqrt(1 + 0,0008·64),
    // soit +2,5 %.
    const double attendu = 8.0 * f0 * std::sqrt(1.0 + 0.0008 * 64.0);
    const double droitEn8 = magnitudeAt(a, depart, longueur, 8.0 * f0);
    const double etireEn8 = magnitudeAt(b, depart, longueur, 8.0 * f0);
    const double etireLaOuIlEst = magnitudeAt(b, depart, longueur, attendu);

    VSM_ASSERT(droitEn8 > 1e-5);                    // sans étirement, il est à 8·f0
    VSM_ASSERT(etireEn8 < droitEn8 * 0.5);          // étiré, il n'y est plus
    VSM_ASSERT(etireLaOuIlEst > etireEn8 * 2.0);    // il est là où la physique le met
}

VSM_TEST(additive_partial_count_limits_the_spectrum) {
    // Un réglage qui ne fait rien est pire qu'un réglage absent : celui-ci est
    // mesuré sur sa course. À quatre rangs, le rang 8 doit être ABSENT.
    const int note = 45;
    const double f0 = midiToHz(note);
    auto peu = makeAdditive();
    setByName(*peu, "Partials", 4.0f);
    setByName(*peu, "Decay Tilt", 0.0f);
    setByName(*peu, "Amp Sustain", 1.0f);
    setByName(*peu, "Analog Character", 0.0f);
    setByName(*peu, "Spectral Tilt", -3.0f);
    const auto a = play(peu, static_cast<uint8_t>(note), 65536);
    VSM_ASSERT(magnitudeAt(a, 8000, 32768, f0) > 1e-4);
    VSM_ASSERT(magnitudeAt(a, 8000, 32768, 8.0 * f0) < 1e-5);
}

VSM_TEST(additive_never_aliases_a_high_note) {
    // AU-DESSUS DE NYQUIST, ON ÉTEINT PLUTÔT QUE DE REPLIER. Sur une note
    // aiguë avec trente-deux rangs demandés, la plupart sortent de la bande :
    // s'ils étaient repliés, ils reviendraient sous le fondamental, où il ne
    // doit y avoir RIEN.
    auto p = makeAdditive();
    setByName(*p, "Partials", 32.0f);
    setByName(*p, "Spectral Tilt", 0.0f);      // tous les rangs à plein niveau
    setByName(*p, "Decay Tilt", 0.0f);
    setByName(*p, "Amp Sustain", 1.0f);
    setByName(*p, "Analog Character", 0.0f);
    const int note = 96;                        // ~2093 Hz : le rang 12 dépasse déjà
    const double f0 = midiToHz(note);
    const auto out = play(p, static_cast<uint8_t>(note), 65536);
    const double fond = magnitudeAt(out, 8000, 32768, f0);
    VSM_ASSERT(fond > 1e-4);
    // Sous le fondamental, il ne doit rien y avoir : un repliement s'y verrait.
    for (double f : {f0 * 0.25, f0 * 0.5, f0 * 0.75})
        VSM_ASSERT(magnitudeAt(out, 8000, 32768, f) < fond * 0.02);
}

VSM_TEST(additive_is_polyphonic) {
    auto p = makeAdditive();
    std::vector<MidiNoteEvent> ev{noteOn(0, 48), noteOn(0, 52), noteOn(0, 55)};
    std::vector<float> l(24000, 0.0f), r(24000, 0.0f);
    p->process(ev.data(), static_cast<int>(ev.size()), l.data(), r.data(), 24000);
    VSM_ASSERT_EQ(p->activeVoiceCount(), 3);
}

VSM_TEST(additive_a_full_chord_does_not_clip) {
    auto p = makeAdditive();
    setByName(*p, "Spectral Tilt", 0.0f);
    setByName(*p, "Amp Sustain", 1.0f);
    std::vector<MidiNoteEvent> ev;
    for (int k = 0; k < 8; ++k) ev.push_back(noteOn(0, static_cast<uint8_t>(48 + 2 * k), 110));
    std::vector<float> l(48000, 0.0f), r(48000, 0.0f);
    p->process(ev.data(), static_cast<int>(ev.size()), l.data(), r.data(), 48000);
    VSM_ASSERT(peakAbs(l) > 0.05f);
    VSM_ASSERT(peakAbs(l) < 1.0f);
}

VSM_TEST(additive_stays_finite_under_extreme_settings) {
    auto p = makeAdditive();
    for (const auto& info : p->parameterList()) p->setParameter(info.id, info.maxValue);
    const auto a = play(p, 36, 24000, 127);
    for (float s : a) VSM_ASSERT(std::isfinite(s));
    auto q = makeAdditive();
    for (const auto& info : q->parameterList()) q->setParameter(info.id, info.minValue);
    const auto b = play(q, 96, 24000, 127);
    for (float s : b) VSM_ASSERT(std::isfinite(s));
}

VSM_TEST(additive_is_deterministic) {
    auto a = makeAdditive();
    auto b = makeAdditive();
    const auto x = play(a, 57, 24000);
    const auto y = play(b, 57, 24000);
    for (size_t i = 0; i < x.size(); ++i) VSM_ASSERT_EQ(x[i], y[i]);
}

VSM_TEST(additive_save_load_roundtrip) {
    auto p = makeAdditive();
    setByName(*p, "Spectral Tilt", -11.5f);
    setByName(*p, "Inharmonicity", 0.37f);
    const auto state = p->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.additive"));
    auto autre = makeAdditive();
    autre->loadState(state);
    VSM_ASSERT_NEAR(autre->getParameter(paramByName(*autre, "Spectral Tilt")), -11.5f, 1e-6);
    VSM_ASSERT_NEAR(autre->getParameter(paramByName(*autre, "Inharmonicity")), 0.37f, 1e-6);
}

VSM_TEST(additive_parameter_list_is_complete) {
    auto p = makeAdditive();
    VSM_ASSERT_EQ(static_cast<int>(p->parameterList().size()), 13);
}
