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

SynthPluginPtr makeString(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.string");
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

/// Rend une note en blocs de 256 : c'est ainsi que l'application appelle la
/// machine, et certains réglages (l'accord de la boucle) ne sont recalculés
/// qu'une fois par bloc. Rendre en un seul bloc géant testerait un chemin
/// que personne n'emprunte.
std::vector<float> renderNote(SynthPluginPtr& synth, uint8_t note, uint8_t velocity, int frames,
                              int releaseAt = -1) {
    std::vector<float> out(static_cast<size_t>(frames), 0.0f);
    std::vector<float> right(static_cast<size_t>(frames), 0.0f);
    constexpr int kBlock = 256;
    for (int start = 0; start < frames; start += kBlock) {
        const int count = std::min(kBlock, frames - start);
        std::vector<MidiNoteEvent> events;
        if (start == 0) events.push_back(noteOn(0, note, velocity));
        if (releaseAt >= start && releaseAt < start + count)
            events.push_back(noteOff(releaseAt - start, note));
        synth->process(events.empty() ? nullptr : events.data(), static_cast<int>(events.size()),
                       out.data() + start, right.data() + start, count);
    }
    return out;
}

float midiToHz(int note) { return 440.0f * std::exp2f((static_cast<float>(note) - 69.0f) / 12.0f); }

/// Amplitude à une fréquence donnée, par produit scalaire avec une paire
/// sinus/cosinus (Goertzel dans sa forme la plus lisible). Fenêtre de Hann,
/// pour que les partiels voisins ne fuient pas les uns sur les autres.
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

/// Fréquence du partiel de rang `n`, cherchée finement autour de sa position
/// harmonique. Sert à mesurer l'inharmonicité et la justesse.
double findPartialHz(const std::vector<float>& x, size_t from, size_t count, double around, double spanRatio) {
    double best = around, bestMag = -1.0;
    const double lo = around * (1.0 - spanRatio), hi = around * (1.0 + spanRatio);
    const int steps = 400;
    for (int i = 0; i <= steps; ++i) {
        const double hz = lo + (hi - lo) * static_cast<double>(i) / steps;
        const double mag = magnitudeAt(x, from, count, hz);
        if (mag > bestMag) { bestMag = mag; best = hz; }
    }
    return best;
}

double centsBetween(double a, double b) { return 1200.0 * std::log2(a / b); }

float windowPeak(const std::vector<float>& x, size_t from, size_t count) {
    float p = 0.0f;
    for (size_t i = from; i < from + count && i < x.size(); ++i) p = std::max(p, std::abs(x[i]));
    return p;
}
} // namespace

VSM_TEST(string_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.string"));
}

