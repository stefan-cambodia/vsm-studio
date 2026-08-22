#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

namespace {
constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeCone(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.cone");
    if (plugin) plugin->initialize(sr, 512);
    return plugin;
}
ParamId byName(const ISynthPlugin& plugin, const std::string& name) {
    for (const auto& info : plugin.parameterList()) if (info.name == name) return info.id;
    return 0;
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
float windowPeak(const std::vector<float>& x, size_t from, size_t count) {
    float p = 0.0f;
    for (size_t i = from; i < from + count && i < x.size(); ++i) p = std::max(p, std::abs(x[i]));
    return p;
}
std::vector<float> render(SynthPluginPtr& synth, const std::vector<MidiNoteEvent>& events, int frames) {
    std::vector<float> left(static_cast<size_t>(frames), 0.0f), right(static_cast<size_t>(frames), 0.0f);
    constexpr int kBlock = 256;
    for (int start = 0; start < frames; start += kBlock) {
        const int count = std::min(kBlock, frames - start);
        std::vector<MidiNoteEvent> block;
        for (const auto& e : events)
            if (e.sampleOffset >= start && e.sampleOffset < start + count)
                block.push_back({e.kind, e.sampleOffset - start, e.channel, e.note, e.velocity});
        synth->process(block.empty() ? nullptr : block.data(), static_cast<int>(block.size()),
                       left.data() + start, right.data() + start, count);
    }
    return left;
}
float midiToHz(int note) { return 440.0f * std::exp2f((static_cast<float>(note) - 69.0f) / 12.0f); }

double magnitudeAt(const std::vector<float>& x, size_t from, size_t count, double hz) {
    double re = 0.0, im = 0.0, norm = 0.0;
    for (size_t i = 0; i < count && from + i < x.size(); ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / static_cast<double>(count));
        const double phase = 2.0 * M_PI * hz * static_cast<double>(i) / kSampleRate;
        re += w * static_cast<double>(x[from + i]) * std::cos(phase);
        im += w * static_cast<double>(x[from + i]) * std::sin(phase);
        norm += w;
    }
    return std::sqrt(re * re + im * im) / std::max(1.0, norm);
}

/// Réglage de base des mesures de timbre : ni dérive, ni vibrato, ni souffle --
/// on veut mesurer la PERCE, pas ce qui l'anime.
void steady(ISynthPlugin& plugin) {
    set(plugin, "Analog Character", 0.0f);
    set(plugin, "Vibrato Depth", 0.0f);
    set(plugin, "Breath Noise", 0.0f);
    set(plugin, "Tone Bass", 0.0f);
    set(plugin, "Tone Treble", 0.0f);
}
} // namespace

VSM_TEST(cone_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.cone"));
}

VSM_TEST(cone_silent_with_no_events) {
    auto synth = makeCone();
    const auto audio = render(synth, {}, 4096);
    VSM_ASSERT(peakAbs(audio) < 1e-6f);
}

VSM_TEST(cone_note_produces_sound) {
    auto synth = makeCone();
    steady(*synth);
    const auto audio = render(synth, {noteOn(0, 57, 110)}, 32768);
    VSM_ASSERT(peakAbs(audio) > 0.02f);
}

VSM_TEST(cone_bore_carries_even_harmonics) {
    // LE TRAIT DISTINCTIF, et la raison d'être de la machine. Une perce
    // conique porte la série harmonique COMPLÈTE ; c'est pour cela qu'un
    // saxophone octavie là où la clarinette saute à la douzième. `vsm.wind` ne
    // peut structurellement pas le faire : sa boucle inversante à
    // demi-longueur impose `x(t + T/2) = -x(t)`, qui annule les rangs pairs
    // (voir `wind_bore_supports_only_odd_harmonics`, qui vérifie l'inverse).
    //
    // Ce test est donc l'exact miroir de celui-là, et c'est lui qui justifie
    // qu'une machine de plus existe.
    auto synth = makeCone();
    steady(*synth);
    set(*synth, "Cone Taper", 1.0f);
    set(*synth, "Breath Pressure", 0.85f);
    set(*synth, "Reed Stiffness", 0.30f);
    set(*synth, "Bore Decay", 0.25f);
    set(*synth, "Attack", 0.01f);

    const int note = 57;
    const double f0 = midiToHz(note);
    const auto audio = render(synth, {noteOn(0, static_cast<uint8_t>(note), 110)}, 65536);

    const double h1 = magnitudeAt(audio, 32000, 16384, f0);
    const double h2 = magnitudeAt(audio, 32000, 16384, 2.0 * f0);
    const double h3 = magnitudeAt(audio, 32000, 16384, 3.0 * f0);

    VSM_ASSERT(h1 > 0.005);            // ça sonne vraiment
    VSM_ASSERT(h2 > h1 * 0.10);        // la 2e est PRÉSENTE -- l'inverse du cylindre
    VSM_ASSERT(h2 > h3 * 0.20);        // et elle n'est pas un résidu devant la 3e
}

VSM_TEST(cone_taper_moves_even_harmonics) {
    // UN RÉGLAGE QUI NE FAIT RIEN EST PIRE QU'UN RÉGLAGE ABSENT : `Bore Shape`
    // a été retiré de `vsm.wind` pour cela (§ 33 d'ARCHITECTURE.md). Celui-ci
    // est donc mesuré sur toute sa course, et il doit DÉPLACER les rangs pairs.
    const int note = 57;
    const double f0 = midiToHz(note);

    auto mesurer = [&](float taper) {
        auto synth = makeCone();
        steady(*synth);
        set(*synth, "Cone Taper", taper);
        set(*synth, "Breath Pressure", 0.85f);
        set(*synth, "Reed Stiffness", 0.30f);
        set(*synth, "Bore Decay", 0.25f);
        set(*synth, "Attack", 0.01f);
        const auto audio = render(synth, {noteOn(0, static_cast<uint8_t>(note), 110)}, 65536);
        const double h1 = magnitudeAt(audio, 32000, 16384, f0);
        const double h2 = magnitudeAt(audio, 32000, 16384, 2.0 * f0);
        return h2 / std::max(1e-9, h1);
    };

    const double plein = mesurer(1.0f);
    const double tronque = mesurer(0.0f);
    VSM_ASSERT(plein > tronque * 2.0);   // le réglage agit, et dans le bon sens
}

