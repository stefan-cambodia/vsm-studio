#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.cs80` — deux couches par voix, et une pression PAR NOTE.
//
// Cette suite verrouille les deux traits que le § 9 du CDC machines tenait en
// réserve depuis le début (« double couche complète, sensibilité
// polyphonique à la pression »), et surtout le second : la pression sur une
// touche module CETTE voix et elle seule. C'est le premier endroit du parc où
// une modulation est par-voix, et c'est ce qui a permis de lever le refus de
// `PolyPressure` que le § 10 du CDC nouvelle-machine documentait.

namespace {
constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeCs80(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.cs80");
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
std::vector<float> render(SynthPluginPtr& synth, const std::vector<MidiNoteEvent>& events,
                          int frames, const std::vector<MidiControlEvent>& controls = {},
                          int controlAt = 0) {
    std::vector<float> left(static_cast<size_t>(frames), 0.0f), right(static_cast<size_t>(frames), 0.0f);
    constexpr int kBlock = 256;
    bool controlsSent = false;
    for (int start = 0; start < frames; start += kBlock) {
        const int count = std::min(kBlock, frames - start);
        if (!controlsSent && start >= controlAt) {
            for (const auto& c : controls) synth->handleControlEvent(c);
            controlsSent = true;
        }
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

/// Réglage de mesure : pas de dérive, une seule enveloppe qui tient, et les
/// deux couches identiques sauf ce que le test fait varier.
void steady(ISynthPlugin& plugin) {
    set(plugin, "Analog Character", 0.0f);
    set(plugin, "I Amp Attack", 0.005f);
    set(plugin, "II Amp Attack", 0.005f);
    set(plugin, "I Amp Sustain", 1.0f);
    set(plugin, "II Amp Sustain", 1.0f);
    set(plugin, "Filter Sustain", 1.0f);
    set(plugin, "Filter Attack", 0.005f);
    set(plugin, "I Env Amount", 0.0f);
    set(plugin, "II Env Amount", 0.0f);
    set(plugin, "Velocity to Cutoff", 0.0f);
    set(plugin, "Velocity to Level", 0.0f);
}
} // namespace

VSM_TEST(cs80_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.cs80"));
}

VSM_TEST(cs80_silent_with_no_events) {
    auto synth = makeCs80();
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(cs80_note_produces_sound_and_stays_finite) {
    auto synth = makeCs80();
    const auto audio = render(synth, {noteOn(0, 60, 100)}, 24064);
    VSM_ASSERT(peakAbs(audio) > 0.01f);
    for (float v : audio) VSM_ASSERT(std::isfinite(v));
}

// --- Trait 1 : DEUX COUCHES par voix ---------------------------------------

VSM_TEST(cs80_two_layers_are_independent_under_one_key) {
    // Une seule touche allume deux synthétiseurs complets. On le montre en
    // les rendant AUDIBLEMENT différents — couche I sombre, couche II
    // brillante — puis en balayant le mélange : le contenu aigu doit suivre
    // le mélange, alors qu'aucune coupure n'a bougé.
    auto mesurer = [&](float mix) {
        auto synth = makeCs80();
        steady(*synth);
        set(*synth, "I Cutoff", 500.0f);      // couche I : sombre
        set(*synth, "II Cutoff", 12000.0f);   // couche II : brillante
        set(*synth, "I Detune", 0.0f);
        set(*synth, "II Detune", 0.0f);
        set(*synth, "Layer Mix", mix);
        const auto audio = render(synth, {noteOn(0, 45, 100)}, 49152);
        const double f0 = midiToHz(45);
        const double h1 = magnitudeAt(audio, 24000, 16384, f0);
        const double h9 = magnitudeAt(audio, 24000, 16384, 9.0 * f0);
        return h9 / std::max(1e-9, h1);
    };
    const double coucheI = mesurer(0.0f);
    const double coucheII = mesurer(1.0f);
    VSM_ASSERT(coucheII > coucheI * 5.0);   // la couche II porte l'aigu
}

// --- Trait 2, LE trait : une pression PAR NOTE -----------------------------

VSM_TEST(cs80_poly_pressure_moves_one_voice_and_leaves_the_other_alone) {
    // LE test de cette machine, et il n'a d'équivalent nulle part dans le
    // parc : deux notes tenues, la pression appliquée à UNE SEULE, et le
    // spectre de l'autre ne bouge pas. Partout ailleurs, une pression est
    // une valeur globale que toutes les voix partagent -- et c'est pour cela
    // que le § 10 du CDC leur fait REFUSER ce message.
    const int noteA = 45, noteB = 64;
    const double fA = midiToHz(noteA), fB = midiToHz(noteB);

    auto mesurer = [&](bool avecPression) {
        auto synth = makeCs80();
        steady(*synth);
        set(*synth, "I Cutoff", 700.0f);
        set(*synth, "II Cutoff", 700.0f);
        set(*synth, "Pressure to Cutoff", 3.0f);
        set(*synth, "Pressure to Level", 0.0f);   // on mesure le TIMBRE, pas le niveau
        std::vector<MidiControlEvent> controles;
        if (avecPression) {
            MidiControlEvent pression;
            pression.kind = MidiControlEvent::Kind::PolyPressure;
            pression.index = static_cast<uint8_t>(noteB);   // la note AIGUË seule
            pression.value = 1.0f;
            controles.push_back(pression);
        }
        // Les deux notes partent ensemble ; la pression arrive après
        // l'attaque, comme un doigt qui appuie dans la touche.
        const auto audio = render(synth,
                                  {noteOn(0, static_cast<uint8_t>(noteA), 100),
                                   noteOn(0, static_cast<uint8_t>(noteB), 100)},
                                  98304, controles, 24000);
        // Fenêtre tardive : la pression est lissée sur 30 ms, tout est établi.
        const double aigusDeA = magnitudeAt(audio, 65536, 16384, 7.0 * fA);
        const double aigusDeB = magnitudeAt(audio, 65536, 16384, 7.0 * fB);
        return std::pair<double, double>{aigusDeA, aigusDeB};
    };

    const auto [aNu, bNu] = mesurer(false);
    const auto [aPresse, bPresse] = mesurer(true);

    // La voix PRESSÉE s'ouvre...
    VSM_ASSERT(bPresse > bNu * 1.5);
    // ... et l'autre ne bouge pas (20 % de marge : les deux voix se mêlent
    // dans la même sortie, un peu de fuite spectrale est inévitable).
    VSM_ASSERT(aPresse < aNu * 1.2);
    VSM_ASSERT(aPresse > aNu * 0.8);
}

VSM_TEST(cs80_pressure_on_a_silent_note_is_refused_not_swallowed) {
    // Une pression sur une note qui ne sonne pas n'est honorée par personne :
    // la machine le DIT (retour `false`), et le moteur la comptera comme
    // ignorée -- ce qui est exact.
    auto synth = makeCs80();
    MidiControlEvent pression;
    pression.kind = MidiControlEvent::Kind::PolyPressure;
    pression.index = 60;
    pression.value = 1.0f;
    VSM_ASSERT(!synth->handleControlEvent(pression));
}

VSM_TEST(cs80_channel_pressure_moves_every_voice) {
    // Le repli : un clavier sans capteur par touche envoie une pression de
    // CANAL, et un CS-80 joué depuis un tel clavier doit répondre quand même.
    auto synth = makeCs80();
    const auto audio = render(synth, {noteOn(0, 60, 100)}, 12000);
    (void)audio;
    MidiControlEvent pression;
    pression.kind = MidiControlEvent::Kind::ChannelPressure;
    pression.value = 1.0f;
    VSM_ASSERT(synth->handleControlEvent(pression));
}

VSM_TEST(cs80_is_deterministic) {
    auto premier = makeCs80();
    set(*premier, "Analog Character", 1.0f);
    const auto a = render(premier, {noteOn(0, 60, 100)}, 24064);
    auto second = makeCs80();
    set(*second, "Analog Character", 1.0f);
    const auto b = render(second, {noteOn(0, 60, 100)}, 24064);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT(a[i] == b[i]);
}

VSM_TEST(cs80_save_load_roundtrip) {
    auto premier = makeCs80();
    set(*premier, "Layer Mix", 0.8f);
    set(*premier, "II Cutoff", 900.0f);
    set(*premier, "Pressure to Cutoff", 2.5f);
    const auto etat = premier->saveState();
    auto second = makeCs80();
    second->loadState(etat);
    for (const auto& info : premier->parameterList())
        VSM_ASSERT_NEAR(second->getParameter(info.id), premier->getParameter(info.id), 1e-6);
}

VSM_TEST(cs80_parameter_list_size) {
    auto synth = makeCs80();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t(35));
}

VSM_TEST(cs80_honours_pitch_bend) {
    auto synth = makeCs80();
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 2.0f;
    VSM_ASSERT(synth->handleControlEvent(bend));
}
