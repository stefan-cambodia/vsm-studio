#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.vector` — la synthèse VECTORIELLE : quatre timbres aux coins d'un
// carré, un point qui s'y déplace, et le TRAJET qui devient le timbre.
// Ajoutée au titre du § 7 du CDC machines (le jeu, pas la reconstruction) ;
// ce que cette suite verrouille est la définition de la famille : les coins
// sont PURS (une seule source y sonne), et l'orbite change la couleur SANS
// qu'aucun filtre ne bouge.

namespace {
constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeVector(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.vector");
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
float peakAbs(const std::vector<float>& b) {
    float p = 0.0f; for (float v : b) p = std::max(p, std::abs(v)); return p;
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

/// Réglage de mesure : pas de dérive, filtre grand ouvert, désaccords à zéro,
/// orbite immobile -- on mesure le MÉLANGE, pas ce qui l'anime.
void steady(ISynthPlugin& plugin) {
    set(plugin, "Analog Character", 0.0f);
    set(plugin, "Filter Cutoff", 16000.0f);
    set(plugin, "Filter Resonance", 0.0f);
    set(plugin, "Filter Env Amount", 0.0f);
    set(plugin, "Orbit Depth", 0.0f);
    set(plugin, "A Detune", 0.0f);
    set(plugin, "B Detune", 0.0f);
    set(plugin, "C Detune", 0.0f);
    set(plugin, "D Detune", 0.0f);
    set(plugin, "Amp Attack", 0.005f);
}
} // namespace

VSM_TEST(vector_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.vector"));
}

VSM_TEST(vector_silent_with_no_events) {
    auto synth = makeVector();
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(vector_note_produces_sound_and_stays_finite) {
    auto synth = makeVector();
    const auto audio = render(synth, {noteOn(0, 60, 100)}, 24064);
    VSM_ASSERT(peakAbs(audio) > 0.01f);
    for (float v : audio) VSM_ASSERT(std::isfinite(v));
}

// --- LE trait distinctif, première moitié : les coins sont PURS ------------

VSM_TEST(vector_corners_are_pure_sources) {
    // Le mélange bilinéaire annule trois poids sur quatre à chaque coin :
    // en (0,0) seul A sonne, en (1,0) seul B. Avec A en sinus et B en scie,
    // le déplacement d'un coin à l'autre fait apparaître les harmoniques --
    // aucun filtre n'a bougé, c'est la DÉFINITION du vecteur.
    auto mesurer = [&](float x, float y) {
        auto synth = makeVector();
        steady(*synth);
        set(*synth, "A Shape", 0.0f);   // sinus
        set(*synth, "B Shape", 2.0f);   // scie
        set(*synth, "Vector X", x);
        set(*synth, "Vector Y", y);
        const auto audio = render(synth, {noteOn(0, 57, 100)}, 49152);
        const double f0 = midiToHz(57);
        const double h1 = magnitudeAt(audio, 24000, 16384, f0);
        const double h2 = magnitudeAt(audio, 24000, 16384, 2.0 * f0);
        const double h3 = magnitudeAt(audio, 24000, 16384, 3.0 * f0);
        return std::array<double, 3>{h1, h2, h3};
    };

    const auto coinA = mesurer(0.0f, 0.0f);
    const auto coinB = mesurer(1.0f, 0.0f);
    VSM_ASSERT(coinA[0] > 0.01);                    // le sinus sonne
    VSM_ASSERT(coinA[1] < coinA[0] * 0.02);         // ... sans harmoniques
    VSM_ASSERT(coinA[2] < coinA[0] * 0.02);
    VSM_ASSERT(coinB[1] > coinB[0] * 0.25);         // la scie en a
    VSM_ASSERT(coinB[2] > coinB[0] * 0.15);
}

// --- LE trait, seconde moitié : le TRAJET est le timbre --------------------

VSM_TEST(vector_the_orbit_moves_the_timbre_without_any_filter) {
    // Orbite lente et large entre un coin sinus et un coin scie : deux
    // fenêtres du MÊME rendu, à la même hauteur, n'ont pas le même contenu
    // harmonique. La couleur bouge, le filtre est resté grand ouvert.
    auto synth = makeVector();
    steady(*synth);
    set(*synth, "A Shape", 0.0f);
    set(*synth, "B Shape", 2.0f);
    set(*synth, "C Shape", 0.0f);
    set(*synth, "D Shape", 2.0f);
    set(*synth, "Vector X", 0.5f);
    set(*synth, "Vector Y", 0.5f);
    set(*synth, "Orbit Depth", 1.0f);
    set(*synth, "Orbit Rate", 0.5f);     // une période = 2 s
    const auto audio = render(synth, {noteOn(0, 57, 100)}, 96256);

    const double f0 = midiToHz(57);
    // Phase 0 : le point part vers +x (la scie B) ; une demi-période plus
    // tard il est vers -x (le sinus A). Fenêtres d'un quart de seconde.
    const double hRiche = magnitudeAt(audio, 4800, 12000, 2.0 * f0)
                        / std::max(1e-9, magnitudeAt(audio, 4800, 12000, f0));
    const double hPur = magnitudeAt(audio, 52800, 12000, 2.0 * f0)
                      / std::max(1e-9, magnitudeAt(audio, 52800, 12000, f0));
    VSM_ASSERT(hRiche > hPur * 3.0);
}

VSM_TEST(vector_is_deterministic) {
    auto premier = makeVector();
    set(*premier, "Analog Character", 1.0f);
    set(*premier, "Orbit Depth", 0.7f);
    const auto a = render(premier, {noteOn(0, 60, 100)}, 24064);
    auto second = makeVector();
    set(*second, "Analog Character", 1.0f);
    set(*second, "Orbit Depth", 0.7f);
    const auto b = render(second, {noteOn(0, 60, 100)}, 24064);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT(a[i] == b[i]);
}

VSM_TEST(vector_save_load_roundtrip) {
    auto premier = makeVector();
    set(*premier, "Vector X", 0.8f);
    set(*premier, "B Shape", 1.3f);
    set(*premier, "Filter Cutoff", 900.0f);
    const auto etat = premier->saveState();
    auto second = makeVector();
    second->loadState(etat);
    for (const auto& info : premier->parameterList())
        VSM_ASSERT_NEAR(second->getParameter(info.id), premier->getParameter(info.id), 1e-6);
}

VSM_TEST(vector_parameter_list_size) {
    auto synth = makeVector();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t(27));
}

VSM_TEST(vector_honours_pitch_bend_and_refuses_the_wheel) {
    // Un synthé se plie (§ 10) ; mais cette machine n'a pas de LFO vers la
    // hauteur -- son mouvement est le vecteur -- et elle refuse le CC 1 en
    // le disant plutôt que de faire semblant.
    auto synth = makeVector();
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 2.0f;
    VSM_ASSERT(synth->handleControlEvent(bend));
    MidiControlEvent cc;
    cc.kind = MidiControlEvent::Kind::ControlChange;
    cc.index = 1;
    cc.value = 1.0f;
    VSM_ASSERT(!synth->handleControlEvent(cc));
}
