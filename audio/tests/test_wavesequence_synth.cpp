#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.wavesequence` — le timbre est une SÉQUENCE : huit pas, un fondu, une
// boucle, la remise à la note ou la course libre (H35, écrite avant sa mesure,
// CDC machines-manquantes § 31).

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kPas = 9600;   // 200 ms

SynthPluginPtr makeSeq(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.wavesequence");
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
/// Le centroïde spectral en RANG d'harmonique, sur les quarante premiers
/// harmoniques de la2 (110 Hz) : c'est ce qui dit « clair » ou « sombre ».
double centroide(const std::vector<float>& x, size_t from, size_t count) {
    double num = 0.0, den = 0.0;
    for (int k = 1; k <= 40; ++k) {
        const double m = magnitudeAt(x, from, count, 110.0 * k);
        num += k * m; den += m;
    }
    return den > 1e-12 ? num / den : 0.0;
}
/// Rend `frames` échantillons ; la note part à `departNote`, dure `tenue`
/// (0 = jamais relâchée).
std::vector<float> rendre(ISynthPlugin& synth, uint8_t note, uint8_t velocity, int departNote, int tenue, int frames) {
    std::vector<float> left(static_cast<size_t>(frames), 0.0f), right(static_cast<size_t>(frames), 0.0f);
    constexpr int kBlock = 256;
    for (int start = 0; start + kBlock <= frames; start += kBlock) {
        std::vector<MidiNoteEvent> block;
        if (start <= departNote && departNote < start + kBlock) block.push_back(noteOn(departNote - start, note, velocity));
        const int fin = departNote + tenue;
        if (tenue > 0 && start <= fin && fin < start + kBlock) block.push_back(noteOff(fin - start, note));
        synth.process(block.empty() ? nullptr : block.data(), static_cast<int>(block.size()),
                      left.data() + start, right.data() + start, kBlock);
    }
    return left;
}
/// Une séquence AB : les pas impairs sur A, les pairs sur B.
void alterne(ISynthPlugin& s, float a, float b) {
    for (int i = 1; i <= 8; ++i) set(s, "Step " + std::to_string(i) + " Wave", i % 2 == 1 ? a : b);
    set(s, "Step Time", 200.0f);
    set(s, "Crossfade", 0.0f);
    set(s, "Filter Cutoff", 18000.0f);
    set(s, "Attack", 0.001f);
    set(s, "Sustain", 1.0f);
}
constexpr float kA = 0.05f, kB = 3.95f;   // les deux extrêmes de la banque

} // namespace

VSM_TEST(wavesequence_registered) {
    auto synth = makeSeq();
    VSM_ASSERT(synth != nullptr);
    VSM_ASSERT_EQ(std::string(synth->machineName()), std::string("Wave Sequence (le timbre est une séquence)"));
}

VSM_TEST(wavesequence_silent_with_no_events) {
    auto synth = makeSeq();
    std::vector<float> l(2048, 1.0f), r(2048, 1.0f);
    synth->process(nullptr, 0, l.data(), r.data(), 2048);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0f, 1e-9f);
}

VSM_TEST(wavesequence_note_produces_sound_and_stays_finite) {
    auto synth = makeSeq();
    auto out = rendre(*synth, 57, 100, 0, 0, 48000);
    VSM_ASSERT(peakAbs(out) > 0.01f);
    for (float v : out) VSM_ASSERT(std::isfinite(v));
    VSM_ASSERT(peakAbs(out) < 3.0f);
}

