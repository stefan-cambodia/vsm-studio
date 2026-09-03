#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.bagpipe` — la RÉSERVE D'AIR qui interdit le silence : pas de trou entre
// deux notes, une note de grâce obligatoire pour répéter une note, et un sac
// qui se vide en détendant toutes les anches ensemble (H30, écrite avant sa
// mesure).

namespace {

constexpr double kSampleRate = 48000.0;

SynthPluginPtr makePipes(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.bagpipe");
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
struct Geste { int at; MidiNoteEvent::Kind kind; uint8_t note; uint8_t velocity; };

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
/// La fréquence du plus fort pic dans une bande, au hertz près.
double picHz(const std::vector<float>& x, size_t from, size_t count, double lo, double hi) {
    double best = lo, m = 0.0;
    for (double f = lo; f <= hi; f += 0.5) { const double v = magnitudeAt(x, from, count, f); if (v > m) { m = v; best = f; } }
    return best;
}

/// Rend une suite de gestes (en échantillons) ; sortie = gauche + droite.
std::vector<float> rendre(ISynthPlugin& synth, const std::vector<Geste>& gestes, int frames) {
    std::vector<float> left(static_cast<size_t>(frames), 0.0f), right(static_cast<size_t>(frames), 0.0f);
    constexpr int kBlock = 256;
    for (int start = 0; start + kBlock <= frames; start += kBlock) {
        std::vector<MidiNoteEvent> block;
        for (const auto& g : gestes)
            if (g.at >= start && g.at < start + kBlock)
                block.push_back({g.kind, g.at - start, 0, g.note, g.velocity});
        synth.process(block.empty() ? nullptr : block.data(), static_cast<int>(block.size()),
                      left.data() + start, right.data() + start, kBlock);
    }
    for (size_t i = 0; i < left.size(); ++i) left[i] += right[i];
    return left;
}
constexpr auto On = MidiNoteEvent::Kind::NoteOn;
constexpr auto Off = MidiNoteEvent::Kind::NoteOff;

} // namespace

VSM_TEST(bagpipe_registered) {
    auto synth = makePipes();
    VSM_ASSERT(synth != nullptr);
    VSM_ASSERT_EQ(std::string(synth->machineName()), std::string("Bagpipe (la réserve d'air)"));
}

VSM_TEST(bagpipe_silent_with_no_events) {
    auto synth = makePipes();
    std::vector<float> l(2048, 1.0f), r(2048, 1.0f);
    synth->process(nullptr, 0, l.data(), r.data(), 2048);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0f, 1e-9f);
}

VSM_TEST(bagpipe_note_produces_sound_and_stays_finite) {
    auto synth = makePipes();
    auto out = rendre(*synth, {{0, On, 64, 100}}, 48000);
    VSM_ASSERT(rmsOf(out, 24000, 12000) > 0.01);
    for (float v : out) VSM_ASSERT(std::isfinite(v));
    VSM_ASSERT(peakAbs(out) < 3.0f);
}

/// LE TRAIT DISTINCTIF, PREMIER : pas de silence entre deux notes. Une note
/// lâchée, 150 ms de rien, une autre note : le niveau dans le trou reste
/// celui de la note. Un vent ordinaire y serait retombé.
VSM_TEST(bagpipe_never_falls_silent_between_two_notes) {
    auto synth = makePipes();
    set(*synth, "Drones", 0.0f);
    auto out = rendre(*synth, {{0, On, 64, 100}, {24000, Off, 64, 0}, {31200, On, 66, 100}}, 60000);
    const double pendant = rmsOf(out, 19200, 4800);
    const double trou = rmsOf(out, 25200, 4800);
    std::printf("    [banc cornemuse] niveau pendant la note %.4f, dans le trou de 150 ms %.4f (%.0f %%)\n",
                pendant, trou, 100.0 * trou / std::max(1e-9, pendant));
    VSM_ASSERT(trou > pendant * 0.8);
}

/// SECOND TRAIT : la note répétée reçoit une note de grâce — le la aigu
/// (tonique + 12, 440 Hz pour la tonique 57) — dans la fenêtre qui suit la
/// seconde frappe, et pas quand la note change.
VSM_TEST(bagpipe_repeated_note_gets_a_grace_note) {
    auto meme = makePipes();
    auto autre = makePipes();
    for (auto* s : {meme.get(), autre.get()}) { set(*s, "Drones", 0.0f); set(*s, "Grace Length", 40.0f); }
    // La même note rejouée (64), contre une note différente (62) — le la aigu
    // (440 Hz) est loin des deux (329,6 Hz et 293,7 Hz) et de leurs octaves.
    auto a = rendre(*meme, {{0, On, 64, 100}, {24000, Off, 64, 0}, {26400, On, 64, 100}}, 48000);
    auto b = rendre(*autre, {{0, On, 64, 100}, {24000, Off, 64, 0}, {26400, On, 62, 100}}, 48000);
    const size_t fenetre = 1680;   // 35 ms
    const double avant = magnitudeAt(a, 26400 - fenetre, fenetre, 440.0);
    const double graceMeme = magnitudeAt(a, 26400 + 240, fenetre, 440.0);
    const double graceAutre = magnitudeAt(b, 26400 + 240, fenetre, 440.0);
    const double fondMeme = magnitudeAt(a, 26400 + 240, fenetre, 329.6);
    std::printf("    [banc cornemuse] la aigu (440 Hz) : avant %.5f, après la note répétée %.5f (fondamental %.5f), après une autre note %.5f\n",
                avant, graceMeme, fondMeme, graceAutre);
    VSM_ASSERT(graceMeme > avant * 3.0);
    VSM_ASSERT(graceMeme > fondMeme);
    VSM_ASSERT(graceMeme > graceAutre * 3.0);
}

