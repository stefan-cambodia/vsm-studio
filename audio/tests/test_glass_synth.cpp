#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.glass` — l'harmonica de verre : un son qui met des SECONDES à naître.
//
// Le parc savait déjà frotter (`vsm.string` et son archet), mais sur un GUIDE
// D'ONDES, qui s'établit en quelques dizaines de millisecondes. Un bol de
// verre est un résonateur à Q très élevé : l'énergie qu'un décrochement du
// doigt lui apporte est minuscule devant celle qu'il faut accumuler, et c'est
// ce qui fait son établissement interminable.

namespace {
constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeGlass(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.glass");
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

/// Rend une note frottée, avec ou sans relâchement à 2 secondes.
std::vector<float> frotter(float pression, bool relacher, int frames) {
    auto synth = makeGlass();
    set(*synth, "Finger Pressure", pression);
    set(*synth, "Velocity Sensitivity", 0.0f);
    set(*synth, "Ring Time", 8.0f);
    std::vector<float> left(static_cast<size_t>(frames), 0.0f), right(static_cast<size_t>(frames), 0.0f);
    constexpr int kBlock = 256;
    const int relachement = 96000;
    for (int start = 0; start + kBlock <= frames; start += kBlock) {
        std::vector<MidiNoteEvent> block;
        if (start == 0) block.push_back({MidiNoteEvent::Kind::NoteOn, 0, 0, 69, 110});
        if (relacher && start <= relachement && relachement < start + kBlock)
            block.push_back({MidiNoteEvent::Kind::NoteOff, relachement - start, 0, 69, 0});
        synth->process(block.empty() ? nullptr : block.data(), static_cast<int>(block.size()),
                       left.data() + start, right.data() + start, kBlock);
    }
    return left;
}

/// Combien le son a GRANDI entre 0,3 s et 1,5 s. Au-dessus de un, il monte
/// encore ; à un, il est déjà établi.
double croissance(float pression) {
    const auto audio = frotter(pression, false, 240000);
    return rms(audio, 72000, 8192) / std::max(1e-12, rms(audio, 14400, 8192));
}
} // namespace

VSM_TEST(glass_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.glass"));
}

VSM_TEST(glass_silent_with_no_events) {
    auto synth = makeGlass();
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(glass_note_produces_sound_and_stays_finite) {
    const auto audio = frotter(0.6f, false, 96000);
    VSM_ASSERT(peakAbs(audio) > 0.01f);
    for (float v : audio) VSM_ASSERT(std::isfinite(v));
}

// --- Trait 1 : le son met des SECONDES à naître ---------------------------

VSM_TEST(glass_takes_seconds_to_speak) {
    // Une seconde et demie après l'attaque, le son est encore en train de
    // monter — et pas d'un peu. Aucune autre machine entretenue du parc n'est
    // dans ce cas : elles ont atteint leur régime bien avant.
    VSM_ASSERT(croissance(0.2f) > 5.0);   // mesuré : 31,7
}

// --- Trait 2, LE trait : la vitesse dépend de la PRESSION ------------------

VSM_TEST(glass_speaks_faster_when_you_press_harder) {
    // Ce que ce test sépare : une enveloppe d'attaque lente mettrait le MÊME
    // temps à toutes les nuances. Ici le doigt décide, et c'est ce qui fait
    // qu'on joue de cet instrument au lieu de le déclencher.
    const double doux = croissance(0.2f);
    const double moyen = croissance(0.5f);
    const double fort = croissance(1.0f);

    VSM_ASSERT(doux > moyen * 5.0);      // mesuré : 31,7 contre 2,05
    VSM_ASSERT(moyen > fort * 1.5);      // 2,05 contre 1,00
    // Pressé à fond, le verre est établi dès la première demi-seconde.
    VSM_ASSERT(fort < 1.2);
}

// --- Trait 3 : lâcher ne coupe pas ----------------------------------------

VSM_TEST(glass_keeps_ringing_after_the_finger_lifts) {
    // Le Q est tel que le verre continue. C'est le contraire exact de
    // `vsm.clavichord`, dont le feutre coupe le son en cinquante
    // millisecondes — le parc a maintenant les deux extrêmes, mesurés au même
    // protocole.
    const auto audio = frotter(0.6f, true, 240000);
    const double avant = rms(audio, 88000, 8192);
    const double apresUneSeconde = rms(audio, 144000, 8192);
    VSM_ASSERT(avant > 0.01);
    VSM_ASSERT(apresUneSeconde > avant * 0.5);
}

VSM_TEST(glass_stays_at_the_level_of_the_rest_of_the_park) {
    // Le cycle limite de la friction s'établit vers ±2 : sans mise à l'échelle,
    // cette machine saturerait tout projet où on l'ajoute. Le niveau est donc
    // vérifié, pas seulement réglé.
    for (float pression : {0.2f, 0.6f, 1.0f}) {
        const auto audio = frotter(pression, false, 192000);
        VSM_ASSERT(peakAbs(audio) < 0.8f);
        for (float v : audio) VSM_ASSERT(std::isfinite(v));
    }
}

VSM_TEST(glass_is_deterministic) {
    const auto a = frotter(0.6f, false, 48128);
    const auto b = frotter(0.6f, false, 48128);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT(a[i] == b[i]);
}

VSM_TEST(glass_save_load_roundtrip) {
    auto premier = makeGlass();
    set(*premier, "Finger Pressure", 0.31f);
    set(*premier, "Ring Time", 14.0f);
    const auto etat = premier->saveState();
    auto second = makeGlass();
    second->loadState(etat);
    for (const auto& info : premier->parameterList())
        VSM_ASSERT_NEAR(second->getParameter(info.id), premier->getParameter(info.id), 1e-6);
}

VSM_TEST(glass_parameter_list_size) {
    auto synth = makeGlass();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t(8));
}

VSM_TEST(glass_honours_pitch_bend_and_channel_pressure) {
    auto synth = makeGlass();
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 2.0f;
    VSM_ASSERT(synth->handleControlEvent(bend));
    MidiControlEvent doigt;
    doigt.kind = MidiControlEvent::Kind::ChannelPressure;
    doigt.value = 0.7f;
    VSM_ASSERT(synth->handleControlEvent(doigt));
}
