#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.pipeorgan` — une soufflerie COMMUNE (le vent s'affaisse quand un accord
// s'ajoute), des tuyaux qui parlent (l'octave sort la première), des jeux qui
// se tirent, un tremblant qui fait onduler hauteur et niveau ensemble (H36,
// écrite avant sa mesure, CDC machines-manquantes § 32).

namespace {

constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeOrgan(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.pipeorgan");
    if (plugin) plugin->initialize(sr, 512);
    return plugin;
}
ParamId byName(const ISynthPlugin& plugin, const std::string& name) {
    for (const auto& info : plugin.parameterList()) if (info.name == name) return info.id;
    throw vsm::test::AssertionFailure("paramètre inconnu : « " + name + " »");
}
void set(ISynthPlugin& plugin, const std::string& name, float value) { plugin.setParameter(byName(plugin, name), value); }
MidiNoteEvent noteOn(int offset, uint8_t note, uint8_t velocity = 100) { return {MidiNoteEvent::Kind::NoteOn, offset, 0, note, velocity}; }
MidiNoteEvent noteOff(int offset, uint8_t note) { return {MidiNoteEvent::Kind::NoteOff, offset, 0, note, 0}; }
float peakAbs(const std::vector<float>& b) { float p = 0.0f; for (float v : b) p = std::max(p, std::abs(v)); return p; }
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
double picHz(const std::vector<float>& x, size_t from, size_t count, double lo, double hi) {
    double meilleur = lo, m = -1.0;
    for (double f = lo; f <= hi; f += 0.1) { const double v = magnitudeAt(x, from, count, f); if (v > m) { m = v; meilleur = f; } }
    return meilleur;
}
/// Rend `frames` ; chaque note de `notes` part à `departs[i]`, `fins[i]` = 0 jamais relâchée.
std::vector<float> rendre(ISynthPlugin& synth, const std::vector<uint8_t>& notes, const std::vector<int>& departs,
                          const std::vector<int>& fins, int frames, uint8_t velocity = 100) {
    std::vector<float> left(static_cast<size_t>(frames), 0.0f), right(static_cast<size_t>(frames), 0.0f);
    constexpr int kBlock = 256;
    for (int start = 0; start + kBlock <= frames; start += kBlock) {
        std::vector<MidiNoteEvent> block;
        for (size_t k = 0; k < notes.size(); ++k) {
            if (start <= departs[k] && departs[k] < start + kBlock) block.push_back(noteOn(departs[k] - start, notes[k], velocity));
            if (fins[k] > 0 && start <= fins[k] && fins[k] < start + kBlock) block.push_back(noteOff(fins[k] - start, notes[k]));
        }
        std::sort(block.begin(), block.end(), [](const MidiNoteEvent& a, const MidiNoteEvent& b) { return a.sampleOffset < b.sampleOffset; });
        synth.process(block.empty() ? nullptr : block.data(), static_cast<int>(block.size()),
                      left.data() + start, right.data() + start, kBlock);
    }
    return left;
}
std::vector<float> uneNote(ISynthPlugin& s, uint8_t note, int tenue, int frames, uint8_t velocity = 100) {
    return rendre(s, {note}, {0}, {tenue}, frames, velocity);
}
/// L'instant où l'énergie à `hz` (fenêtres de 20 ms, au pas de 2 ms -- une
/// fenêtre plus courte qu'une période de do3 ne sépare pas ses harmoniques)
/// atteint la moitié de sa valeur à 200 ms. L'instant est celui du MILIEU de
/// la fenêtre.
double demiMontee(const std::vector<float>& x, double hz) {
    const double regime = magnitudeAt(x, 9600, 4800, hz);
    for (size_t t = 0; t + 960 < 9600; t += 96)
        if (magnitudeAt(x, t, 960, hz) >= 0.5 * regime) return static_cast<double>(t + 480) / kSampleRate * 1000.0;
    return 200.0;
}

} // namespace

VSM_TEST(pipeorgan_registered) {
    auto synth = makeOrgan();
    VSM_ASSERT(synth != nullptr);
    VSM_ASSERT_EQ(std::string(synth->machineName()), std::string("Pipe Organ (une soufflerie commune)"));
}

VSM_TEST(pipeorgan_silent_with_no_events) {
    auto synth = makeOrgan();
    std::vector<float> l(2048, 1.0f), r(2048, 1.0f);
    synth->process(nullptr, 0, l.data(), r.data(), 2048);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0f, 1e-9f);
}