VSM_TEST(cone_oscillation_needs_breath) {
    // L'oscillation naît de la boucle anche/tuyau : sans souffle, rien. Ce
    // n'est pas un oscillateur qu'on module par une enveloppe.
    auto synth = makeCone();
    steady(*synth);
    set(*synth, "Breath Pressure", 0.0f);
    const auto audio = render(synth, {noteOn(0, 57, 110)}, 32768);
    VSM_ASSERT(peakAbs(audio) < 0.01f);
}

VSM_TEST(cone_blowing_harder_opens_the_timbre) {
    // La vélocité doit changer le TIMBRE et pas seulement le niveau : c'est la
    // loi expressive d'un instrument à anche, et elle sort de la physique du
    // modèle, pas d'une enveloppe de filtre.
    const int note = 57;
    const double f0 = midiToHz(note);
    auto rapport = [&](uint8_t velocity) {
        auto synth = makeCone();
        steady(*synth);
        set(*synth, "Velocity Sensitivity", 1.0f);
        set(*synth, "Attack", 0.01f);
        const auto audio = render(synth, {noteOn(0, static_cast<uint8_t>(note), velocity)}, 65536);
        const double h1 = magnitudeAt(audio, 32000, 16384, f0);
        const double h3 = magnitudeAt(audio, 32000, 16384, 3.0 * f0);
        return h3 / std::max(1e-9, h1);
    };
    VSM_ASSERT(rapport(127) > rapport(50));
}

VSM_TEST(cone_release_lets_the_bore_empty) {
    auto synth = makeCone();
    steady(*synth);
    set(*synth, "Release", 0.05f);
    const auto audio = render(synth, {noteOn(0, 57, 110), noteOff(16000, 57)}, 65536);
    const float pendant = windowPeak(audio, 8000, 4000);
    const float apres = windowPeak(audio, 48000, 4000);
    VSM_ASSERT(pendant > 0.02f);
    VSM_ASSERT(apres < pendant * 0.1f);
}

VSM_TEST(cone_is_polyphonic_as_a_section) {
    auto synth = makeCone();
    steady(*synth);
    const auto un = render(synth, {noteOn(0, 57, 100)}, 32768);
    auto synth2 = makeCone();
    steady(*synth2);
    const auto trois = render(synth2, {noteOn(0, 57, 100), noteOn(0, 61, 100), noteOn(0, 64, 100)}, 32768);
    VSM_ASSERT(peakAbs(trois) > peakAbs(un) * 1.2f);
}

VSM_TEST(cone_a_full_section_does_not_clip) {
    auto synth = makeCone();
    steady(*synth);
    set(*synth, "Breath Pressure", 1.0f);
    const auto audio = render(synth, {noteOn(0, 45, 127), noteOn(0, 52, 127),
                                      noteOn(0, 57, 127), noteOn(0, 61, 127)}, 65536);
    VSM_ASSERT(peakAbs(audio) < 1.0f);
}

VSM_TEST(cone_stays_finite_under_extreme_settings) {
    // Le banc modal est stable par construction, mais la boucle passe par une
    // non-linéarité : on le VÉRIFIE aux bornes plutôt que de le supposer.
    for (float stiffness : {0.0f, 1.0f}) {
        for (float breath : {0.0f, 1.0f}) {
            for (float decay : {0.02f, 0.60f}) {
                auto synth = makeCone();
                set(*synth, "Reed Stiffness", stiffness);
                set(*synth, "Breath Pressure", breath);
                set(*synth, "Bore Decay", decay);
                set(*synth, "Radiation Damping", 0.0f);
                set(*synth, "Cone Taper", 1.0f);
                const auto audio = render(synth, {noteOn(0, 33, 127), noteOn(0, 96, 127)}, 32768);
                for (float v : audio) VSM_ASSERT(std::isfinite(v));
                VSM_ASSERT(peakAbs(audio) < 8.0f);
            }
        }
    }
}

VSM_TEST(cone_is_deterministic) {
    auto a = makeCone();
    auto b = makeCone();
    const auto x = render(a, {noteOn(0, 57, 100)}, 16384);
    const auto y = render(b, {noteOn(0, 57, 100)}, 16384);
    for (size_t i = 0; i < x.size(); ++i) VSM_ASSERT(x[i] == y[i]);
}

VSM_TEST(cone_save_load_roundtrip) {
    auto synth = makeCone();
    set(*synth, "Cone Taper", 0.42f);
    set(*synth, "Bore Decay", 0.31f);
    const auto state = synth->saveState();
    VSM_ASSERT(state.pluginTypeId == "vsm.cone");
    auto other = makeCone();
    other->loadState(state);
    VSM_ASSERT(std::abs(other->getParameter(byName(*other, "Cone Taper")) - 0.42f) < 1e-6f);
    VSM_ASSERT(std::abs(other->getParameter(byName(*other, "Bore Decay")) - 0.31f) < 1e-6f);
}

VSM_TEST(cone_parameter_list_size) {
    auto synth = makeCone();
    VSM_ASSERT(synth->parameterList().size() == 16);
}