/// LE TRAIT DISTINCTIF, PREMIER : LE TIMBRE EST UNE SÉQUENCE. Pas impairs sur
/// A, pairs sur B : les centroïdes alternent, et le motif se répète.
VSM_TEST(wavesequence_timbre_is_a_sequence_that_repeats) {
    auto synth = makeSeq();
    alterne(*synth, kA, kB);
    auto out = rendre(*synth, 45, 100, 0, 0, kPas * 17);
    std::vector<double> c;
    // Seize fenêtres d'un pas, la première écartée (l'attaque).
    for (int i = 1; i <= 16; ++i) c.push_back(centroide(out, static_cast<size_t>(i * kPas) + 480, static_cast<size_t>(kPas) - 960));
    double impairMin = 1e9, impairMax = 0.0, pairMin = 1e9, pairMax = 0.0;
    std::printf("    [banc séquence] centroïdes par pas :");
    for (size_t i = 0; i < c.size(); ++i) {
        std::printf(" %.2f", c[i]);
        // La fenêtre i (0-based) est le pas (i+1) mod 8 : i pair → pas impair… le
        // premier pas écarté est le pas 1 ; la fenêtre 0 ici est le pas 2 (B).
        if (i % 2 == 0) { pairMin = std::min(pairMin, c[i]); pairMax = std::max(pairMax, c[i]); }
        else { impairMin = std::min(impairMin, c[i]); impairMax = std::max(impairMax, c[i]); }
    }
    std::printf("\n    [banc séquence] A (pas impairs) %.2f-%.2f ; B (pas pairs) %.2f-%.2f\n", impairMin, impairMax, pairMin, pairMax);
    VSM_ASSERT(impairMax <= impairMin * 1.10);
    VSM_ASSERT(pairMax <= pairMin * 1.10);
    const double rapport = pairMin / std::max(1e-9, impairMax);
    VSM_ASSERT(rapport >= 1.5 || rapport <= 1.0 / 1.5);
}

/// SECOND TRAIT : LE FONDU ENTRE DEUX PAS. Sans fondu, le franchissement est
/// un saut d'échantillon ; avec, il ne l'est plus (trois fois plus petit, et
/// sous 0,1).
VSM_TEST(wavesequence_crossfade_removes_the_click_at_step_boundaries) {
    auto dur = makeSeq();
    auto doux = makeSeq();
    alterne(*dur, kA, kB);
    alterne(*doux, kA, kB);
    set(*doux, "Crossfade", 0.5f);
    // LA NOTE NE DIVISE PAS LE PAS. Sur la2 (110 Hz), un pas de 200 ms fait
    // vingt-deux cycles tout rond : chaque franchissement tombe à la phase
    // zéro, où la plupart des formes passent par zéro, et le saut ne se voit
    // pas (mesuré : 0,088 sans fondu contre 0,096 avec). Sur si♭2 (116,5 Hz)
    // le pas fait 23,3 cycles : les franchissements balaient les phases.
    auto a = rendre(*dur, 46, 100, 0, 0, kPas * 9);
    auto b = rendre(*doux, 46, 100, 0, 0, kPas * 9);
    auto sautMax = [](const std::vector<float>& x) {
        double m = 0.0;
        for (int p = 1; p <= 8; ++p)
            for (int d = -2; d <= 2; ++d) {
                const auto i = static_cast<size_t>(p * kPas + d);
                m = std::max(m, static_cast<double>(std::abs(x[i] - x[i - 1])));
            }
        return m;
    };
    const double sd = sautMax(a), sx = sautMax(b);
    std::printf("    [banc séquence] plus grand saut au franchissement d'un pas : sans fondu %.4f, avec %.4f\n", sd, sx);
    VSM_ASSERT(sx * 3.0 <= sd);
    VSM_ASSERT(sx < 0.1);
}

/// TROISIÈME TRAIT : LA BOUCLE. Retour au pas 5 : après le premier passage,
/// seuls 5 à 8 se répètent.
VSM_TEST(wavesequence_loops_from_the_loop_start) {
    auto synth = makeSeq();
    for (int i = 1; i <= 8; ++i) set(*synth, "Step " + std::to_string(i) + " Wave", i <= 4 ? kA : kB);
    set(*synth, "Step Time", 200.0f); set(*synth, "Crossfade", 0.0f);
    set(*synth, "Filter Cutoff", 18000.0f); set(*synth, "Attack", 0.001f); set(*synth, "Sustain", 1.0f);
    set(*synth, "Loop Start", 5.0f);
    auto out = rendre(*synth, 45, 100, 0, 0, kPas * 13);
    const double cA = centroide(out, static_cast<size_t>(kPas) + 480, static_cast<size_t>(kPas) - 960);      // pas 2 : A
    const double cB = centroide(out, static_cast<size_t>(5 * kPas) + 480, static_cast<size_t>(kPas) - 960);  // pas 6 : B
    std::printf("    [banc séquence] boucle : A %.2f, B %.2f ; fenêtres 9-12 :", cA, cB);
    for (int i = 8; i < 12; ++i) {
        const double c = centroide(out, static_cast<size_t>(i * kPas) + 480, static_cast<size_t>(kPas) - 960);
        std::printf(" %.2f", c);
        VSM_ASSERT(std::abs(c - cB) < std::abs(c - cA));
    }
    std::printf("\n");
}

