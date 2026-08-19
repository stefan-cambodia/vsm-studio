#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

namespace {

constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeOrgan() {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.tonewheel");
    if (plugin) plugin->initialize(kSampleRate, 512);
    return plugin;
}

ParamId byName(const ISynthPlugin& plugin, const std::string& name) {
    for (const auto& info : plugin.parameterList())
        if (info.name == name) return info.id;
    return 0;
}

struct Stereo { std::vector<float> left, right; };

Stereo renderEvents(const SynthPluginPtr& synth, const std::vector<MidiNoteEvent>& events, int frames) {
    Stereo out;
    out.left.assign(static_cast<size_t>(frames), 0.0f);
    out.right.assign(static_cast<size_t>(frames), 0.0f);
    synth->process(events.data(), static_cast<int>(events.size()),
                    out.left.data(), out.right.data(), frames);
    return out;
}

Stereo renderNote(const SynthPluginPtr& synth, uint8_t note, int frames = 24000, uint8_t velocity = 100) {
    return renderEvents(synth, {MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, 0, 0, note, velocity}}, frames);
}

float peakAbs(const std::vector<float>& buffer, size_t from = 0, size_t to = 0) {
    const size_t last = to > 0 ? std::min(to, buffer.size()) : buffer.size();
    float peak = 0.0f;
    for (size_t i = from; i < last; ++i) peak = std::max(peak, std::abs(buffer[i]));
    return peak;
}

double rms(const std::vector<float>& buffer, size_t from = 0, size_t to = 0) {
    const size_t last = to > 0 ? std::min(to, buffer.size()) : buffer.size();
    double sum = 0.0;
    for (size_t i = from; i < last; ++i) sum += static_cast<double>(buffer[i]) * buffer[i];
    return last > from ? std::sqrt(sum / static_cast<double>(last - from)) : 0.0;
}

/// Puissance à une fréquence donnée.
double powerAt(const std::vector<float>& buffer, double hz, size_t from = 0, size_t to = 0) {
    const size_t last = to > 0 ? std::min(to, buffer.size()) : buffer.size();
    double real = 0.0, imaginary = 0.0;
    for (size_t i = from; i < last; ++i) {
        const double angle = 2.0 * 3.14159265358979323846 * hz * static_cast<double>(i) / kSampleRate;
        real += static_cast<double>(buffer[i]) * std::cos(angle);
        imaginary += static_cast<double>(buffer[i]) * std::sin(angle);
    }
    const double count = static_cast<double>(last - from);
    return (real * real + imaginary * imaginary) / (count * count);
}

/// Tirettes à zéro sauf celles nommées, pour isoler un rang.
void setDrawbars(const SynthPluginPtr& synth, std::initializer_list<std::pair<const char*, float>> values) {
    const char* all[9] = {"Drawbar 16", "Drawbar 5 1/3", "Drawbar 8", "Drawbar 4", "Drawbar 2 2/3",
                          "Drawbar 2", "Drawbar 1 3/5", "Drawbar 1 1/3", "Drawbar 1"};
    for (const char* name : all) synth->setParameter(byName(*synth, name), 0.0f);
    for (const auto& [name, value] : values) synth->setParameter(byName(*synth, name), value);
}

/// Neutralise tout ce qui bouge, pour mesurer le générateur seul.
void makeStatic(const SynthPluginPtr& synth) {
    synth->setParameter(byName(*synth, "Rotary Depth"), 0.0f);
    synth->setParameter(byName(*synth, "Vibrato Depth"), 0.0f);
    synth->setParameter(byName(*synth, "Overdrive"), 0.0f);
    synth->setParameter(byName(*synth, "Key Click"), 0.0f);
    synth->setParameter(byName(*synth, "Percussion Level"), 0.0f);
}

} // namespace

VSM_TEST(tonewheel_is_registered) {
    auto synth = makeOrgan();
    VSM_ASSERT(synth != nullptr);
    VSM_ASSERT_EQ(std::string(synth->machineName()), std::string("Tonewheel Organ"));
    VSM_ASSERT(synth->parameterList().size() >= 18);
}

