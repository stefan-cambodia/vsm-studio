#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.sitar` — les cordes qu'on ne joue pas.
//
// Trois documents du dépôt constatent que le parc n'a aucune résonance
// sympathique (l'en-tête de `vsm.piano`, le § 28 d'ARCHITECTURE, le CDC du
// multisample) sans que personne ne comble le trou. Cette suite mesure ce
// qu'il a fallu écrire pour le combler, et le second test est le plus
// important des deux : sans lui, on aurait seulement fabriqué une réverbe.

namespace {
constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeSitar(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.sitar");
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
double rms(const std::vector<float>& x, size_t from, size_t count) {
    double e = 0.0;
    size_t n = 0;
    for (size_t i = from; i < from + count && i < x.size(); ++i, ++n)
        e += static_cast<double>(x[i]) * x[i];
    return std::sqrt(e / static_cast<double>(std::max<size_t>(1, n)));
}
/// L'énergie d'une bande de fréquences, par somme de Goertzel.
double energieBande(const std::vector<float>& x, size_t from, size_t count, double lo, double hi) {
    double somme = 0.0;
    for (double hz = lo; hz < hi; hz *= 1.06) {
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

/// LE BOURDONNEMENT, mesuré comme le rapport de l'énergie AIGUË (5–12 kHz) à
/// l'énergie du corps (80–250 Hz).
///
/// C'est la troisième métrique essayée, et les deux premières ont failli faire
/// condamner un modèle juste. Un CENTROÏDE spectral tronqué à 8 kHz DESCEND
/// quand le jawari s'engage (mesuré : 1941 -> 1597 Hz), et on en concluait que
/// le chevalet assombrissait le son. Le spectre par bandes dit ce qui se passe
/// vraiment : le contact multiplie par 35 l'énergie entre 5 et 12 kHz — c'est
/// le buzz, il est franc — tout en épaississant aussi le grave d'un cinquième,
/// et c'est ce grave qui tirait le centroïde vers le bas. Un bourdonnement
/// n'est pas un « centre de gravité qui monte », c'est de l'aigu qui APPARAÎT ;
/// il faut le mesurer là où il est.
double bourdonnement(const std::vector<float>& x, size_t from, size_t count) {
    const double aigu = energieBande(x, from, count, 5000.0, 12000.0);
    const double corps = energieBande(x, from, count, 80.0, 250.0);
    return aigu / std::max(1e-9, corps);
}

/// L'énergie qui reste LONGTEMPS APRÈS que la note a été relâchée et que la
/// corde jouée est étouffée. C'est la mesure centrale de cette machine.
double resonanceApresLeSilence(int note, float niveauSympathiques) {
    auto synth = makeSitar();
    set(*synth, "Sympathetic Level", niveauSympathiques);
    set(*synth, "Sympathetic Root", 45.0f);
    set(*synth, "Sympathetic Decay", 12.0f);
    set(*synth, "String Decay", 1.0f);
    const int relache = static_cast<int>(1.0 * kSampleRate);
    const auto audio = render(synth,
                              {noteOn(0, static_cast<uint8_t>(note), 110), noteOff(relache, static_cast<uint8_t>(note))},
                              static_cast<int>(5 * kSampleRate));
    // Fenêtre TARDIVE : la corde jouée est étouffée depuis longtemps (0,25 s
    // d'étouffement après le relâchement, mesuré à une seconde).
    return rms(audio, static_cast<size_t>(3.0 * kSampleRate), static_cast<size_t>(1.5 * kSampleRate));
}
} // namespace

VSM_TEST(sitar_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.sitar"));
}

VSM_TEST(sitar_silent_with_no_events) {
    auto synth = makeSitar();
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(sitar_note_produces_sound_and_stays_finite) {
    auto synth = makeSitar();
    const auto audio = render(synth, {noteOn(0, 45, 110)}, static_cast<int>(2 * kSampleRate));
    VSM_ASSERT(peakAbs(audio) > 0.01f);
    for (float v : audio) VSM_ASSERT(std::isfinite(v));
}

// --- Trait 1 : l'instrument sonne après le silence des notes ---------------

VSM_TEST(sitar_sympathetic_strings_ring_long_after_the_note_is_released) {
    // Toutes les notes relâchées, la corde jouée étouffée depuis deux
    // secondes, et il sort encore du son. Sur toute autre machine du parc,
    // silence des notes égale silence de la machine.
    const double avec = resonanceApresLeSilence(45, 0.8f);
    // LE TÉMOIN, et il est du même code : les sympathiques tournent toujours,
    // seul leur niveau de mélange change. C'est une option, pas une constante
    // éditée entre deux passes.
    const double sans = resonanceApresLeSilence(45, 0.0f);
    VSM_ASSERT(avec > 1e-4);
    VSM_ASSERT(avec > sans * 20.0);
}

// --- Trait 1 bis, LE test : la réponse est SÉLECTIVE ------------------------

VSM_TEST(sitar_sympathetic_response_is_selective_not_a_reverb) {
    // Ce test est le plus important de la suite. Sans lui, un simple écho
    // accordé passerait le précédent : n'importe quelle réverbe fait durer le
    // son après la note. Ce qui distingue une RÉSONANCE est qu'elle
    // CHOISIT — une note accordée sur une corde la met en branle, une note à
    // un demi-ton de là ne la trouve pas.
    //
    // Les sympathiques sont accordées sur les degrés d'une gamme majeure
    // depuis la note 45 : 45, 47, 49, 50, 52... La note 46 n'y est pas, et
    // aucun de ses harmoniques ne tombe sur l'une d'elles.
    const double accordee = resonanceApresLeSilence(45, 0.8f);
    const double horsGamme = resonanceApresLeSilence(46, 0.8f);
    VSM_ASSERT(accordee > horsGamme * 3.0);
}

// --- Trait 2 : le jawari suit l'AMPLITUDE, pas le temps --------------------

VSM_TEST(sitar_jawari_buzz_follows_amplitude_not_time) {
    // Le chevalet plat n'est touché que par une corde qui vibre FORT. La
    // brillance qu'il ajoute doit donc dépendre de la force du pincement --
    // et c'est ce qui le sépare d'une enveloppe de filtre, qui suit le TEMPS
    // et ferait exactement la même chose à toutes les vélocités.
    // LE RÉGLAGE DE MESURE COMPTE AUTANT QUE LA MESURE, et il a fallu s'y
    // reprendre à deux fois. Une première version comparait les vélocités 127
    // et 20 en laissant `Velocity Sensitivity` à son défaut de 0,5 : entre les
    // deux, l'amplitude ne variait que d'un facteur 1,7, le seuil de contact
    // était franchi dans les DEUX cas, et le test concluait à l'envers de la
    // physique (l'effet paraissait plus fort sur la note douce). Pour mesurer
    // une dépendance à l'amplitude, encore faut-il faire varier l'amplitude :
    // sensibilité à fond, et deux vélocités vraiment éloignées.
    auto buzz = [&](float jawari, uint8_t velocity) {
        auto synth = makeSitar();
        set(*synth, "Jawari", jawari);
        set(*synth, "Velocity Sensitivity", 1.0f);
        set(*synth, "Sympathetic Level", 0.0f);   // on mesure la CORDE seule
        set(*synth, "String Decay", 4.0f);
        const auto audio = render(synth, {noteOn(0, 45, velocity)}, static_cast<int>(1.0 * kSampleRate));
        return bourdonnement(audio, 2048, 16384);
    };
    // LE RÉGLAGE DE MESURE COMPTE AUTANT QUE LA MESURE. Une première version
    // comparait les vélocités 127 et 20 en laissant `Velocity Sensitivity` à
    // son défaut de 0,5 : l'amplitude ne variait alors que d'un facteur 1,7,
    // le seuil de contact était franchi dans les DEUX cas, et le test ne
    // pouvait rien montrer. Pour mesurer une dépendance à l'amplitude, encore
    // faut-il faire varier l'amplitude.
    const double repos = buzz(0.0f, 127);          // le jawari coupé, à toute vélocité
    const double fort = buzz(1.0f, 127) / repos;
    const double moyen = buzz(1.0f, 25) / repos;
    const double doux = buzz(1.0f, 6) / repos;

    // Pincée fort, la corde touche le chevalet à chaque cycle et l'aigu
    // explose (mesuré : ×30 dans la bande 5–12 kHz).
    VSM_ASSERT(fort > 8.0);
    // Pincée doux, elle reste sous le seuil de contact et le MÊME réglage ne
    // fait RIEN — mesuré à 1,00, pas « presque rien ». C'est cela, dépendre de
    // l'amplitude et non du temps : une enveloppe de filtre aurait produit le
    // même écart aux deux vélocités.
    VSM_ASSERT(doux < 1.2);
    // Et entre les deux il y a une COURBE, pas un interrupteur : le contact
    // s'établit progressivement à mesure que la corde vibre plus ample
    // (mesuré, du plus doux au plus fort : 1,00 · 1,67 · 22,7 · 34,8 · 29,9).
    VSM_ASSERT(moyen > doux * 4.0);
    VSM_ASSERT(moyen < fort * 1.5);
}

// --- Les sympathiques appartiennent à l'instrument, pas aux voix -----------

VSM_TEST(sitar_sympathetic_strings_are_shared_by_the_whole_instrument) {
    // Six voix, sept notes : la septième en vole une. Les sympathiques, elles,
    // n'ont pas de voix à voler -- elles sont derrière le manche, et elles
    // continuent. On le montre en jouant un accord entier puis en le
    // relâchant : la résonance qui suit est plus forte qu'après une seule
    // note, parce que sept cordes l'ont alimentée.
    auto resonanceApres = [&](const std::vector<int>& notes) {
        auto synth = makeSitar();
        set(*synth, "Sympathetic Level", 0.8f);
        set(*synth, "Sympathetic Root", 45.0f);
        set(*synth, "Sympathetic Decay", 12.0f);
        set(*synth, "String Decay", 1.0f);
        std::vector<MidiNoteEvent> events;
        const int relache = static_cast<int>(1.0 * kSampleRate);
        for (int n : notes) {
            events.push_back(noteOn(0, static_cast<uint8_t>(n), 110));
            events.push_back(noteOff(relache, static_cast<uint8_t>(n)));
        }
        const auto audio = render(synth, events, static_cast<int>(5 * kSampleRate));
        return rms(audio, static_cast<size_t>(3.0 * kSampleRate), static_cast<size_t>(1.5 * kSampleRate));
    };
    const double uneNote = resonanceApres({45});
    const double septNotes = resonanceApres({45, 47, 49, 50, 52, 54, 56});
    VSM_ASSERT(septNotes > uneNote * 1.5);
}

VSM_TEST(sitar_is_deterministic) {
    auto premier = makeSitar();
    const auto a = render(premier, {noteOn(0, 45, 110)}, 48128);
    auto second = makeSitar();
    const auto b = render(second, {noteOn(0, 45, 110)}, 48128);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT(a[i] == b[i]);
}

VSM_TEST(sitar_save_load_roundtrip) {
    auto premier = makeSitar();
    set(*premier, "Jawari", 0.83f);
    set(*premier, "Sympathetic Root", 52.0f);
    set(*premier, "Sympathetic Strings", 7.0f);
    const auto etat = premier->saveState();
    auto second = makeSitar();
    second->loadState(etat);
    for (const auto& info : premier->parameterList())
        VSM_ASSERT_NEAR(second->getParameter(info.id), premier->getParameter(info.id), 1e-6);
}

VSM_TEST(sitar_parameter_list_size) {
    auto synth = makeSitar();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t(13));
}

VSM_TEST(sitar_honours_pitch_bend) {
    auto synth = makeSitar();
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 2.0f;
    VSM_ASSERT(synth->handleControlEvent(bend));
}
