#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.banjo` — la corde pincée dont la table est une PEAU : la peau chante
// ses propres modes quelle que soit la note, et elle mange la corde (H28,
// écrite avant sa mesure).

namespace {

constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeBanjo(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.banjo");
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
/// La plus grande magnitude dans une bande, par balayage fin : un mode de
/// peau ne tombe pas exactement sur une case.
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

} // namespace

VSM_TEST(banjo_registered) {
    auto synth = makeBanjo();
    VSM_ASSERT(synth != nullptr);
    VSM_ASSERT_EQ(std::string(synth->machineName()), std::string("Banjo (la corde sur la peau)"));
}

VSM_TEST(banjo_silent_with_no_events) {
    auto synth = makeBanjo();
    std::vector<float> l(2048, 1.0f), r(2048, 1.0f);
    synth->process(nullptr, 0, l.data(), r.data(), 2048);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0f, 1e-9f);
}

VSM_TEST(banjo_note_produces_sound_and_stays_finite) {
    auto synth = makeBanjo();
    auto out = rendre(*synth, 57, 100, 24000, 48000);
    VSM_ASSERT(peakAbs(out) > 0.01f);
    for (float v : out) VSM_ASSERT(std::isfinite(v));
    VSM_ASSERT(peakAbs(out) < 3.0f);
}

/// LE TRAIT DISTINCTIF, PREMIER : la peau chante SES modes, quelle que soit
/// la note. Deux notes éloignées, une peau tendue à 300 Hz : le spectre porte
/// un pic autour de 300 Hz dans les deux cas, bien plus fort que sans peau.
VSM_TEST(banjo_head_sings_its_own_modes_whatever_the_note) {
    for (uint8_t note : {45, 69}) {
        auto avec = makeBanjo();
        auto sans = makeBanjo();
        for (auto* s : {avec.get(), sans.get()}) { set(*s, "Head Tension", 300.0f); set(*s, "Head Damping", 0.2f); }
        set(*avec, "Head Mix", 1.0f);
        set(*sans, "Head Mix", 0.0f);
        auto a = rendre(*avec, note, 100, 0, 48000);
        auto b = rendre(*sans, note, 100, 0, 48000);
        // Le mode fondamental de la peau, 300 Hz, rapporté au niveau global :
        // c'est la PART du mode dans le son qui compte, pas son absolu.
        const double picAvec = pic(a, 2400, 16384, 280.0, 320.0) / std::max(1e-9, rmsOf(a, 2400, 16384));
        const double picSans = pic(b, 2400, 16384, 280.0, 320.0) / std::max(1e-9, rmsOf(b, 2400, 16384));
        std::printf("    [banc banjo] note %d : part du mode de peau (300 Hz) avec %.4f, sans %.4f (x%.1f)\n",
                    note, picAvec, picSans, picAvec / std::max(1e-9, picSans));
        VSM_ASSERT(picAvec > picSans * 2.0);
    }
}

/// SECOND TRAIT : la peau MANGE la corde. À peau égale de mixage nul, la
/// corde tient plus longtemps qu'avec la peau qui prend son énergie.
VSM_TEST(banjo_head_shortens_the_string) {
    auto avec = makeBanjo();
    auto sans = makeBanjo();
    set(*avec, "Head Mix", 1.0f);
    set(*sans, "Head Mix", 0.0f);
    auto a = rendre(*avec, 57, 100, 0, 96000);
    auto b = rendre(*sans, 57, 100, 0, 96000);
    const double tardAvec = rmsOf(a, 48000, 9600) / std::max(1e-9, rmsOf(a, 0, 4800));
    const double tardSans = rmsOf(b, 48000, 9600) / std::max(1e-9, rmsOf(b, 0, 4800));
    std::printf("    [banc banjo] tenue à 1 s (relative à l'attaque) : avec peau %.4f, sans %.4f (x%.2f)\n",
                tardAvec, tardSans, tardAvec / std::max(1e-9, tardSans));
    VSM_ASSERT(tardAvec < tardSans * 0.6);
}

VSM_TEST(banjo_velocity_matters) {
    auto douce = makeBanjo();
    auto forte = makeBanjo();
    auto a = rendre(*douce, 57, 30, 0, 24000);
    auto b = rendre(*forte, 57, 120, 0, 24000);
    VSM_ASSERT(rmsOf(b, 0, 9600) > rmsOf(a, 0, 9600) * 1.5);
}

VSM_TEST(banjo_is_deterministic) {
    auto a = makeBanjo();
    auto b = makeBanjo();
    VSM_ASSERT(rendre(*a, 62, 90, 12000, 24000) == rendre(*b, 62, 90, 12000, 24000));
}

VSM_TEST(banjo_save_load_roundtrip) {
    auto synth = makeBanjo();
    set(*synth, "Head Tension", 420.0f);
    set(*synth, "Head Mix", 0.3f);
    auto state = synth->saveState();
    auto other = makeBanjo();
    other->loadState(state);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Head Tension")), 420.0f, 1e-4f);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Head Mix")), 0.3f, 1e-6f);
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.banjo"));
}

VSM_TEST(banjo_parameter_list_size) {
    auto synth = makeBanjo();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t{10});
}

VSM_TEST(banjo_honours_pitch_bend) {
    auto synth = makeBanjo();
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 2.0f;
    VSM_ASSERT(synth->handleControlEvent(bend));
}
