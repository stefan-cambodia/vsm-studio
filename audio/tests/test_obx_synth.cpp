#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

namespace {
SynthPluginPtr makeObx(double sr = 48000.0) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.obx");
    if (plugin) plugin->initialize(sr, 512);
    return plugin;
}
ParamId byName(const ISynthPlugin& plugin, const std::string& name) {
    for (const auto& info : plugin.parameterList()) if (info.name == name) return info.id;
    return 0;
}
MidiNoteEvent noteOn(int offset, uint8_t note, uint8_t velocity = 100) {
    return {MidiNoteEvent::Kind::NoteOn, offset, 0, note, velocity};
}
float peakAbs(const std::vector<float>& b) {
    float p = 0.0f; for (float v : b) p = std::max(p, std::abs(v)); return p;
}
double brightness(const std::vector<float>& b) {
    double energy = 0.0;
    for (size_t i = 1; i < b.size(); ++i) {
        const double d = static_cast<double>(b[i]) - b[i - 1];
        energy += d * d;
    }
    return energy;
}
std::vector<float> renderNote(SynthPluginPtr& synth, uint8_t note, uint8_t velocity = 100, int frames = 12000) {
    const auto event = noteOn(0, note, velocity);
    std::vector<float> left(static_cast<size_t>(frames), 0.0f), right(static_cast<size_t>(frames), 0.0f);
    synth->process(&event, 1, left.data(), right.data(), frames);
    return left;
}
} // namespace

VSM_TEST(obx_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.obx"));
}

VSM_TEST(obx_silent_with_no_events) {
    auto synth = makeObx();
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(obx_note_produces_sound) {
    auto synth = makeObx();
    const auto audio = renderNote(synth, 52);
    VSM_ASSERT(peakAbs(audio) > 0.02f);
    for (float v : audio) VSM_ASSERT(std::isfinite(v));
}

VSM_TEST(obx_two_pole_filter_passes_more_highs_than_four_pole) {
    // LA raison d'être de cette machine : une pente de 12 dB/oct laisse passer
    // les harmoniques hautes qu'un filtre à quatre pôles étouffe. Si ce test
    // échoue, la machine n'apporte plus rien au parc.
    auto twoPole = makeObx();
    auto fourPole = makeObx();
    for (auto* synth : {&twoPole, &fourPole}) {
        (*synth)->setParameter(byName(**synth, "Filter Cutoff"), 700.0f);
        (*synth)->setParameter(byName(**synth, "Filter Env Amount"), 0.0f);
        (*synth)->setParameter(byName(**synth, "Filter Key Track"), 0.0f);
        (*synth)->setParameter(byName(**synth, "Velocity to Filter"), 0.0f);
    }
    twoPole->setParameter(byName(*twoPole, "Filter Slope"), 0.0f);
    fourPole->setParameter(byName(*fourPole, "Filter Slope"), 1.0f);

    VSM_ASSERT(brightness(renderNote(twoPole, 40)) > brightness(renderNote(fourPole, 40)) * 1.5);
}

VSM_TEST(obx_resonance_changes_the_timbre) {
    auto flat = makeObx();
    auto resonant = makeObx();
    flat->setParameter(byName(*flat, "Filter Resonance"), 0.0f);
    resonant->setParameter(byName(*resonant, "Filter Resonance"), 0.9f);
    const auto a = renderNote(flat, 45), b = renderNote(resonant, 45);
    bool different = false;
    for (size_t i = 0; i < a.size(); ++i) if (std::abs(a[i] - b[i]) > 1e-4f) different = true;
    VSM_ASSERT(different);
}

VSM_TEST(obx_unison_stacks_every_voice_on_one_note) {
    // Sur ces machines, l'unisson est un MODE DE JEU, pas un effet : toutes
    // les voix jouent la même note, légèrement désaccordées.
    auto synth = makeObx();
    synth->setParameter(byName(*synth, "Unison"), 1.0f);
    const auto event = noteOn(0, 45, 110);
    std::vector<float> left(2000, 0.0f), right(2000, 0.0f);
    synth->process(&event, 1, left.data(), right.data(), 2000);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 8);
}

VSM_TEST(obx_unison_is_not_eight_times_louder) {
    // Empiler huit voix sans compenser ferait sursauter l'utilisateur au
    // moment où il active l'unisson.
    auto poly = makeObx();
    auto unison = makeObx();
    unison->setParameter(byName(*unison, "Unison"), 1.0f);
    const float polyPeak = peakAbs(renderNote(poly, 45, 110));
    const float unisonPeak = peakAbs(renderNote(unison, 45, 110));
    VSM_ASSERT(unisonPeak < polyPeak * 3.0f);
    VSM_ASSERT(unisonPeak > polyPeak * 0.5f);
}

