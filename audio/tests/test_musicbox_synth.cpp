#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include "musicbox/MusicBoxSynth.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.musicbox` — la seule machine du parc qui REFUSE une note.
//
// Chaque note est une lame qu'une goupille soulève puis lâche. Redemandée
// avant que la lame soit revenue, elle ne sonne pas du tout : il n'y a rien à
// pincer. Toutes les autres machines du parc acceptent n'importe quel débit.

namespace {
constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeBox(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.musicbox");
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
double rms(const std::vector<float>& x, size_t from, size_t count) {
    double e = 0.0;
    for (size_t i = from; i < from + count && i < x.size(); ++i)
        e += static_cast<double>(x[i]) * x[i];
    return std::sqrt(e / static_cast<double>(count));
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

/// Deux frappes de la MÊME note, séparées de `ecart` secondes. Renvoie
/// l'audio et le nombre de notes refusées.
std::pair<std::vector<float>, int> deuxFrappes(double ecart, float retour = 0.18f) {
    auto synth = makeBox();
    set(*synth, "Return Time", retour);
    std::vector<float> left(96000, 0.0f), right(96000, 0.0f);
    constexpr int kBlock = 256;
    const int seconde = static_cast<int>(ecart * kSampleRate);
    for (int start = 0; start + kBlock <= 96000; start += kBlock) {
        std::vector<MidiNoteEvent> block;
        if (start == 0) block.push_back({MidiNoteEvent::Kind::NoteOn, 0, 0, 72, 110});
        if (start <= seconde && seconde < start + kBlock)
            block.push_back({MidiNoteEvent::Kind::NoteOn, seconde - start, 0, 72, 110});
        synth->process(block.empty() ? nullptr : block.data(), static_cast<int>(block.size()),
                       left.data() + start, right.data() + start, kBlock);
    }
    const auto* box = dynamic_cast<vsm::plugins::musicbox::MusicBoxSynth*>(synth.get());
    return {left, box != nullptr ? box->refusedNotes() : -1};
}
} // namespace

VSM_TEST(musicbox_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.musicbox"));
}

VSM_TEST(musicbox_silent_with_no_events) {
    auto synth = makeBox();
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(musicbox_note_produces_sound_and_stays_finite) {
    const auto [audio, refusees] = deuxFrappes(10.0);   // la seconde n'arrive jamais
    VSM_ASSERT(peakAbs(audio) > 0.01f);
    VSM_ASSERT_EQ(refusees, 0);
    for (float v : audio) VSM_ASSERT(std::isfinite(v));
}

// --- LE trait : une note redemandée trop tôt ne sonne pas ------------------

VSM_TEST(musicbox_refuses_a_note_whose_blade_has_not_come_back) {
    // La goupille passe sous une lame déjà levée : il n'y a rien à pincer.
    // Ce n'est pas une note plus douce, c'est PAS DE NOTE — et la machine le
    // COMPTE, parce qu'une panne muette reste interdite même quand le silence
    // est le comportement juste.
    const auto [audio, refusees] = deuxFrappes(0.10);   // 0,10 s < 0,18 s de retour
    VSM_ASSERT_EQ(refusees, 1);

    // Et cela s'entend : juste après l'instant de la seconde frappe, le niveau
    // suit la décroissance de la première au lieu de repartir.
    const size_t instant = static_cast<size_t>(0.10 * kSampleRate);
    const double avant = rms(audio, instant - 4096, 4096);
    const double apres = rms(audio, instant + 2048, 4096);
    VSM_ASSERT(avant > 0.01);
    VSM_ASSERT(apres < avant * 1.10);      // mesuré : 0,96
}

VSM_TEST(musicbox_accepts_the_note_once_the_blade_is_back) {
    // Le contrôle, et il est du même code : seul l'écart change.
    const auto [audio, refusees] = deuxFrappes(0.30);   // 0,30 s > 0,18 s
    VSM_ASSERT_EQ(refusees, 0);
    const size_t instant = static_cast<size_t>(0.30 * kSampleRate);
    const double avant = rms(audio, instant - 4096, 4096);
    const double apres = rms(audio, instant + 2048, 4096);
    VSM_ASSERT(apres > avant * 1.5);       // mesuré : 2,01
}

VSM_TEST(musicbox_the_return_time_is_what_decides) {
    // Le même écart de 0,10 s passe ou ne passe pas selon la lame : c'est bien
    // le temps de retour qui refuse, et non une limite arbitraire.
    VSM_ASSERT_EQ(deuxFrappes(0.10, 0.18f).second, 1);
    VSM_ASSERT_EQ(deuxFrappes(0.10, 0.05f).second, 0);
}

VSM_TEST(musicbox_two_different_notes_never_block_each_other) {
    // Chaque touche a SA lame : jouer deux notes voisines à toute vitesse est
    // parfaitement possible, et c'est ainsi qu'on joue une boîte à musique.
    auto synth = makeBox();
    std::vector<float> left(48000, 0.0f), right(48000, 0.0f);
    const MidiNoteEvent deux[2] = {
        {MidiNoteEvent::Kind::NoteOn, 0, 0, 72, 110},
        {MidiNoteEvent::Kind::NoteOn, 10, 0, 74, 110}};
    synth->process(deux, 2, left.data(), right.data(), 256);
    const auto* box = dynamic_cast<vsm::plugins::musicbox::MusicBoxSynth*>(synth.get());
    VSM_ASSERT(box != nullptr);
    VSM_ASSERT_EQ(box->refusedNotes(), 0);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 2);
}

// --- Second trait : les partiels d'une lame ENCASTRÉE ----------------------

VSM_TEST(musicbox_second_partial_is_that_of_a_clamped_blade) {
    // 6,267 fois le fondamental : c'est la loi d'une lame encastrée d'un côté,
    // et elle est hors de portée de `vsm.modal`, dont la course couvre
    // [1,866 ; 2,978] sur ce partiel (calcul fait sur son `ratioOf`). Une
    // barre libre aux deux bouts et une lame encastrée sont deux lois, pas
    // deux points d'un même segment.
    const auto [audio, refusees] = deuxFrappes(10.0);
    (void)refusees;
    const double f0 = 440.0 * std::pow(2.0, (72 - 69) / 12.0);
    double meilleur = 0.0, rapport = 0.0;
    for (double x = 3.0; x < 12.0; x += 0.01) {
        const double m = magnitudeAt(audio, 4096, 16384, f0 * x);
        if (m > meilleur) { meilleur = m; rapport = x; }
    }
    VSM_ASSERT(std::abs(rapport - 6.267) < 0.15);   // mesuré : 6,27
    VSM_ASSERT(rapport > 2.978);                     // hors de portée de vsm.modal
}

VSM_TEST(musicbox_is_deterministic) {
    const auto a = deuxFrappes(0.30).first;
    const auto b = deuxFrappes(0.30).first;
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT(a[i] == b[i]);
}

VSM_TEST(musicbox_save_load_roundtrip) {
    auto premier = makeBox();
    set(*premier, "Return Time", 0.44f);
    set(*premier, "Brightness", 0.71f);
    const auto etat = premier->saveState();
    auto second = makeBox();
    second->loadState(etat);
    for (const auto& info : premier->parameterList())
        VSM_ASSERT_NEAR(second->getParameter(info.id), premier->getParameter(info.id), 1e-6);
}

VSM_TEST(musicbox_parameter_list_size) {
    auto synth = makeBox();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t(6));
}