/// QUATRIÈME TRAIT : REMISE À LA NOTE OU COURSE LIBRE. Pas 1 en A, les autres
/// en B, une note trois pas après le départ de l'horloge : remise, elle
/// commence en A ; libre, en B.
VSM_TEST(wavesequence_key_restart_or_free_running_clock) {
    for (float restart : {1.0f, 0.0f}) {
        auto synth = makeSeq();
        for (int i = 1; i <= 8; ++i) set(*synth, "Step " + std::to_string(i) + " Wave", i == 1 ? kA : kB);
        set(*synth, "Step Time", 200.0f); set(*synth, "Crossfade", 0.0f);
        set(*synth, "Filter Cutoff", 18000.0f); set(*synth, "Attack", 0.001f); set(*synth, "Sustain", 1.0f);
        set(*synth, "Key Restart", restart);
        auto out = rendre(*synth, 45, 100, 3 * kPas, 0, kPas * 6);
        const double premiere = centroide(out, static_cast<size_t>(3 * kPas) + 480, static_cast<size_t>(kPas) - 960);
        const double suivante = centroide(out, static_cast<size_t>(4 * kPas) + 480, static_cast<size_t>(kPas) - 960);
        std::printf("    [banc séquence] %s : première fenêtre %.2f, suivante %.2f\n",
                    restart > 0.5f ? "remise à la note" : "course libre", premiere, suivante);
        if (restart > 0.5f) VSM_ASSERT(premiere / suivante >= 1.5 || premiere / suivante <= 1.0 / 1.5);
        else VSM_ASSERT(std::abs(premiere - suivante) <= 0.10 * std::max(premiere, suivante));
    }
}

VSM_TEST(wavesequence_honours_pitch_bend_and_velocity) {
    auto synth = makeSeq();
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 12.0f;
    VSM_ASSERT(synth->handleControlEvent(bend));
    auto haut = rendre(*synth, 45, 100, 0, 0, 24000);
    auto ref = makeSeq();
    auto bas = rendre(*ref, 45, 100, 0, 0, 24000);
    VSM_ASSERT(magnitudeAt(haut, 2400, 16384, 220.0) > magnitudeAt(haut, 2400, 16384, 110.0));
    VSM_ASSERT(magnitudeAt(bas, 2400, 16384, 110.0) > magnitudeAt(bas, 2400, 16384, 220.0) * 0.5);
    auto douce = makeSeq(); auto forte = makeSeq();
    auto a = rendre(*douce, 45, 30, 0, 0, 24000), b = rendre(*forte, 45, 120, 0, 0, 24000);
    VSM_ASSERT(rmsOf(b, 0, 9600) > rmsOf(a, 0, 9600) * 1.5);
}

VSM_TEST(wavesequence_is_deterministic) {
    auto a = makeSeq(); auto b = makeSeq();
    VSM_ASSERT(rendre(*a, 50, 90, 0, 12000, 24000) == rendre(*b, 50, 90, 0, 12000, 24000));
}

VSM_TEST(wavesequence_save_load_roundtrip) {
    auto synth = makeSeq();
    set(*synth, "Step 3 Wave", 2.5f);
    set(*synth, "Step Time", 333.0f);
    auto state = synth->saveState();
    auto other = makeSeq();
    other->loadState(state);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Step 3 Wave")), 2.5f, 1e-6f);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Step Time")), 333.0f, 1e-3f);
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.wavesequence"));
}

VSM_TEST(wavesequence_parameter_list_size) {
    auto synth = makeSeq();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t{20});
}
