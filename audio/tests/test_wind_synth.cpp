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

SynthPluginPtr makeWind(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.wind");
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
double brightness(const std::vector<float>& b, size_t from, size_t count) {
    double energy = 0.0;
    for (size_t i = from + 1; i < from + count && i < b.size(); ++i) {
        const double d = static_cast<double>(b[i]) - b[i - 1];
        energy += d * d;
    }
    return energy;
}

/// Réglage de base commun aux mesures de timbre : pas de dérive, pas de
/// vibrato, pas de souffle -- on veut mesurer la PERCE, pas ce qui l'anime.
void steady(ISynthPlugin& plugin) {
    set(plugin, "Analog Character", 0.0f);
    set(plugin, "Vibrato Depth", 0.0f);
    set(plugin, "Breath Noise", 0.0f);
    set(plugin, "Brassiness", 0.0f);
    set(plugin, "Tone Bass", 0.0f);
    set(plugin, "Tone Treble", 0.0f);
}
} // namespace

VSM_TEST(wind_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.wind"));
}

VSM_TEST(wind_silent_with_no_events) {
    auto synth = makeWind();
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(wind_note_produces_sound) {
    auto synth = makeWind();
    const auto audio = render(synth, {noteOn(0, 60, 100)}, 24064);
    VSM_ASSERT(peakAbs(audio) > 0.01f);
    for (float v : audio) VSM_ASSERT(std::isfinite(v));
}

// --- Le trait distinctif : la PERCE décide des harmoniques -----------------

VSM_TEST(wind_bore_supports_only_odd_harmonics) {
    // LE trait de cette machine, et ce qui la sépare de tout le reste du parc :
    // un tuyau cylindrique FERMÉ du côté de la valve impose la symétrie
    // demi-onde `x(t + T/2) = -x(t)`, qui interdit les harmoniques PAIRES. Le
    // creux de la clarinette n'est pas un effet de filtre, c'est une
    // conséquence de la géométrie -- et c'est aussi, mesuré ici, la limite de
    // la machine : elle ne fera jamais un saxophone (voir l'en-tête).
    auto synth = makeWind();
    steady(*synth);
    set(*synth, "Bell Damping", 0.2f);
    set(*synth, "Breath Pressure", 0.8f);
    set(*synth, "Attack", 0.01f);

    const int note = 52;
    const double f0 = midiToHz(note);
    const auto audio = render(synth, {noteOn(0, static_cast<uint8_t>(note), 110)}, 65536);

    const double h1 = magnitudeAt(audio, 32000, 16384, f0);
    const double h2 = magnitudeAt(audio, 32000, 16384, 2.0 * f0);
    const double h3 = magnitudeAt(audio, 32000, 16384, 3.0 * f0);
    const double h4 = magnitudeAt(audio, 32000, 16384, 4.0 * f0);
    VSM_ASSERT(h1 > 0.02);              // ça sonne vraiment
    VSM_ASSERT(h3 > h1 * 0.05);         // la 3e est bien là
    VSM_ASSERT(h2 < h3 * 0.05);         // la 2e, non
    VSM_ASSERT(h4 < h3 * 0.05);         // la 4e non plus
}

VSM_TEST(wind_oscillation_needs_breath) {
    // L'oscillation naît de la boucle valve/tuyau : sans souffle, il ne se
    // passe rien. Ce n'est pas un oscillateur qu'on module.
    auto synth = makeWind();
    steady(*synth);
    set(*synth, "Breath Pressure", 0.0f);
    const auto audio = render(synth, {noteOn(0, 60, 110)}, 24064);
    VSM_ASSERT(peakAbs(audio) < 1e-3f);
}

VSM_TEST(wind_brassiness_adds_harmonics_when_pushed) {
    // Plus on pousse, plus l'onde se raidit : c'est ce qui sépare un cor tenu
    // piano d'un trombone qui claque.
    auto soft = makeWind();
    auto brassy = makeWind();
    for (auto* s : {&soft, &brassy}) {
        steady(**s);
        set(**s, "Breath Pressure", 0.95f);
    }
    set(*brassy, "Brassiness", 1.0f);
    const auto a = render(soft, {noteOn(0, 52, 120)}, 24064);
    const auto b = render(brassy, {noteOn(0, 52, 120)}, 24064);
    const double dull = brightness(a, 8000, 8000) / std::max(1e-9, static_cast<double>(peakAbs(a)));
    const double bright = brightness(b, 8000, 8000) / std::max(1e-9, static_cast<double>(peakAbs(b)));
    VSM_ASSERT(bright > dull * 1.2);
}

VSM_TEST(wind_vibrato_arrives_after_the_attack) {
    // Un instrumentiste pose le son d'abord et l'anime ensuite. Le vibrato
    // module la HAUTEUR : on mesure donc la dispersion de la période par les
    // passages à zéro, et non l'enveloppe -- un vibrato de hauteur ne déplace
    // presque pas le niveau, si bien que le mesurer ainsi ne prouverait rien
    // (essayé : les deux fenêtres donnaient le même chiffre).
    auto synth = makeWind();
    set(*synth, "Analog Character", 0.0f);
    set(*synth, "Breath Noise", 0.0f);
    set(*synth, "Vibrato Depth", 1.0f);
    set(*synth, "Vibrato Rate", 6.0f);
    set(*synth, "Vibrato Delay", 0.6f);
    set(*synth, "Attack", 0.01f);
    const auto audio = render(synth, {noteOn(0, 60, 110)}, 192000);

    auto pitchSpread = [&](size_t from, size_t span) {
        std::vector<double> rates;
        constexpr size_t kWindow = 4000;
        for (size_t w = 0; w + kWindow <= span; w += kWindow) {
            size_t crossings = 0;
            for (size_t i = from + w + 1; i < from + w + kWindow && i < audio.size(); ++i)
                if (audio[i - 1] <= 0.0f && audio[i] > 0.0f) ++crossings;
            rates.push_back(static_cast<double>(crossings));
        }
        double lo = 1e9, hi = 0.0, sum = 0.0;
        for (double r : rates) { lo = std::min(lo, r); hi = std::max(hi, r); sum += r; }
        const double mean = sum / static_cast<double>(std::max<size_t>(1, rates.size()));
        return (hi - lo) / std::max(1e-9, mean);
    };
    VSM_ASSERT(pitchSpread(140000, 48000) > pitchSpread(20000, 24000) + 0.02);
}

VSM_TEST(wind_release_lets_the_bore_empty) {
    // Couper au relâchement supprimerait l'extinction, qui est ce qui
    // s'entend le plus sur un vent.
    auto synth = makeWind();
    steady(*synth);
    set(*synth, "Release", 0.5f);
    const auto audio = render(synth, {noteOn(0, 60, 110), noteOff(24000, 60)}, 48128);
    VSM_ASSERT(windowPeak(audio, 24200, 2000) > 0.005f);   // ça continue après la touche
    VSM_ASSERT(windowPeak(audio, 46000, 2000) <
               windowPeak(audio, 20000, 2000) * 0.3f);      // et ça finit par s'éteindre
}

VSM_TEST(wind_is_polyphonic_as_a_section) {
    auto synth = makeWind();
    std::vector<MidiNoteEvent> chord = {noteOn(0, 52), noteOn(0, 55), noteOn(0, 59)};
    std::vector<float> left(4096, 0.0f), right(4096, 0.0f);
    synth->process(chord.data(), 3, left.data(), right.data(), 4096);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 3);
}

