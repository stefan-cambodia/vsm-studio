#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.harpsichord` — le clavier qui REFUSE la vélocité, et dont la touche
// relâchée pince une seconde fois avant l'étouffoir.
//
// Le bec du sautereau lâche la corde toujours de la même façon : vite ou
// lentement, la touche donne le même son (H26, écrite avant sa mesure). En
// retombant, le bec frôle la corde — un petit pincement — puis l'étouffoir se
// pose. Et les registres (8', 4', luth) sont la seule façon de changer le son.

namespace {

constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeHarpsichord(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.harpsichord");
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
float midiToHz(int note) { return 440.0f * std::exp2f((static_cast<float>(note) - 69.0f) / 12.0f); }

/// Une note tenue `tenue` échantillons puis relâchée, rendue sur `frames`.
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

VSM_TEST(harpsichord_registered) {
    auto synth = makeHarpsichord();
    VSM_ASSERT(synth != nullptr);
    VSM_ASSERT_EQ(std::string(synth->machineName()), std::string("Harpsichord (le clavecin)"));
}

VSM_TEST(harpsichord_silent_with_no_events) {
    auto synth = makeHarpsichord();
    std::vector<float> l(2048, 1.0f), r(2048, 1.0f);
    synth->process(nullptr, 0, l.data(), r.data(), 2048);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0f, 1e-9f);
}

VSM_TEST(harpsichord_note_produces_sound_and_stays_finite) {
    auto synth = makeHarpsichord();
    auto out = rendre(*synth, 60, 100, 24000, 48000);
    VSM_ASSERT(peakAbs(out) > 0.01f);
    for (float v : out) VSM_ASSERT(std::isfinite(v));
    VSM_ASSERT(peakAbs(out) < 2.0f);
}

/// LE TRAIT DISTINCTIF, PREMIER : la vélocité est ignorée AU BIT PRÈS. Un
/// sautereau pince toujours de la même façon, et un pianiste qui frappe fort
/// n'obtient pas un clavecin plus fort — c'est ce qui a fait inventer le
/// piano-forte.
VSM_TEST(harpsichord_ignores_velocity_bit_for_bit) {
    auto douce = makeHarpsichord();
    auto forte = makeHarpsichord();
    auto a = rendre(*douce, 64, 20, 24000, 48000);
    auto b = rendre(*forte, 64, 120, 24000, 48000);
    VSM_ASSERT(peakAbs(a) > 0.01f);
    VSM_ASSERT(a == b);
}

/// LE TRAIT DISTINCTIF, SECOND : le relâchement PINCE une seconde fois. On
/// laisse la corde mourir presque entièrement (touche tenue longtemps, corde
/// à décroissance courte), puis on relâche : le niveau juste APRÈS le
/// relâchement dépasse nettement celui juste avant — puis l'étouffoir coupe.
VSM_TEST(harpsichord_release_plucks_again_then_the_damper_stops_it) {
    auto synth = makeHarpsichord();
    set(*synth, "String Decay", 0.5f);
    set(*synth, "Release Pluck", 1.0f);
    set(*synth, "Damper Time", 0.03f);
    const int tenue = 96000;          // 2 s : la corde s'est presque tue
    auto out = rendre(*synth, 60, 100, tenue, 144000);
    const double avant = rmsOf(out, static_cast<size_t>(tenue) - 4800, 4800);   // 100 ms avant
    const double apres = rmsOf(out, static_cast<size_t>(tenue), 480);           // 10 ms après
    const double plusTard = rmsOf(out, static_cast<size_t>(tenue) + 9600, 4800); // 200 ms après
    const double attaque = rmsOf(out, 0, 480);                                  // 10 ms d'attaque
    std::printf("    [banc clavecin] relâchement : attaque %.5f, corde avant %.6f, frôlement %.5f "
                "(%.1f dB sous l'attaque), 200 ms plus tard %.6f\n",
                attaque, avant, apres, 20.0 * std::log10(apres / std::max(1e-9, attaque)), plusTard);
    VSM_ASSERT(apres > avant * 3.0);
    VSM_ASSERT(plusTard < apres * 0.1);
}

/// Sans frôlement (« Release Pluck » à zéro, le bec idéal), relâcher n'ajoute
/// rien : l'étouffoir se pose seulement.
VSM_TEST(harpsichord_release_pluck_at_zero_adds_nothing) {
    auto synth = makeHarpsichord();
    set(*synth, "String Decay", 0.5f);
    set(*synth, "Release Pluck", 0.0f);
    const int tenue = 96000;
    auto out = rendre(*synth, 60, 100, tenue, 144000);
    const double avant = rmsOf(out, static_cast<size_t>(tenue) - 4800, 4800);
    const double apres = rmsOf(out, static_cast<size_t>(tenue), 480);
    VSM_ASSERT(apres <= avant * 1.5 + 1e-6);
}

/// LES REGISTRES : tirer le 4' ajoute une corde à l'octave, et cela se mesure
/// à 2·f0. Le jeu de luth raccourcit la note.
VSM_TEST(harpsichord_four_foot_register_adds_the_octave) {
    auto sans = makeHarpsichord();
    auto avec = makeHarpsichord();
    set(*sans, "Register 4'", 0.0f);
    set(*avec, "Register 4'", 1.0f);
    auto a = rendre(*sans, 57, 100, 0, 48000);
    auto b = rendre(*avec, 57, 100, 0, 48000);
    const double f0 = midiToHz(57);
    const double octaveSans = magnitudeAt(a, 4800, 16384, 2.0 * f0);
    const double octaveAvec = magnitudeAt(b, 4800, 16384, 2.0 * f0);
    std::printf("    [banc clavecin] 4' : magnitude à 2·f0 sans %.5f, avec %.5f (x%.1f)\n",
                octaveSans, octaveAvec, octaveAvec / std::max(1e-9, octaveSans));
    VSM_ASSERT(octaveAvec > octaveSans * 1.5);
}

VSM_TEST(harpsichord_lute_stop_shortens_the_note) {
    auto libre = makeHarpsichord();
    auto luth = makeHarpsichord();
    set(*libre, "Lute Stop", 0.0f);
    set(*luth, "Lute Stop", 1.0f);
    auto a = rendre(*libre, 60, 100, 0, 96000);
    auto b = rendre(*luth, 60, 100, 0, 96000);
    const double tardLibre = rmsOf(a, 48000, 9600);
    const double tardLuth = rmsOf(b, 48000, 9600);
    std::printf("    [banc clavecin] jeu de luth : RMS à 1 s libre %.5f, luth %.5f (x%.2f)\n",
                tardLibre, tardLuth, tardLuth / std::max(1e-9, tardLibre));
    VSM_ASSERT(tardLuth < tardLibre * 0.5);
}

VSM_TEST(harpsichord_is_deterministic) {
    auto a = makeHarpsichord();
    auto b = makeHarpsichord();
    VSM_ASSERT(rendre(*a, 62, 90, 12000, 24000) == rendre(*b, 62, 90, 12000, 24000));
}

VSM_TEST(harpsichord_save_load_roundtrip) {
    auto synth = makeHarpsichord();
    set(*synth, "Register 4'", 0.8f);
    set(*synth, "Lute Stop", 0.3f);
    auto state = synth->saveState();
    auto other = makeHarpsichord();
    other->loadState(state);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Register 4'")), 0.8f, 1e-6f);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Lute Stop")), 0.3f, 1e-6f);
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.harpsichord"));
}

VSM_TEST(harpsichord_parameter_list_size) {
    auto synth = makeHarpsichord();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t{10});
}

/// Ni molette ni pression : le refus est en connaissance de cause, et le
/// moteur le compte.
VSM_TEST(harpsichord_refuses_pitch_bend_knowingly) {
    auto synth = makeHarpsichord();
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 1.0f;
    VSM_ASSERT(!synth->handleControlEvent(bend));
}
