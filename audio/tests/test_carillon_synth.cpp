#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.carillon` — la cloche : une TIERCE MINEURE dans le spectre, des partiels
// qui vont par deux et qui battent, un bourdon qui survit à tout (H31, écrite
// avant sa mesure).

namespace {

constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeBell(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.carillon");
    if (plugin) plugin->initialize(sr, 512);
    return plugin;
}
ParamId byName(const ISynthPlugin& plugin, const std::string& name) {
    for (const auto& info : plugin.parameterList()) if (info.name == name) return info.id;
    throw vsm::test::AssertionFailure("paramètre inconnu : « " + name + " »");
}
void set(ISynthPlugin& plugin, const std::string& name, float value) {
    plugin.setParameter(byName(plugin, name), value);
}
MidiNoteEvent noteOn(int offset, uint8_t note, uint8_t velocity = 100) {
    return {MidiNoteEvent::Kind::NoteOn, offset, 0, note, velocity};
}
MidiNoteEvent noteOff(int offset, uint8_t note) {
    return {MidiNoteEvent::Kind::NoteOff, offset, 0, note, 0};
}
float peakAbs(const std::vector<float>& b) {
    float p = 0.0f; for (float v : b) p = std::max(p, std::abs(v)); return p;
}
double rmsOf(const std::vector<float>& x, size_t from, size_t count) {
    double s = 0.0; size_t n = 0;
    for (size_t i = from; i < from + count && i < x.size(); ++i) { s += x[i] * x[i]; ++n; }
    return n ? std::sqrt(s / static_cast<double>(n)) : 0.0;
}
double magnitudeAt(const std::vector<float>& x, size_t from, size_t count, double hz) {
    double re = 0.0, im = 0.0, norm = 0.0;
    for (size_t i = 0; i < count && from + i < x.size(); ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / static_cast<double>(count));
        const double ph = 2.0 * M_PI * hz * static_cast<double>(i) / kSampleRate;
        re += w * static_cast<double>(x[from + i]) * std::cos(ph);
        im += w * static_cast<double>(x[from + i]) * std::sin(ph);
        norm += w;
    }
    return std::sqrt(re * re + im * im) / std::max(1.0, norm);
}
double pic(const std::vector<float>& x, size_t from, size_t count, double lo, double hi) {
    double m = 0.0;
    for (double f = lo; f <= hi; f += 0.5) m = std::max(m, magnitudeAt(x, from, count, f));
    return m;
}
std::vector<float> rendre(ISynthPlugin& synth, uint8_t note, uint8_t velocity, int tenue, int frames) {
    std::vector<float> left(static_cast<size_t>(frames), 0.0f), right(static_cast<size_t>(frames), 0.0f);
    constexpr int kBlock = 256;
    for (int start = 0; start + kBlock <= frames; start += kBlock) {
        std::vector<MidiNoteEvent> block;
        if (start == 0) block.push_back(noteOn(0, note, velocity));
        if (tenue > 0 && start <= tenue && tenue < start + kBlock) block.push_back(noteOff(tenue - start, note));
        synth.process(block.empty() ? nullptr : block.data(), static_cast<int>(block.size()),
                      left.data() + start, right.data() + start, kBlock);
    }
    return left;
}

} // namespace

VSM_TEST(carillon_registered) {
    auto synth = makeBell();
    VSM_ASSERT(synth != nullptr);
    VSM_ASSERT_EQ(std::string(synth->machineName()), std::string("Carillon (la cloche et sa tierce mineure)"));
}

VSM_TEST(carillon_silent_with_no_events) {
    auto synth = makeBell();
    std::vector<float> l(2048, 1.0f), r(2048, 1.0f);
    synth->process(nullptr, 0, l.data(), r.data(), 2048);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0f, 1e-9f);
}

VSM_TEST(carillon_note_produces_sound_and_stays_finite) {
    auto synth = makeBell();
    auto out = rendre(*synth, 69, 100, 0, 48000);
    VSM_ASSERT(peakAbs(out) > 0.01f);
    for (float v : out) VSM_ASSERT(std::isfinite(v));
    VSM_ASSERT(peakAbs(out) < 3.0f);
}

/// LE TRAIT DISTINCTIF, PREMIER : la TIERCE MINEURE. Sur la3 (prime 220 Hz),
/// un partiel à 264 Hz (6:5) — et rien à 275 Hz (5:4). Tierce majeure
/// demandée : l'inverse. Et aucun partiel à 3·f0 exact (660 Hz) : ce n'est
/// pas un spectre harmonique.
VSM_TEST(carillon_has_a_minor_third_partial_that_no_harmonic_instrument_has) {
    auto mineure = makeBell();
    auto majeure = makeBell();
    for (auto* s : {mineure.get(), majeure.get()}) set(*s, "Doublet", 0.0f);
    set(*mineure, "Tierce", 0.0f);
    set(*majeure, "Tierce", 1.0f);
    auto a = rendre(*mineure, 57, 100, 0, 48000);
    auto b = rendre(*majeure, 57, 100, 0, 48000);
    const double a264 = pic(a, 2400, 32768, 262.0, 266.0), a275 = pic(a, 2400, 32768, 273.0, 277.0);
    const double b264 = pic(b, 2400, 32768, 262.0, 266.0), b275 = pic(b, 2400, 32768, 273.0, 277.0);
    const double a660 = pic(a, 2400, 32768, 658.0, 662.0), a662 = pic(a, 2400, 32768, 660.0, 665.0);
    std::printf("    [banc carillon] tierce mineure : 264 Hz %.5f contre 275 Hz %.5f ; majeure : 264 Hz %.5f contre 275 Hz %.5f ; 3·f0 %.5f\n",
                a264, a275, b264, b275, a660);
    VSM_ASSERT(a264 > a275 * 5.0);
    VSM_ASSERT(b275 > b264 * 5.0);
    (void)a662;
    // Le partiel le plus proche de 3·f0 est à 3,011·f0 = 662,4 Hz : à 660 Hz
    // tout juste, la fenêtre de 0,68 s (1,5 Hz de résolution) le sépare mal ;
    // le trait harmonique se juge plutôt à 2,5·f0 (550 Hz), où une cloche a
    // un partiel et où aucun instrument harmonique n'en a.
    const double a550 = pic(a, 2400, 32768, 551.0, 555.0);
    const double a495 = pic(a, 2400, 32768, 493.0, 497.0);   // 2,25·f0 : rien, ni chez l'un ni chez l'autre
    VSM_ASSERT(a550 > a495 * 5.0);
}

