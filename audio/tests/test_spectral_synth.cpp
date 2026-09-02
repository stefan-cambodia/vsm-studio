#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include "vsm/audio/dsp/RealFft.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.spectral` — on ÉCRIT le spectre, et une transformée inverse rend le
// signal. La dernière grande famille qui manquait au parc.
//
// Ce que rien d'autre ne peut faire : des centaines de raies à des fréquences
// QUELCONQUES pour un coût constant. `vsm.additive` pose des rangs entiers,
// `vsm.modal` a vingt-quatre modes, `vsm.chebyshev` huit rangs harmoniques.

namespace {
constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeSpectral(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.spectral");
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

std::vector<float> jouer(float etirement, int partiels, int nbNotes) {
    auto synth = makeSpectral();
    set(*synth, "Sustain", 1.0f);
    set(*synth, "Attack", 0.01f);
    set(*synth, "Stretch", etirement);
    set(*synth, "Partials", static_cast<float>(partiels));
    set(*synth, "Velocity Sensitivity", 0.0f);
    std::vector<MidiNoteEvent> ev;
    for (int k = 0; k < nbNotes; ++k)
        ev.push_back({MidiNoteEvent::Kind::NoteOn, 0, 0,
                      static_cast<uint8_t>(45 + 7 * k), 110});
    std::vector<float> left(96000, 0.0f), right(96000, 0.0f);
    for (int s = 0; s + 256 <= 96000; s += 256)
        synth->process(s == 0 ? ev.data() : nullptr, s == 0 ? static_cast<int>(ev.size()) : 0,
                       left.data() + s, right.data() + s, 256);
    return left;
}
constexpr double kF0 = 110.0;   // note 45
} // namespace

VSM_TEST(spectral_ifft_reconstructs_a_known_signal) {
    // La brique d'abord : une raie unique doit rendre exactement un cosinus.
    // Sans cette garantie, tout ce qui suit ne voudrait rien dire.
    vsm::audio::dsp::RealIfft<64> ifft;
    float re[33] = {0}, im[33] = {0}, out[64] = {0};
    re[4] = 32.0f;
    ifft.inverse(re, im, out);
    for (int i = 0; i < 64; ++i)
        VSM_ASSERT(std::abs(out[i] - std::cos(2.0 * M_PI * 4 * i / 64.0)) < 1e-5);
}

VSM_TEST(spectral_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.spectral"));
}

VSM_TEST(spectral_silent_with_no_events) {
    auto synth = makeSpectral();
    std::vector<float> left(2048, 0.0f), right(2048, 0.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 2048);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(spectral_note_produces_sound_and_stays_finite) {
    const auto audio = jouer(1.0f, 32, 1);
    VSM_ASSERT(peakAbs(audio) > 0.01f);
    for (float v : audio) VSM_ASSERT(std::isfinite(v));
}

// --- LE trait : des partiels à des fréquences QUELCONQUES ------------------

VSM_TEST(spectral_stretched_partials_land_where_no_harmonic_series_can) {
    // À `Stretch` 1,3, le partiel k est à `k^1,3·f0` : les fréquences entières
    // sont VIDES. Ni `vsm.additive` (rangs entiers) ni `vsm.chebyshev` (huit
    // rangs harmoniques) ne peuvent produire cela.
    const auto audio = jouer(1.3f, 32, 1);
    for (int k : {2, 3, 5}) {
        const double entier = k * kF0;
        const double etire = kF0 * std::pow(static_cast<double>(k), 1.3);
        const double aEntier = magnitudeAt(audio, 24000, 16384, entier);
        const double aEtire = magnitudeAt(audio, 24000, 16384, etire);
        VSM_ASSERT(aEtire > 1e-5);
        VSM_ASSERT(aEntier < aEtire * 0.05);   // mesuré : 0,00000 exactement
    }
}

VSM_TEST(spectral_at_stretch_one_it_is_harmonic_again) {
    // Le contrôle, sur la course du même réglage : à 1,0 les partiels
    // retombent sur les rangs entiers.
    const auto audio = jouer(1.0f, 32, 1);
    for (int k : {2, 3, 5})
        VSM_ASSERT(magnitudeAt(audio, 24000, 16384, k * kF0) > 1e-5);
}

// --- Le coût ne dépend pas du nombre de partiels ni de notes --------------

VSM_TEST(spectral_hundreds_of_partials_cost_the_same_as_eight) {
    // C'est l'argument même de la famille : la transformée coûte la même chose
    // qu'on y dépose huit raies ou deux cent cinquante-six. On ne mesure pas
    // le temps ici (un test ne doit pas dépendre de la charge de la machine),
    // mais son corollaire audible : le NIVEAU ne s'effondre pas et le signal
    // reste borné quand on multiplie les partiels par trente-deux.
    for (int partiels : {8, 64, 256}) {
        const auto audio = jouer(1.15f, partiels, 1);
        VSM_ASSERT(peakAbs(audio) > 0.05f);
        VSM_ASSERT(peakAbs(audio) < 1.5f);
        for (float v : audio) VSM_ASSERT(std::isfinite(v));
    }
}

VSM_TEST(spectral_polyphony_is_free_there_are_no_voices) {
    // Toutes les notes déposent leurs partiels dans LE MÊME spectre : il n'y a
    // pas de voix à voler, et six notes coûtent une transformée comme une.
    for (int notes : {1, 3, 6}) {
        const auto audio = jouer(1.0f, 32, notes);
        VSM_ASSERT(peakAbs(audio) < 2.5f);
        for (float v : audio) VSM_ASSERT(std::isfinite(v));
    }
    auto synth = makeSpectral();
    std::vector<float> l(2048, 0.0f), r(2048, 0.0f);
    const MidiNoteEvent six[6] = {
        {MidiNoteEvent::Kind::NoteOn, 0, 0, 45, 100}, {MidiNoteEvent::Kind::NoteOn, 0, 0, 52, 100},
        {MidiNoteEvent::Kind::NoteOn, 0, 0, 59, 100}, {MidiNoteEvent::Kind::NoteOn, 0, 0, 66, 100},
        {MidiNoteEvent::Kind::NoteOn, 0, 0, 73, 100}, {MidiNoteEvent::Kind::NoteOn, 0, 0, 80, 100}};
    synth->process(six, 6, l.data(), r.data(), 2048);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 6);
}

// --- Le piège de la famille : le raccord entre trames ---------------------

VSM_TEST(spectral_no_click_at_the_frame_boundary) {
    // Une synthèse par trames claque à chaque raccord si le recouvrement est
    // mal fait. La fenêtre de Hann à saut de moitié somme à une constante, et
    // les phases avancent d'une trame à l'autre : le test vérifie qu'aucun
    // saut d'échantillon ne dépasse franchement les autres, ce qui serait la
    // signature d'une discontinuité périodique.
    const auto audio = jouer(1.15f, 64, 1);
    double sauMax = 0.0, sauMoyen = 0.0;
    size_t n = 0;
    for (size_t i = 24001; i < 72000; ++i) {
        const double d = std::abs(static_cast<double>(audio[i]) - audio[i - 1]);
        sauMax = std::max(sauMax, d);
        sauMoyen += d;
        ++n;
    }
    sauMoyen /= static_cast<double>(n);
    VSM_ASSERT(sauMoyen > 0.0);
    // Un clic de raccord se verrait comme un saut dix fois plus grand que le
    // reste ; on tolère un facteur six, qu'un signal riche atteint déjà.
    VSM_ASSERT(sauMax < sauMoyen * 6.0);
}

VSM_TEST(spectral_is_deterministic) {
    const auto a = jouer(1.15f, 64, 2);
    const auto b = jouer(1.15f, 64, 2);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT(a[i] == b[i]);
}

VSM_TEST(spectral_save_load_roundtrip) {
    auto premier = makeSpectral();
    set(*premier, "Stretch", 1.42f);
    set(*premier, "Partials", 199.0f);
    const auto etat = premier->saveState();
    auto second = makeSpectral();
    second->loadState(etat);
    for (const auto& info : premier->parameterList())
        VSM_ASSERT_NEAR(second->getParameter(info.id), premier->getParameter(info.id), 1e-6);
}

VSM_TEST(spectral_parameter_list_size) {
    auto synth = makeSpectral();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t(10));
}

VSM_TEST(spectral_honours_pitch_bend) {
    auto synth = makeSpectral();
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 2.0f;
    VSM_ASSERT(synth->handleControlEvent(bend));
}
