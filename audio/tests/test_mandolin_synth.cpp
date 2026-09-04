#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.mandolin` — les cordes vont par DEUX, et le plectre ne s'arrête pas :
// le battement du chœur, l'octave de la douze-cordes, le trémolo du plectre
// (H33, écrite avant sa mesure, CDC machines-manquantes § 29).

namespace {

constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeMandolin(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.mandolin");
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
/// Une note tenue `tenue` échantillons (0 = jamais relâchée), rendue sur `frames`.
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
/// Le chœur seul : ni trémolo, ni octave, sauf demande.
void chœurNu(ISynthPlugin& s, float cents, float octave = 0.0f, float spreadMs = 3.0f) {
    set(s, "Course Detune", cents);
    set(s, "Octave Pair", octave);
    set(s, "Strum Spread", spreadMs);
    set(s, "Tremolo Rate", 0.0f);
}

} // namespace

VSM_TEST(mandolin_registered) {
    auto synth = makeMandolin();
    VSM_ASSERT(synth != nullptr);
    VSM_ASSERT_EQ(std::string(synth->machineName()), std::string("Mandolin (les cordes par deux)"));
}

VSM_TEST(mandolin_silent_with_no_events) {
    auto synth = makeMandolin();
    std::vector<float> l(2048, 1.0f), r(2048, 1.0f);
    synth->process(nullptr, 0, l.data(), r.data(), 2048);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0f, 1e-9f);
}

VSM_TEST(mandolin_note_produces_sound_and_stays_finite) {
    auto synth = makeMandolin();
    auto out = rendre(*synth, 69, 100, 0, 48000);
    VSM_ASSERT(peakAbs(out) > 0.01f);
    for (float v : out) VSM_ASSERT(std::isfinite(v));
    VSM_ASSERT(peakAbs(out) < 3.0f);
}

/// LE TRAIT DISTINCTIF, PREMIER : deux cordes à 6 cents BATTENT. Sur la3
/// (220 Hz), Δf = 0,76 Hz, une période de 1,3 s : l'enveloppe du fondamental
/// ondule (profondeur ≥ 0,5 sur 2,6 s) ; à 0 cent elle ne fait que décroître.
VSM_TEST(mandolin_courses_beat_at_their_detune) {
    auto bat = makeMandolin();
    auto droit = makeMandolin();
    chœurNu(*bat, 6.0f);
    chœurNu(*droit, 0.0f);
    auto a = rendre(*bat, 57, 100, 0, 144000);
    auto b = rendre(*droit, 57, 100, 0, 144000);
    double lo = 1e9, hi = 0.0;
    for (size_t debut = 12000; debut < 12000 + 124800; debut += 4800) {
        const double m = magnitudeAt(a, debut, 4800, 220.0);
        lo = std::min(lo, m); hi = std::max(hi, m);
    }
    std::printf("    [banc mandoline] battement à 6 cents : fondamental min %.5f, max %.5f (profondeur %.2f)\n",
                lo, hi, 1.0 - lo / hi);
    VSM_ASSERT(1.0 - lo / hi >= 0.5);
    double precedent = 1e9;
    for (size_t debut = 12000; debut < 12000 + 124800; debut += 4800) {
        const double m = magnitudeAt(b, debut, 4800, 220.0);
        VSM_ASSERT(m <= precedent * 1.02);
        precedent = m;
    }
}

/// SECOND TRAIT : la douze-cordes porte son OCTAVE dès la frappe. La première
/// forme de l'attendu (« 440 Hz double ») a été RÉFUTÉE par la cohérence : la
/// fondamentale de la seconde corde et l'harmonique 2 de la première sont à
/// la même fréquence et s'annulent en partie (0,00535 contre 0,00592). On
/// écarte donc la seconde corde de 30 cents (447,7 Hz, à 7,7 Hz de 440 : une
/// fenêtre de 0,5 s les sépare) : là, l'unisson n'a rien, l'octave doit avoir
/// au moins le double.
VSM_TEST(mandolin_octave_pair_carries_the_octave_from_the_strike) {
    auto unisson = makeMandolin();
    auto octave = makeMandolin();
    chœurNu(*unisson, 0.0f, 0.0f);
    chœurNu(*octave, 30.0f, 1.0f);
    auto a = rendre(*unisson, 57, 100, 0, 24000);
    auto b = rendre(*octave, 57, 100, 0, 24000);
    const double hzB = 440.0 * std::exp2(30.0 / 1200.0);
    const double aB = magnitudeAt(a, 0, 24000, hzB), bB = magnitudeAt(b, 0, 24000, hzB);
    const double a220 = magnitudeAt(a, 0, 24000, 220.0), b220 = magnitudeAt(b, 0, 24000, 220.0);
    std::printf("    [banc mandoline] octave : %.1f Hz %.5f (unisson) contre %.5f (octave à 30 cents) ; 220 Hz %.5f contre %.5f\n",
                hzB, aB, bB, a220, b220);
    VSM_ASSERT(bB >= aB * 2.0);
    VSM_ASSERT(b220 > 0.0);   // la première corde est toujours là
}

