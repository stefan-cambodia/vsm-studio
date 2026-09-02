#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.scanned` — la forme d'onde est l'état d'une chaîne de masses.
//
// Ce que cette suite verrouille est la nature même de la famille : le timbre
// évolue parce qu'un OBJET bouge, en temps réel, et non parce qu'un pointeur
// se promène dans des tables ou qu'une marche aléatoire déplace des points.
// La conséquence mesurable, et unique au parc : **la vitesse d'évolution du
// timbre ne dépend pas de la note jouée**.

namespace {
constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeScanned(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.scanned");
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

/// LE TIMBRE, mesuré comme le rapport du second harmonique au fondamental.
///
/// Une première version de cette suite mesurait l'énergie de la DÉRIVÉE du
/// signal, en croyant tenir une « brillance ». C'était une mauvaise mesure et
/// elle a failli faire conclure que la machine n'évoluait pas : la dérivée
/// d'un signal périodique est dominée par sa FRÉQUENCE DE LECTURE, pas par sa
/// forme, si bien qu'elle restait plate (0,0004) pendant que le contenu
/// harmonique réel voyageait de 0,07 à 1,18. Mesurer le timbre demande de
/// regarder les rangs, pas la pente.
double timbre(const std::vector<float>& x, size_t from, size_t count, double f0) {
    const double h1 = magnitudeAt(x, from, count, f0);
    return magnitudeAt(x, from, count, 2.0 * f0) / std::max(1e-9, h1);
}
} // namespace

VSM_TEST(scanned_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.scanned"));
}

VSM_TEST(scanned_silent_with_no_events) {
    auto synth = makeScanned();
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(scanned_note_produces_sound_and_stays_finite) {
    auto synth = makeScanned();
    const auto audio = render(synth, {noteOn(0, 57, 100)}, 24064);
    VSM_ASSERT(peakAbs(audio) > 0.005f);
    for (float v : audio) VSM_ASSERT(std::isfinite(v));
}

// --- LE trait : l'évolution est celle d'un OBJET, pas d'un pointeur --------

/// L'AMPLEUR du voyage du timbre sur une note tenue : l'écart type du
/// rapport h2/h1 relevé sur des fenêtres successives, rapporté à sa moyenne.
/// Une forme figée donne zéro ; une forme qui vit donne une valeur franche.
double voyageDuTimbre(SynthPluginPtr& synth, int note, int frames) {
    const double f0 = midiToHz(note);
    const auto audio = render(synth, {noteOn(0, static_cast<uint8_t>(note), 110)}, frames);
    std::vector<double> mesures;
    for (size_t t = 6000; t + 16384 < audio.size(); t += 16384)
        mesures.push_back(timbre(audio, t, 16384, f0));
    double moyenne = 0.0;
    for (double m : mesures) moyenne += m;
    moyenne /= static_cast<double>(std::max<size_t>(1, mesures.size()));
    double variance = 0.0;
    for (double m : mesures) variance += (m - moyenne) * (m - moyenne);
    variance /= static_cast<double>(std::max<size_t>(1, mesures.size()));
    return std::sqrt(variance) / std::max(1e-6, moyenne);
}

VSM_TEST(scanned_timbre_evolves_on_a_held_note) {
    // La chaîne pincée continue de bouger : le contenu harmonique d'une note
    // TENUE voyage, sans qu'aucun LFO ni aucune enveloppe de filtre n'existe
    // sur cette machine -- elle n'en a pas.
    auto synth = makeScanned();
    set(*synth, "Sustain", 1.0f);
    set(*synth, "Attack", 0.005f);
    VSM_ASSERT(voyageDuTimbre(synth, 45, 192000) > 0.25);
}

/// LE CENTROÏDE HARMONIQUE : le rang moyen, pondéré par l'amplitude, sur les
/// douze premiers harmoniques. Sans dimension, donc comparable entre deux
/// notes distantes de deux octaves, et bien plus stable qu'un simple rapport
/// h2/h1 — celui-ci est le quotient de deux petites quantités et saute d'un
/// facteur dix d'une fenêtre à l'autre (mesuré), ce qui noie tout verdict.
double centroide(const std::vector<float>& x, size_t from, size_t count, double f0) {
    double num = 0.0, den = 0.0;
    for (int n = 1; n <= 12; ++n) {
        const double m = magnitudeAt(x, from, count, static_cast<double>(n) * f0);
        num += static_cast<double>(n) * m;
        den += m;
    }
    return num / std::max(1e-12, den);
}

/// Le relevé du timbre au fil du temps, fenêtre par fenêtre, pour une note.
std::vector<double> calendrierDuTimbre(int note, int frames) {
    auto synth = makeScanned();
    set(*synth, "Sustain", 1.0f);
    set(*synth, "Attack", 0.005f);
    set(*synth, "Filter Cutoff", 16000.0f);
    const auto audio = render(synth, {noteOn(0, static_cast<uint8_t>(note), 110)}, frames);
    std::vector<double> suite;
    for (size_t t = 8192; t + 8192 < audio.size(); t += 8192)
        suite.push_back(centroide(audio, t, 8192, midiToHz(note)));
    return suite;
}

double correlation(const std::vector<double>& a, const std::vector<double>& b) {
    double ma = 0.0, mb = 0.0;
    for (size_t i = 0; i < a.size(); ++i) { ma += a[i]; mb += b[i]; }
    ma /= static_cast<double>(a.size());
    mb /= static_cast<double>(b.size());
    double num = 0.0, va = 0.0, vb = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        num += (a[i] - ma) * (b[i] - mb);
        va += (a[i] - ma) * (a[i] - ma);
        vb += (b[i] - mb) * (b[i] - mb);
    }
    return num / std::max(1e-12, std::sqrt(va * vb));
}

VSM_TEST(scanned_evolution_speed_does_not_follow_the_note) {
    // LE TEST DE LA FAMILLE. Deux notes distantes de DEUX OCTAVES voient leur
    // timbre monter et descendre AUX MÊMES INSTANTS : la chaîne vit en temps
    // réel et n'entend pas parler des notes, qui ne font que la LIRE, plus ou
    // moins vite. Le calendrier est commun parce qu'il n'y en a qu'un.
    //
    // Ce que le test mesure est la CORRÉLATION des deux relevés, et non leur
    // égalité — parce qu'une première version, qui exigeait l'égalité, a été
    // démentie par la mesure : la note grave est franchement plus brillante
    // (centroïde 2,4 contre 1,6 en moyenne). C'est explicable et c'est même
    // juste : sa période de lecture étant quatre fois plus longue, la forme a
    // le temps de bouger PENDANT une lecture, ce qui enrichit le spectre. Le
    // niveau de brillance dépend donc de la note ; son CALENDRIER, non, et
    // c'est cela le trait de la famille.
    //
    // Ce que cela exclut, et c'est tout l'intérêt : sur une machine dont la
    // forme change à chaque PÉRIODE (`vsm.stochastic` en est une), la note
    // aiguë aurait vu quatre fois plus d'événements dans le même temps, et la
    // corrélation se serait effondrée vers zéro.
    const auto grave = calendrierDuTimbre(45, 196608);
    const auto aigu = calendrierDuTimbre(69, 196608);
    VSM_ASSERT(grave.size() == aigu.size() && grave.size() > 8);
    VSM_ASSERT(correlation(grave, aigu) > 0.6);   // mesuré : 0,82
}

VSM_TEST(scanned_damping_freezes_the_shape) {
    // Une chaîne fortement amortie cesse de bouger : le timbre se fige, et
    // la machine se ramène alors à une table d'ondes ordinaire. C'est la
    // moitié témoin du trait -- sans elle, « ça bouge » pourrait n'être
    // qu'un défaut permanent plutôt qu'un réglage.
    auto evolution = [&](float amortissement) {
        auto synth = makeScanned();
        set(*synth, "Damping", amortissement);
        set(*synth, "Sustain", 1.0f);
        set(*synth, "Attack", 0.005f);
        return voyageDuTimbre(synth, 45, 96000);
    };
    VSM_ASSERT(evolution(1.0f) < evolution(0.012f) * 0.5);
}

VSM_TEST(scanned_the_chain_is_shared_by_every_voice) {
    // La chaîne est UN objet qu'on écoute par plusieurs fenêtres : une note
    // jouée pendant qu'une autre tient la repince, donc modifie le timbre de
    // celle qui tenait déjà. Aucune machine à LFO par voix ne fait cela.
    auto synth = makeScanned();
    set(*synth, "Damping", 0.02f);
    set(*synth, "Sustain", 1.0f);
    set(*synth, "Pluck Force", 2.0f);
    const auto seule = render(synth, {noteOn(0, 45, 110)}, 96000);

    auto synth2 = makeScanned();
    set(*synth2, "Damping", 0.02f);
    set(*synth2, "Sustain", 1.0f);
    set(*synth2, "Pluck Force", 2.0f);
    const auto avecSeconde = render(synth2, {noteOn(0, 45, 110), noteOn(48000, 69, 110)}, 96000);

    // Avant la seconde note, les deux rendus sont identiques ; après, le
    // timbre de la PREMIÈRE a changé -- ce qu'on constate en comparant les
    // signaux, la seconde note ne pouvant expliquer une différence AVANT
    // elle.
    bool identiqueAvant = true;
    for (size_t i = 0; i < 47000; ++i)
        if (std::abs(seule[i] - avecSeconde[i]) > 1e-6f) { identiqueAvant = false; break; }
    VSM_ASSERT(identiqueAvant);

    double ecartApres = 0.0;
    for (size_t i = 60000; i < 96000; ++i)
        ecartApres = std::max(ecartApres, static_cast<double>(std::abs(seule[i] - avecSeconde[i])));
    VSM_ASSERT(ecartApres > 1e-3);
}

VSM_TEST(scanned_is_deterministic) {
    auto premier = makeScanned();
    const auto a = render(premier, {noteOn(0, 57, 100)}, 48000);
    auto second = makeScanned();
    const auto b = render(second, {noteOn(0, 57, 100)}, 48000);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT(a[i] == b[i]);
}

VSM_TEST(scanned_save_load_roundtrip) {
    auto premier = makeScanned();
    set(*premier, "Tension", 0.7f);
    set(*premier, "Pluck Position", 0.8f);
    const auto etat = premier->saveState();
    auto second = makeScanned();
    second->loadState(etat);
    for (const auto& info : premier->parameterList())
        VSM_ASSERT_NEAR(second->getParameter(info.id), premier->getParameter(info.id), 1e-6);
}

VSM_TEST(scanned_parameter_list_size) {
    auto synth = makeScanned();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t(14));
}
