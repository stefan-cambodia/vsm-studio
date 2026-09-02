#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.chebyshev` — le spectre se COMMANDE.
//
// Ce que cette suite verrouille est une propriété EXACTE, pas un effet
// approché : `T_n(cos θ) = cos(n·θ)`. Poids sur le seul rang 3, et le rendu
// ne contient que l'harmonique 3 — aucune autre machine du parc ne peut
// prétendre à un spectre commandé, et c'est ce qui justifie qu'elle existe.

namespace {
constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeChebyshev(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.chebyshev");
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

/// Réglage de mesure : enveloppe qui tient à plein, index à fond, un seul
/// rang actif à la fois -- on mesure le SHAPER, pas ce qui l'entoure.
void steady(ISynthPlugin& plugin) {
    set(plugin, "Attack", 0.002f);
    set(plugin, "Decay", 0.005f);
    set(plugin, "Sustain", 1.0f);
    set(plugin, "Index", 1.0f);
    set(plugin, "Velocity to Index", 0.0f);
    for (int n = 1; n <= 8; ++n) set(plugin, "Partial " + std::to_string(n), 0.0f);
}
} // namespace

VSM_TEST(chebyshev_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.chebyshev"));
}

VSM_TEST(chebyshev_silent_with_no_events) {
    auto synth = makeChebyshev();
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(chebyshev_note_produces_sound_and_stays_finite) {
    auto synth = makeChebyshev();
    const auto audio = render(synth, {noteOn(0, 60, 100)}, 24064);
    VSM_ASSERT(peakAbs(audio) > 0.01f);
    for (float v : audio) VSM_ASSERT(std::isfinite(v));
}

// --- LE trait : le spectre est une COMMANDE, exacte ------------------------

VSM_TEST(chebyshev_partial_three_alone_yields_only_harmonic_three) {
    // `T_3(cos θ) = cos(3θ)`, exactement. Poids sur le seul rang 3 : le
    // rendu doit contenir l'harmonique 3 et RIEN d'autre -- ni le
    // fondamental, ni le rang 2. Aucune autre machine du parc ne peut
    // prétendre à cela ; c'est la raison d'être de celle-ci.
    auto synth = makeChebyshev();
    steady(*synth);
    set(*synth, "Partial 3", 1.0f);
    const int note = 45;
    const double f0 = midiToHz(note);
    const auto audio = render(synth, {noteOn(0, static_cast<uint8_t>(note), 127)}, 65536);

    const double h1 = magnitudeAt(audio, 24000, 32768, f0);
    const double h2 = magnitudeAt(audio, 24000, 32768, 2.0 * f0);
    const double h3 = magnitudeAt(audio, 24000, 32768, 3.0 * f0);
    VSM_ASSERT(h3 > 0.02);              // le rang commandé est là
    VSM_ASSERT(h1 < h3 * 0.05);         // le fondamental n'y est PAS
    VSM_ASSERT(h2 < h3 * 0.05);         // ni le rang 2
}

VSM_TEST(chebyshev_each_partial_lands_on_its_own_rank) {
    // La commande vaut pour tous les rangs, pas seulement le troisième :
    // chaque poids isolé doit rendre SON harmonique. Le test balaye les
    // rangs 2, 4 et 5, ce qui interdit un heureux hasard sur un seul.
    for (int rang : {2, 4, 5}) {
        auto synth = makeChebyshev();
        steady(*synth);
        set(*synth, "Partial " + std::to_string(rang), 1.0f);
        const int note = 45;
        const double f0 = midiToHz(note);
        const auto audio = render(synth, {noteOn(0, static_cast<uint8_t>(note), 127)}, 65536);
        const double vise = magnitudeAt(audio, 24000, 32768, rang * f0);
        const double fondamental = magnitudeAt(audio, 24000, 32768, f0);
        VSM_ASSERT(vise > 0.02);
        VSM_ASSERT(fondamental < vise * 0.10);
    }
}

VSM_TEST(chebyshev_index_opens_the_timbre_without_any_filter) {
    // LE TRAIT MUSICAL : sous l'amplitude 1, un rang n se comporte en A^n.
    // Baisser l'index effondre donc les rangs hauts bien plus vite que les
    // bas -- une note qui meurt s'assombrit d'elle-même, sans filtre. Aucun
    // paramètre de filtre n'existe d'ailleurs sur cette machine.
    auto brillance = [&](float index) {
        auto synth = makeChebyshev();
        steady(*synth);
        set(*synth, "Partial 1", 1.0f);
        set(*synth, "Partial 5", 1.0f);
        set(*synth, "Index", index);
        const int note = 45;
        const double f0 = midiToHz(note);
        const auto audio = render(synth, {noteOn(0, static_cast<uint8_t>(note), 127)}, 65536);
        return magnitudeAt(audio, 24000, 32768, 5.0 * f0)
             / std::max(1e-9, magnitudeAt(audio, 24000, 32768, f0));
    };
    VSM_ASSERT(brillance(1.0f) > brillance(0.35f) * 5.0);
}

VSM_TEST(chebyshev_high_notes_do_not_alias) {
    // Le repliement est LE piège de cette famille : un rang 8 occupe huit
    // fois la bande. La machine travaille suréchantillonnée ; le test le
    // vérifie là où ça compte -- une note haute, tous les rangs actifs, et
    // rien d'inharmonique sous le fondamental.
    auto synth = makeChebyshev();
    steady(*synth);
    for (int n = 1; n <= 8; ++n) set(*synth, "Partial " + std::to_string(n), 1.0f);
    const int note = 93;                 // la6, 1760 Hz : le rang 8 est à 14 kHz
    const double f0 = midiToHz(note);
    const auto audio = render(synth, {noteOn(0, static_cast<uint8_t>(note), 127)}, 65536);

    const double fondamental = magnitudeAt(audio, 24000, 32768, f0);
    // Sous le fondamental, il ne doit y avoir QUE du silence : tout ce qui
    // s'y trouverait serait un rang haut redescendu par repliement.
    double pireSousLeFondamental = 0.0;
    for (double hz = 200.0; hz < f0 * 0.9; hz += 137.0)
        pireSousLeFondamental = std::max(pireSousLeFondamental,
                                         magnitudeAt(audio, 24000, 32768, hz));
    VSM_ASSERT(fondamental > 0.01);
    VSM_ASSERT(pireSousLeFondamental < fondamental * 0.05);
}

VSM_TEST(chebyshev_is_deterministic) {
    auto premier = makeChebyshev();
    const auto a = render(premier, {noteOn(0, 60, 100)}, 24064);
    auto second = makeChebyshev();
    const auto b = render(second, {noteOn(0, 60, 100)}, 24064);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT(a[i] == b[i]);
}

VSM_TEST(chebyshev_save_load_roundtrip) {
    auto premier = makeChebyshev();
    set(*premier, "Index", 0.42f);
    set(*premier, "Partial 6", 0.75f);
    const auto etat = premier->saveState();
    auto second = makeChebyshev();
    second->loadState(etat);
    for (const auto& info : premier->parameterList())
        VSM_ASSERT_NEAR(second->getParameter(info.id), premier->getParameter(info.id), 1e-6);
}

VSM_TEST(chebyshev_parameter_list_size) {
    auto synth = makeChebyshev();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t(15));
}
