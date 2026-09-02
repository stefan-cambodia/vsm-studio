#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.membrane` — la peau tendue, le premier objet à DEUX dimensions du parc.
//
// Le premier test est celui qui justifie l'existence de la machine : il mesure
// un rapport que `vsm.modal` ne peut PAS produire, et ce n'est pas une opinion
// mais un calcul sur son code (voir l'en-tête de MembraneSynth.h).

namespace {
constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeMembrane(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.membrane");
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
float midiToHz(int note) { return 440.0f * std::exp2f((static_cast<float>(note) - 69.0f) / 12.0f); }

/// Le rapport du SECOND partiel au fondamental, trouvé en cherchant le pic
/// dominant au-dessus du fondamental. On le CHERCHE au lieu de le supposer :
/// c'est justement sa position qui est en cause.
double rapportDuSecondPartiel(SynthPluginPtr& synth, int note) {
    const double f0 = midiToHz(note);
    const auto audio = render(synth, {noteOn(0, static_cast<uint8_t>(note), 110)},
                              static_cast<int>(1.5 * kSampleRate));
    double meilleur = 0.0, rapport = 0.0;
    for (double r = 1.15; r < 2.6; r += 0.002) {
        const double m = magnitudeAt(audio, 2048, 16384, f0 * r);
        if (m > meilleur) { meilleur = m; rapport = r; }
    }
    return rapport;
}
} // namespace

VSM_TEST(membrane_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.membrane"));
}

VSM_TEST(membrane_silent_with_no_events) {
    auto synth = makeMembrane();
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(membrane_note_produces_sound_and_stays_finite) {
    auto synth = makeMembrane();
    const auto audio = render(synth, {noteOn(0, 48, 110)}, 48000);
    VSM_ASSERT(peakAbs(audio) > 0.01f);
    for (float v : audio) VSM_ASSERT(std::isfinite(v));
}

// --- Trait 1 : les rapports de BESSEL, hors de portée de vsm.modal ---------

VSM_TEST(membrane_second_mode_sits_where_no_one_dimensional_object_can) {
    // 1,593 est le rapport des deux premiers zéros de Bessel, et c'est la
    // signature d'un objet à DEUX dimensions. `vsm.modal`, qui interpole entre
    // la corde (rapport 2) et la barre libre-libre (2,778) puis applique son
    // `spread`, couvre exactement [1,866 ; 2,978] sur ce partiel : 1,593 lui
    // est inaccessible, quel que soit le réglage. C'est ce calcul, fait sur
    // son code, qui a décidé d'écrire une machine plutôt qu'un matériau de
    // plus.
    auto synth = makeMembrane();
    set(*synth, "Loading", 0.0f);           // la peau nue, sans charge
    set(*synth, "Strike Radius", 0.7f);     // hors du centre, sinon ce mode est muet
    set(*synth, "Decay Tilt", 0.2f);
    const double rapport = rapportDuSecondPartiel(synth, 48);
    VSM_ASSERT(std::abs(rapport - 1.593) < 0.05);
    VSM_ASSERT(rapport < 1.866);            // hors de la course de vsm.modal
}

// --- Trait 2, LE trait : la charge rend la peau ACCORDABLE ------------------

VSM_TEST(membrane_loading_turns_an_inharmonic_drum_into_a_tuned_one) {
    // Le miracle du tabla, mesuré. Le disque de pâte collé au centre déplace
    // les modes de Bessel vers des ENTIERS (Raman, 1934), et c'est pour cela
    // qu'un tabla joue des notes là où une timbale joue des bruits accordés.
    // Un seul bouton fait le trajet, et aucune autre machine du parc ne rend
    // harmonique un objet qui ne l'était pas.
    auto mesurer = [&](float charge) {
        auto synth = makeMembrane();
        set(*synth, "Loading", charge);
        set(*synth, "Strike Radius", 0.7f);
        set(*synth, "Decay Tilt", 0.2f);
        return rapportDuSecondPartiel(synth, 48);
    };
    const double timbale = mesurer(0.0f);
    const double tabla = mesurer(1.0f);
    VSM_ASSERT(std::abs(timbale - 1.593) < 0.05);   // inharmonique
    VSM_ASSERT(std::abs(tabla - 2.000) < 0.05);     // et accordé, à l'octave
    VSM_ASSERT(mesurer(0.5f) > timbale + 0.1);      // le trajet est continu
    VSM_ASSERT(mesurer(0.5f) < tabla - 0.1);
}

// --- Trait 3 : frapper au centre n'est pas frapper au bord ------------------

VSM_TEST(membrane_striking_the_centre_kills_every_diametral_mode) {
    // Un mode à `m ≥ 1` diamètres nodaux a un NŒUD au centre : le frapper au
    // milieu ne l'excite pas. Le second partiel en est un, et il doit donc
    // disparaître quand on frappe au centre — c'est la différence entre le
    // *na* sourd et le *tin* chantant d'un tabla, et elle est binaire.
    auto energieDuSecond = [&](float rayon) {
        auto synth = makeMembrane();
        set(*synth, "Loading", 0.0f);
        set(*synth, "Strike Radius", rayon);
        set(*synth, "Decay Tilt", 0.2f);
        const auto audio = render(synth, {noteOn(0, 48, 110)}, static_cast<int>(1.5 * kSampleRate));
        return magnitudeAt(audio, 2048, 16384, midiToHz(48) * 1.593);
    };
    const double auBord = energieDuSecond(0.7f);
    const double auCentre = energieDuSecond(0.0f);
    VSM_ASSERT(auBord > 1e-5);
    VSM_ASSERT(auCentre < auBord * 0.05);   // le centre est un nœud, pas un creux
}

VSM_TEST(membrane_is_deterministic) {
    auto premier = makeMembrane();
    const auto a = render(premier, {noteOn(0, 48, 110)}, 24064);
    auto second = makeMembrane();
    const auto b = render(second, {noteOn(0, 48, 110)}, 24064);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT(a[i] == b[i]);
}

VSM_TEST(membrane_save_load_roundtrip) {
    auto premier = makeMembrane();
    set(*premier, "Loading", 0.77f);
    set(*premier, "Strike Radius", 0.31f);
    set(*premier, "Modes", 7.0f);
    const auto etat = premier->saveState();
    auto second = makeMembrane();
    second->loadState(etat);
    for (const auto& info : premier->parameterList())
        VSM_ASSERT_NEAR(second->getParameter(info.id), premier->getParameter(info.id), 1e-6);
}

VSM_TEST(membrane_parameter_list_size) {
    auto synth = makeMembrane();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t(8));
}

VSM_TEST(membrane_honours_pitch_bend) {
    auto synth = makeMembrane();
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 2.0f;
    VSM_ASSERT(synth->handleControlEvent(bend));
}
