#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.clavinet` — la corde que la touche tient contre l'enclume, et qui sonne
// ENTIÈRE, plus bas, l'instant où on la lâche, avant que la laine ne la taise
// (H32, écrite avant sa mesure).

namespace {

constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeClav(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.clavinet");
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
    for (double f = lo; f <= hi; f += 1.0) m = std::max(m, magnitudeAt(x, from, count, f));
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
/// Le centre de gravité spectral, en Hz, sur une fenêtre.
double centroide(const std::vector<float>& x, size_t from, size_t count) {
    double num = 0.0, den = 0.0;
    for (double f = 100.0; f <= 8000.0; f += 50.0) {
        const double m = magnitudeAt(x, from, count, f);
        num += f * m; den += m;
    }
    return den > 0.0 ? num / den : 0.0;
}

} // namespace

VSM_TEST(clavinet_registered) {
    auto synth = makeClav();
    VSM_ASSERT(synth != nullptr);
    VSM_ASSERT_EQ(std::string(synth->machineName()), std::string("Clavinet (la corde qui sonne entière au relâchement)"));
}

VSM_TEST(clavinet_silent_with_no_events) {
    auto synth = makeClav();
    std::vector<float> l(2048, 1.0f), r(2048, 1.0f);
    synth->process(nullptr, 0, l.data(), r.data(), 2048);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0f, 1e-9f);
}

VSM_TEST(clavinet_note_produces_sound_and_stays_finite) {
    auto synth = makeClav();
    auto out = rendre(*synth, 57, 100, 24000, 48000);
    VSM_ASSERT(peakAbs(out) > 0.01f);
    for (float v : out) VSM_ASSERT(std::isfinite(v));
    VSM_ASSERT(peakAbs(out) < 3.0f);
}

/// LE TRAIT DISTINCTIF, PREMIER : au relâchement, la corde ENTIÈRE sonne, plus
/// bas. La3 (220 Hz) tenue, corde derrière l'embout à 0,35 : dans les 60 ms
/// qui suivent le relâchement, le pic est à 220/1,35 = 163 Hz, pas à 220 Hz.
/// Sans corde derrière (0), rien ne descend.
VSM_TEST(clavinet_release_lets_the_whole_string_ring_lower) {
    auto avec = makeClav();
    auto sans = makeClav();
    for (auto* s : {avec.get(), sans.get()}) { set(*s, "Yarn Damping", 0.12f); set(*s, "Pickup Mix", 0.0f); }
    set(*avec, "String Behind", 0.35f);
    set(*sans, "String Behind", 0.0f);
    auto a = rendre(*avec, 57, 100, 24000, 48000);
    auto b = rendre(*sans, 57, 100, 24000, 48000);
    const size_t debut = 24000 + 480, fenetre = 2880;   // 10 à 70 ms après
    const double aBas = pic(a, debut, fenetre, 155.0, 171.0), aTenue = pic(a, debut, fenetre, 212.0, 228.0);
    const double bBas = pic(b, debut, fenetre, 155.0, 171.0), bTenue = pic(b, debut, fenetre, 212.0, 228.0);
    std::printf("    [banc clavinet] après relâchement : 163 Hz %.5f contre 220 Hz %.5f (corde derrière 0,35) ; %.5f contre %.5f (0)\n",
                aBas, aTenue, bBas, bTenue);
    VSM_ASSERT(aBas > aTenue * 2.0);
    VSM_ASSERT(bTenue > bBas * 2.0);
}

/// SECOND TRAIT : la laine étouffe. 300 ms après le relâchement, il ne reste
/// rien ; et une laine lâche (0,3 s) laisse plus de queue qu'une laine
/// serrée (0,03 s).
VSM_TEST(clavinet_yarn_damps_the_release_quickly) {
    auto serre = makeClav();
    auto lache = makeClav();
    set(*serre, "Yarn Damping", 0.03f);
    set(*lache, "Yarn Damping", 0.3f);
    auto a = rendre(*serre, 57, 100, 24000, 72000);
    auto b = rendre(*lache, 57, 100, 24000, 72000);
    const double tenueA = rmsOf(a, 12000, 9600);
    const double apresA = rmsOf(a, 24000 + 14400, 2400);    // 300 ms après
    const double apresB = rmsOf(b, 24000 + 14400, 2400);
    std::printf("    [banc clavinet] tenue %.4f ; 300 ms après le relâchement : laine serrée %.6f, laine lâche %.6f\n",
                tenueA, apresA, apresB);
    VSM_ASSERT(apresA < tenueA * 0.02);
    VSM_ASSERT(apresB > apresA * 5.0);
}