/// TROISIÈME TRAIT : le plectre REFRAPPE tant que la touche tient. À 10 Hz,
/// au moins huit maxima de l'enveloppe dans la seconde, et le niveau à 0,95 s
/// reste au moins la moitié de celui à 0,05 s — sans trémolo il tombe sous 30 %.
VSM_TEST(mandolin_tremolo_restrikes_while_the_key_is_held) {
    auto trem = makeMandolin();
    auto tenu = makeMandolin();
    chœurNu(*trem, 6.0f);
    chœurNu(*tenu, 6.0f);
    set(*trem, "Tremolo Rate", 10.0f);
    auto a = rendre(*trem, 57, 100, 0, 48000);
    auto b = rendre(*tenu, 57, 100, 0, 48000);
    std::vector<double> env;
    for (size_t debut = 0; debut + 960 <= 48000; debut += 960) env.push_back(rmsOf(a, debut, 960));
    int maxima = 0;
    for (size_t i = 1; i + 1 < env.size(); ++i)
        if (env[i] > env[i - 1] && env[i] >= env[i + 1] && env[i] > 1e-4) ++maxima;
    const double tremTot = rmsOf(a, 2400, 960), tremTard = rmsOf(a, 45600, 960);
    const double tenuTot = rmsOf(b, 2400, 960), tenuTard = rmsOf(b, 45600, 960);
    std::printf("    [banc mandoline] trémolo 10 Hz : %d maxima en 1 s ; niveau 0,05 s %.4f -> 0,95 s %.4f ; tenu : %.4f -> %.4f\n",
                maxima, tremTot, tremTard, tenuTot, tenuTard);
    VSM_ASSERT(maxima >= 8);
    VSM_ASSERT(tremTard >= tremTot * 0.5);
    VSM_ASSERT(tenuTard < tenuTot * 0.3);
}

/// QUATRIÈME TRAIT : la seconde corde attend le plectre. La première forme de
/// l'attendu (un rapport spectral à 880 Hz) a été RÉFUTÉE par le masquage :
/// l'harmonique 2 de la première corde occupe la même fréquence (1,043 contre
/// 0,787, pas le ×2 attendu). La forme exacte : tant que le plectre n'a pas
/// atteint la seconde corde, elle ne fait RIEN — 10 ms et 20 ms d'écart
/// donnent les 8 premières ms IDENTIQUES AU BIT PRÈS, et 0 ms en diffère.
/// Cela exige que chaque corde ait son propre bruit de plectre.
VSM_TEST(mandolin_second_string_comes_after_the_first) {
    auto ensemble = makeMandolin();
    auto dix = makeMandolin();
    auto vingt = makeMandolin();
    chœurNu(*ensemble, 0.0f, 1.0f, 0.0f);
    chœurNu(*dix, 0.0f, 1.0f, 10.0f);
    chœurNu(*vingt, 0.0f, 1.0f, 20.0f);
    auto a = rendre(*ensemble, 69, 100, 0, 4096);
    auto b = rendre(*dix, 69, 100, 0, 4096);
    auto c = rendre(*vingt, 69, 100, 0, 4096);
    const size_t huitMs = 384;
    bool dixEgalVingt = true, zeroDiffere = false;
    for (size_t i = 0; i < huitMs; ++i) {
        if (b[i] != c[i]) dixEgalVingt = false;
        if (a[i] != b[i]) zeroDiffere = true;
    }
    std::vector<float> diff(huitMs);
    for (size_t i = 0; i < huitMs; ++i) diff[i] = a[i] - b[i];
    std::printf("    [banc mandoline] retard du plectre : 10 ms et 20 ms identiques avant 8 ms : %s ; 0 ms diffère : %s (rms de la seconde corde %.5f contre %.5f au total)\n",
                dixEgalVingt ? "oui" : "NON", zeroDiffere ? "oui" : "NON", rmsOf(diff, 0, huitMs), rmsOf(a, 0, huitMs));
    VSM_ASSERT(dixEgalVingt);
    VSM_ASSERT(zeroDiffere);
    // Et la seconde corde n'est pas un détail : au moins un cinquième du rms.
    VSM_ASSERT(rmsOf(diff, 0, huitMs) > rmsOf(a, 0, huitMs) * 0.2);
}

/// CINQUIÈME TRAIT : le relâchement ÉTOUFFE — la main quitte les cordes
/// frettées. 300 ms après, moins de 10 % de la tenue.
VSM_TEST(mandolin_release_damps_the_strings) {
    auto synth = makeMandolin();
    chœurNu(*synth, 6.0f);
    auto out = rendre(*synth, 57, 100, 24000, 48000);
    const double avant = rmsOf(out, 19200, 4800), apres = rmsOf(out, 24000 + 14400, 4800);
    std::printf("    [banc mandoline] relâchement : rms tenue %.5f, 300 ms après %.5f\n", avant, apres);
    VSM_ASSERT(apres < avant * 0.1);
}

VSM_TEST(mandolin_velocity_matters) {
    auto douce = makeMandolin();
    auto forte = makeMandolin();
    auto a = rendre(*douce, 57, 30, 0, 24000);
    auto b = rendre(*forte, 57, 120, 0, 24000);
    VSM_ASSERT(rmsOf(b, 0, 9600) > rmsOf(a, 0, 9600) * 1.5);
}

VSM_TEST(mandolin_is_deterministic) {
    auto a = makeMandolin();
    auto b = makeMandolin();
    set(*a, "Tremolo Rate", 9.0f);
    set(*b, "Tremolo Rate", 9.0f);
    VSM_ASSERT(rendre(*a, 62, 90, 12000, 24000) == rendre(*b, 62, 90, 12000, 24000));
}

VSM_TEST(mandolin_save_load_roundtrip) {
    auto synth = makeMandolin();
    set(*synth, "Course Detune", 11.0f);
    set(*synth, "Tremolo Rate", 12.0f);
    auto state = synth->saveState();
    auto other = makeMandolin();
    other->loadState(state);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Course Detune")), 11.0f, 1e-4f);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Tremolo Rate")), 12.0f, 1e-4f);
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.mandolin"));
}

VSM_TEST(mandolin_parameter_list_size) {
    auto synth = makeMandolin();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t{10});
}

VSM_TEST(mandolin_refuses_pitch_bend) {
    auto synth = makeMandolin();
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 2.0f;
    VSM_ASSERT(!synth->handleControlEvent(bend));
}
