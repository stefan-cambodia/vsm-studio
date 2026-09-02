#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.theremin` — un instrument sans touches, donc sans sauts.
//
// Toutes les machines du parc ont un portamento RÉGLABLE, c'est-à-dire
// optionnel et nul par défaut. Celle-ci ne peut pas sauter : il n'y a rien à
// toucher, la main traverse toutes les notes du milieu, et on les entend.

namespace {
constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeTheremin(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.theremin");
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
double hauteur(const std::vector<float>& x, size_t from) {
    double meilleur = 0.0, retenu = 0.0;
    for (double hz = 180.0; hz < 900.0; hz *= 1.002) {
        const double m = magnitudeAt(x, from, 8192, hz);
        if (m > meilleur) { meilleur = m; retenu = hz; }
    }
    return retenu;
}
double rms(const std::vector<float>& x, size_t from, size_t count) {
    double e = 0.0;
    for (size_t i = from; i < from + count && i < x.size(); ++i)
        e += static_cast<double>(x[i]) * x[i];
    return std::sqrt(e / static_cast<double>(count));
}

/// Joue la note 57 (220 Hz), puis éventuellement la note 69 (440 Hz) à une
/// seconde. `pression` négative = aucune main gauche envoyée.
std::vector<float> jouer(int velocity, float pression, bool deuxNotes) {
    auto synth = makeTheremin();
    set(*synth, "Vibrato Depth", 0.0f);
    set(*synth, "Glide", 0.4f);
    if (pression >= 0.0f) {
        MidiControlEvent e;
        e.kind = MidiControlEvent::Kind::ChannelPressure;
        e.value = pression;
        synth->handleControlEvent(e);
    }
    std::vector<float> left(144000, 0.0f), right(144000, 0.0f);
    constexpr int kBlock = 256;
    const int seconde = 48000;
    for (int start = 0; start + kBlock <= 144000; start += kBlock) {
        std::vector<MidiNoteEvent> block;
        if (start == 0)
            block.push_back({MidiNoteEvent::Kind::NoteOn, 0, 0, 57, static_cast<uint8_t>(velocity)});
        if (deuxNotes && start <= seconde && seconde < start + kBlock)
            block.push_back({MidiNoteEvent::Kind::NoteOn, seconde - start, 0, 69,
                             static_cast<uint8_t>(velocity)});
        synth->process(block.empty() ? nullptr : block.data(), static_cast<int>(block.size()),
                       left.data() + start, right.data() + start, kBlock);
    }
    return left;
}
} // namespace

VSM_TEST(theremin_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.theremin"));
}

VSM_TEST(theremin_silent_with_no_events) {
    auto synth = makeTheremin();
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(theremin_note_produces_sound_and_stays_finite) {
    const auto audio = jouer(100, -1.0f, false);
    VSM_ASSERT(peakAbs(audio) > 0.01f);
    for (float v : audio) VSM_ASSERT(std::isfinite(v));
}

// --- LE trait : la hauteur ne saute JAMAIS --------------------------------

VSM_TEST(theremin_never_jumps_between_notes) {
    // De 220 à 440 Hz, la main traverse tout : à mi-parcours elle est
    // STRICTEMENT entre les deux, et loin des deux. Sur n'importe quelle autre
    // machine du parc réglée par défaut, elle serait déjà arrivée.
    const auto audio = jouer(100, -1.0f, true);
    const double avant = hauteur(audio, static_cast<size_t>(0.90 * kSampleRate));
    const double milieu = hauteur(audio, static_cast<size_t>(1.05 * kSampleRate));
    const double apres = hauteur(audio, static_cast<size_t>(2.50 * kSampleRate));

    VSM_ASSERT(std::abs(avant - 220.0) < 8.0);
    VSM_ASSERT(std::abs(apres - 440.0) < 15.0);
    // Le point décisif : à mi-chemin, elle est au MILIEU, à plus d'un
    // demi-ton de chacune des deux notes (mesuré : 284 Hz).
    VSM_ASSERT(milieu > 240.0 && milieu < 400.0);
}

VSM_TEST(theremin_cannot_be_told_not_to_glide) {
    // Le glissando n'est pas une option qu'on désactive : la borne basse du
    // réglage est à vingt millisecondes, et c'est la définition de
    // l'instrument. Même au minimum, la main met du temps.
    auto synth = makeTheremin();
    const auto id = byName(*synth, "Glide");
    synth->setParameter(id, 0.0f);            // on essaie de l'annuler...
    VSM_ASSERT(synth->getParameter(id) >= 0.0f);
    for (const auto& info : synth->parameterList())
        if (info.name == "Glide") VSM_ASSERT(info.minValue >= 0.02f);   // ... la course l'interdit
}

// --- Second trait : la vélocité ne dit RIEN d'un thérémine -----------------

VSM_TEST(theremin_ignores_velocity_because_there_is_nothing_to_strike) {
    // Il n'y a pas de frappe : la vélocité MIDI n'a aucun sens ici. Deux
    // rendus aux vélocités extrêmes donnent le MÊME signal.
    const auto doux = jouer(10, -1.0f, false);
    const auto fort = jouer(127, -1.0f, false);
    const double a = rms(doux, 72000, 16384);
    const double b = rms(fort, 72000, 16384);
    VSM_ASSERT(a > 0.01);
    VSM_ASSERT(std::abs(b - a) < a * 0.01);   // mesuré : rapport 1,0000
}

VSM_TEST(theremin_left_hand_makes_the_whole_dynamic) {
    // Ce que la vélocité ne fait pas, la MAIN GAUCHE le fait — et elle en fait
    // tout : l'attaque, les nuances, l'extinction. C'est un geste continu, et
    // c'est ce qui remplace ici la frappe.
    const double basse = rms(jouer(100, 0.2f, false), 72000, 16384);
    const double haute = rms(jouer(100, 0.9f, false), 72000, 16384);
    VSM_ASSERT(haute > basse * 3.0);   // mesuré : 4,50
}

VSM_TEST(theremin_is_monophonic_because_a_hand_is_in_one_place) {
    // Jouer un accord sur un thérémine n'a aucun sens. Une seconde note ne
    // prend pas une voix de plus : elle DÉPLACE la main.
    const auto audio = jouer(100, -1.0f, true);
    auto synth = makeTheremin();
    std::vector<float> l(256, 0.0f), r(256, 0.0f);
    const MidiNoteEvent deux[2] = {
        {MidiNoteEvent::Kind::NoteOn, 0, 0, 57, 100},
        {MidiNoteEvent::Kind::NoteOn, 0, 0, 69, 100}};
    synth->process(deux, 2, l.data(), r.data(), 256);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 1);
    (void)audio;
}

VSM_TEST(theremin_is_deterministic) {
    const auto a = jouer(100, 0.7f, true);
    const auto b = jouer(100, 0.7f, true);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT(a[i] == b[i]);
}

VSM_TEST(theremin_save_load_roundtrip) {
    auto premier = makeTheremin();
    set(*premier, "Glide", 0.77f);
    set(*premier, "Warmth", 0.61f);
    const auto etat = premier->saveState();
    auto second = makeTheremin();
    second->loadState(etat);
    for (const auto& info : premier->parameterList())
        VSM_ASSERT_NEAR(second->getParameter(info.id), premier->getParameter(info.id), 1e-6);
}

VSM_TEST(theremin_parameter_list_size) {
    auto synth = makeTheremin();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t(7));
}
