#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.granular` — le son comme NUAGE de grains fenêtrés. Ajoutée au titre du
// § 7 du CDC machines (le jeu, pas la reconstruction). Ce que cette suite
// verrouille est le CONTINUUM qui définit la famille : à dispersion nulle, la
// machine est périodique et nette ; la dispersion de hauteur étale le spectre,
// celle du temps dérègle l'horloge — et tout le nuage est rejouable au bit
// près, parce que son aléatoire est seedé et repart à chaque note.

namespace {
constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeGranular(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.granular");
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
double rmsOf(const std::vector<float>& x, size_t from, size_t to) {
    double s = 0.0; size_t n = 0;
    for (size_t i = from; i < to && i < x.size(); ++i) { s += static_cast<double>(x[i]) * x[i]; ++n; }
    return n ? std::sqrt(s / static_cast<double>(n)) : 0.0;
}

/// Réglage de mesure : filtre ouvert, stéréo au centre, source sinus.
void steady(ISynthPlugin& plugin) {
    set(plugin, "Filter Cutoff", 16000.0f);
    set(plugin, "Filter Resonance", 0.0f);
    set(plugin, "Stereo Spread", 0.0f);
    set(plugin, "Grain Shape", 0.0f);
    set(plugin, "Pitch Spray", 0.0f);
    set(plugin, "Time Spray", 0.0f);
    set(plugin, "Shimmer", 0.0f);
    set(plugin, "Amp Attack", 0.005f);
}
} // namespace

VSM_TEST(granular_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.granular"));
}

VSM_TEST(granular_silent_with_no_events) {
    auto synth = makeGranular();
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(granular_note_produces_sound_and_stays_finite) {
    auto synth = makeGranular();
    const auto audio = render(synth, {noteOn(0, 60, 100)}, 24064);
    VSM_ASSERT(peakAbs(audio) > 0.01f);
    for (float v : audio) VSM_ASSERT(std::isfinite(v));
}

// --- LE trait distinctif : le continuum de la note à la texture ------------

VSM_TEST(granular_zero_spray_is_a_clean_pitch) {
    // La moitié témoin : dispersion nulle, grains alignés à recouvrement --
    // la machine doit rendre une hauteur NETTE. Sans elle, la dispersion ne
    // serait pas un réglage, ce serait un état permanent.
    auto synth = makeGranular();
    steady(*synth);
    const auto audio = render(synth, {noteOn(0, 69, 100)}, 49152);
    const double f0 = midiToHz(69);
    const double h1 = magnitudeAt(audio, 24000, 16384, f0);
    const double aCote = magnitudeAt(audio, 24000, 16384, f0 * 1.26);   // une tierce à côté
    VSM_ASSERT(h1 > 0.01);
    VSM_ASSERT(aCote < h1 * 0.05);
}

VSM_TEST(granular_pitch_spray_spreads_the_spectrum) {
    // La dispersion de hauteur étale l'énergie AUTOUR du fondamental : la
    // raie nette du témoin s'affaisse, ce qui était silence entre les notes
    // se remplit. C'est la définition du nuage.
    auto mesurer = [&](float spray) {
        auto synth = makeGranular();
        steady(*synth);
        set(*synth, "Pitch Spray", spray);
        const auto audio = render(synth, {noteOn(0, 69, 100)}, 49152);
        const double f0 = midiToHz(69);
        const double raie = magnitudeAt(audio, 24000, 16384, f0);
        const double entre = magnitudeAt(audio, 24000, 16384, f0 * 1.26);
        return entre / std::max(1e-9, raie);
    };
    const double net = mesurer(0.0f);
    const double nuage = mesurer(4.0f);
    VSM_ASSERT(nuage > net * 10.0);
}

VSM_TEST(granular_density_fills_the_gaps) {
    // À faible densité les grains sont ÉPARS : l'amplitude passe par des
    // creux entre deux grains. À forte densité le recouvrement lisse tout.
    // La variance de l'énergie par tranche de 10 ms doit chuter avec la
    // densité -- c'est l'horloge de grains qu'on entend, pas un volume.
    auto varianceDe = [&](float density) {
        auto synth = makeGranular();
        steady(*synth);
        set(*synth, "Grain Size", 20.0f);
        set(*synth, "Density", density);
        set(*synth, "Amp Sustain", 1.0f);
        const auto audio = render(synth, {noteOn(0, 69, 100)}, 49152);
        const size_t tranche = 480;   // 10 ms
        std::vector<double> energies;
        for (size_t debut = 24000; debut + tranche <= audio.size(); debut += tranche)
            energies.push_back(rmsOf(audio, debut, debut + tranche));
        double moyenne = 0.0;
        for (double e : energies) moyenne += e;
        moyenne /= static_cast<double>(energies.size());
        double variance = 0.0;
        for (double e : energies) variance += (e - moyenne) * (e - moyenne);
        variance /= static_cast<double>(energies.size());
        return variance / std::max(1e-12, moyenne * moyenne);   // normalisée
    };
    const double epars = varianceDe(8.0f);
    const double dense = varianceDe(80.0f);
    VSM_ASSERT(epars > dense * 4.0);
}

VSM_TEST(granular_is_deterministic_with_spray_on) {
    // Tout le nuage est seedé, et l'aléatoire repart à chaque note : deux
    // rendus du même projet sont identiques au bit près, dispersion comprise.
    auto premier = makeGranular();
    set(*premier, "Pitch Spray", 6.0f);
    set(*premier, "Time Spray", 0.8f);
    set(*premier, "Shimmer", 0.5f);
    const auto a = render(premier, {noteOn(0, 60, 100)}, 24064);
    auto second = makeGranular();
    set(*second, "Pitch Spray", 6.0f);
    set(*second, "Time Spray", 0.8f);
    set(*second, "Shimmer", 0.5f);
    const auto b = render(second, {noteOn(0, 60, 100)}, 24064);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT(a[i] == b[i]);
}

VSM_TEST(granular_save_load_roundtrip) {
    auto premier = makeGranular();
    set(*premier, "Density", 60.0f);
    set(*premier, "Pitch Spray", 3.5f);
    set(*premier, "Grain Size", 150.0f);
    const auto etat = premier->saveState();
    auto second = makeGranular();
    second->loadState(etat);
    for (const auto& info : premier->parameterList())
        VSM_ASSERT_NEAR(second->getParameter(info.id), premier->getParameter(info.id), 1e-6);
}

VSM_TEST(granular_parameter_list_size) {
    auto synth = makeGranular();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t(15));
}

VSM_TEST(granular_honours_pitch_bend_and_refuses_the_wheel) {
    auto synth = makeGranular();
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 2.0f;
    VSM_ASSERT(synth->handleControlEvent(bend));
    MidiControlEvent cc;
    cc.kind = MidiControlEvent::Kind::ControlChange;
    cc.index = 1;
    cc.value = 1.0f;
    VSM_ASSERT(!synth->handleControlEvent(cc));
}
