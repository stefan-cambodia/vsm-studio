#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.reed` — l'anche LIBRE, la seule machine du parc dont la pression de jeu
// change la HAUTEUR.
//
// Cette suite existe surtout pour une raison : le dépôt a mesuré cinq échecs
// sur les instruments à vent, et sa conclusion (§ 11 du CDC machines-
// manquantes) désigne le couplage excitateur↔colonne d'air. Une anche libre
// n'a pas de colonne, et le premier test vérifie donc ce qui avait échoué cinq
// fois : que ça s'amorce, et que ça s'amorce JUSTE.

namespace {
constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeReed(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.reed");
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
double rms(const std::vector<float>& x, size_t from, size_t count) {
    double e = 0.0;
    size_t n = 0;
    for (size_t i = from; i < from + count && i < x.size(); ++i, ++n)
        e += static_cast<double>(x[i]) * x[i];
    return std::sqrt(e / static_cast<double>(std::max<size_t>(1, n)));
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

/// LA HAUTEUR AU CENT PRÈS, par balayage fin du pic de magnitude.
///
/// Une autocorrélation à décalage ENTIER ne peut pas servir ici, et c'est une
/// affaire de résolution, pas de goût : à 440 Hz la période fait 109
/// échantillons, si bien qu'un décalage d'un seul échantillon vaut 158 cents.
/// L'effet qu'on mesure en fait une dizaine. Le premier banc s'y est laissé
/// prendre et rapportait « aucune variation » sur la note 69.
double hauteurFine(const std::vector<float>& x, size_t from, size_t count, double f0) {
    double meilleur = 0.0, retenue = f0;
    for (double r = 0.95; r < 1.05; r += 0.0004) {
        const double m = magnitudeAt(x, from, count, f0 * r);
        if (m > meilleur) { meilleur = m; retenue = f0 * r; }
    }
    return retenue;
}

/// L'écart de hauteur en CENTS pour une pression donnée, note 57.
double ecartEnCents(float pression) {
    auto synth = makeReed();
    set(*synth, "Bellows Pressure", pression);
    set(*synth, "Reed Stiffness", 0.2f);
    set(*synth, "Air Loading", 1.0f);
    set(*synth, "Velocity to Pressure", 0.0f);
    set(*synth, "Sustain", 1.0f);
    set(*synth, "Filter Cutoff", 12000.0f);
    const auto audio = render(synth, {noteOn(0, 57, 110)}, static_cast<int>(2.0 * kSampleRate));
    const double f0 = midiToHz(57);
    return 1200.0 * std::log2(hauteurFine(audio, 48000, 16384, f0) / f0);
}
} // namespace

VSM_TEST(reed_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.reed"));
}

VSM_TEST(reed_silent_with_no_events) {
    auto synth = makeReed();
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(reed_note_produces_sound_and_stays_finite) {
    auto synth = makeReed();
    const auto audio = render(synth, {noteOn(0, 57, 110)}, 48000);
    VSM_ASSERT(peakAbs(audio) > 0.01f);
    for (float v : audio) VSM_ASSERT(std::isfinite(v));
}

// --- Ce qui avait échoué cinq fois : s'amorcer, et s'amorcer JUSTE ---------

VSM_TEST(reed_oscillates_in_tune_across_the_range) {
    // Cinq tentatives d'instrument à vent ont buté sur l'amorçage ou la
    // justesse (§ 33 et § 44 d'ARCHITECTURE : « la fréquence se fige à
    // 1 412 Hz quelle que soit la note »). Ici la fréquence est celle de la
    // LAME, que rien ne dispute : elle suit le clavier au cent près.
    for (int note : {45, 52, 57, 64, 69}) {
        auto synth = makeReed();
        set(*synth, "Sustain", 1.0f);
        set(*synth, "Velocity to Pressure", 0.0f);
        set(*synth, "Filter Cutoff", 12000.0f);
        const auto audio = render(synth, {noteOn(0, static_cast<uint8_t>(note), 110)},
                                  static_cast<int>(2.0 * kSampleRate));
        const double f0 = midiToHz(note);
        VSM_ASSERT(rms(audio, 48000, 16384) > 0.01);
        const double cents = 1200.0 * std::log2(hauteurFine(audio, 48000, 16384, f0) / f0);
        VSM_ASSERT(std::abs(cents) < 20.0);
    }
}

// --- LE trait : souffler plus fort fait BAISSER la note --------------------

VSM_TEST(reed_blowing_harder_lowers_the_pitch) {
    // Contre-intuitif, et c'est le fait qui définit une anche libre : la
    // charge d'air ajoutée alourdit la lame, donc la ralentit. C'est le seul
    // endroit du parc où la pression de jeu déplace la HAUTEUR.
    //
    // `vsm.wind`, mesurée au même protocole, ne bouge PAS (+0,5 à +1,0 cent) :
    // sa hauteur est imposée par la longueur du tuyau, pas par son anche. Le
    // contraste oppose donc une machine sensible à une machine insensible —
    // et non deux sens opposés, comme l'hypothèse H11 l'avait d'abord écrit.
    const double douce = ecartEnCents(0.45f);
    const double moyenne = ecartEnCents(0.6f);
    const double forte = ecartEnCents(0.9f);
    VSM_ASSERT(douce > moyenne);
    VSM_ASSERT(moyenne > forte);
    VSM_ASSERT(douce - forte > 5.0);    // mesuré : environ 10 cents
}

VSM_TEST(reed_has_a_pressure_threshold_below_which_nothing_speaks) {
    // Une anche ne parle pas si on ne pousse pas assez : le seuil est le
    // point où l'air rend plus d'énergie que la lame n'en perd. En dessous,
    // la machine est MUETTE, et c'est juste — pas un défaut à corriger.
    auto souffle = [&](float pression) {
        auto synth = makeReed();
        set(*synth, "Bellows Pressure", pression);
        set(*synth, "Reed Stiffness", 0.6f);      // seuil haut : 0,12 + 0,5·0,6
        set(*synth, "Velocity to Pressure", 0.0f);
        set(*synth, "Sustain", 1.0f);
        const auto audio = render(synth, {noteOn(0, 57, 110)}, static_cast<int>(1.5 * kSampleRate));
        return rms(audio, 36000, 16384);
    };
    VSM_ASSERT(souffle(0.2f) < 1e-4);      // sous le seuil : rien
    VSM_ASSERT(souffle(0.9f) > 0.01);      // au-dessus : ça parle
}

VSM_TEST(reed_channel_pressure_is_the_bellows) {
    // Un accordéoniste ne joue pas du clavier seul, il POUSSE. La pression de
    // canal est donc branchée sur le soufflet, et elle prend le pas sur le
    // potentiomètre tant qu'elle arrive.
    auto synth = makeReed();
    MidiControlEvent pression;
    pression.kind = MidiControlEvent::Kind::ChannelPressure;
    pression.value = 0.15f;                // sous le seuil d'amorçage
    VSM_ASSERT(synth->handleControlEvent(pression));
    set(*synth, "Bellows Pressure", 1.0f); // le potentiomètre dit « à fond »...
    set(*synth, "Sustain", 1.0f);
    const auto audio = render(synth, {noteOn(0, 57, 110)}, static_cast<int>(1.0 * kSampleRate));
    VSM_ASSERT(rms(audio, 24000, 16384) < 1e-4);   // ... le soufflet a le dernier mot
}

VSM_TEST(reed_is_deterministic) {
    auto premier = makeReed();
    const auto a = render(premier, {noteOn(0, 57, 110)}, 48128);
    auto second = makeReed();
    const auto b = render(second, {noteOn(0, 57, 110)}, 48128);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT(a[i] == b[i]);
}

VSM_TEST(reed_save_load_roundtrip) {
    auto premier = makeReed();
    set(*premier, "Bellows Pressure", 0.77f);
    set(*premier, "Air Loading", 0.33f);
    set(*premier, "Reed Stiffness", 0.21f);
    const auto etat = premier->saveState();
    auto second = makeReed();
    second->loadState(etat);
    for (const auto& info : premier->parameterList())
        VSM_ASSERT_NEAR(second->getParameter(info.id), premier->getParameter(info.id), 1e-6);
}

VSM_TEST(reed_parameter_list_size) {
    auto synth = makeReed();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t(11));
}

VSM_TEST(reed_honours_pitch_bend) {
    auto synth = makeReed();
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 2.0f;
    VSM_ASSERT(synth->handleControlEvent(bend));
}
