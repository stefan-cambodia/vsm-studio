#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.jewsharp` — la seule machine du parc qui REFUSE de suivre le clavier.
//
// Une guimbarde a une lame d'acier de fréquence FIXE ; ce que le joueur change,
// c'est sa cavité buccale, qui fait ressortir tel ou tel harmonique du bourdon.
// C'est le miroir exact de `vsm.vocal` : là-bas la hauteur bouge et les
// formants restent, ici la hauteur reste et le formant bouge.

namespace {
constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeHarp(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.jewsharp");
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
float peakAbs(const std::vector<float>& b) {
    float p = 0.0f; for (float v : b) p = std::max(p, std::abs(v)); return p;
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

std::vector<float> jouer(int note, float lameHz = 82.0f) {
    auto synth = makeHarp();
    set(*synth, "Sustain", 1.0f);
    set(*synth, "Attack", 0.005f);
    set(*synth, "Velocity Sensitivity", 0.0f);
    set(*synth, "Reed Pitch", lameHz);
    std::vector<float> left(96000, 0.0f), right(96000, 0.0f);
    const MidiNoteEvent n{MidiNoteEvent::Kind::NoteOn, 0, 0, static_cast<uint8_t>(note), 110};
    for (int s = 0; s + 256 <= 96000; s += 256)
        synth->process(s == 0 ? &n : nullptr, s == 0 ? 1 : 0,
                       left.data() + s, right.data() + s, 256);
    return left;
}

/// Le fondamental réellement produit, cherché SANS supposer où il est.
double fondamental(const std::vector<float>& audio) {
    double meilleur = 0.0, retenu = 0.0;
    for (double hz = 55.0; hz < 140.0; hz += 0.2) {
        const double m = magnitudeAt(audio, 24000, 16384, hz);
        if (m > meilleur) { meilleur = m; retenu = hz; }
    }
    return retenu;
}

/// Le centroïde spectral : c'est LUI qui dit où est le formant.
///
/// Une première version cherchait le PIC dominant : il restait obstinément sur
/// le troisième harmonique du bourdon (246 Hz) quelle que soit la note, parce
/// qu'un formant déplace l'ENVELOPPE du spectre et non son maximum. Encore une
/// mesure qui regardait à côté.
double centroide(const std::vector<float>& audio) {
    double num = 0.0, den = 0.0;
    for (double hz = 100.0; hz < 8000.0; hz *= 1.02) {
        const double m = magnitudeAt(audio, 24000, 16384, hz);
        num += hz * m;
        den += m;
    }
    return num / std::max(1e-12, den);
}
} // namespace

VSM_TEST(jewsharp_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.jewsharp"));
}

VSM_TEST(jewsharp_silent_with_no_events) {
    auto synth = makeHarp();
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(jewsharp_note_produces_sound_and_stays_finite) {
    const auto audio = jouer(60);
    VSM_ASSERT(peakAbs(audio) > 0.01f);
    for (float v : audio) VSM_ASSERT(std::isfinite(v));
}

// --- LE trait : la hauteur ne suit PAS le clavier --------------------------

VSM_TEST(jewsharp_pitch_does_not_follow_the_keyboard) {
    // Sur n'importe quelle autre machine du parc, ce test serait absurde. Ici
    // il dit la vérité de l'instrument : la lame est en acier, elle a une
    // fréquence et une seule, et le clavier n'y peut rien.
    for (int note : {40, 48, 55, 62, 70, 78}) {
        const double f0 = fondamental(jouer(note));
        VSM_ASSERT(std::abs(f0 - 82.0) < 1.5);   // mesuré : 82,00 partout
    }
}

VSM_TEST(jewsharp_the_reed_itself_can_be_changed) {
    // Ce que le musicien change, c'est d'INSTRUMENT : les guimbardes se
    // vendent par tonalité. La lame est donc un réglage — mais un réglage,
    // pas une note.
    VSM_ASSERT(std::abs(fondamental(jouer(60, 110.0f)) - 110.0) < 2.0);
    VSM_ASSERT(std::abs(fondamental(jouer(36, 110.0f)) - 110.0) < 2.0);
}

// --- Seconde moitié : mais la note FAIT quelque chose ----------------------

VSM_TEST(jewsharp_the_note_moves_the_formant) {
    // Sans cette moitié, on n'aurait pas écrit une guimbarde mais un bourdon
    // qui ignore le clavier — c'est-à-dire une machine cassée. Le clavier
    // joue la CAVITÉ : il déplace le formant, donc l'harmonique qui ressort.
    const double grave = centroide(jouer(40));
    const double medium = centroide(jouer(55));
    const double aigu = centroide(jouer(78));

    VSM_ASSERT(medium > grave * 1.2);        // mesuré : 705 · 994 · 1554 Hz
    VSM_ASSERT(aigu > medium * 1.2);
    VSM_ASSERT(aigu > grave * 1.8);
}

VSM_TEST(jewsharp_refuses_pitch_bend_and_says_so) {
    // Une guimbarde n'a aucun geste de hauteur : sa lame est en acier, on ne
    // la plie pas en jouant. Le refus est donc un choix, et le moteur le
    // compte (`ignoredControlEvents`) pour que l'interface puisse dire
    // pourquoi la modulation ne s'entend pas.
    auto synth = makeHarp();
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 2.0f;
    VSM_ASSERT(!synth->handleControlEvent(bend));
}

VSM_TEST(jewsharp_is_deterministic) {
    const auto a = jouer(60);
    const auto b = jouer(60);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT(a[i] == b[i]);
}

VSM_TEST(jewsharp_save_load_roundtrip) {
    auto premier = makeHarp();
    set(*premier, "Reed Pitch", 97.0f);
    set(*premier, "Twang", 0.83f);
    const auto etat = premier->saveState();
    auto second = makeHarp();
    second->loadState(etat);
    for (const auto& info : premier->parameterList())
        VSM_ASSERT_NEAR(second->getParameter(info.id), premier->getParameter(info.id), 1e-6);
}

VSM_TEST(jewsharp_parameter_list_size) {
    auto synth = makeHarp();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t(11));
}