/// TROISIÈME TRAIT : les deux micros EN DIFFÉRENCE creusent le fondamental
/// et gardent l'octave — le son « nasal » du D6. Les micros sont près des
/// deux bouts de la corde : les rangs impairs y ont le même signe (ils se
/// retranchent), les pairs le signe opposé (ils se doublent).
VSM_TEST(clavinet_pickup_difference_hollows_the_fundamental_and_keeps_the_octave) {
    auto somme = makeClav();
    auto difference = makeClav();
    for (auto* s : {somme.get(), difference.get()}) { set(*s, "Filter Cutoff", 16000.0f); set(*s, "Pickup Mix", 0.5f); }
    set(*somme, "Pickup Phase", 0.0f);
    set(*difference, "Pickup Phase", 1.0f);
    auto a = rendre(*somme, 57, 100, 0, 24000);
    auto c = rendre(*difference, 57, 100, 0, 24000);
    const double f0S = magnitudeAt(a, 2400, 8192, 220.0), f0D = magnitudeAt(c, 2400, 8192, 220.0);
    const double h2S = magnitudeAt(a, 2400, 8192, 440.0), h2D = magnitudeAt(c, 2400, 8192, 440.0);
    std::printf("    [banc clavinet] fondamental : somme %.5f, différence %.5f ; octave : somme %.5f, différence %.5f\n",
                f0S, f0D, h2S, h2D);
    VSM_ASSERT(f0D < f0S * 0.3);
    VSM_ASSERT(h2D > h2S * 1.5);
    // Et le centroïde : la différence est plus haute, puisqu'elle a perdu
    // son fondamental.
    VSM_ASSERT(centroide(c, 2400, 8192) > centroide(a, 2400, 8192) * 1.1);
}

/// Le curseur de sourdine raccourcit la tenue.
VSM_TEST(clavinet_mute_slider_shortens_the_note) {
    auto ouvert = makeClav();
    auto sourdine = makeClav();
    set(*ouvert, "Mute", 0.0f);
    set(*sourdine, "Mute", 1.0f);
    auto a = rendre(*ouvert, 57, 100, 0, 72000);
    auto b = rendre(*sourdine, 57, 100, 0, 72000);
    const double tardA = rmsOf(a, 48000, 9600) / std::max(1e-9, rmsOf(a, 0, 4800));
    const double tardB = rmsOf(b, 48000, 9600) / std::max(1e-9, rmsOf(b, 0, 4800));
    std::printf("    [banc clavinet] tenue à 1 s (relative) : ouvert %.4f, sourdine %.4f\n", tardA, tardB);
    VSM_ASSERT(tardB < tardA * 0.3);
}

VSM_TEST(clavinet_velocity_matters) {
    auto douce = makeClav();
    auto forte = makeClav();
    auto a = rendre(*douce, 57, 30, 0, 24000);
    auto b = rendre(*forte, 57, 120, 0, 24000);
    VSM_ASSERT(rmsOf(b, 0, 9600) > rmsOf(a, 0, 9600) * 1.5);
}

VSM_TEST(clavinet_is_deterministic) {
    auto a = makeClav();
    auto b = makeClav();
    VSM_ASSERT(rendre(*a, 62, 90, 12000, 24000) == rendre(*b, 62, 90, 12000, 24000));
}

VSM_TEST(clavinet_save_load_roundtrip) {
    auto synth = makeClav();
    set(*synth, "String Behind", 0.6f);
    set(*synth, "Pickup Mix", 0.9f);
    auto state = synth->saveState();
    auto other = makeClav();
    other->loadState(state);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "String Behind")), 0.6f, 1e-6f);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Pickup Mix")), 0.9f, 1e-6f);
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.clavinet"));
}

VSM_TEST(clavinet_parameter_list_size) {
    auto synth = makeClav();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t{11});
}

VSM_TEST(clavinet_refuses_pitch_bend) {
    auto synth = makeClav();
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 2.0f;
    VSM_ASSERT(!synth->handleControlEvent(bend));
}
