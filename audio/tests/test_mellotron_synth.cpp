#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.mellotron` — le Mellotron, dont la bande FINIT.
//
// Le parc lit déjà des échantillons de trois façons, et toutes les trois se
// comportent comme un ordinateur : la note tenue dure autant qu'on la tient,
// et transposer relit l'enregistrement plus vite. Cette suite mesure les
// quatre endroits où un transport à bande fait exactement le contraire, et
// c'est pour ces quatre faits que la machine existe.

namespace {
constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeMellotron(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.mellotron");
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
MidiNoteEvent noteOff(int offset, uint8_t note) {
    return {MidiNoteEvent::Kind::NoteOff, offset, 0, note, 0};
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

/// L'INSTANT OÙ LE SON S'ARRÊTE, en secondes : le dernier échantillon au-dessus
/// d'un plancher, rapporté au temps. C'est la mesure centrale de cette suite,
/// et elle n'aurait de sens sur aucune autre machine du parc — partout
/// ailleurs, la réponse serait « quand l'enveloppe a fini », c'est-à-dire une
/// propriété de l'enveloppe et non de l'instrument.
double instantDArret(const std::vector<float>& x, float plancher = 1e-4f) {
    for (size_t i = x.size(); i-- > 0;)
        if (std::abs(x[i]) > plancher) return static_cast<double>(i) / kSampleRate;
    return 0.0;
}

/// La hauteur d'une fenêtre, par AUTOCORRÉLATION — et non par comptage de
/// passages par zéro, qui a déjà menti au banc de `vsm.juno106` (le compte
/// BAISSAIT quand la hauteur montait, le filtre ajoutant ses propres
/// traversées).
double hauteurHz(const std::vector<float>& x, size_t from, size_t count, double attendueHz) {
    const auto lagMin = static_cast<size_t>(kSampleRate / (attendueHz * 1.4));
    const auto lagMax = static_cast<size_t>(kSampleRate / (attendueHz / 1.4));
    double meilleur = 0.0;
    size_t lagRetenu = lagMin;
    for (size_t lag = lagMin; lag <= lagMax && from + count + lag < x.size(); ++lag) {
        double somme = 0.0;
        for (size_t i = 0; i < count; ++i)
            somme += static_cast<double>(x[from + i]) * x[from + i + lag];
        if (somme > meilleur) { meilleur = somme; lagRetenu = lag; }
    }
    return kSampleRate / static_cast<double>(lagRetenu);
}

/// Le relevé du pleurage d'une note, en cents autour de sa propre moyenne.
std::vector<double> pleurage(int note, int frames) {
    auto synth = makeMellotron();
    set(*synth, "Sustain", 1.0f);
    set(*synth, "Attack", 0.005f);
    set(*synth, "Tape Length", 20.0f);      // qu'elle ne finisse pas pendant la mesure
    set(*synth, "Tape Hiss", 0.0f);         // le souffle brouillerait l'autocorrélation
    set(*synth, "Flutter Depth", 0.0f);     // on mesure le PLEURAGE lent
    set(*synth, "Wow Depth", 40.0f);
    const double attendue = 440.0 * std::pow(2.0, (note - 69) / 12.0);
    const auto audio = render(synth, {noteOn(0, static_cast<uint8_t>(note))}, frames);
    std::vector<double> hz;
    for (size_t t = 4096; t + 8192 < audio.size(); t += 4096)
        hz.push_back(hauteurHz(audio, t, 4096, attendue));
    double moyenne = 0.0;
    for (double v : hz) moyenne += v;
    moyenne /= static_cast<double>(std::max<size_t>(1, hz.size()));
    std::vector<double> cents;
    for (double v : hz) cents.push_back(1200.0 * std::log2(v / moyenne));
    return cents;
}

double correlation(const std::vector<double>& a, const std::vector<double>& b) {
    const size_t n = std::min(a.size(), b.size());
    double ma = 0.0, mb = 0.0;
    for (size_t i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
    ma /= static_cast<double>(n); mb /= static_cast<double>(n);
    double num = 0.0, va = 0.0, vb = 0.0;
    for (size_t i = 0; i < n; ++i) {
        num += (a[i] - ma) * (b[i] - mb);
        va += (a[i] - ma) * (a[i] - ma);
        vb += (b[i] - mb) * (b[i] - mb);
    }
    return num / std::max(1e-12, std::sqrt(va * vb));
}

double ecartType(const std::vector<double>& v) {
    double m = 0.0;
    for (double x : v) m += x;
    m /= static_cast<double>(std::max<size_t>(1, v.size()));
    double s = 0.0;
    for (double x : v) s += (x - m) * (x - m);
    return std::sqrt(s / static_cast<double>(std::max<size_t>(1, v.size())));
}
} // namespace

VSM_TEST(mellotron_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.mellotron"));
}

VSM_TEST(mellotron_silent_with_no_events) {
    auto synth = makeMellotron();
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(mellotron_note_produces_sound_and_stays_finite) {
    auto synth = makeMellotron();
    const auto audio = render(synth, {noteOn(0, 60, 100)}, 48000);
    VSM_ASSERT(peakAbs(audio) > 0.01f);
    for (float v : audio) VSM_ASSERT(std::isfinite(v));
}

// --- Trait 1, LE trait : la bande finit ------------------------------------

VSM_TEST(mellotron_the_tape_runs_out_on_a_held_note) {
    // Sustain à fond, release long, touche JAMAIS relâchée : sur n'importe
    // quelle autre machine du parc, le son durerait les douze secondes du
    // rendu. Ici il s'arrête à trois secondes, parce qu'il n'y a plus de
    // bande — et c'est l'instrument qui décide, pas l'enveloppe.
    auto synth = makeMellotron();
    set(*synth, "Sustain", 1.0f);
    set(*synth, "Release", 8.0f);
    set(*synth, "Attack", 0.005f);
    set(*synth, "Tape Length", 3.0f);
    const auto audio = render(synth, {noteOn(0, 60, 100)}, static_cast<int>(12 * kSampleRate));

    const double arret = instantDArret(audio);
    VSM_ASSERT(arret > 2.9 && arret < 3.2);   // la bande, et rien d'autre
}

// --- Trait 2 : une bande par touche, donc pas de transposition -------------

VSM_TEST(mellotron_tape_length_does_not_follow_the_note) {
    // Un échantillonneur qui transpose verrait sa bande passer de trois
    // secondes à moins d'une en montant de deux octaves : il relit le même
    // enregistrement quatre fois plus vite. Le Mellotron a un brin de bande
    // PAR TOUCHE, chacun enregistré à sa hauteur, donc la durée disponible
    // ne bouge pas d'un pouce.
    auto mesurer = [&](int note) {
        auto synth = makeMellotron();
        set(*synth, "Sustain", 1.0f);
        set(*synth, "Release", 8.0f);
        set(*synth, "Attack", 0.005f);
        set(*synth, "Tape Length", 3.0f);
        const auto audio = render(synth, {noteOn(0, static_cast<uint8_t>(note), 100)},
                                  static_cast<int>(12 * kSampleRate));
        return instantDArret(audio);
    };
    const double grave = mesurer(45);
    const double aigu = mesurer(69);          // deux octaves au-dessus
    VSM_ASSERT(std::abs(grave - aigu) < 0.05);
}

// --- Trait 3 : chaque bande a son propre défaut ----------------------------

VSM_TEST(mellotron_each_key_wows_on_its_own) {
    // Deux touches, deux brins de bande, deux moteurs d'entraînement : leurs
    // pleurages ne sont PAS en phase. C'est le contraire exact d'un LFO de
    // vibrato, qui ferait onduler toutes les voix à l'unisson -- et c'est ce
    // qui donne au chœur de Mellotron son grain vivant.
    const auto a = pleurage(57, static_cast<int>(6 * kSampleRate));
    const auto b = pleurage(64, static_cast<int>(6 * kSampleRate));
    VSM_ASSERT(a.size() > 10 && b.size() > 10);

    // Le pleurage EXISTE : sans cette moitié, un instrument parfaitement
    // stable passerait le test de décorrélation les doigts dans le nez.
    VSM_ASSERT(ecartType(a) > 3.0);
    VSM_ASSERT(ecartType(b) > 3.0);
    // ... et les deux bandes ne pleurent pas ensemble.
    VSM_ASSERT(std::abs(correlation(a, b)) < 0.6);
}

// --- Trait 4 : la tête revient, mais pas instantanément --------------------

VSM_TEST(mellotron_replaying_before_the_rewind_finishes_gives_a_shorter_note) {
    // On joue deux secondes, on relâche, on rejoue AUSSITÔT : la bande n'a
    // pas eu le temps de revenir à son début, et la seconde note est plus
    // courte que la première. Aucun échantillonneur ne fait cela -- chez lui,
    // rejouer repart toujours de l'échantillon zéro.
    auto synth = makeMellotron();
    set(*synth, "Sustain", 1.0f);
    set(*synth, "Release", 0.05f);
    set(*synth, "Attack", 0.005f);
    set(*synth, "Tape Length", 4.0f);
    set(*synth, "Rewind Time", 4.0f);        // rembobinage LENT, pour le voir

    const int deuxSecondes = static_cast<int>(2 * kSampleRate);
    const int reprise = deuxSecondes + static_cast<int>(0.2 * kSampleRate);
    const auto audio = render(synth,
                              {noteOn(0, 60, 100), noteOff(deuxSecondes, 60), noteOn(reprise, 60, 100)},
                              static_cast<int>(12 * kSampleRate));

    // Durée de la SECONDE note : de sa reprise à l'arrêt du son.
    const double dureeSeconde = instantDArret(audio) - static_cast<double>(reprise) / kSampleRate;
    // La première a duré ses deux secondes pleines sans finir sa bande ; la
    // seconde repart vers 1,8 s de bande consommée, donc il lui en reste
    // franchement moins que les quatre secondes du neuf.
    VSM_ASSERT(dureeSeconde > 0.5);
    VSM_ASSERT(dureeSeconde < 3.5);
}

VSM_TEST(mellotron_is_deterministic) {
    auto premier = makeMellotron();
    const auto a = render(premier, {noteOn(0, 60, 100)}, 24064);
    auto second = makeMellotron();
    const auto b = render(second, {noteOn(0, 60, 100)}, 24064);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT(a[i] == b[i]);
}

VSM_TEST(mellotron_save_load_roundtrip) {
    auto premier = makeMellotron();
    set(*premier, "Tape Length", 5.5f);
    set(*premier, "Wow Depth", 33.0f);
    set(*premier, "Rewind Time", 2.25f);
    const auto etat = premier->saveState();
    auto second = makeMellotron();
    second->loadState(etat);
    for (const auto& info : premier->parameterList())
        VSM_ASSERT_NEAR(second->getParameter(info.id), premier->getParameter(info.id), 1e-6);
}

VSM_TEST(mellotron_parameter_list_size) {
    auto synth = makeMellotron();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t(15));
}

VSM_TEST(mellotron_honours_pitch_bend) {
    auto synth = makeMellotron();
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 2.0f;
    VSM_ASSERT(synth->handleControlEvent(bend));
}