VSM_TEST(string_silent_with_no_events) {
    auto synth = makeString();
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(string_note_produces_sound) {
    auto synth = makeString();
    const auto audio = renderNote(synth, 45, 100, 24000);
    VSM_ASSERT(peakAbs(audio) > 0.02f);
    for (float v : audio) VSM_ASSERT(std::isfinite(v));
}

// --- Le trait distinctif : c'est une CORDE, pas un oscillateur -------------

VSM_TEST(string_waveguide_is_in_tune_across_the_range) {
    // LA promesse d'un guide d'ondes : la hauteur naît de la longueur de la
    // boucle, qui n'est pas un nombre entier d'échantillons. Sans le retard
    // fractionnaire, la justesse se quantifierait -- à 660 Hz, un échantillon
    // de retard vaut déjà plus d'un quart de ton. Le corps et la dérive sont
    // coupés : on mesure la corde, pas ce qu'il y a autour.
    for (int note : {28, 40, 52, 64, 76}) {
        auto synth = makeString();
        set(*synth, "Analog Character", 0.0f);
        set(*synth, "Body Level", 0.0f);
        set(*synth, "String Damping", 0.15f);
        set(*synth, "Stiffness", 0.0f);
        set(*synth, "String Decay", 8.0f);
        const auto audio = renderNote(synth, static_cast<uint8_t>(note), 110, 32768);

        const double expected = midiToHz(note);
        const double measured = findPartialHz(audio, 4096, 16384, expected, 0.04);
        VSM_ASSERT(std::abs(centsBetween(measured, expected)) < 8.0);
    }
}

VSM_TEST(string_pick_position_removes_the_harmonics_it_should) {
    // Pincer une corde en son milieu ne peut pas exciter les harmoniques
    // paires : le point de pincement est un noeud pour elles. C'est de la
    // physique, pas un effet de filtre -- et c'est ce que fait entendre la
    // différence entre un médiator près du chevalet et un pouce au milieu.
    auto middle = makeString();
    auto bridge = makeString();
    for (auto* s : {&middle, &bridge}) {
        set(**s, "Analog Character", 0.0f);
        set(**s, "Body Level", 0.0f);
        set(**s, "Stiffness", 0.0f);
        set(**s, "String Damping", 0.1f);
        set(**s, "String Decay", 8.0f);
    }
    set(*middle, "Pick Position", 0.5f);
    set(*bridge, "Pick Position", 0.12f);

    const auto a = renderNote(middle, 45, 110, 32768);
    const auto b = renderNote(bridge, 45, 110, 32768);
    const double f0 = midiToHz(45);

    auto ratio = [&](const std::vector<float>& x) {
        return magnitudeAt(x, 4096, 16384, 2.0 * f0) / std::max(1e-12, magnitudeAt(x, 4096, 16384, f0));
    };
    // Au milieu, l'harmonique 2 doit être très en dessous de ce qu'elle est
    // près du chevalet.
    VSM_ASSERT(ratio(a) < ratio(b) * 0.2);
}

VSM_TEST(string_stiffness_stretches_the_partials_upward) {
    // Une corde raide n'est pas harmonique : ses partiels montent. Le
    // réglage est calibré pour que le 16e partiel monte de 25 cents à fond ;
    // on vérifie le sens et l'ordre de grandeur, pas la 3e décimale.
    auto plain = makeString();
    auto stiff = makeString();
    for (auto* s : {&plain, &stiff}) {
        set(**s, "Analog Character", 0.0f);
        set(**s, "Body Level", 0.0f);
        set(**s, "String Damping", 0.03f);
        set(**s, "String Decay", 10.0f);
        set(**s, "Pick Position", 0.13f);
        set(**s, "Pick Hardness", 1.0f);
    }
    set(*plain, "Stiffness", 0.0f);
    set(*stiff, "Stiffness", 1.0f);

    const int note = 45;
    const double f0 = midiToHz(note);
    const auto a = renderNote(plain, static_cast<uint8_t>(note), 110, 32768);
    const auto b = renderNote(stiff, static_cast<uint8_t>(note), 110, 32768);

    const double target = 12.0 * f0;
    const double plainHz = findPartialHz(a, 2048, 16384, target, 0.02);
    const double stiffHz = findPartialHz(b, 2048, 16384, target, 0.02);
    VSM_ASSERT(std::abs(centsBetween(plainHz, target)) < 5.0);   // sans raideur : harmonique
    VSM_ASSERT(centsBetween(stiffHz, plainHz) > 5.0);            // avec raideur : plus haut
}

VSM_TEST(string_bow_sustains_where_a_pluck_decays) {
    // Les deux excitations ne sont pas deux timbres : ce sont deux physiques.
    // Un pincement rend son énergie d'un coup et s'éteint ; un archet en
    // fournit tant qu'il frotte. C'est la raison d'être du paramètre.
    auto plucked = makeString();
    auto bowed = makeString();
    for (auto* s : {&plucked, &bowed}) {
        set(**s, "Analog Character", 0.0f);
        set(**s, "Body Level", 0.0f);
        set(**s, "String Decay", 1.2f);
    }
    set(*plucked, "Excitation", 0.0f);
    set(*bowed, "Excitation", 1.0f);

    const auto a = renderNote(plucked, 52, 110, 96000);
    const auto b = renderNote(bowed, 52, 110, 96000);

    const float pluckEarly = windowPeak(a, 2000, 4000), pluckLate = windowPeak(a, 80000, 4000);
    const float bowEarly = windowPeak(b, 20000, 4000), bowLate = windowPeak(b, 80000, 4000);

    VSM_ASSERT(pluckEarly > 0.01f);
    VSM_ASSERT(pluckLate < pluckEarly * 0.25f);   // le pincement s'éteint
    VSM_ASSERT(bowEarly > 0.01f);
    VSM_ASSERT(bowLate > bowEarly * 0.6f);        // l'archet tient
}

VSM_TEST(string_release_damps_the_string) {
    // Lever le doigt étouffe la corde : ce n'est pas une enveloppe qui se
    // ferme, c'est la boucle qui perd davantage à chaque tour.
    auto held = makeString();
    auto damped = makeString();
    for (auto* s : {&held, &damped}) {
        set(**s, "Analog Character", 0.0f);
        set(**s, "String Decay", 12.0f);
        set(**s, "Release", 0.05f);
    }
    const auto a = renderNote(held, 45, 110, 48000);
    const auto b = renderNote(damped, 45, 110, 48000, 12000);
    VSM_ASSERT(windowPeak(b, 24000, 4000) < windowPeak(a, 24000, 4000) * 0.3f);
}

VSM_TEST(string_decay_parameter_sets_the_ring_time) {
    auto shortRing = makeString();
    auto longRing = makeString();
    for (auto* s : {&shortRing, &longRing}) {
        set(**s, "Analog Character", 0.0f);
        set(**s, "Body Level", 0.0f);
    }
    set(*shortRing, "String Decay", 0.4f);
    set(*longRing, "String Decay", 12.0f);
    const auto a = renderNote(shortRing, 45, 110, 96000);
    const auto b = renderNote(longRing, 45, 110, 96000);
    VSM_ASSERT(windowPeak(a, 60000, 4000) < windowPeak(b, 60000, 4000) * 0.2f);
}

VSM_TEST(string_body_is_transparent_when_silent) {
    // Une basse électrique n'a pas de caisse. À `Body Level = 0` la machine
    // doit être EXACTEMENT transparente, pas « presque ».
    auto synth = makeString();
    set(*synth, "Body Level", 0.0f);
    const auto flat = renderNote(synth, 45, 110, 8192);

    auto coloured = makeString();
    set(*coloured, "Body Level", 0.8f);
    const auto sung = renderNote(coloured, 45, 110, 8192);

    const double f0 = midiToHz(45);
    // La caisse résonne dans le grave : elle doit y ajouter de l'énergie.
    VSM_ASSERT(magnitudeAt(sung, 512, 4096, f0) > magnitudeAt(flat, 512, 4096, f0) * 1.15);
}

VSM_TEST(string_is_polyphonic) {
    auto synth = makeString();
    std::vector<MidiNoteEvent> chord = {noteOn(0, 40), noteOn(0, 45), noteOn(0, 52)};
    std::vector<float> left(4000, 0.0f), right(4000, 0.0f);
    synth->process(chord.data(), 3, left.data(), right.data(), 4000);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 3);
}