VSM_TEST(wind_a_full_section_does_not_clip) {
    auto synth = makeWind();
    set(*synth, "Breath Pressure", 1.0f);
    set(*synth, "Brassiness", 1.0f);
    std::vector<MidiNoteEvent> chord;
    for (uint8_t n : {40, 47, 52, 59}) chord.push_back(noteOn(0, n, 127));
    const auto audio = render(synth, chord, 48128);
    VSM_ASSERT(peakAbs(audio) < 1.0f);
    VSM_ASSERT(peakAbs(audio) > 0.05f);
}

VSM_TEST(wind_stays_finite_under_extreme_settings) {
    auto source = makeWind();
    for (const auto& info : source->parameterList()) source->setParameter(info.id, info.maxValue);
    const auto state = source->saveState();
    for (uint8_t note : {0, 24, 60, 108, 127}) {
        for (uint8_t velocity : {uint8_t{1}, uint8_t{127}}) {
            auto synth = makeWind();
            synth->loadState(state);
            std::vector<float> left(9600, 0.0f), right(9600, 0.0f);
            const auto event = noteOn(0, note, velocity);
            for (int start = 0; start < 9600; start += 32) // 9600 = 300 x 32
                synth->process(start == 0 ? &event : nullptr, start == 0 ? 1 : 0,
                               left.data() + start, right.data() + start, 32);
            for (float v : left) VSM_ASSERT(std::isfinite(v));
            VSM_ASSERT(peakAbs(left) < 12.0f);
        }
    }
}

VSM_TEST(wind_is_deterministic) {
    auto once = [] {
        auto synth = makeWind();
        set(*synth, "Analog Character", 0.9f);
        return render(synth, {noteOn(0, 57, 100)}, 24064);
    };
    const auto a = once(), b = once();
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], b[i], 1e-9);
}

VSM_TEST(wind_save_load_roundtrip) {
    auto source = makeWind();
    set(*source, "Bell Damping", 0.23f);
    set(*source, "Reed Stiffness", 0.77f);
    const auto state = source->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.wind"));
    auto target = makeWind();
    target->loadState(state);
    VSM_ASSERT_NEAR(target->getParameter(byName(*target, "Bell Damping")), 0.23f, 1e-6);
    VSM_ASSERT_NEAR(target->getParameter(byName(*target, "Reed Stiffness")), 0.77f, 1e-6);
}

VSM_TEST(wind_parameter_list_size) {
    auto synth = makeWind();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t{15});
}