VSM_TEST(obx_unison_detune_is_symmetric_around_the_note) {
    // Un désaccord asymétrique ferait MONTER la note perçue quand on ouvre le
    // réglage : l'accord du morceau partirait avec.
    //
    // La hauteur est mesurée par AUTOCORRÉLATION, pas par comptage de passages
    // par zéro : huit voix désaccordées battent entre elles, ce qui crée des
    // passages par zéro supplémentaires sans que la hauteur bouge. Une
    // première version de ce test s'y est laissé prendre.
    auto measurePeriod = [](const std::vector<float>& signal, int minLag, int maxLag) {
        double best = -1.0;
        int bestLag = minLag;
        for (int lag = minLag; lag <= maxLag; ++lag) {
            double correlation = 0.0;
            for (size_t i = static_cast<size_t>(lag); i < signal.size(); ++i)
                correlation += static_cast<double>(signal[i]) * signal[i - static_cast<size_t>(lag)];
            if (correlation > best) { best = correlation; bestLag = lag; }
        }
        return bestLag;
    };

    auto narrow = makeObx();
    auto wide = makeObx();
    for (auto* synth : {&narrow, &wide}) {
        (*synth)->setParameter(byName(**synth, "Unison"), 1.0f);
        (*synth)->setParameter(byName(**synth, "Analog Character"), 0.0f); // isole le désaccord
    }
    narrow->setParameter(byName(*narrow, "Unison Detune"), 0.0f);
    wide->setParameter(byName(*wide, "Unison Detune"), 1.0f);

    // Note 45 = 110 Hz, soit ~436 échantillons à 48 kHz.
    const int narrowPeriod = measurePeriod(renderNote(narrow, 45, 110, 24000), 300, 600);
    const int widePeriod = measurePeriod(renderNote(wide, 45, 110, 24000), 300, 600);
    const double drift = std::abs(static_cast<double>(widePeriod - narrowPeriod)) / narrowPeriod;
    VSM_ASSERT(drift < 0.05); // moins d'un demi-ton de dérive
}

VSM_TEST(obx_is_polyphonic_and_steals) {
    auto synth = makeObx();
    std::vector<MidiNoteEvent> chord;
    for (int i = 0; i < 8; ++i) chord.push_back(noteOn(0, static_cast<uint8_t>(48 + 3 * i)));
    std::vector<float> left(3000, 0.0f), right(3000, 0.0f);
    synth->process(chord.data(), 8, left.data(), right.data(), 3000);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 8);

    auto stealing = makeObx();
    std::vector<MidiNoteEvent> many;
    for (int i = 0; i < 11; ++i) many.push_back(noteOn(i * 10, static_cast<uint8_t>(40 + 2 * i)));
    std::vector<float> l2(3000, 0.0f), r2(3000, 0.0f);
    stealing->process(many.data(), 11, l2.data(), r2.data(), 3000);
    VSM_ASSERT_EQ(stealing->activeVoiceCount(), 8); // jamais plus que ses voix
}

VSM_TEST(obx_sync_changes_timbre) {
    auto plain = makeObx();
    auto synced = makeObx();
    synced->setParameter(byName(*synced, "Sync"), 1.0f);
    synced->setParameter(byName(*synced, "Osc2 Detune"), 7.0f);
    plain->setParameter(byName(*plain, "Osc2 Detune"), 7.0f);
    const auto a = renderNote(plain, 45), b = renderNote(synced, 45);
    bool different = false;
    for (size_t i = 0; i < a.size(); ++i) if (std::abs(a[i] - b[i]) > 1e-3f) different = true;
    VSM_ASSERT(different);
}

VSM_TEST(obx_stays_bounded_at_maximum_resonance) {
    auto synth = makeObx();
    synth->setParameter(byName(*synth, "Filter Resonance"), 1.0f);
    synth->setParameter(byName(*synth, "Filter Cutoff"), 400.0f);
    const auto audio = renderNote(synth, 36, 127, 48000);
    for (float v : audio) VSM_ASSERT(std::isfinite(v));
    VSM_ASSERT(peakAbs(audio) < 8.0f);
}

VSM_TEST(obx_is_deterministic) {
    auto render = [] {
        auto synth = makeObx();
        synth->setParameter(byName(*synth, "Analog Character"), 0.9f);
        return renderNote(synth, 50, 100, 8000);
    };
    const auto a = render(), b = render();
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], b[i], 1e-9);
}

VSM_TEST(obx_save_load_roundtrip) {
    auto source = makeObx();
    source->setParameter(byName(*source, "Filter Cutoff"), 3300.0f);
    source->setParameter(byName(*source, "Unison Detune"), 0.7f);
    const auto state = source->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.obx"));

    auto target = makeObx();
    target->loadState(state);
    VSM_ASSERT_NEAR(target->getParameter(byName(*target, "Filter Cutoff")), 3300.0f, 1e-3);
    VSM_ASSERT_NEAR(target->getParameter(byName(*target, "Unison Detune")), 0.7f, 1e-6);
}

VSM_TEST(obx_parameter_list_size) {
    auto synth = makeObx();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t{30});
}

VSM_TEST(obx_stays_within_headroom_on_a_full_chord) {
    // Huit voix simultanées à forte vélocité : le cas où un facteur de niveau
    // trop généreux se traduit par de l'écrêtage AVANT même le mixer. Ce test
    // a été ajouté après avoir mesuré une crête à 1.17 sur cet accord.
    auto synth = makeObx();
    MidiNoteEvent events[8]{};
    const uint8_t notes[8] = {48, 52, 55, 59, 60, 64, 67, 71};
    for (int i = 0; i < 8; ++i) events[i] = noteOn(0, notes[i], 110);
    std::vector<float> left(24000, 0.0f), right(24000, 0.0f);
    synth->process(events, 8, left.data(), right.data(), 24000);
    VSM_ASSERT(peakAbs(left) < 1.0f);
    VSM_ASSERT(peakAbs(left) > 0.1f); // ...sans tomber dans l'inaudible
}