/// TROISIÈME TRAIT : le sac. Tout lâché, la note tient sa réserve, puis le
/// sac se vide et tout se tait.
VSM_TEST(bagpipe_bag_holds_its_reserve_then_empties) {
    auto synth = makePipes();
    set(*synth, "Bag Reserve", 0.4f);
    set(*synth, "Cut-off", 0.2f);
    auto out = rendre(*synth, {{0, On, 64, 100}, {24000, Off, 64, 0}}, 144000);
    const double pendant = rmsOf(out, 19200, 4800);
    const double reserve = rmsOf(out, 24000 + 9600, 4800);     // 0,2 à 0,3 s après
    const double vide = rmsOf(out, 24000 + 96000, 4800);       // 2 s après
    std::printf("    [banc cornemuse] pendant %.4f, 0,2 s après le relâchement %.4f, 2 s après %.6f\n", pendant, reserve, vide);
    VSM_ASSERT(reserve > pendant * 0.8);
    VSM_ASSERT(vide < pendant * 0.02);
}

/// Et pendant que le sac se vide, la hauteur BAISSE — toutes les anches
/// ensemble. Mesuré sur le chalumeau (mi 329,6 Hz) : au moins 8 cents plus
/// bas dans la coupure que dans la tenue.
VSM_TEST(bagpipe_pitch_sags_as_the_bag_empties) {
    auto synth = makePipes();
    set(*synth, "Drones", 0.0f);
    set(*synth, "Bag Reserve", 0.2f);
    set(*synth, "Cut-off", 0.6f);
    set(*synth, "Breath Noise", 0.0f);
    auto out = rendre(*synth, {{0, On, 64, 100}, {48000, Off, 64, 0}}, 144000);
    const double tenue = picHz(out, 30000, 16384, 300.0, 360.0);
    // 0,2 s de réserve puis la chute : à 0,45 s après le relâchement, le sac
    // a perdu une part de sa pression et le chalumeau sonne encore.
    const double coupure = picHz(out, 48000 + 21600, 16384, 300.0, 360.0);
    const double cents = 1200.0 * std::log2(coupure / tenue);
    std::printf("    [banc cornemuse] hauteur en tenue %.1f Hz, dans la coupure %.1f Hz (%.1f cents)\n", tenue, coupure, cents);
    VSM_ASSERT(cents < -8.0);
}

/// Les bourdons : le ténor à la tonique moins une octave (la2, 110 Hz) est
/// là quand on les demande, absent sinon.
VSM_TEST(bagpipe_drones_sound_at_the_tonic_below) {
    auto avec = makePipes();
    auto sans = makePipes();
    set(*avec, "Drones", 1.0f);
    set(*sans, "Drones", 0.0f);
    auto a = rendre(*avec, {{0, On, 64, 100}}, 48000);
    auto b = rendre(*sans, {{0, On, 64, 100}}, 48000);
    const double tenorAvec = magnitudeAt(a, 24000, 16384, 110.0);
    const double tenorSans = magnitudeAt(b, 24000, 16384, 110.0);
    std::printf("    [banc cornemuse] ténor à 110 Hz : avec bourdons %.5f, sans %.5f\n", tenorAvec, tenorSans);
    VSM_ASSERT(tenorAvec > tenorSans * 5.0);
}

/// PAS DE NUANCE : la vélocité est ignorée au bit près.
VSM_TEST(bagpipe_ignores_velocity_bit_exactly) {
    auto doux = makePipes();
    auto fort = makePipes();
    auto a = rendre(*doux, {{0, On, 64, 20}, {24000, Off, 64, 0}}, 36000);
    auto b = rendre(*fort, {{0, On, 64, 127}, {24000, Off, 64, 0}}, 36000);
    VSM_ASSERT(a == b);
}

VSM_TEST(bagpipe_is_deterministic) {
    auto a = makePipes();
    auto b = makePipes();
    VSM_ASSERT(rendre(*a, {{0, On, 62, 90}, {12000, Off, 62, 0}}, 24000) == rendre(*b, {{0, On, 62, 90}, {12000, Off, 62, 0}}, 24000));
}

VSM_TEST(bagpipe_save_load_roundtrip) {
    auto synth = makePipes();
    set(*synth, "Drone Note", 55.0f);
    set(*synth, "Bag Reserve", 1.2f);
    auto state = synth->saveState();
    auto other = makePipes();
    other->loadState(state);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Drone Note")), 55.0f, 1e-4f);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Bag Reserve")), 1.2f, 1e-6f);
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.bagpipe"));
}

VSM_TEST(bagpipe_parameter_list_size) {
    auto synth = makePipes();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t{11});
}

/// Ni molette ni contrôleur : les doigts bouchent des trous.
VSM_TEST(bagpipe_refuses_pitch_bend_and_controllers) {
    auto synth = makePipes();
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 2.0f;
    VSM_ASSERT(!synth->handleControlEvent(bend));
    MidiControlEvent cc;
    cc.kind = MidiControlEvent::Kind::ControlChange;
    cc.index = 1;
    cc.value = 1.0f;
    VSM_ASSERT(!synth->handleControlEvent(cc));
}