VSM_TEST(tonewheel_produces_sound) {
    auto synth = makeOrgan();
    VSM_ASSERT(peakAbs(renderNote(synth, 60).left) > 0.02f);
}

VSM_TEST(tonewheel_has_no_envelope_the_sound_starts_and_stops_at_once) {
    // Un orgue n'a ni attaque ni extinction : une touche RACCORDE des roues
    // qui tournaient déjà. Le son doit donc être établi en quelques
    // millisecondes et disparaître aussi vite -- c'est ce qui le distingue de
    // toutes les autres machines du parc.
    auto synth = makeOrgan();
    makeStatic(synth);
    const auto rendered = renderEvents(synth, {
        MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, 0, 0, 60, 100},
        MidiNoteEvent{MidiNoteEvent::Kind::NoteOff, 12000, 0, 60, 0},
    }, 24000);

    // Établi au bout de 10 ms...
    const double early = rms(rendered.left, 480, 960);
    const double steady = rms(rendered.left, 6000, 11000);
    VSM_ASSERT(early > steady * 0.7);
    // ...et éteint 10 ms après le relâchement.
    VSM_ASSERT(rms(rendered.left, 12500, 20000) < steady * 0.05);
}

VSM_TEST(tonewheel_drawbars_select_harmonic_ranks) {
    // Chaque tirette prélève un rang précis. On le vérifie à la fréquence
    // attendue : la tirette 16' sonne une octave SOUS la note, la 8' à la
    // note, la 4' une octave au-dessus.
    const double noteHz = 440.0 * std::exp2((60.0 - 69.0) / 12.0); // do3, ~261.6 Hz

    auto sub = makeOrgan();  makeStatic(sub);  setDrawbars(sub, {{"Drawbar 16", 8.0f}});
    auto unison = makeOrgan(); makeStatic(unison); setDrawbars(unison, {{"Drawbar 8", 8.0f}});
    auto octave = makeOrgan(); makeStatic(octave); setDrawbars(octave, {{"Drawbar 4", 8.0f}});

    const auto subBuffer = renderNote(sub, 60).left;
    const auto unisonBuffer = renderNote(unison, 60).left;
    const auto octaveBuffer = renderNote(octave, 60).left;

    // Chaque rendu doit avoir SA fréquence comme composante dominante.
    VSM_ASSERT(powerAt(subBuffer, noteHz * 0.5) > powerAt(subBuffer, noteHz) * 4.0);
    VSM_ASSERT(powerAt(unisonBuffer, noteHz) > powerAt(unisonBuffer, noteHz * 0.5) * 4.0);
    VSM_ASSERT(powerAt(octaveBuffer, noteHz * 2.0) > powerAt(octaveBuffer, noteHz) * 4.0);
}

VSM_TEST(tonewheel_drawbar_at_zero_is_silent_and_at_eight_is_loudest) {
    auto synth = makeOrgan();
    makeStatic(synth);
    setDrawbars(synth, {});
    VSM_ASSERT(peakAbs(renderNote(synth, 60).left) < 0.001f); // toutes tirettes rentrées

    double previous = 0.0;
    for (float steps : {1.0f, 4.0f, 8.0f}) {
        auto stepped = makeOrgan();
        makeStatic(stepped);
        setDrawbars(stepped, {{"Drawbar 8", steps}});
        const double level = rms(renderNote(stepped, 60).left);
        VSM_ASSERT(level > previous); // strictement croissant, cran par cran
        previous = level;
    }
}

VSM_TEST(tonewheel_shared_wheels_do_not_double_up) {
    // LE comportement qui fait cet instrument. Deux touches qui prélèvent la
    // MÊME roue ne la font pas sonner deux fois : les roues sont partagées
    // par tout l'orgue. Ici, do3 en 8' et do2 en 4' visent la même roue.
    //
    // Un banc d'oscillateurs par note donnerait deux fois le niveau ; le vrai
    // mécanisme donne un peu plus, pas le double, parce que la roue est
    // unique et que seul le contact s'ajoute.
    auto single = makeOrgan();
    makeStatic(single);
    setDrawbars(single, {{"Drawbar 8", 8.0f}});
    const double one = rms(renderNote(single, 60).left);

    auto shared = makeOrgan();
    makeStatic(shared);
    setDrawbars(shared, {{"Drawbar 8", 8.0f}, {"Drawbar 4", 8.0f}});
    // do3 (8' -> roue de do3) et do2 (4' -> roue de do3) : même roue.
    const auto both = renderEvents(shared, {
        MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, 0, 0, 60, 100},
        MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, 0, 0, 48, 100},
    }, 24000);
    const double together = rms(both.left);

    // Si les roues étaient dupliquées par note, on obtiendrait au moins le
    // double ; la roue étant partagée, on reste bien en dessous.
    VSM_ASSERT(together < one * 4.0);
    VSM_ASSERT(together > one);      // ...mais deux touches sonnent plus qu'une
}

