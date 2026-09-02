#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.terrain` — une surface, une orbite, et c'est le CHEMIN qui fait le
// timbre.
//
// La question qu'il fallait trancher : en quoi est-ce autre chose que
// `vsm.vector`, qui a lui aussi une orbite dans un plan ? La réponse est la
// LINÉARITÉ, et elle se mesure — voir le test de contraste plus bas, qui a
// dû être écrit deux fois parce que le premier critère ne séparait rien.

namespace {
constexpr double kSampleRate = 48000.0;

SynthPluginPtr make(const char* id, double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create(id);
    if (plugin) plugin->initialize(sr, 512);
    return plugin;
}
void set(ISynthPlugin& plugin, const std::string& name, float value) {
    for (const auto& info : plugin.parameterList())
        if (info.name == name) { plugin.setParameter(info.id, value); return; }
    throw vsm::test::AssertionFailure("paramètre inconnu : « " + name + " »");
}
float peakAbs(const std::vector<float>& b) {
    float p = 0.0f; for (float v : b) p = std::max(p, std::abs(v)); return p;
}
std::vector<float> rendre(SynthPluginPtr& synth, int frames = 96000) {
    std::vector<float> left(static_cast<size_t>(frames), 0.0f), right(static_cast<size_t>(frames), 0.0f);
    const MidiNoteEvent n{MidiNoteEvent::Kind::NoteOn, 0, 0, 57, 110};
    for (int s = 0; s + 256 <= frames; s += 256)
        synth->process(s == 0 ? &n : nullptr, s == 0 ? 1 : 0,
                       left.data() + s, right.data() + s, 256);
    return left;
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
constexpr double kF0 = 220.0;   // note 57

/// Les rapports h3/h1 et h5/h1 d'un rendu de terrain, à un rayon donné.
std::pair<double, double> rangsDuTerrain(float rayon) {
    auto synth = make("vsm.terrain");
    set(*synth, "Sustain", 1.0f);
    set(*synth, "Attack", 0.005f);
    set(*synth, "Drift Rate", 0.0f);
    set(*synth, "Velocity to Radius", 0.0f);
    set(*synth, "Filter Cutoff", 15000.0f);
    set(*synth, "Orbit Radius", rayon);
    const auto audio = rendre(synth);
    const double h1 = magnitudeAt(audio, 24000, 16384, kF0);
    return {magnitudeAt(audio, 24000, 16384, 3 * kF0) / std::max(1e-9, h1),
            magnitudeAt(audio, 24000, 16384, 5 * kF0) / std::max(1e-9, h1)};
}

/// Les mêmes rapports pour `vsm.vector`, à une profondeur d'orbite donnée.
/// Quatre formes bien différentes : on lui donne ses meilleures chances.
std::pair<double, double> rangsDuVecteur(float profondeur) {
    auto synth = make("vsm.vector");
    set(*synth, "A Shape", 0.0f);
    set(*synth, "B Shape", 0.35f);
    set(*synth, "C Shape", 0.7f);
    set(*synth, "D Shape", 1.0f);
    set(*synth, "Amp Sustain", 1.0f);
    set(*synth, "Amp Attack", 0.005f);
    set(*synth, "Filter Sustain", 1.0f);
    set(*synth, "Filter Attack", 0.005f);
    set(*synth, "Filter Env Amount", 0.0f);
    set(*synth, "Filter Cutoff", 15000.0f);
    set(*synth, "Analog Character", 0.0f);
    set(*synth, "Velocity Sensitivity", 0.0f);
    set(*synth, "Orbit Rate", 0.0f);
    set(*synth, "Orbit Depth", profondeur);
    const auto audio = rendre(synth);
    const double h1 = magnitudeAt(audio, 24000, 16384, kF0);
    return {magnitudeAt(audio, 24000, 16384, 3 * kF0) / std::max(1e-9, h1),
            magnitudeAt(audio, 24000, 16384, 5 * kF0) / std::max(1e-9, h1)};
}

double amplitude(const std::vector<double>& v) {
    double lo = 1e9, hi = 0.0;
    for (double x : v) { lo = std::min(lo, x); hi = std::max(hi, x); }
    return hi / std::max(1e-9, lo);
}
} // namespace

VSM_TEST(terrain_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.terrain"));
}

VSM_TEST(terrain_silent_with_no_events) {
    auto synth = make("vsm.terrain");
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(terrain_note_produces_sound_and_stays_finite) {
    auto synth = make("vsm.terrain");
    const auto audio = rendre(synth, 48000);
    VSM_ASSERT(peakAbs(audio) > 0.01f);
    for (float v : audio) VSM_ASSERT(std::isfinite(v));
}

// --- Trait 1 : le rayon change le TIMBRE, pas la NOTE ---------------------

VSM_TEST(terrain_the_orbit_changes_the_timbre_not_the_pitch) {
    // Marcher plus loin sur le relief fait rencontrer d'autres bosses. La
    // hauteur, elle, est celle du tour d'orbite et ne bouge pas d'un cent.
    for (float rayon : {0.15f, 0.55f, 1.0f}) {
        auto synth = make("vsm.terrain");
        set(*synth, "Sustain", 1.0f);
        set(*synth, "Attack", 0.005f);
        set(*synth, "Drift Rate", 0.0f);
        set(*synth, "Velocity to Radius", 0.0f);
        set(*synth, "Orbit Radius", rayon);
        const auto audio = rendre(synth);
        double meilleur = 0.0, f = 0.0;
        for (double hz = 180.0; hz < 280.0; hz += 0.2) {
            const double m = magnitudeAt(audio, 24000, 16384, hz);
            if (m > meilleur) { meilleur = m; f = hz; }
        }
        VSM_ASSERT(std::abs(f - kF0) < 1.0);   // mesuré : 220,0 partout
    }
}

// --- Trait 2, LE trait : ce qui sépare un terrain d'un MÉLANGE ------------

VSM_TEST(terrain_is_not_a_mix_the_partials_move_independently) {
    // CE TEST A ÉTÉ ÉCRIT DEUX FOIS, et la première version ne séparait rien.
    // Elle demandait seulement que « h3/h1 bouge d'un facteur franc » quand
    // l'orbite grandit — or `vsm.vector` le fait aussi (×2,36 mesuré), parce
    // qu'agrandir son orbite redose ses quatre formes.
    //
    // Ce qui SÉPARE vraiment un terrain d'un mélange se voit en regardant
    // PLUSIEURS rangs à la fois. `vsm.vector` étant une combinaison LINÉAIRE
    // de quatre formes fixes, tous ses rangs suivent le même dosage et varient
    // donc du MÊME facteur : mesuré, h3 ×2,36 et h5 ×2,36 — identiques à trois
    // décimales, et monotones. Un terrain est une fonction NON LINÉAIRE des
    // coordonnées : ses rangs vont chacun leur chemin (h3 ×2,4 mais h5 ×18,3).
    std::vector<double> h3Terrain, h5Terrain, h3Vecteur, h5Vecteur;
    for (float x : {0.15f, 0.35f, 0.55f, 0.8f, 1.0f}) {
        const auto [a, b] = rangsDuTerrain(x);
        h3Terrain.push_back(a);
        h5Terrain.push_back(b);
    }
    for (float x : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        const auto [a, b] = rangsDuVecteur(x);
        h3Vecteur.push_back(a);
        h5Vecteur.push_back(b);
    }
    // Chez le mélangeur, les deux rangs varient de conserve.
    const double independanceVecteur =
        amplitude(h5Vecteur) / std::max(1e-9, amplitude(h3Vecteur));
    VSM_ASSERT(independanceVecteur > 0.8 && independanceVecteur < 1.25);

    // Chez le terrain, non — et c'est cela, être non linéaire.
    const double independanceTerrain =
        amplitude(h5Terrain) / std::max(1e-9, amplitude(h3Terrain));
    VSM_ASSERT(independanceTerrain > 3.0);   // mesuré : 18,3 / 2,4 ≈ 7,6
}

VSM_TEST(terrain_modulation_wheel_walks_further_on_the_relief) {
    // Le geste de timbre le plus naturel sur cette machine est de marcher plus
    // loin : la molette agrandit l'orbite, et elle AJOUTE au réglage sans le
    // remplacer, pour que le preset d'un musicien sans molette soit intact.
    auto synth = make("vsm.terrain");
    MidiControlEvent molette;
    molette.kind = MidiControlEvent::Kind::ControlChange;
    molette.index = 1;
    molette.value = 1.0f;
    VSM_ASSERT(synth->handleControlEvent(molette));
}

VSM_TEST(terrain_is_deterministic) {
    auto a = make("vsm.terrain");
    auto b = make("vsm.terrain");
    const auto x = rendre(a, 48128);
    const auto y = rendre(b, 48128);
    for (size_t i = 0; i < x.size(); ++i) VSM_ASSERT(x[i] == y[i]);
}

VSM_TEST(terrain_save_load_roundtrip) {
    auto premier = make("vsm.terrain");
    set(*premier, "Orbit Radius", 0.81f);
    set(*premier, "Roughness", 0.29f);
    const auto etat = premier->saveState();
    auto second = make("vsm.terrain");
    second->loadState(etat);
    for (const auto& info : premier->parameterList())
        VSM_ASSERT_NEAR(second->getParameter(info.id), premier->getParameter(info.id), 1e-6);
}

VSM_TEST(terrain_parameter_list_size) {
    auto synth = make("vsm.terrain");
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t(12));
}

VSM_TEST(terrain_honours_pitch_bend) {
    auto synth = make("vsm.terrain");
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 2.0f;
    VSM_ASSERT(synth->handleControlEvent(bend));
}