VSM_TEST(string_a_full_chord_does_not_clip) {
    // Leçon de `vsm.obx` : une machine polyphonique se calibre sur son ACCORD,
    // pas sur sa note seule.
    auto synth = makeString();
    set(*synth, "Body Level", 1.0f);
    std::vector<MidiNoteEvent> chord;
    for (uint8_t n : {40, 45, 50, 55, 59, 64, 67, 71}) chord.push_back(noteOn(0, n, 127));
    constexpr int kBlock = 256;
    constexpr int kFrames = 48128; // multiple entier de kBlock
    std::vector<float> left(kFrames, 0.0f), right(kFrames, 0.0f);
    for (int start = 0; start < kFrames; start += kBlock)
        synth->process(start == 0 ? chord.data() : nullptr, start == 0 ? static_cast<int>(chord.size()) : 0,
                       left.data() + start, right.data() + start, kBlock);
    VSM_ASSERT(peakAbs(left) < 1.0f);
    VSM_ASSERT(peakAbs(left) > 0.1f);
}

VSM_TEST(string_stays_finite_under_extreme_settings) {
    // Une boucle de réaction (l'archet en est une) doit rester bornée aux
    // réglages que personne ne choisirait.
    for (float excitation : {0.0f, 0.5f, 1.0f}) {
        auto synth = makeString();
        set(*synth, "Excitation", excitation);
        set(*synth, "Bow Pressure", 1.0f);
        set(*synth, "Bow Speed", 1.0f);
        set(*synth, "String Decay", 20.0f);
        set(*synth, "String Damping", 0.0f);
        set(*synth, "Stiffness", 1.0f);
        set(*synth, "Drive", 1.0f);
        set(*synth, "Body Level", 1.0f);
        set(*synth, "Output Level", 2.0f);
        for (uint8_t note : {0, 24, 60, 108, 127}) {
            for (uint8_t velocity : {uint8_t{1}, uint8_t{127}}) {
                auto fresh = makeString();
                fresh->loadState(synth->saveState());
                // Blocs très courts : le chemin le moins souvent emprunté.
                std::vector<float> left(9600, 0.0f), right(9600, 0.0f);
                const auto event = noteOn(0, note, velocity);
                for (int start = 0; start < 9600; start += 32) // 9600 = 300 x 32
                    fresh->process(start == 0 ? &event : nullptr, start == 0 ? 1 : 0,
                                   left.data() + start, right.data() + start, 32);
                for (float v : left) VSM_ASSERT(std::isfinite(v));
                VSM_ASSERT(peakAbs(left) < 12.0f);
            }
        }
    }
}

VSM_TEST(string_is_deterministic) {
    auto render = [] {
        auto synth = makeString();
        set(*synth, "Analog Character", 0.9f);
        return renderNote(synth, 45, 100, 24000);
    };
    const auto a = render(), b = render();
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], b[i], 1e-9);
}

VSM_TEST(string_save_load_roundtrip) {
    auto source = makeString();
    set(*source, "Pick Position", 0.41f);
    set(*source, "String Decay", 9.25f);
    set(*source, "Excitation", 0.75f);
    const auto state = source->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.string"));

    auto target = makeString();
    target->loadState(state);
    VSM_ASSERT_NEAR(target->getParameter(byName(*target, "Pick Position")), 0.41f, 1e-6);
    VSM_ASSERT_NEAR(target->getParameter(byName(*target, "String Decay")), 9.25f, 1e-6);
    VSM_ASSERT_NEAR(target->getParameter(byName(*target, "Excitation")), 0.75f, 1e-6);
}

VSM_TEST(string_parameter_list_size) {
    auto synth = makeString();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t{15});
}