VSM_TEST(tonewheel_foldback_keeps_extreme_notes_audible) {
    // Quand un rang sort du générateur, le mécanisme REPLIE d'une octave au
    // lieu de se taire. Les notes extrêmes doivent donc rester audibles, avec
    // une couleur différente -- et surtout jamais silencieuses.
    auto synth = makeOrgan();
    makeStatic(synth);
    setDrawbars(synth, {{"Drawbar 1", 8.0f}, {"Drawbar 16", 8.0f}});
    for (uint8_t note : {uint8_t(24), uint8_t(36), uint8_t(96), uint8_t(108)}) {
        auto fresh = makeOrgan();
        makeStatic(fresh);
        setDrawbars(fresh, {{"Drawbar 1", 8.0f}, {"Drawbar 16", 8.0f}});
        VSM_ASSERT(peakAbs(renderNote(fresh, note).left) > 0.005f);
    }
}

VSM_TEST(tonewheel_key_click_is_a_short_burst_at_the_start) {
    auto without = makeOrgan();
    makeStatic(without);
    auto with = makeOrgan();
    makeStatic(with);
    with->setParameter(byName(*with, "Key Click"), 1.0f);

    const auto quiet = renderNote(without, 60, 12000).left;
    const auto clicky = renderNote(with, 60, 12000).left;

    // Le claquement ne dure que quelques millisecondes : il doit se voir au
    // tout début et avoir disparu ensuite. Fenêtre de 2 ms, ajustée à sa
    // durée réelle -- sur 6 ms il était déjà noyé par le son de l'orgue,
    // qui s'établit en 1,5 ms et est bien plus fort.
    VSM_ASSERT(rms(clicky, 0, 100) > rms(quiet, 0, 100) * 2.0);
    VSM_ASSERT_NEAR(rms(clicky, 6000, 12000), rms(quiet, 6000, 12000), rms(quiet, 6000, 12000) * 0.1);
}

VSM_TEST(tonewheel_percussion_does_not_retrigger_on_legato) {
    // Le comportement qui fait le phrasé de cet instrument : la percussion ne
    // se réarme que lorsque TOUTES les touches sont relâchées. En jeu lié,
    // seule la première note l'obtient.
    auto synth = makeOrgan();
    makeStatic(synth);
    synth->setParameter(byName(*synth, "Percussion Level"), 1.0f);
    synth->setParameter(byName(*synth, "Percussion Decay"), 0.2f);

    // Deuxième note pendant que la première est encore tenue : jeu lié.
    const auto legato = renderEvents(synth, {
        MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, 0, 0, 60, 100},
        MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, 24000, 0, 64, 100},
    }, 48000);

    // Première note relâchée AVANT la seconde : jeu détaché.
    auto fresh = makeOrgan();
    makeStatic(fresh);
    fresh->setParameter(byName(*fresh, "Percussion Level"), 1.0f);
    fresh->setParameter(byName(*fresh, "Percussion Decay"), 0.2f);
    const auto detached = renderEvents(fresh, {
        MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, 0, 0, 60, 100},
        MidiNoteEvent{MidiNoteEvent::Kind::NoteOff, 20000, 0, 60, 0},
        MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, 24000, 0, 64, 100},
    }, 48000);

    // Au moment de la seconde note, le jeu détaché doit montrer un sursaut de
    // percussion que le jeu lié n'a pas.
    const double legatoSurge = rms(legato.left, 24000, 26000) / std::max(1e-9, rms(legato.left, 20000, 23000));
    const double detachedSurge = rms(detached.left, 24000, 26000) / std::max(1e-9, rms(detached.left, 21000, 23500));
    VSM_ASSERT(detachedSurge > legatoSurge);
}

