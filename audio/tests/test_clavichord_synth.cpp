#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.clavichord` — le seul clavier du parc où appuyer plus fort MONTE la
// note.
//
// La tangente ne rebondit pas : elle reste en contact et définit la longueur
// vibrante, si bien qu'appuyer tend la corde. C'est le *Bebung*, la seule
// façon de faire un vibrato sur un clavier. Le pendant exact de `vsm.reed`,
// qui déplace aussi la hauteur sous la pression — mais vers le bas.

namespace {
constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeClavichord(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.clavichord");
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
MidiNoteEvent noteOff(int offset, uint8_t note) {
    return {MidiNoteEvent::Kind::NoteOff, offset, 0, note, 0};
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
/// Hauteur au cent près : balayage fin du pic. Une autocorrélation à décalage
/// ENTIER ne peut pas servir — à 220 Hz, un échantillon vaut 79 cents, et
/// l'effet mesuré en fait trente. La leçon du banc de `vsm.reed`.
double hauteurFine(const std::vector<float>& x, size_t from, size_t count, double f0) {
    double meilleur = 0.0, retenue = f0;
    for (double r = 0.97; r < 1.06; r += 0.0003) {
        const double m = magnitudeAt(x, from, count, f0 * r);
        if (m > meilleur) { meilleur = m; retenue = f0 * r; }
    }
    return retenue;
}
float midiToHz(int note) { return 440.0f * std::exp2f((static_cast<float>(note) - 69.0f) / 12.0f); }

/// Rend une note tenue avec une pression appliquée APRÈS l'attaque, comme le
/// doigt du claviériste qui appuie dans une touche déjà enfoncée.
std::vector<float> avecPression(float pression, bool relacher, int frames,
                                bool parNote = false) {
    auto synth = makeClavichord();
    set(*synth, "String Decay", 8.0f);
    set(*synth, "Velocity Sensitivity", 0.0f);
    set(*synth, "Filter Cutoff", 14000.0f);
    std::vector<float> left(static_cast<size_t>(frames), 0.0f), right(static_cast<size_t>(frames), 0.0f);
    constexpr int kBlock = 256;
    const int relachement = 24000;
    for (int start = 0; start + kBlock <= frames; start += kBlock) {
        std::vector<MidiNoteEvent> block;
        if (start == 0) block.push_back(noteOn(0, 57, 110));
        if (relacher && start <= relachement && relachement < start + kBlock)
            block.push_back(noteOff(relachement - start, 57));
        if (start == 2560) {
            MidiControlEvent pr;
            pr.kind = parNote ? MidiControlEvent::Kind::PolyPressure
                              : MidiControlEvent::Kind::ChannelPressure;
            pr.index = 57;
            pr.value = pression;
            synth->handleControlEvent(pr);
        }
        synth->process(block.empty() ? nullptr : block.data(), static_cast<int>(block.size()),
                       left.data() + start, right.data() + start, kBlock);
    }
    return left;
}
double ecartEnCents(float pression) {
    const auto audio = avecPression(pression, false, 96000);
    const double f0 = midiToHz(57);
    return 1200.0 * std::log2(hauteurFine(audio, 24000, 32768, f0) / f0);
}
double rms(const std::vector<float>& x, size_t from, size_t count) {
    double e = 0.0;
    for (size_t i = from; i < from + count && i < x.size(); ++i)
        e += static_cast<double>(x[i]) * x[i];
    return std::sqrt(e / static_cast<double>(count));
}
} // namespace

VSM_TEST(clavichord_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.clavichord"));
}

VSM_TEST(clavichord_silent_with_no_events) {
    auto synth = makeClavichord();
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(clavichord_note_produces_sound_and_stays_finite) {
    auto synth = makeClavichord();
    std::vector<float> left(48000, 0.0f), right(48000, 0.0f);
    const auto note = noteOn(0, 57, 110);
    for (int s = 0; s + 256 <= 48000; s += 256)
        synth->process(s == 0 ? &note : nullptr, s == 0 ? 1 : 0,
                       left.data() + s, right.data() + s, 256);
    VSM_ASSERT(peakAbs(left) > 0.005f);
    for (float v : left) VSM_ASSERT(std::isfinite(v));
}

// --- LE trait : appuyer plus fort MONTE la note ---------------------------

VSM_TEST(clavichord_pressing_harder_raises_the_pitch) {
    // Le *Bebung*, et c'est la seule façon de faire un vibrato sur un clavier.
    // La tangente reste en contact avec la corde : appuyer la TEND.
    //
    // C'est aussi le pendant exact de `vsm.reed`, qui déplace la hauteur sous
    // la pression mais vers le BAS (−8,9 cents en bout de course, mesuré au
    // même protocole) : le parc a désormais les deux sens, et ils viennent de
    // deux mécaniques opposées — une corde qu'on tend, une lame qu'on alourdit.
    const double repos = ecartEnCents(0.0f);
    const double quart = ecartEnCents(0.25f);
    const double moitie = ecartEnCents(0.5f);
    const double fond = ecartEnCents(1.0f);

    VSM_ASSERT(std::abs(repos) < 3.0);      // sans pression, la note est juste
    VSM_ASSERT(quart > repos + 3.0);        // et la montée est MONOTONE
    VSM_ASSERT(moitie > quart + 3.0);
    VSM_ASSERT(fond > moitie + 3.0);
    VSM_ASSERT(fond > 15.0);                // mesuré : +29,2 cents
    VSM_ASSERT(fond < 60.0);                // un Bebung colore, il ne transpose pas
}

VSM_TEST(clavichord_pressure_can_be_sent_per_key) {
    // Un clavicordiste fait vibrer UNE note pendant que les autres tiennent :
    // la pression par touche est donc honorée, comme sur `vsm.cs80` — sauf
    // qu'ici elle va à la TENSION et non au filtre.
    const double parNote = 1200.0 * std::log2(
        hauteurFine(avecPression(1.0f, false, 96000, true), 24000, 32768, midiToHz(57))
        / midiToHz(57));
    VSM_ASSERT(parNote > 15.0);
}

// --- Second trait : relâcher COUPE, ça ne laisse pas mourir ----------------

VSM_TEST(clavichord_releasing_the_key_stops_the_sound_at_once) {
    // La tangente quitte la corde, dont l'autre bout est tressé de feutre : ni
    // résonance ni traîne. Sur toutes les autres cordes du parc, le
    // relâchement OUVRE une décroissance ; ici il coupe. Et le réglage de
    // décroissance de la corde — huit secondes ici — n'y peut rien, parce que
    // ce n'est pas la corde qui décide, c'est le feutre.
    const auto audio = avecPression(0.0f, true, 96000);
    const double avant = rms(audio, 19200, 4096);       // 0,40 s : la note sonne
    const double apres = rms(audio, 26400, 4096);       // +50 ms après le lâcher
    VSM_ASSERT(avant > 0.001);
    VSM_ASSERT(apres < avant * 0.01);
}

VSM_TEST(clavichord_is_deterministic) {
    const auto a = avecPression(0.5f, false, 48128);
    const auto b = avecPression(0.5f, false, 48128);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT(a[i] == b[i]);
}

VSM_TEST(clavichord_save_load_roundtrip) {
    auto premier = makeClavichord();
    set(*premier, "Pressure to Tension", 0.61f);
    set(*premier, "Tangent Position", 0.29f);
    const auto etat = premier->saveState();
    auto second = makeClavichord();
    second->loadState(etat);
    for (const auto& info : premier->parameterList())
        VSM_ASSERT_NEAR(second->getParameter(info.id), premier->getParameter(info.id), 1e-6);
}

VSM_TEST(clavichord_parameter_list_size) {
    auto synth = makeClavichord();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t(8));
}

VSM_TEST(clavichord_honours_pitch_bend) {
    auto synth = makeClavichord();
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 2.0f;
    VSM_ASSERT(synth->handleControlEvent(bend));
}
