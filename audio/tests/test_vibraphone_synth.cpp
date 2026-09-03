#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.vibraphone` — la barre CREUSÉE (1:4:10), le tube qu'un MOTEUR ouvre et
// ferme (seul le fondamental ondule), le feutre qu'une PÉDALE soulève (H29,
// écrite avant sa mesure).

namespace {

constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeVibes(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.vibraphone");
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
void pedale(ISynthPlugin& synth, bool enfoncee) {
    MidiControlEvent cc;
    cc.kind = MidiControlEvent::Kind::ControlChange;
    cc.index = 64;
    cc.value = enfoncee ? 1.0f : 0.0f;
    synth.handleControlEvent(cc);
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

/// Rend une note ; `tenue` = 0 garde la touche enfoncée jusqu'au bout.
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
    // Le rendu est stéréo (grave à gauche, aigu à droite) : on juge la somme.
    for (size_t i = 0; i < left.size(); ++i) left[i] += right[i];
    return left;
}

} // namespace

VSM_TEST(vibraphone_registered) {
    auto synth = makeVibes();
    VSM_ASSERT(synth != nullptr);
    VSM_ASSERT_EQ(std::string(synth->machineName()), std::string("Vibraphone (la barre, le tube et le moteur)"));
}

VSM_TEST(vibraphone_silent_with_no_events) {
    auto synth = makeVibes();
    std::vector<float> l(2048, 1.0f), r(2048, 1.0f);
    synth->process(nullptr, 0, l.data(), r.data(), 2048);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0f, 1e-9f);
}

VSM_TEST(vibraphone_note_produces_sound_and_stays_finite) {
    auto synth = makeVibes();
    auto out = rendre(*synth, 69, 100, 0, 48000);
    VSM_ASSERT(peakAbs(out) > 0.01f);
    for (float v : out) VSM_ASSERT(std::isfinite(v));
    VSM_ASSERT(peakAbs(out) < 3.0f);
}

/// LE TRAIT DISTINCTIF, PREMIER : la barre est CREUSÉE jusqu'à 1 : 4 : 10.
/// Sur la3 (220 Hz), le second partiel est à 880 Hz — pas à 611 Hz (2,78·f0),
/// où `vsm.modal` met celui d'une barre libre. Et si l'on ne creuse pas, il
/// y revient.
VSM_TEST(vibraphone_undercut_bar_tunes_second_partial_to_double_octave) {
    auto creusee = makeVibes();
    auto libre = makeVibes();
    set(*creusee, "Bar Undercut", 1.0f);
    set(*libre, "Bar Undercut", 0.0f);
    for (auto* s : {creusee.get(), libre.get()}) { set(*s, "Motor Depth", 0.0f); set(*s, "Mallet Hardness", 0.9f); }
    auto a = rendre(*creusee, 57, 110, 0, 48000);
    auto b = rendre(*libre, 57, 110, 0, 48000);
    const double a880 = pic(a, 2400, 16384, 870.0, 890.0), a611 = pic(a, 2400, 16384, 600.0, 622.0);
    const double b880 = pic(b, 2400, 16384, 870.0, 890.0), b611 = pic(b, 2400, 16384, 600.0, 622.0);
    std::printf("    [banc vibraphone] creusée : 880 Hz %.5f contre 611 Hz %.5f ; libre : 880 Hz %.5f contre 611 Hz %.5f\n",
                a880, a611, b880, b611);
    VSM_ASSERT(a880 > a611 * 5.0);
    VSM_ASSERT(b611 > b880 * 5.0);
}

/// SECOND TRAIT : le moteur n'ondule QUE le fondamental. Le tube est accordé
/// sur f0 ; le 4·f0 ne passe pas par lui. On compare, fenêtre par fenêtre,
/// la même note moteur ouvert et moteur tournant : le rapport ondule à f0,
/// et reste plat à 4·f0.
VSM_TEST(vibraphone_motor_modulates_fundamental_not_upper_partials) {
    auto tourne = makeVibes();
    auto ouvert = makeVibes();
    for (auto* s : {tourne.get(), ouvert.get()}) {
        set(*s, "Resonator Mix", 1.0f); set(*s, "Motor Speed", 4.0f); set(*s, "Mallet Hardness", 0.9f);
    }
    set(*tourne, "Motor Depth", 1.0f);
    set(*ouvert, "Motor Depth", 0.0f);
    auto a = rendre(*tourne, 69, 110, 0, 96000);
    auto b = rendre(*ouvert, 69, 110, 0, 96000);
    // Fenêtres de 25 ms sur une période entière du moteur (250 ms), après
    // que le tube s'est établi.
    double minF0 = 1e9, maxF0 = 0.0, min4 = 1e9, max4 = 0.0;
    for (size_t debut = 24000; debut < 24000 + 12000; debut += 1200) {
        const double r0 = magnitudeAt(a, debut, 1200, 440.0) / std::max(1e-12, magnitudeAt(b, debut, 1200, 440.0));
        const double r4 = magnitudeAt(a, debut, 1200, 1760.0) / std::max(1e-12, magnitudeAt(b, debut, 1200, 1760.0));
        minF0 = std::min(minF0, r0); maxF0 = std::max(maxF0, r0);
        min4 = std::min(min4, r4); max4 = std::max(max4, r4);
    }
    const double profondeurF0 = 1.0 - minF0 / maxF0;
    const double profondeur4 = 1.0 - min4 / max4;
    std::printf("    [banc vibraphone] profondeur d'ondulation : f0 %.3f, 4·f0 %.3f\n", profondeurF0, profondeur4);
    VSM_ASSERT(profondeurF0 > 0.25);
    VSM_ASSERT(profondeur4 < 0.05);
    VSM_ASSERT(profondeurF0 > profondeur4 * 5.0);
}