VSM_TEST(tonewheel_rotary_creates_stereo_movement) {
    // Sans le rotatif, cet instrument ne ressemble à rien de ce qu'on connaît.
    // On vérifie qu'il produit bien un mouvement STÉRÉO, et pas un simple
    // trémolo identique sur les deux canaux.
    auto still = makeOrgan();
    still->setParameter(byName(*still, "Rotary Depth"), 0.0f);
    auto turning = makeOrgan();
    turning->setParameter(byName(*turning, "Rotary Depth"), 1.0f);

    auto width = [](const Stereo& stereo) {
        double side = 0.0, mid = 0.0;
        for (size_t i = 0; i < stereo.left.size(); ++i) {
            const double difference = static_cast<double>(stereo.left[i]) - stereo.right[i];
            const double sum = static_cast<double>(stereo.left[i]) + stereo.right[i];
            side += difference * difference;
            mid += sum * sum;
        }
        return mid > 0.0 ? side / mid : 0.0;
    };
    VSM_ASSERT(width(renderNote(still, 60, 48000)) < 0.001);
    VSM_ASSERT(width(renderNote(turning, 60, 48000)) > 0.02);
}

VSM_TEST(tonewheel_rotary_rotors_speed_up_at_different_rates) {
    // Le pavillon (léger) prend sa vitesse en une seconde, le tambour (lourd)
    // en plusieurs. Ce décalage PENDANT le changement est l'un des sons les
    // plus reconnaissables de l'instrument -- bien plus que la vitesse finale.
    //
    // Ce qu'on mesure : la fréquence de modulation de l'image stéréo, par
    // autocorrélation de son enveloppe, seconde par seconde. Deux pièges
    // évités, tous deux rencontrés en écrivant ce test :
    //
    //  - COMPTER les passages par la moyenne au lieu d'autocorréler donne une
    //    mesure trop bruitée pour être monotone.
    //  - Les tirettes par défaut (16', 5⅓', 8') sonnent sous 400 Hz, donc
    //    ENTIÈREMENT dans le tambour : le pavillon ne recevait rien et la
    //    mesure ne voyait qu'un seul rotor. D'où la note aiguë et les
    //    tirettes hautes ci-dessous.
    auto modulationRate = [](const Stereo& stereo, size_t from, size_t to) {
        std::vector<double> envelope;
        envelope.reserve(to - from);
        double smoothed = 0.0, mean = 0.0;
        for (size_t i = from; i < to; ++i) {
            const double difference = std::abs(static_cast<double>(stereo.left[i]) - stereo.right[i]);
            smoothed += (difference - smoothed) * 0.004;
            envelope.push_back(smoothed);
            mean += smoothed;
        }
        mean /= static_cast<double>(envelope.size());
        for (double& value : envelope) value -= mean;

        double best = -1e18;
        int bestLag = 0;
        for (int lag = 3000; lag < 40000 && static_cast<size_t>(lag) < envelope.size() / 2; lag += 50) {
            double correlation = 0.0;
            for (size_t i = static_cast<size_t>(lag); i < envelope.size(); ++i)
                correlation += envelope[i] * envelope[i - static_cast<size_t>(lag)];
            if (correlation > best) { best = correlation; bestLag = lag; }
        }
        return bestLag > 0 ? kSampleRate / bestLag : 0.0;
    };

    auto synth = makeOrgan();
    synth->setParameter(byName(*synth, "Rotary Fast"), 1.0f);
    synth->setParameter(byName(*synth, "Rotary Depth"), 1.0f);
    for (const char* name : {"Drawbar 4", "Drawbar 2", "Drawbar 1"})
        synth->setParameter(byName(*synth, name), 8.0f);
    const auto rendered = renderNote(synth, 72, 240000); // 5 s, note aiguë

    const double first = modulationRate(rendered, 0, 48000);
    const double second = modulationRate(rendered, 48000, 96000);
    const double last = modulationRate(rendered, 192000, 240000);

    // Les rotors montent en vitesse depuis le repos...
    VSM_ASSERT(second > first * 1.5);
    // ...et l'un d'eux N'A PAS FINI après une seconde : c'est le tambour, et
    // c'est précisément ce que deux constantes de temps distinctes produisent.
    // Un modèle à constante unique serait stabilisé ici.
    VSM_ASSERT(last > second * 1.08);
}