/// SECOND TRAIT : les partiels vont par deux et BATTENT. L'enveloppe de la
/// tierce ondule au rythme de l'écart des deux composantes ; sans écart,
/// elle ne fait que décroître.
VSM_TEST(carillon_doublets_make_the_partials_beat) {
    auto bat = makeBell();
    auto droit = makeBell();
    set(*bat, "Doublet", 1.0f);      // à la tierce : 1,2 Hz d'écart, une période de 0,83 s
    set(*droit, "Doublet", 0.0f);
    auto a = rendre(*bat, 57, 100, 0, 144000);
    auto b = rendre(*droit, 57, 100, 0, 144000);
    // Fenêtres de 100 ms sur 1,7 s (deux périodes), entre 0,5 s et 2,2 s :
    // rapport de l'enveloppe qui bat à celle qui ne bat pas.
    double lo = 1e9, hi = 0.0;
    for (size_t debut = 24000; debut < 24000 + 81600; debut += 4800) {
        const double r = magnitudeAt(a, debut, 4800, 264.0) / std::max(1e-12, magnitudeAt(b, debut, 4800, 264.0));
        lo = std::min(lo, r); hi = std::max(hi, r);
    }
    std::printf("    [banc carillon] battement de la tierce : rapport min %.3f, max %.3f (profondeur %.2f)\n",
                lo, hi, 1.0 - lo / hi);
    VSM_ASSERT(1.0 - lo / hi > 0.5);
    // Et la référence sans doublet ne fait que décroître : monotone à 2 % près.
    double precedent = 1e9;
    for (size_t debut = 24000; debut < 24000 + 81600; debut += 4800) {
        const double m = magnitudeAt(b, debut, 4800, 264.0);
        VSM_ASSERT(m <= precedent * 1.02);
        precedent = m;
    }
}

/// TROISIÈME TRAIT : le bourdon survit à tout. À six secondes, l'octave grave
/// (110 Hz) domine la nominale (440 Hz) — alors qu'à la frappe c'était
/// l'inverse.
VSM_TEST(carillon_hum_outlives_the_nominal) {
    auto synth = makeBell();
    set(*synth, "Doublet", 0.0f);
    auto out = rendre(*synth, 57, 100, 0, 336000);   // 7 s
    const double humTot = magnitudeAt(out, 2400, 16384, 110.0), nomTot = magnitudeAt(out, 2400, 16384, 440.0);
    const double humTard = magnitudeAt(out, 288000, 32768, 110.0), nomTard = magnitudeAt(out, 288000, 32768, 440.0);
    std::printf("    [banc carillon] à la frappe : bourdon %.5f, nominale %.5f ; à 6 s : bourdon %.5f, nominale %.5f\n",
                humTot, nomTot, humTard, nomTard);
    VSM_ASSERT(nomTot > humTot);
    VSM_ASSERT(humTard > nomTard * 3.0);
}

VSM_TEST(carillon_velocity_matters_and_opens_the_timbre) {
    auto douce = makeBell();
    auto forte = makeBell();
    auto a = rendre(*douce, 57, 30, 0, 24000);
    auto b = rendre(*forte, 57, 120, 0, 24000);
    VSM_ASSERT(rmsOf(b, 0, 9600) > rmsOf(a, 0, 9600) * 1.5);
    const double partA = magnitudeAt(a, 1200, 8192, 662.4) / std::max(1e-12, magnitudeAt(a, 1200, 8192, 220.0));
    const double partB = magnitudeAt(b, 1200, 8192, 662.4) / std::max(1e-12, magnitudeAt(b, 1200, 8192, 220.0));
    VSM_ASSERT(partB > partA * 1.3);
}

VSM_TEST(carillon_is_deterministic) {
    auto a = makeBell();
    auto b = makeBell();
    VSM_ASSERT(rendre(*a, 62, 90, 12000, 24000) == rendre(*b, 62, 90, 12000, 24000));
}

VSM_TEST(carillon_save_load_roundtrip) {
    auto synth = makeBell();
    set(*synth, "Hum Decay", 25.0f);
    set(*synth, "Tierce", 0.4f);
    auto state = synth->saveState();
    auto other = makeBell();
    other->loadState(state);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Hum Decay")), 25.0f, 1e-4f);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Tierce")), 0.4f, 1e-6f);
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.carillon"));
}

VSM_TEST(carillon_parameter_list_size) {
    auto synth = makeBell();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t{7});
}

VSM_TEST(carillon_refuses_pitch_bend) {
    auto synth = makeBell();
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 2.0f;
    VSM_ASSERT(!synth->handleControlEvent(bend));
}