/// Sans moteur, rien n'ondule : le disque est ouvert, la note décroît sans
/// autre mouvement que sa propre extinction.
VSM_TEST(vibraphone_motor_off_leaves_the_note_steady) {
    auto synth = makeVibes();
    set(*synth, "Motor Depth", 0.0f);
    set(*synth, "Resonator Mix", 1.0f);
    auto out = rendre(*synth, 69, 110, 0, 96000);
    double lo = 1e9, hi = 0.0;
    for (size_t debut = 24000; debut < 36000; debut += 1200) {
        const double m = magnitudeAt(out, debut, 1200, 440.0);
        lo = std::min(lo, m); hi = std::max(hi, m);
    }
    // Sur 250 ms, un T60 de 6 s ne perd que 2,5 dB : le rapport reste > 0,7.
    VSM_ASSERT(lo / hi > 0.7);
}

/// TROISIÈME TRAIT : la pédale. Touche lâchée sans pédale, le feutre tait
/// la barre en un quart de seconde ; pédale enfoncée, elle tient.
VSM_TEST(vibraphone_pedal_lifts_the_damper) {
    auto sans = makeVibes();
    auto avec = makeVibes();
    set(*sans, "Motor Depth", 0.0f);
    set(*avec, "Motor Depth", 0.0f);
    pedale(*avec, true);
    auto a = rendre(*sans, 69, 100, 4800, 96000);   // lâchée à 0,1 s
    auto b = rendre(*avec, 69, 100, 4800, 96000);
    const double tardSans = rmsOf(a, 48000, 4800) / std::max(1e-9, rmsOf(a, 0, 2400));
    const double tardAvec = rmsOf(b, 48000, 4800) / std::max(1e-9, rmsOf(b, 0, 2400));
    std::printf("    [banc vibraphone] tenue à 1 s après une touche lâchée à 0,1 s : sans pédale %.5f, avec %.5f (x%.0f)\n",
                tardSans, tardAvec, tardAvec / std::max(1e-9, tardSans));
    VSM_ASSERT(tardAvec > tardSans * 20.0);
}

/// Une touche TENUE vaut la pédale : la baguette posée n'étouffe rien.
VSM_TEST(vibraphone_held_key_rings_like_the_pedal) {
    auto tenue = makeVibes();
    auto pedalee = makeVibes();
    set(*tenue, "Motor Depth", 0.0f);
    set(*pedalee, "Motor Depth", 0.0f);
    pedale(*pedalee, true);
    auto a = rendre(*tenue, 69, 100, 0, 96000);
    auto b = rendre(*pedalee, 69, 100, 4800, 96000);
    const double ra = rmsOf(a, 48000, 4800), rb = rmsOf(b, 48000, 4800);
    VSM_ASSERT(ra > 0.0 && rb > 0.0);
    VSM_ASSERT(std::abs(ra - rb) / std::max(ra, rb) < 0.05);
}

VSM_TEST(vibraphone_velocity_matters_and_opens_the_timbre) {
    auto douce = makeVibes();
    auto forte = makeVibes();
    for (auto* s : {douce.get(), forte.get()}) set(*s, "Motor Depth", 0.0f);
    auto a = rendre(*douce, 57, 30, 0, 24000);
    auto b = rendre(*forte, 57, 120, 0, 24000);
    VSM_ASSERT(rmsOf(b, 0, 9600) > rmsOf(a, 0, 9600) * 1.5);
    // Frapper fort réveille le 4·f0 plus que le fondamental.
    const double partA = pic(a, 1200, 8192, 870.0, 890.0) / std::max(1e-12, magnitudeAt(a, 1200, 8192, 220.0));
    const double partB = pic(b, 1200, 8192, 870.0, 890.0) / std::max(1e-12, magnitudeAt(b, 1200, 8192, 220.0));
    VSM_ASSERT(partB > partA * 1.3);
}

VSM_TEST(vibraphone_is_deterministic) {
    auto a = makeVibes();
    auto b = makeVibes();
    VSM_ASSERT(rendre(*a, 62, 90, 12000, 24000) == rendre(*b, 62, 90, 12000, 24000));
}

VSM_TEST(vibraphone_save_load_roundtrip) {
    auto synth = makeVibes();
    set(*synth, "Motor Speed", 7.5f);
    set(*synth, "Bar Undercut", 0.3f);
    auto state = synth->saveState();
    auto other = makeVibes();
    other->loadState(state);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Motor Speed")), 7.5f, 1e-4f);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Bar Undercut")), 0.3f, 1e-6f);
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.vibraphone"));
}

VSM_TEST(vibraphone_parameter_list_size) {
    auto synth = makeVibes();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t{12});
}

/// Une barre n'a pas de molette : refusée en le disant. La pédale (CC 64),
/// elle, est honorée.
VSM_TEST(vibraphone_refuses_pitch_bend_and_honours_sustain_pedal) {
    auto synth = makeVibes();
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 2.0f;
    VSM_ASSERT(!synth->handleControlEvent(bend));
    MidiControlEvent cc;
    cc.kind = MidiControlEvent::Kind::ControlChange;
    cc.index = 64;
    cc.value = 1.0f;
    VSM_ASSERT(synth->handleControlEvent(cc));
}