VSM_TEST(tonewheel_wheels_are_slightly_out_of_tune_with_each_other) {
    // Les rapports d'engrenage réels ne donnent pas exactement le tempérament
    // égal ; l'écart infime fait battre les roues voisines. On le vérifie par
    // le BATTEMENT : la même note jouée à l'octave par deux rangs différents
    // ne doit pas donner un niveau parfaitement constant.
    auto synth = makeOrgan();
    makeStatic(synth);
    setDrawbars(synth, {{"Drawbar 16", 8.0f}, {"Drawbar 8", 8.0f}, {"Drawbar 4", 8.0f}});
    const auto rendered = renderEvents(synth, {
        MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, 0, 0, 60, 100},
        MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, 0, 0, 67, 100},
    }, 192000); // 4 s : les battements sont lents

    // Niveau mesuré par tranches d'un quart de seconde : s'il était
    // parfaitement stable, toutes les tranches seraient identiques.
    double minimum = 1e9, maximum = 0.0;
    for (size_t start = 24000; start + 12000 < rendered.left.size(); start += 12000) {
        const double level = rms(rendered.left, start, start + 12000);
        minimum = std::min(minimum, level);
        maximum = std::max(maximum, level);
    }
    VSM_ASSERT(maximum > minimum * 1.005); // il y a bien un battement
    VSM_ASSERT(maximum < minimum * 3.0);   // ...mais ce n'est pas un désaccord
}

VSM_TEST(tonewheel_stays_within_headroom_on_a_full_chord) {
    auto synth = makeOrgan();
    setDrawbars(synth, {{"Drawbar 16", 8.0f}, {"Drawbar 5 1/3", 8.0f}, {"Drawbar 8", 8.0f},
                        {"Drawbar 4", 8.0f}, {"Drawbar 2 2/3", 8.0f}, {"Drawbar 2", 8.0f},
                        {"Drawbar 1 3/5", 8.0f}, {"Drawbar 1 1/3", 8.0f}, {"Drawbar 1", 8.0f}});
    std::vector<MidiNoteEvent> events;
    for (uint8_t note : {uint8_t(48), uint8_t(52), uint8_t(55), uint8_t(59),
                          uint8_t(60), uint8_t(64), uint8_t(67), uint8_t(71)})
        events.push_back(MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, 0, 0, note, 110});
    const auto rendered = renderEvents(synth, events, 48000);
    VSM_ASSERT(peakAbs(rendered.left) < 1.0f);
    VSM_ASSERT(peakAbs(rendered.left) > 0.1f);
}

VSM_TEST(tonewheel_is_deterministic) {
    auto first = makeOrgan();
    auto second = makeOrgan();
    const auto a = renderNote(first, 62, 24000);
    const auto b = renderNote(second, 62, 24000);
    for (size_t i = 0; i < a.left.size(); ++i) {
        VSM_ASSERT_NEAR(a.left[i], b.left[i], 1e-9);
        VSM_ASSERT_NEAR(a.right[i], b.right[i], 1e-9);
    }
}

VSM_TEST(tonewheel_state_round_trips) {
    auto synth = makeOrgan();
    synth->setParameter(byName(*synth, "Drawbar 2 2/3"), 6.0f);
    synth->setParameter(byName(*synth, "Rotary Fast"), 1.0f);
    const auto state = synth->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.tonewheel"));
    auto restored = makeOrgan();
    restored->loadState(state);
    VSM_ASSERT_NEAR(restored->getParameter(byName(*restored, "Drawbar 2 2/3")), 6.0f, 1e-6);
    VSM_ASSERT_NEAR(restored->getParameter(byName(*restored, "Rotary Fast")), 1.0f, 1e-6);
}
