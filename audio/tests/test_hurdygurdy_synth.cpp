#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.hurdygurdy` — la vielle à roue : la roue est un archet qui ne finit
// pas, les bourdons sonnent tant qu'elle tourne, et la vélocité n'est pas la
// force mais le coup de poignet qui fait claquer le chien (H27, écrite avant
// sa mesure).

namespace {

constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeVielle(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.hurdygurdy");
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

/// Une note tenue `tenue` échantillons (0 = jamais relâchée), sur `frames`.
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

VSM_TEST(hurdygurdy_registered) {
    auto synth = makeVielle();
    VSM_ASSERT(synth != nullptr);
    VSM_ASSERT_EQ(std::string(synth->machineName()), std::string("Hurdy-Gurdy (la vielle à roue)"));
}

VSM_TEST(hurdygurdy_silent_with_no_events) {
    auto synth = makeVielle();
    std::vector<float> l(4096, 1.0f), r(4096, 1.0f);
    synth->process(nullptr, 0, l.data(), r.data(), 4096);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0f, 1e-9f);
}

VSM_TEST(hurdygurdy_note_produces_sound_and_stays_finite) {
    auto synth = makeVielle();
    auto out = rendre(*synth, 62, 100, 72000, 96000);
    VSM_ASSERT(peakAbs(out) > 0.01f);
    for (float v : out) VSM_ASSERT(std::isfinite(v));
    VSM_ASSERT(peakAbs(out) < 3.0f);
}

/// LA ROUE NE FINIT PAS : une note tenue garde son niveau. À trois secondes,
/// la chanterelle sonne comme à une -- un archet, pas un pincement.
VSM_TEST(hurdygurdy_held_note_does_not_decay) {
    auto synth = makeVielle();
    set(*synth, "Drones", 0.0f);
    set(*synth, "Chien", 0.0f);
    auto out = rendre(*synth, 62, 100, 0, 192000);
    const double a1s = rmsOf(out, 48000, 9600);
    const double a3s = rmsOf(out, 144000, 9600);
    std::printf("    [banc vielle] chanterelle tenue : RMS à 1 s %.5f, à 3 s %.5f (%.1f dB)\n",
                a1s, a3s, 20.0 * std::log10(std::max(1e-9, a3s) / std::max(1e-9, a1s)));
    VSM_ASSERT(a1s > 0.005);
    VSM_ASSERT(std::abs(20.0 * std::log10(std::max(1e-9, a3s) / std::max(1e-9, a1s))) < 3.0);
}

/// LA VÉLOCITÉ NE FAIT PAS LA FORCE. Chien levé, deux vélocités extrêmes
/// donnent la même chanterelle au bit près.
VSM_TEST(hurdygurdy_velocity_does_not_change_the_melody_string) {
    auto douce = makeVielle();
    auto forte = makeVielle();
    for (auto* s : {douce.get(), forte.get()}) { set(*s, "Chien", 0.0f); }
    auto a = rendre(*douce, 62, 15, 0, 48000);
    auto b = rendre(*forte, 62, 127, 0, 48000);
    VSM_ASSERT(peakAbs(a) > 0.005f);
    VSM_ASSERT(a == b);
}

/// LE CHIEN CLAQUE AU COUP DE POIGNET : à vélocité forte, l'attaque porte
/// bien plus d'énergie de claquement (une modulation à la fréquence du chien)
/// qu'à vélocité faible -- et c'est LA différence que la vélocité fait ici.
VSM_TEST(hurdygurdy_velocity_drives_the_chien) {
    auto douce = makeVielle();
    auto forte = makeVielle();
    for (auto* s : {douce.get(), forte.get()}) { set(*s, "Chien", 1.0f); set(*s, "Chien Buzz", 60.0f); }
    auto a = rendre(*douce, 62, 15, 0, 48000);
    auto b = rendre(*forte, 62, 127, 0, 48000);
    // La modulation du chien à 60 Hz crée des bandes latérales : on mesure
    // l'énergie de la trompette autour de sa fondamentale ± 60 Hz sur les
    // 150 ms qui suivent l'attaque, contre une note sans coup.
    const double tromp = midiToHz(36) * 2.0;
    auto bandes = [&](const std::vector<float>& x) {
        return magnitudeAt(x, 2400, 7200, tromp + 60.0) + magnitudeAt(x, 2400, 7200, tromp - 60.0);
    };
    const double faible = bandes(a), fort = bandes(b);
    std::printf("    [banc vielle] chien : bandes latérales vél. 15 = %.6f, vél. 127 = %.6f (x%.1f)\n",
                faible, fort, fort / std::max(1e-9, faible));
    VSM_ASSERT(fort > faible * 2.0);
}

/// LES BOURDONS SONNENT TANT QUE LA ROUE TOURNE, PAS TANT QU'UNE TOUCHE EST
/// ENFONCÉE : après le relâchement, ils continuent le temps de l'inertie,
/// puis s'éteignent avec la roue.
VSM_TEST(hurdygurdy_drones_follow_the_wheel_and_its_inertia) {
    auto synth = makeVielle();
    set(*synth, "Drones", 1.0f);
    set(*synth, "Chien", 0.0f);
    set(*synth, "Wheel Inertia", 0.4f);
    const int tenue = 48000;
    auto out = rendre(*synth, 62, 100, tenue, 144000);
    const double f0 = midiToHz(36);             // le gros bourdon, à la tonique
    const double tenu = magnitudeAt(out, 24000, 16384, f0);
    const double juste_apres = magnitudeAt(out, static_cast<size_t>(tenue) + 2400, 16384, f0);  // 50 ms après
    const double bien_apres = magnitudeAt(out, static_cast<size_t>(tenue) + 96000, 16384, f0);  // 2 s après
    std::printf("    [banc vielle] bourdon grave : tenu %.6f, 50 ms après le relâchement %.6f (x%.2f), 2 s après %.6f (x%.3f)\n",
                tenu, juste_apres, juste_apres / std::max(1e-9, tenu), bien_apres, bien_apres / std::max(1e-9, tenu));
    std::printf("    [banc vielle] bourdon grave tenu : f0 %.6f, 2f0 %.6f, 3f0 %.6f\n", tenu,
                magnitudeAt(out, 24000, 16384, 2.0 * f0), magnitudeAt(out, 24000, 16384, 3.0 * f0));
    VSM_ASSERT(tenu > 1e-4);
    VSM_ASSERT(juste_apres > tenu * 0.3);
    VSM_ASSERT(bien_apres < tenu * 0.05);
}

/// Bourdons levés (« Drones » à zéro), il ne reste que la chanterelle : rien
/// ne sonne au bourdon.
VSM_TEST(hurdygurdy_drones_can_be_lifted) {
    auto avec = makeVielle();
    auto sans = makeVielle();
    set(*avec, "Drones", 1.0f); set(*avec, "Chien", 0.0f);
    set(*sans, "Drones", 0.0f); set(*sans, "Chien", 0.0f);
    auto a = rendre(*avec, 69, 100, 0, 48000);
    auto b = rendre(*sans, 69, 100, 0, 48000);
    const double f0 = midiToHz(36);
    VSM_ASSERT(magnitudeAt(b, 24000, 16384, f0) < magnitudeAt(a, 24000, 16384, f0) * 0.1);
}

VSM_TEST(hurdygurdy_is_deterministic) {
    auto a = makeVielle();
    auto b = makeVielle();
    VSM_ASSERT(rendre(*a, 64, 90, 12000, 24000) == rendre(*b, 64, 90, 12000, 24000));
}

VSM_TEST(hurdygurdy_save_load_roundtrip) {
    auto synth = makeVielle();
    set(*synth, "Drone Note", 43.0f);
    set(*synth, "Chien", 0.25f);
    auto state = synth->saveState();
    auto other = makeVielle();
    other->loadState(state);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Drone Note")), 43.0f, 1e-6f);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Chien")), 0.25f, 1e-6f);
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.hurdygurdy"));
}

VSM_TEST(hurdygurdy_parameter_list_size) {
    auto synth = makeVielle();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t{10});
}

VSM_TEST(hurdygurdy_refuses_pitch_bend_knowingly) {
    auto synth = makeVielle();
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 1.0f;
    VSM_ASSERT(!synth->handleControlEvent(bend));
}
