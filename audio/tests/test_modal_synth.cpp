#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.modal` — les modes d'un objet frappé, à rapports LIBRES.
//
// Ce que cette suite verrouille est précisément ce qu'aucune autre machine du
// parc ne peut faire : placer un partiel ailleurs qu'à un multiple entier du
// fondamental. `vsm.additive`, la plus proche, étire ses rangs par la loi de
// la corde raide et plafonne à 2,003·f0 ; une barre libre-libre a son second
// mode à 2,76·f0, et c'est mesuré ici.

namespace {
constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeModal(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.modal");
    if (plugin) plugin->initialize(sr, 512);
    return plugin;
}
ParamId byName(const ISynthPlugin& plugin, const std::string& name) {
    for (const auto& info : plugin.parameterList()) if (info.name == name) return info.id;
    return 0;
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
double rmsOf(const std::vector<float>& x, size_t from, size_t to) {
    double s = 0.0; size_t n = 0;
    for (size_t i = from; i < to && i < x.size(); ++i) { s += static_cast<double>(x[i]) * x[i]; ++n; }
    return n ? std::sqrt(s / static_cast<double>(n)) : 0.0;
}
} // namespace

VSM_TEST(modal_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.modal"));
}

VSM_TEST(modal_silent_with_no_events) {
    auto synth = makeModal();
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(modal_note_produces_sound_and_stays_finite) {
    auto synth = makeModal();
    const auto audio = render(synth, {noteOn(0, 60, 100)}, 24064);
    VSM_ASSERT(peakAbs(audio) > 0.01f);
    for (float v : audio) VSM_ASSERT(std::isfinite(v));
}

// --- LE trait distinctif : un partiel AILLEURS qu'à un multiple entier -----

VSM_TEST(modal_bar_puts_its_second_mode_at_two_point_seven_six) {
    // Ce qu'aucune machine du parc ne peut faire. À matériau « barre », le
    // second mode doit être à 2,76·f0 -- et il ne doit RIEN y avoir à 2·f0,
    // là où toute machine harmonique en aurait.
    auto synth = makeModal();
    set(*synth, "Material", 1.0f);        // barre libre-libre
    set(*synth, "Decay", 6.0f);
    set(*synth, "Decay Tilt", 0.0f);      // tous les modes tiennent
    set(*synth, "Mallet Hardness", 1.0f); // maillet dur : les modes hauts vivent
    set(*synth, "Strike Position", 0.28f);
    const int note = 57;
    const double f0 = midiToHz(note);
    const auto audio = render(synth, {noteOn(0, static_cast<uint8_t>(note), 110)}, 98304);

    const double aFondamental = magnitudeAt(audio, 24000, 32768, f0);
    const double aDeuxFois = magnitudeAt(audio, 24000, 32768, 2.0 * f0);
    const double a276 = magnitudeAt(audio, 24000, 32768, 2.7778 * f0);

    VSM_ASSERT(aFondamental > 0.005);              // ça sonne
    VSM_ASSERT(a276 > aFondamental * 0.10);        // le mode de barre est là
    VSM_ASSERT(aDeuxFois < a276 * 0.25);           // et l'harmonique 2 n'y est PAS
}

VSM_TEST(modal_material_sweeps_from_string_to_bar) {
    // Le matériau est un CONTINUUM, pas un sélecteur : à 0 le second partiel
    // est à 2·f0 (une corde), à 1 il est à 2,78·f0 (une barre). Le test
    // mesure les deux bouts, ce qui interdit un réglage qui ne ferait rien.
    auto mesurer = [&](float material, double ratio) {
        auto synth = makeModal();
        set(*synth, "Material", material);
        set(*synth, "Decay", 6.0f);
        set(*synth, "Decay Tilt", 0.0f);
        set(*synth, "Mallet Hardness", 1.0f);
        const int note = 57;
        const double f0 = midiToHz(note);
        const auto audio = render(synth, {noteOn(0, static_cast<uint8_t>(note), 110)}, 98304);
        return magnitudeAt(audio, 24000, 32768, ratio * f0)
             / std::max(1e-9, magnitudeAt(audio, 24000, 32768, f0));
    };
    // À matériau « corde », l'énergie est à 2·f0 et pas à 2,78 ; à matériau
    // « barre », l'inverse.
    VSM_ASSERT(mesurer(0.0f, 2.0) > mesurer(0.0f, 2.7778) * 4.0);
    VSM_ASSERT(mesurer(1.0f, 2.7778) > mesurer(1.0f, 2.0) * 4.0);
}

VSM_TEST(modal_strike_position_kills_the_modes_it_should) {
    // Frapper au MILIEU annule les modes pairs : c'est le peigne
    // `sin(n·π·pos)`, la même physique que le marteau du piano au huitième.
    // Mesuré sur un matériau « corde », où le mode 2 est à 2·f0.
    auto mesurer = [&](float position) {
        auto synth = makeModal();
        set(*synth, "Material", 0.0f);
        set(*synth, "Decay", 6.0f);
        set(*synth, "Decay Tilt", 0.0f);
        set(*synth, "Mallet Hardness", 1.0f);
        set(*synth, "Strike Position", position);
        const int note = 57;
        const double f0 = midiToHz(note);
        const auto audio = render(synth, {noteOn(0, static_cast<uint8_t>(note), 110)}, 98304);
        return magnitudeAt(audio, 24000, 32768, 2.0 * f0)
             / std::max(1e-9, magnitudeAt(audio, 24000, 32768, f0));
    };
    const double auQuart = mesurer(0.25f);
    const double auMilieu = mesurer(0.5f);
    VSM_ASSERT(auMilieu < auQuart * 0.1);   // le mode 2 disparaît au milieu
}

VSM_TEST(modal_decay_tilt_makes_the_high_modes_die_first) {
    // Sur du bois, les modes hauts s'éteignent bien avant le fondamental ;
    // sur du métal, ils tiennent. Un seul réglage sépare le marimba du
    // vibraphone, et il doit s'entendre : on compare le contenu haut TÔT et
    // TARD dans la même note.
    auto rapportTardif = [&](float tilt) {
        auto synth = makeModal();
        set(*synth, "Material", 1.0f);
        set(*synth, "Decay", 4.0f);
        set(*synth, "Decay Tilt", tilt);
        set(*synth, "Mallet Hardness", 1.0f);
        const int note = 57;
        const double f0 = midiToHz(note);
        const auto audio = render(synth, {noteOn(0, static_cast<uint8_t>(note), 110)}, 147456);
        const double tot = magnitudeAt(audio, 4800, 24576, 5.444 * f0)
                         / std::max(1e-9, magnitudeAt(audio, 4800, 24576, f0));
        const double tard = magnitudeAt(audio, 110000, 24576, 5.444 * f0)
                          / std::max(1e-9, magnitudeAt(audio, 110000, 24576, f0));
        return tard / std::max(1e-9, tot);
    };
    // À tilt nul, le rapport se conserve ; à tilt fort, il s'effondre.
    VSM_ASSERT(rapportTardif(2.5f) < rapportTardif(0.0f) * 0.5);
}

VSM_TEST(modal_hardness_opens_the_timbre_not_only_the_level) {
    // Un maillet dur réveille les modes hauts ; un maillet mou ne peut pas.
    // C'est la loi du marteau de `vsm.piano`, appliquée à un objet.
    auto brillance = [&](float durete) {
        auto synth = makeModal();
        set(*synth, "Material", 1.0f);
        set(*synth, "Decay", 4.0f);
        set(*synth, "Decay Tilt", 0.0f);
        set(*synth, "Mallet Hardness", durete);
        set(*synth, "Velocity to Hardness", 0.0f);
        const int note = 57;
        const double f0 = midiToHz(note);
        const auto audio = render(synth, {noteOn(0, static_cast<uint8_t>(note), 110)}, 98304);
        return magnitudeAt(audio, 24000, 32768, 5.444 * f0)
             / std::max(1e-9, magnitudeAt(audio, 24000, 32768, f0));
    };
    VSM_ASSERT(brillance(1.0f) > brillance(0.0f) * 5.0);
}

VSM_TEST(modal_note_off_does_not_stop_a_struck_object) {
    // Une barre frappée ne s'arrête pas quand on lâche la touche : elle
    // s'éteint d'elle-même. C'est un choix ASSUMÉ (cette machine n'a pas
    // d'étouffoir), et il doit être vérifié plutôt que supposé.
    auto synth = makeModal();
    set(*synth, "Decay", 6.0f);
    const std::vector<MidiNoteEvent> evenements{
        noteOn(0, 57, 110),
        {MidiNoteEvent::Kind::NoteOff, 12000, 0, 57, 0}};
    const auto audio = render(synth, evenements, 72000);
    // Après le relâchement, le son continue franchement.
    VSM_ASSERT(rmsOf(audio, 24000, 36000) > 0.2 * rmsOf(audio, 4800, 12000));
}

VSM_TEST(modal_is_deterministic) {
    auto premier = makeModal();
    const auto a = render(premier, {noteOn(0, 60, 100)}, 24064);
    auto second = makeModal();
    const auto b = render(second, {noteOn(0, 60, 100)}, 24064);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT(a[i] == b[i]);
}

VSM_TEST(modal_save_load_roundtrip) {
    auto premier = makeModal();
    set(*premier, "Material", 0.3f);
    set(*premier, "Decay", 7.5f);
    set(*premier, "Strike Position", 0.4f);
    const auto etat = premier->saveState();
    auto second = makeModal();
    second->loadState(etat);
    for (const auto& info : premier->parameterList())
        VSM_ASSERT_NEAR(second->getParameter(info.id), premier->getParameter(info.id), 1e-6);
}

VSM_TEST(modal_parameter_list_size) {
    auto synth = makeModal();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t(9));
}
