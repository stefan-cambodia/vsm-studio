#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.plate` — le gong, seul objet du parc dont la brillance MONTE.
//
// Partout ailleurs — `vsm.modal`, `vsm.membrane`, `vsm.perc`, la cymbale de
// `vsm.drums` — les modes sont INDÉPENDANTS et ne font que décroître : aucun
// chemin ne mène de l'énergie d'un mode grave vers un mode aigu, et le son ne
// peut donc que s'assombrir. Cette suite mesure le chemin qui manquait.

namespace {
constexpr double kSampleRate = 48000.0;

SynthPluginPtr makePlate(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.plate");
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
double energieBande(const std::vector<float>& x, size_t from, size_t count, double lo, double hi) {
    double somme = 0.0;
    for (double hz = lo; hz < hi; hz *= 1.10) {
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < count && from + i < x.size(); ++i) {
            const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / static_cast<double>(count));
            const double ph = 2.0 * M_PI * hz * static_cast<double>(i) / kSampleRate;
            re += w * static_cast<double>(x[from + i]) * std::cos(ph);
            im += w * static_cast<double>(x[from + i]) * std::sin(ph);
        }
        somme += std::sqrt(re * re + im * im);
    }
    return somme;
}
/// La brillance : l'aigu rapporté au grave. Mesurée par BANDES et non par un
/// centroïde, parce qu'un chiffre agrégé cache ce qui bouge dans une bande
/// étroite — la leçon que le banc du sitar a coûté le même jour.
double brillance(const std::vector<float>& x, size_t from) {
    return energieBande(x, from, 16384, 900.0, 8000.0)
         / std::max(1e-9, energieBande(x, from, 16384, 60.0, 400.0));
}

/// Le rapport entre la brillance TARDIVE (1,5 s) et la brillance PRÉCOCE
/// (0,2 s). Au-dessus de un, le son s'éclaircit en durant ; en dessous, il
/// s'assombrit comme tout le reste du parc.
double monteeDeBrillance(float couplage, uint8_t velocity) {
    auto synth = makePlate();
    set(*synth, "Coupling", couplage);
    set(*synth, "Decay", 12.0f);
    set(*synth, "Decay Tilt", 0.3f);
    set(*synth, "Velocity Sensitivity", 0.9f);
    const auto audio = render(synth, {noteOn(0, 40, velocity)}, static_cast<int>(2.5 * kSampleRate));
    return brillance(audio, 9600) > 0.0
         ? brillance(audio, 72000) / std::max(1e-9, brillance(audio, 9600))
         : 0.0;
}
} // namespace

VSM_TEST(plate_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.plate"));
}

VSM_TEST(plate_silent_with_no_events) {
    auto synth = makePlate();
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(plate_note_produces_sound_and_stays_finite) {
    auto synth = makePlate();
    const auto audio = render(synth, {noteOn(0, 40, 110)}, 48000);
    VSM_ASSERT(peakAbs(audio) > 0.01f);
    for (float v : audio) VSM_ASSERT(std::isfinite(v));
}

// --- LE trait, première moitié : la brillance MONTE ------------------------

VSM_TEST(plate_brightness_rises_after_a_hard_strike) {
    // Un tam-tam frappé fort est d'abord sourd, puis s'éclaircit pendant
    // plusieurs secondes. Aucune autre machine du parc ne peut faire cela :
    // leurs modes sont indépendants et ne font que décroître.
    VSM_ASSERT(monteeDeBrillance(0.6f, 127) > 5.0);   // mesuré : environ 130
}

// --- Deuxième moitié, et elle compte autant : ça dépend de la FORCE --------

VSM_TEST(plate_the_rise_depends_on_how_hard_you_strike) {
    // Le couplage est QUADRATIQUE, donc il n'existe qu'aux grandes
    // amplitudes. Sans cette moitié-là, un simple filtre qui s'ouvrirait avec
    // le temps passerait le test précédent — mais il ferait la même chose à
    // toutes les nuances, ce qui serait un effet et non un instrument.
    const double fort = monteeDeBrillance(0.6f, 127);
    const double doux = monteeDeBrillance(0.6f, 30);
    VSM_ASSERT(fort > doux * 5.0);   // mesuré : 129 contre 2,3
}

// --- Le témoin, du même code : sans couplage, ça ne fait que s'assombrir ---

VSM_TEST(plate_without_coupling_it_only_darkens_like_everything_else) {
    // Couplage à zéro, la machine redevient une banque de modes ordinaire —
    // et son témoin est une OPTION, pas une constante éditée entre deux
    // passes. La brillance baisse alors, comme partout ailleurs dans le parc.
    VSM_ASSERT(monteeDeBrillance(0.0f, 127) < 1.0);   // mesuré : 0,88
}

VSM_TEST(plate_stays_bounded_over_the_whole_coupling_range) {
    // Un transfert d'énergie non linéaire est le genre de mécanisme qui
    // diverge, et ce dépôt a déjà payé cinq divergences (§ 33 et § 44
    // d'ARCHITECTURE). Le haut de la course est donc vérifié explicitement :
    // une machine faite pour être CHERCHÉE ne doit pas avoir de zone
    // inutilisable sur la course d'un de ses réglages.
    for (float couplage : {0.0f, 0.5f, 1.0f}) {
        auto synth = makePlate();
        set(*synth, "Coupling", couplage);
        set(*synth, "Decay", 12.0f);
        const auto audio = render(synth, {noteOn(0, 40, 127)}, static_cast<int>(4.0 * kSampleRate));
        for (float v : audio) VSM_ASSERT(std::isfinite(v));
        VSM_ASSERT(peakAbs(audio) < 2.0f);   // mesuré : 0,50 à 0,57
    }
}

VSM_TEST(plate_is_deterministic) {
    auto premier = makePlate();
    const auto a = render(premier, {noteOn(0, 40, 110)}, 48128);
    auto second = makePlate();
    const auto b = render(second, {noteOn(0, 40, 110)}, 48128);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT(a[i] == b[i]);
}

VSM_TEST(plate_save_load_roundtrip) {
    auto premier = makePlate();
    set(*premier, "Coupling", 0.83f);
    set(*premier, "Decay Tilt", 1.7f);
    const auto etat = premier->saveState();
    auto second = makePlate();
    second->loadState(etat);
    for (const auto& info : premier->parameterList())
        VSM_ASSERT_NEAR(second->getParameter(info.id), premier->getParameter(info.id), 1e-6);
}

VSM_TEST(plate_parameter_list_size) {
    auto synth = makePlate();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t(7));
}

VSM_TEST(plate_honours_pitch_bend) {
    auto synth = makePlate();
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 2.0f;
    VSM_ASSERT(synth->handleControlEvent(bend));
}