VSM_TEST(pipeorgan_note_produces_sound_and_stays_finite) {
    auto synth = makeOrgan();
    auto out = uneNote(*synth, 48, 0, 48000);
    VSM_ASSERT(peakAbs(out) > 0.01f);
    for (float v : out) VSM_ASSERT(std::isfinite(v));
    VSM_ASSERT(peakAbs(out) < 3.0f);
}

/// (1) La vélocité est REFUSÉE : deux vélocités extrêmes, une sortie identique.
VSM_TEST(pipeorgan_refuses_velocity_bit_for_bit) {
    auto a = makeOrgan(); auto b = makeOrgan();
    VSM_ASSERT(uneNote(*a, 48, 12000, 24000, 30) == uneNote(*b, 48, 12000, 24000, 120));
}

/// (2) LE TUYAU PARLE : à chiff 1, l'octave atteint la moitié de son régime
/// au moins 10 ms avant la fondamentale ; à 0, moins de 3 ms d'écart.
VSM_TEST(pipeorgan_chiff_makes_the_octave_speak_first) {
    for (float chiff : {1.0f, 0.0f}) {
        auto synth = makeOrgan();
        set(*synth, "Chiff", chiff);
        set(*synth, "Wind Sag", 0.0f);
        set(*synth, "Flute 4'", 0.0f);
        auto out = uneNote(*synth, 48, 0, 24000);   // do3, 130,8 Hz
        const double tFond = demiMontee(out, 130.81), tOct = demiMontee(out, 261.63);
        std::printf("    [banc orgue] chiff %.0f : la fondamentale à mi-régime à %.1f ms, l'octave à %.1f ms\n", chiff, tFond, tOct);
        if (chiff > 0.5f) VSM_ASSERT(tFond - tOct >= 10.0);
        else VSM_ASSERT(std::abs(tFond - tOct) < 3.0);
    }
}

/// (3) LE VENT S'AFFAISSE : un do3 tenu, rejoint par sept notes, baisse d'au
/// moins 5 cents et de 10 % de niveau ; il remonte quand elles lâchent.
VSM_TEST(pipeorgan_wind_sags_when_a_chord_joins_and_recovers_when_it_leaves) {
    auto synth = makeOrgan();
    set(*synth, "Wind Sag", 1.0f);
    set(*synth, "Chiff", 0.0f);
    set(*synth, "Flute 4'", 0.0f);
    set(*synth, "Mixture", 0.0f);
    // do3 seul de 0 à 1 s ; sept notes AIGUËS (do5 à si5) de 1 s à 2 s ; do3 seul encore.
    std::vector<uint8_t> notes{48, 72, 74, 76, 77, 79, 81, 83};
    std::vector<int> departs{0, 48000, 48000, 48000, 48000, 48000, 48000, 48000};
    std::vector<int> fins{0, 96000, 96000, 96000, 96000, 96000, 96000, 96000};
    auto out = rendre(*synth, notes, departs, fins, 144000);
    const double seul = picHz(out, 24000, 19200, 125.0, 136.0), accord = picHz(out, 72000, 19200, 125.0, 136.0);
    const double apres = picHz(out, 120000, 19200, 125.0, 136.0);
    const double nivSeul = magnitudeAt(out, 24000, 19200, seul), nivAccord = magnitudeAt(out, 72000, 19200, accord);
    const double cents = 1200.0 * std::log2(accord / seul);
    std::printf("    [banc orgue] sag : do3 seul %.2f Hz (%.5f), dans l'accord %.2f Hz (%.5f, %.1f cents), après %.2f Hz\n",
                seul, nivSeul, accord, nivAccord, cents, apres);
    VSM_ASSERT(cents <= -5.0);
    VSM_ASSERT(nivAccord <= nivSeul * 0.9);
    VSM_ASSERT(std::abs(1200.0 * std::log2(apres / seul)) < 2.0);
}

/// (4) LES JEUX : tirer la fourniture multiplie par au moins 3 la douzième.
VSM_TEST(pipeorgan_mixture_adds_the_twelfth) {
    auto sans = makeOrgan(); auto avec = makeOrgan();
    for (auto* s : {sans.get(), avec.get()}) { set(*s, "Chiff", 0.0f); set(*s, "Wind Sag", 0.0f); }
    set(*avec, "Mixture", 1.0f);
    auto a = uneNote(*sans, 48, 0, 48000), b = uneNote(*avec, 48, 0, 48000);
    const double dA = magnitudeAt(a, 9600, 32768, 130.81 * 3.0), dB = magnitudeAt(b, 9600, 32768, 130.81 * 3.0);
    std::printf("    [banc orgue] fourniture : douzième %.5f sans, %.5f avec\n", dA, dB);
    VSM_ASSERT(dB >= dA * 3.0);
}

