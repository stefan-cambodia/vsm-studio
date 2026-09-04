#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.kalimba` — la lame encastrée d'un seul côté (1 : 6,27 : 17,55), le buzz
// du contact qui s'éteint seul, la caisse qu'on bouche (H34, écrite avant sa
// mesure, CDC machines-manquantes § 30).

namespace {

constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeKalimba(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.kalimba");
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
double rmsOf(const std::vector<float>& x, size_t from, size_t count) {
    double s = 0.0; size_t n = 0;
    for (size_t i = from; i < from + count && i < x.size(); ++i) { s += x[i] * x[i]; ++n; }
    return n ? std::sqrt(s / static_cast<double>(n)) : 0.0;
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
double pic(const std::vector<float>& x, size_t from, size_t count, double lo, double hi) {
    double m = 0.0;
    for (double f = lo; f <= hi; f += 0.5) m = std::max(m, magnitudeAt(x, from, count, f));
    return m;
}
/// L'énergie d'une bande, par une grille de 100 Hz : ce qui compte est un rapport.
double bande(const std::vector<float>& x, size_t from, size_t count, double lo, double hi) {
    double s = 0.0;
    for (double f = lo; f <= hi; f += 100.0) { const double m = magnitudeAt(x, from, count, f); s += m * m; }
    return std::sqrt(s);
}
std::vector<float> rendre(ISynthPlugin& synth, uint8_t note, uint8_t velocity, int tenue, int frames,
                          const MidiControlEvent* controle = nullptr) {
    std::vector<float> left(static_cast<size_t>(frames), 0.0f), right(static_cast<size_t>(frames), 0.0f);
    constexpr int kBlock = 256;
    if (controle) synth.handleControlEvent(*controle);
    for (int start = 0; start + kBlock <= frames; start += kBlock) {
        std::vector<MidiNoteEvent> block;
        if (start == 0) block.push_back(noteOn(0, note, velocity));
        if (tenue > 0 && start <= tenue && tenue < start + kBlock) block.push_back(noteOff(tenue - start, note));
        synth.process(block.empty() ? nullptr : block.data(), static_cast<int>(block.size()),
                      left.data() + start, right.data() + start, kBlock);
    }
    return left;
}
/// La lame seule : ni caisse, ni buzz.
void lameNue(ISynthPlugin& s) {
    set(s, "Body Level", 0.0f);
    set(s, "Buzz", 0.0f);
}

} // namespace

VSM_TEST(kalimba_registered) {
    auto synth = makeKalimba();
    VSM_ASSERT(synth != nullptr);
    VSM_ASSERT_EQ(std::string(synth->machineName()), std::string("Kalimba (la lame encastrée et la caisse qu’on bouche)"));
}

VSM_TEST(kalimba_silent_with_no_events) {
    auto synth = makeKalimba();
    std::vector<float> l(2048, 1.0f), r(2048, 1.0f);
    synth->process(nullptr, 0, l.data(), r.data(), 2048);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0f, 1e-9f);
}

VSM_TEST(kalimba_note_produces_sound_and_stays_finite) {
    auto synth = makeKalimba();
    auto out = rendre(*synth, 69, 100, 0, 48000);
    VSM_ASSERT(peakAbs(out) > 0.01f);
    for (float v : out) VSM_ASSERT(std::isfinite(v));
    VSM_ASSERT(peakAbs(out) < 3.0f);
}

/// LE TRAIT DISTINCTIF, PREMIER : les partiels d'une POUTRE ENCASTRÉE. Sur
/// la3 (220 Hz), un pic près de 1 379 Hz (6,27·f0) — et rien à 440 ni à 660.
VSM_TEST(kalimba_partials_are_those_of_a_clamped_free_beam) {
    auto synth = makeKalimba();
    lameNue(*synth);
    auto out = rendre(*synth, 57, 100, 0, 48000);
    // Les 100 premières ms (10 Hz de résolution) : le second mode meurt en
    // 0,3 s, une fenêtre plus tardive ne le verrait plus.
    const double p1379 = pic(out, 0, 4800, 1370.0, 1388.0);
    const double p440 = pic(out, 0, 4800, 430.0, 450.0);
    const double p660 = pic(out, 0, 4800, 650.0, 670.0);
    const double p220 = pic(out, 0, 4800, 210.0, 230.0);
    std::printf("    [banc kalimba] partiels (100 premières ms) : 220 Hz %.5f ; 6,27·f0 (1 379 Hz) %.5f ; 2·f0 %.5f ; 3·f0 %.5f\n",
                p220, p1379, p440, p660);
    VSM_ASSERT(p1379 > p440 * 5.0);
    VSM_ASSERT(p1379 > p660 * 5.0);
    VSM_ASSERT(p1379 > p220 * 0.01);   // et il est bien là
    VSM_ASSERT(p220 > p1379);
}

/// SECOND TRAIT : le BUZZ DU CONTACT dépend de la force, et s'éteint seul.
/// Buzz 0,6 : l'énergie 2–6 kHz rapportée au fondamental, dans les 60
/// premières ms, au moins ×4 à 127 par rapport à 40 ; sous ×2 à Buzz 0 ; et à
/// 127 ce rapport tombe sous un cinquième entre 400 et 460 ms.
VSM_TEST(kalimba_buzz_needs_a_firm_thumb_and_dies_by_itself) {
    auto douxB = makeKalimba(); auto fortB = makeKalimba();
    auto doux0 = makeKalimba(); auto fort0 = makeKalimba();
    for (auto* s : {douxB.get(), fortB.get()}) { set(*s, "Body Level", 0.0f); set(*s, "Buzz", 0.6f); }
    for (auto* s : {doux0.get(), fort0.get()}) { lameNue(*s); }
    auto a = rendre(*douxB, 57, 40, 0, 48000), b = rendre(*fortB, 57, 127, 0, 48000);
    auto c = rendre(*doux0, 57, 40, 0, 48000), d = rendre(*fort0, 57, 127, 0, 48000);
    auto rapport = [&](const std::vector<float>& x, size_t from) {
        return bande(x, from, 2880, 2000.0, 6000.0) / std::max(1e-12, magnitudeAt(x, from, 2880, 220.0));
    };
    const double rDouxB = rapport(a, 0), rFortB = rapport(b, 0), rDoux0 = rapport(c, 0), rFort0 = rapport(d, 0);
    const double rFortTard = rapport(b, 19200);
    std::printf("    [banc kalimba] buzz : 2–6 kHz / fondamental, 60 premières ms — Buzz 0,6 : v40 %.4f, v127 %.4f ; Buzz 0 : v40 %.4f, v127 %.4f ; v127 à 400 ms %.4f\n",
                rDouxB, rFortB, rDoux0, rFort0, rFortTard);
    VSM_ASSERT(rFortB >= rDouxB * 4.0);
    VSM_ASSERT(rFort0 < rDoux0 * 2.0);
    VSM_ASSERT(rFortTard < rFortB * 0.2);
}

/// TROISIÈME TRAIT : la CAISSE QU'ON BOUCHE. Caisse à 233 Hz : sur si♭3, boucher
/// fait baisser le fondamental d'au moins 20 % ; sur do♯3 (138,6 Hz, où la
/// résonance descend), il MONTE d'au moins 20 %.
VSM_TEST(kalimba_covering_the_holes_moves_the_body_resonance_down) {
    auto ouvert = makeKalimba(); auto bouche = makeKalimba();
    auto ouvertBas = makeKalimba(); auto boucheBas = makeKalimba();
    for (auto* s : {ouvert.get(), bouche.get(), ouvertBas.get(), boucheBas.get()}) {
        set(*s, "Body Resonance", 233.0f); set(*s, "Body Level", 1.0f); set(*s, "Buzz", 0.0f);
    }
    set(*bouche, "Hole Cover", 1.0f);
    set(*boucheBas, "Hole Cover", 1.0f);
    // initialize() a lu Hole Cover : on le refait pour partir bouché sans glissement.
    bouche->initialize(kSampleRate, 512);
    boucheBas->initialize(kSampleRate, 512);
    auto a = rendre(*ouvert, 58, 100, 0, 24000), b = rendre(*bouche, 58, 100, 0, 24000);
    auto c = rendre(*ouvertBas, 49, 100, 0, 24000), d = rendre(*boucheBas, 49, 100, 0, 24000);
    const double haut0 = magnitudeAt(a, 0, 9600, 233.08), haut1 = magnitudeAt(b, 0, 9600, 233.08);
    const double bas0 = magnitudeAt(c, 0, 9600, 138.59), bas1 = magnitudeAt(d, 0, 9600, 138.59);
    std::printf("    [banc kalimba] caisse bouchée : si♭3 %.5f -> %.5f ; do♯3 %.5f -> %.5f\n", haut0, haut1, bas0, bas1);
    VSM_ASSERT(haut1 <= haut0 * 0.8);
    VSM_ASSERT(bas1 >= bas0 * 1.2);
}

/// La molette de modulation EST les doigts sur les trous : CC 1 à 1 fait
/// baisser si♭3 d'au moins 20 %, comme Hole Cover 1. Et la pression aussi.
VSM_TEST(kalimba_mod_wheel_and_aftertouch_cover_the_holes) {
    auto ouvert = makeKalimba(); auto molette = makeKalimba(); auto pression = makeKalimba();
    for (auto* s : {ouvert.get(), molette.get(), pression.get()}) {
        set(*s, "Body Resonance", 233.0f); set(*s, "Body Level", 1.0f); set(*s, "Buzz", 0.0f);
    }
    MidiControlEvent cc1; cc1.kind = MidiControlEvent::Kind::ControlChange; cc1.index = 1; cc1.value = 1.0f;
    MidiControlEvent at; at.kind = MidiControlEvent::Kind::ChannelPressure; at.value = 1.0f;
    VSM_ASSERT(molette->handleControlEvent(cc1));
    VSM_ASSERT(pression->handleControlEvent(at));
    auto a = rendre(*ouvert, 58, 100, 0, 24000);
    auto b = rendre(*molette, 58, 100, 0, 24000);
    auto c = rendre(*pression, 58, 100, 0, 24000);
    // La caisse suit les doigts en 20 ms : on mesure de 100 à 300 ms.
    const double o = magnitudeAt(a, 4800, 9600, 233.08), m = magnitudeAt(b, 4800, 9600, 233.08), p = magnitudeAt(c, 4800, 9600, 233.08);
    std::printf("    [banc kalimba] molette : si♭3 ouvert %.5f, CC 1 %.5f, pression %.5f\n", o, m, p);
    VSM_ASSERT(m <= o * 0.8);
    VSM_ASSERT(p <= o * 0.8);
}

/// Une lame courte meurt plus vite : do5 s'éteint avant do4.
VSM_TEST(kalimba_short_tines_die_faster) {
    auto grave = makeKalimba(); auto aigu = makeKalimba();
    lameNue(*grave); lameNue(*aigu);
    auto a = rendre(*grave, 60, 100, 0, 96000), b = rendre(*aigu, 72, 100, 0, 96000);
    const double rA = rmsOf(a, 48000, 4800) / std::max(1e-12, rmsOf(a, 2400, 4800));
    const double rB = rmsOf(b, 48000, 4800) / std::max(1e-12, rmsOf(b, 2400, 4800));
    std::printf("    [banc kalimba] tenue à 1 s : do4 %.3f, do5 %.3f\n", rA, rB);
    VSM_ASSERT(rB < rA);
}

/// Pas d'étouffoir : lâcher la touche ne change RIEN — la note relâchée et la
/// note tenue sont identiques au bit près. (La première forme de l'attendu,
/// « au moins la moitié du niveau 200 ms après », comparait deux instants
/// d'une note qui décroît d'elle-même : ×0,47 en 300 ms à 2,5 s de T60. C'est
/// l'arithmétique qui l'a réfutée, pas le modèle ; la forme exacte est ici.)
VSM_TEST(kalimba_release_does_not_damp_the_tine) {
    auto tenue = makeKalimba(); auto lachee = makeKalimba();
    lameNue(*tenue); lameNue(*lachee);
    auto a = rendre(*tenue, 57, 100, 0, 48000), b = rendre(*lachee, 57, 100, 12000, 48000);
    const double apres = rmsOf(b, 12000 + 9600, 4800), avant = rmsOf(b, 7200, 4800);
    std::printf("    [banc kalimba] relâchement : rms avant %.5f, 200 ms après %.5f ; tenue et relâchée identiques : %s\n",
                avant, apres, a == b ? "oui" : "NON");
    VSM_ASSERT(a == b);
    VSM_ASSERT(apres > 0.01);   // et elle sonne encore
}

VSM_TEST(kalimba_velocity_matters) {
    auto douce = makeKalimba(); auto forte = makeKalimba();
    auto a = rendre(*douce, 57, 30, 0, 24000), b = rendre(*forte, 57, 120, 0, 24000);
    VSM_ASSERT(rmsOf(b, 0, 9600) > rmsOf(a, 0, 9600) * 1.5);
}

VSM_TEST(kalimba_is_deterministic) {
    auto a = makeKalimba(); auto b = makeKalimba();
    VSM_ASSERT(rendre(*a, 62, 120, 12000, 24000) == rendre(*b, 62, 120, 12000, 24000));
}

VSM_TEST(kalimba_save_load_roundtrip) {
    auto synth = makeKalimba();
    set(*synth, "Body Resonance", 310.0f);
    set(*synth, "Buzz", 0.8f);
    auto state = synth->saveState();
    auto other = makeKalimba();
    other->loadState(state);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Body Resonance")), 310.0f, 1e-3f);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Buzz")), 0.8f, 1e-6f);
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.kalimba"));
}

VSM_TEST(kalimba_parameter_list_size) {
    auto synth = makeKalimba();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t{9});
}

VSM_TEST(kalimba_refuses_pitch_bend) {
    auto synth = makeKalimba();
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 2.0f;
    VSM_ASSERT(!synth->handleControlEvent(bend));
}
