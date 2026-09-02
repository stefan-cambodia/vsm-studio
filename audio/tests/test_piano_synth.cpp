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

SynthPluginPtr makePiano(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.piano");
    if (plugin) plugin->initialize(sr, 512);
    return plugin;
}

/// UN NOM INCONNU EST UNE ERREUR, ET IL DOIT LE DIRE. Ce helper renvoyait 0
/// quand il ne trouvait pas le paramètre ; `setParameter(0, v)` ne fait rien
/// et ne se plaint pas, si bien qu'un test réglant `"Damping"` sur une machine
/// qui expose `"String Damping"` mesurait la machine par défaut en croyant
/// mesurer autre chose. C'est arrivé au banc de H10 (CDC machines-manquantes,
/// § 12) : quatre lignes d'un balayage étaient identiques sans que cela
/// alerte, et il s'en est fallu de peu qu'on écrive une machine inutile sur
/// cette base. Panne muette interdite, ici comme ailleurs.
ParamId byName(const ISynthPlugin& plugin, const std::string& name) {
    for (const auto& info : plugin.parameterList()) if (info.name == name) return info.id;
    throw vsm::test::AssertionFailure("paramètre inconnu : « " + name + " » — la machine expose d'autres noms");
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

struct Stereo { std::vector<float> left, right; };

Stereo renderStereo(SynthPluginPtr& synth, const std::vector<MidiNoteEvent>& events, int frames) {
    Stereo out{std::vector<float>(static_cast<size_t>(frames), 0.0f),
               std::vector<float>(static_cast<size_t>(frames), 0.0f)};
    constexpr int kBlock = 256;
    for (int start = 0; start < frames; start += kBlock) {
        const int count = std::min(kBlock, frames - start);
        std::vector<MidiNoteEvent> block;
        for (const auto& event : events)
            if (event.sampleOffset >= start && event.sampleOffset < start + count)
                block.push_back({event.kind, event.sampleOffset - start, event.channel,
                                 event.note, event.velocity});
        synth->process(block.empty() ? nullptr : block.data(), static_cast<int>(block.size()),
                       out.left.data() + start, out.right.data() + start, count);
    }
    return out;
}

std::vector<float> renderNote(SynthPluginPtr& synth, uint8_t note, uint8_t velocity, int frames,
                              int releaseAt = -1) {
    std::vector<MidiNoteEvent> events{noteOn(0, note, velocity)};
    if (releaseAt >= 0) events.push_back(noteOff(releaseAt, note));
    auto stereo = renderStereo(synth, events, frames);
    for (size_t i = 0; i < stereo.left.size(); ++i)
        stereo.left[i] = 0.5f * (stereo.left[i] + stereo.right[i]);
    return stereo.left;
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

double findPartialHz(const std::vector<float>& x, size_t from, size_t count, double around, double spanRatio) {
    double best = around, bestMag = -1.0;
    const double lo = around * (1.0 - spanRatio), hi = around * (1.0 + spanRatio);
    for (int i = 0; i <= 400; ++i) {
        const double hz = lo + (hi - lo) * static_cast<double>(i) / 400.0;
        const double mag = magnitudeAt(x, from, count, hz);
        if (mag > bestMag) { bestMag = mag; best = hz; }
    }
    return best;
}

/// Énergie des aigus : la différence première accentue les hautes fréquences.
double brightness(const std::vector<float>& b, size_t from, size_t count) {
    double energy = 0.0;
    for (size_t i = from + 1; i < from + count && i < b.size(); ++i) {
        const double d = static_cast<double>(b[i]) - b[i - 1];
        energy += d * d;
    }
    return energy;
}

float windowPeak(const std::vector<float>& x, size_t from, size_t count) {
    float p = 0.0f;
    for (size_t i = from; i < from + count && i < x.size(); ++i) p = std::max(p, std::abs(x[i]));
    return p;
}
} // namespace

VSM_TEST(piano_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.piano"));
}

VSM_TEST(piano_silent_with_no_events) {
    auto synth = makePiano();
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(piano_note_produces_sound) {
    auto synth = makePiano();
    const auto audio = renderNote(synth, 60, 100, 24000);
    VSM_ASSERT(peakAbs(audio) > 0.02f);
    for (float v : audio) VSM_ASSERT(std::isfinite(v));
}

// --- Les traits distinctifs : c'est un PIANO, pas une corde pincée ---------

VSM_TEST(piano_striking_harder_opens_the_timbre_not_only_the_level) {
    // LA loi expressive du piano, et elle doit sortir de la PHYSIQUE : un
    // marteau reste en contact d'autant moins longtemps qu'il frappe vite, et
    // la durée de contact fixe la coupure du spectre injecté. On compare donc
    // la brillance À NIVEAU NORMALISÉ -- sinon on ne mesurerait que le gain.
    auto soft = makePiano();
    auto hard = makePiano();
    for (auto* s : {&soft, &hard}) {
        set(**s, "Analog Character", 0.0f);
        set(**s, "Soundboard Level", 0.0f);
    }
    const auto quiet = renderNote(soft, 52, 25, 24000);
    const auto loud = renderNote(hard, 52, 127, 24000);

    const double quietBrightness = brightness(quiet, 0, 8000) / std::max(1e-9, static_cast<double>(peakAbs(quiet)));
    const double loudBrightness = brightness(loud, 0, 8000) / std::max(1e-9, static_cast<double>(peakAbs(loud)));
    VSM_ASSERT(loudBrightness > quietBrightness * 1.5);
}

VSM_TEST(piano_hammer_hardness_opens_the_timbre_at_equal_velocity) {
    auto felt = makePiano();
    auto bright = makePiano();
    for (auto* s : {&felt, &bright}) {
        set(**s, "Analog Character", 0.0f);
        set(**s, "Soundboard Level", 0.0f);
    }
    set(*felt, "Hammer Hardness", 0.0f);
    set(*bright, "Hammer Hardness", 1.0f);
    const auto a = renderNote(felt, 52, 100, 24000), b = renderNote(bright, 52, 100, 24000);
    const double soft = brightness(a, 0, 8000) / std::max(1e-9, static_cast<double>(peakAbs(a)));
    const double sharp = brightness(b, 0, 8000) / std::max(1e-9, static_cast<double>(peakAbs(b)));
    VSM_ASSERT(sharp > soft * 1.3);
}

VSM_TEST(piano_unison_detune_gives_a_two_stage_decay) {
    // Deux cordes désaccordées ne font pas qu'un battement : leur somme chute
    // d'abord vite, puis laisse une TRAÎNE longue et faible. Une corde seule
    // donne une exponentielle unique, qui s'entend aussitôt comme « pas un
    // piano ». On mesure donc la courbure de la décroissance, pas sa vitesse.
    auto single = makePiano();
    auto chorus = makePiano();
    for (auto* s : {&single, &chorus}) {
        set(**s, "Analog Character", 0.0f);
        set(**s, "Soundboard Level", 0.0f);
        set(**s, "String Decay", 6.0f);
    }
    set(*single, "Unison Detune", 0.0f);
    set(*chorus, "Unison Detune", 1.0f);

    auto curvature = [](const std::vector<float>& x) {
        // Rapport entre la chute précoce et la chute tardive. Sur une
        // exponentielle unique il vaut 1 ; une décroissance en deux temps
        // chute plus vite au début qu'à la fin, donc il dépasse 1.
        const double early = std::log(std::max(1e-9, static_cast<double>(windowPeak(x, 2000, 4000)))
                                      / std::max(1e-9, static_cast<double>(windowPeak(x, 40000, 4000))));
        const double late = std::log(std::max(1e-9, static_cast<double>(windowPeak(x, 40000, 4000)))
                                     / std::max(1e-9, static_cast<double>(windowPeak(x, 130000, 4000))));
        return early / std::max(1e-9, late);
    };
    const auto a = renderNote(single, 45, 110, 144000);
    const auto b = renderNote(chorus, 45, 110, 144000);
    VSM_ASSERT(curvature(b) > curvature(a) * 1.15);
}

VSM_TEST(piano_hammer_position_suppresses_the_harmonic_it_should) {
    // Les marteaux frappent vers 1/8 de la corde, ce qui pose un noeud sur le
    // 8e harmonique et le supprime : c'est pour cela qu'un piano ne sonne pas
    // dur. On vérifie que le 8e est bien creusé au huitième, et qu'il ne
    // l'est pas ailleurs.
    auto eighth = makePiano();
    auto elsewhere = makePiano();
    for (auto* s : {&eighth, &elsewhere}) {
        set(**s, "Analog Character", 0.0f);
        set(**s, "Soundboard Level", 0.0f);
        set(**s, "Inharmonicity", 0.0f);
        set(**s, "Unison Detune", 0.0f);
        set(**s, "String Damping", 0.05f);
        set(**s, "Hammer Hardness", 1.0f);
    }
    set(*eighth, "Hammer Position", 0.125f);
    set(*elsewhere, "Hammer Position", 0.19f);

    const int note = 45;
    const double f0 = midiToHz(note);
    const auto a = renderNote(eighth, static_cast<uint8_t>(note), 120, 32768);
    const auto b = renderNote(elsewhere, static_cast<uint8_t>(note), 120, 32768);

    auto ratio = [&](const std::vector<float>& x) {
        return magnitudeAt(x, 2048, 16384, 8.0 * f0) /
               std::max(1e-12, magnitudeAt(x, 2048, 16384, 7.0 * f0));
    };
    VSM_ASSERT(ratio(a) < ratio(b) * 0.5);
}

VSM_TEST(piano_inharmonicity_stretches_the_partials_upward) {
    auto plain = makePiano();
    auto stiff = makePiano();
    for (auto* s : {&plain, &stiff}) {
        set(**s, "Analog Character", 0.0f);
        set(**s, "Soundboard Level", 0.0f);
        set(**s, "Unison Detune", 0.0f);
        set(**s, "String Damping", 0.03f);
        set(**s, "Hammer Hardness", 1.0f);
        set(**s, "Hammer Position", 0.09f);
    }
    set(*plain, "Inharmonicity", 0.0f);
    set(*stiff, "Inharmonicity", 1.0f);

    const int note = 45;
    const double target = 12.0 * midiToHz(note);
    const auto a = renderNote(plain, static_cast<uint8_t>(note), 120, 32768);
    const auto b = renderNote(stiff, static_cast<uint8_t>(note), 120, 32768);
    const double plainHz = findPartialHz(a, 2048, 16384, target, 0.02);
    const double stiffHz = findPartialHz(b, 2048, 16384, target, 0.02);
    VSM_ASSERT(std::abs(1200.0 * std::log2(plainHz / target)) < 5.0);
    VSM_ASSERT(1200.0 * std::log2(stiffHz / plainHz) > 5.0);
}

VSM_TEST(piano_sustain_pedal_keeps_the_damper_off_the_string) {
    // La pédale ne « rallonge » pas le son : elle empêche l'étouffoir de
    // retomber. Relâchée, la touche étouffe ; enfoncée, le relâchement ne
    // change plus rien -- et c'est vérifié en comparant à la note TENUE.
    auto damped = makePiano();
    auto pedalled = makePiano();
    auto held = makePiano();
    for (auto* s : {&damped, &pedalled, &held}) {
        set(**s, "Analog Character", 0.0f);
        set(**s, "String Decay", 15.0f);
        set(**s, "Release", 0.05f);
    }
    set(*pedalled, "Sustain Pedal", 1.0f);

    const auto a = renderNote(damped, 45, 110, 96000, 12000);
    const auto b = renderNote(pedalled, 45, 110, 96000, 12000);
    const auto c = renderNote(held, 45, 110, 96000);

    const float late = windowPeak(c, 60000, 4000);
    VSM_ASSERT(windowPeak(a, 60000, 4000) < late * 0.3f);        // étouffé
    VSM_ASSERT(windowPeak(b, 60000, 4000) > late * 0.8f);        // pédale : rien ne change
}

VSM_TEST(piano_spreads_bass_left_and_treble_right) {
    auto synth = makePiano();
    set(*synth, "Stereo Spread", 1.0f);
    const auto low = renderStereo(*&synth, {noteOn(0, 28, 110)}, 12000);
    auto other = makePiano();
    set(*other, "Stereo Spread", 1.0f);
    const auto high = renderStereo(*&other, {noteOn(0, 96, 110)}, 12000);

    VSM_ASSERT(peakAbs(low.left) > peakAbs(low.right) * 1.5f);
    VSM_ASSERT(peakAbs(high.right) > peakAbs(high.left) * 1.5f);
}

VSM_TEST(piano_stereo_spread_at_zero_is_mono) {
    auto synth = makePiano();
    set(*synth, "Stereo Spread", 0.0f);
    const auto out = renderStereo(*&synth, {noteOn(0, 28, 110)}, 12000);
    for (size_t i = 0; i < out.left.size(); ++i)
        VSM_ASSERT_NEAR(out.left[i], out.right[i], 1e-6);
}

VSM_TEST(piano_is_polyphonic) {
    auto synth = makePiano();
    std::vector<MidiNoteEvent> chord = {noteOn(0, 48), noteOn(0, 52), noteOn(0, 55)};
    std::vector<float> left(4000, 0.0f), right(4000, 0.0f);
    synth->process(chord.data(), 3, left.data(), right.data(), 4000);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 3);
}

VSM_TEST(piano_a_full_chord_does_not_clip) {
    // Leçon de `vsm.obx` : une machine polyphonique se calibre sur son ACCORD.
    auto synth = makePiano();
    set(*synth, "Soundboard Level", 1.0f);
    std::vector<MidiNoteEvent> chord;
    for (uint8_t n : {33, 40, 45, 52, 57, 64, 69, 76}) chord.push_back(noteOn(0, n, 127));
    const auto out = renderStereo(*&synth, chord, 48128);
    VSM_ASSERT(peakAbs(out.left) < 1.0f);
    VSM_ASSERT(peakAbs(out.left) > 0.1f);
}

VSM_TEST(piano_stays_finite_under_extreme_settings) {
    auto source = makePiano();
    set(*source, "Hammer Hardness", 1.0f);
    set(*source, "Hammer Position", 0.04f);
    set(*source, "Inharmonicity", 1.0f);
    set(*source, "String Decay", 30.0f);
    set(*source, "String Damping", 0.0f);
    set(*source, "Unison Detune", 1.0f);
    set(*source, "Soundboard Level", 1.0f);
    set(*source, "Tone Bass", 12.0f);
    set(*source, "Tone Treble", 12.0f);
    set(*source, "Output Level", 2.0f);
    const auto state = source->saveState();

    for (uint8_t note : {0, 21, 60, 108, 127}) {
        for (uint8_t velocity : {uint8_t{1}, uint8_t{127}}) {
            auto synth = makePiano();
            synth->loadState(state);
            std::vector<float> left(9600, 0.0f), right(9600, 0.0f);
            const auto event = noteOn(0, note, velocity);
            for (int start = 0; start < 9600; start += 32) // 9600 = 300 x 32
                synth->process(start == 0 ? &event : nullptr, start == 0 ? 1 : 0,
                               left.data() + start, right.data() + start, 32);
            for (float v : left) VSM_ASSERT(std::isfinite(v));
            for (float v : right) VSM_ASSERT(std::isfinite(v));
            VSM_ASSERT(peakAbs(left) < 12.0f);
        }
    }
}

VSM_TEST(piano_is_deterministic) {
    auto render = [] {
        auto synth = makePiano();
        set(*synth, "Analog Character", 0.9f);
        return renderNote(synth, 45, 100, 24000);
    };
    const auto a = render(), b = render();
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], b[i], 1e-9);
}

VSM_TEST(piano_save_load_roundtrip) {
    auto source = makePiano();
    set(*source, "Hammer Hardness", 0.72f);
    set(*source, "String Decay", 21.5f);
    set(*source, "Unison Detune", 0.66f);
    const auto state = source->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.piano"));

    auto target = makePiano();
    target->loadState(state);
    VSM_ASSERT_NEAR(target->getParameter(byName(*target, "Hammer Hardness")), 0.72f, 1e-6);
    VSM_ASSERT_NEAR(target->getParameter(byName(*target, "String Decay")), 21.5f, 1e-6);
    VSM_ASSERT_NEAR(target->getParameter(byName(*target, "Unison Detune")), 0.66f, 1e-6);
}

VSM_TEST(piano_parameter_list_size) {
    auto synth = makePiano();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t{16});
}