/// (5) LE TREMBLANT fait onduler la hauteur ET le niveau, ensemble.
VSM_TEST(pipeorgan_tremulant_modulates_pitch_and_level_together) {
    auto synth = makeOrgan();
    set(*synth, "Tremulant Rate", 6.0f);
    set(*synth, "Tremulant Depth", 1.0f);
    set(*synth, "Chiff", 0.0f); set(*synth, "Wind Sag", 0.0f); set(*synth, "Flute 4'", 0.0f);
    auto out = uneNote(*synth, 60, 0, 96000);   // do4, 261,6 Hz
    // Fenêtres de 10 ms sur une seconde : la hauteur et le niveau par fenêtre.
    std::vector<double> hauteurs, niveaux;
    for (size_t d = 48000; d + 1200 <= 96000; d += 600) {
        hauteurs.push_back(picHz(out, d, 1200, 250.0, 275.0));
        niveaux.push_back(rmsOf(out, d, 1200));
    }
    double hMin = 1e9, hMax = 0.0, nMin = 1e9, nMax = 0.0;
    for (size_t i = 0; i < hauteurs.size(); ++i) { hMin = std::min(hMin, hauteurs[i]); hMax = std::max(hMax, hauteurs[i]); nMin = std::min(nMin, niveaux[i]); nMax = std::max(nMax, niveaux[i]); }
    double mh = 0.0, mn = 0.0;
    for (size_t i = 0; i < hauteurs.size(); ++i) { mh += hauteurs[i]; mn += niveaux[i]; }
    mh /= hauteurs.size(); mn /= niveaux.size();
    double num = 0.0, eh = 0.0, en = 0.0;
    for (size_t i = 0; i < hauteurs.size(); ++i) { num += (hauteurs[i] - mh) * (niveaux[i] - mn); eh += (hauteurs[i] - mh) * (hauteurs[i] - mh); en += (niveaux[i] - mn) * (niveaux[i] - mn); }
    const double correlation = num / std::sqrt(eh * en + 1e-18);
    const double centsPP = 1200.0 * std::log2(hMax / hMin);
    std::printf("    [banc orgue] tremblant 6 Hz : hauteur %.1f-%.1f Hz (%.1f cents crête à crête), niveau %.4f-%.4f (%.0f %%), corrélation %.2f\n",
                hMin, hMax, centsPP, nMin, nMax, 100.0 * (1.0 - nMin / nMax), correlation);
    VSM_ASSERT(centsPP >= 10.0);
    VSM_ASSERT(1.0 - nMin / nMax >= 0.10);
    VSM_ASSERT(correlation > 0.5);
}

/// (6) Le relâchement ferme la soupape : sous 10 % en 300 ms.
VSM_TEST(pipeorgan_release_closes_the_valve) {
    auto synth = makeOrgan();
    set(*synth, "Chiff", 0.0f);
    auto out = uneNote(*synth, 48, 24000, 48000);
    const double avant = rmsOf(out, 19200, 4800), apres = rmsOf(out, 24000 + 14400, 4800);
    std::printf("    [banc orgue] relâchement : %.4f -> %.5f à 300 ms\n", avant, apres);
    VSM_ASSERT(apres < avant * 0.1);
}

VSM_TEST(pipeorgan_is_deterministic) {
    auto a = makeOrgan(); auto b = makeOrgan();
    VSM_ASSERT(uneNote(*a, 55, 12000, 24000) == uneNote(*b, 55, 12000, 24000));
}

VSM_TEST(pipeorgan_save_load_roundtrip) {
    auto synth = makeOrgan();
    set(*synth, "Mixture", 0.7f);
    set(*synth, "Tremulant Rate", 5.5f);
    auto state = synth->saveState();
    auto other = makeOrgan();
    other->loadState(state);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Mixture")), 0.7f, 1e-6f);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Tremulant Rate")), 5.5f, 1e-4f);
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.pipeorgan"));
}

VSM_TEST(pipeorgan_parameter_list_size) {
    auto synth = makeOrgan();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t{10});
}

VSM_TEST(pipeorgan_refuses_pitch_bend) {
    auto synth = makeOrgan();
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 2.0f;
    VSM_ASSERT(!synth->handleControlEvent(bend));
}
