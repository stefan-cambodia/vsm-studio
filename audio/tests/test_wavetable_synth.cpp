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

SynthPluginPtr makeWavetable() {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.wavetable");
    if (plugin) plugin->initialize(kSampleRate, 512);
    return plugin;
}

ParamId byName(const ISynthPlugin& plugin, const std::string& name) {
    for (const auto& info : plugin.parameterList())
        if (info.name == name) return info.id;
    return 0;
}

std::vector<float> renderNote(const SynthPluginPtr& synth, uint8_t note,
                               int frames = 24000, uint8_t velocity = 100) {
    const MidiNoteEvent event{MidiNoteEvent::Kind::NoteOn, 0, 0, note, velocity};
    std::vector<float> left(static_cast<size_t>(frames), 0.0f), right(static_cast<size_t>(frames), 0.0f);
    synth->process(&event, 1, left.data(), right.data(), frames);
    return left;
}

float peakAbs(const std::vector<float>& buffer) {
    float peak = 0.0f;
    for (float sample : buffer) peak = std::max(peak, std::abs(sample));
    return peak;
}

double rms(const std::vector<float>& buffer, size_t from = 0, size_t to = 0) {
    const size_t last = to > 0 ? std::min(to, buffer.size()) : buffer.size();
    double sum = 0.0;
    for (size_t i = from; i < last; ++i) sum += static_cast<double>(buffer[i]) * buffer[i];
    return last > from ? std::sqrt(sum / static_cast<double>(last - from)) : 0.0;
}

/// Énergie haute fréquence, mesurée par les différences premières. On ne
/// mesure PAS l'énergie totale : la résonance du filtre en ajoute, ce qui
/// rendrait la mesure trompeuse (leçon retenue d'un test précédent).
double brightness(const std::vector<float>& buffer, size_t from = 0, size_t to = 0) {
    const size_t last = to > 0 ? std::min(to, buffer.size()) : buffer.size();
    double sum = 0.0;
    for (size_t i = from + 1; i < last; ++i) {
        const double difference = static_cast<double>(buffer[i]) - buffer[i - 1];
        sum += difference * difference;
    }
    return sum;
}

} // namespace

VSM_TEST(wavetable_synth_is_registered) {
    auto synth = makeWavetable();
    VSM_ASSERT(synth != nullptr);
    VSM_ASSERT_EQ(std::string(synth->machineName()), std::string("Wavetable Synth"));
    VSM_ASSERT(synth->parameterList().size() >= 25);
}

VSM_TEST(wavetable_synth_produces_sound) {
    auto synth = makeWavetable();
    VSM_ASSERT(peakAbs(renderNote(synth, 60)) > 0.05f);
}

VSM_TEST(wavetable_synth_stays_within_headroom_on_a_full_chord) {
    auto synth = makeWavetable();
    std::vector<float> left(24000, 0.0f), right(24000, 0.0f);
    MidiNoteEvent events[8]{};
    const uint8_t notes[8] = {48, 52, 55, 59, 60, 64, 67, 71};
    for (int i = 0; i < 8; ++i)
        events[i] = MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, 0, 0, notes[i], 110};
    synth->process(events, 8, left.data(), right.data(), 24000);
    VSM_ASSERT(peakAbs(left) < 1.0f);
    VSM_ASSERT(peakAbs(left) > 0.1f);
}

VSM_TEST(wavetable_synth_position_changes_the_timbre_without_the_filter) {
    // LE trait de la famille : le timbre change alors que le filtre ne bouge
    // pas. Sur toute autre machine du parc, filtre grand ouvert et enveloppes
    // neutralisées, il ne resterait qu'un seul son possible.
    auto low = makeWavetable();
    auto high = makeWavetable();
    for (auto* synth : {&low, &high}) {
        (*synth)->setParameter(byName(**synth, "Filter Cutoff"), 18000.0f);
        (*synth)->setParameter(byName(**synth, "Filter Env Amount"), 0.0f);
        (*synth)->setParameter(byName(**synth, "Wave Env Amount"), 0.0f);
        (*synth)->setParameter(byName(**synth, "Osc B Level"), 0.0f);
        (*synth)->setParameter(byName(**synth, "Analog Character"), 0.0f);
    }
    low->setParameter(byName(*low, "Position"), 0.0f);
    high->setParameter(byName(*high, "Position"), 1.0f);
    VSM_ASSERT(brightness(renderNote(high, 57)) > brightness(renderNote(low, 57)) * 3.0);
}

VSM_TEST(wavetable_synth_wave_envelope_moves_the_timbre_during_the_note) {
    // L'enveloppe de table doit faire ÉVOLUER le son dans le temps, filtre
    // fixe. C'est ce qu'aucune machine soustractive du parc ne sait faire à
    // filtre constant.
    auto synth = makeWavetable();
    synth->setParameter(byName(*synth, "Position"), 0.0f);
    synth->setParameter(byName(*synth, "Wave Env Amount"), 1.0f);
    synth->setParameter(byName(*synth, "Wave Attack"), 0.25f);
    synth->setParameter(byName(*synth, "Wave Sustain"), 1.0f);
    synth->setParameter(byName(*synth, "Filter Cutoff"), 18000.0f);
    synth->setParameter(byName(*synth, "Filter Env Amount"), 0.0f);
    synth->setParameter(byName(*synth, "Amp Attack"), 0.001f);
    synth->setParameter(byName(*synth, "Amp Sustain"), 1.0f);
    synth->setParameter(byName(*synth, "Amp Decay"), 8.0f);
    synth->setParameter(byName(*synth, "Osc B Level"), 0.0f);
    synth->setParameter(byName(*synth, "Analog Character"), 0.0f);

    const auto buffer = renderNote(synth, 57, 40000);
    // Brillance rapportée au niveau : sinon on mesurerait l'enveloppe de
    // volume plutôt que le mouvement de timbre.
    const double early = brightness(buffer, 0, 4000) / std::max(1e-12, rms(buffer, 0, 4000));
    const double late = brightness(buffer, 30000, 34000) / std::max(1e-12, rms(buffer, 30000, 34000));
    VSM_ASSERT(late > early * 2.0);
}

VSM_TEST(wavetable_synth_all_four_tables_are_reachable_and_distinct) {
    std::vector<std::vector<float>> renders;
    for (int table = 0; table < 4; ++table) {
        auto synth = makeWavetable();
        synth->setParameter(byName(*synth, "Wavetable"), static_cast<float>(table));
        synth->setParameter(byName(*synth, "Wave Env Amount"), 0.0f);
        synth->setParameter(byName(*synth, "Position"), 0.6f);
        synth->setParameter(byName(*synth, "Filter Cutoff"), 18000.0f);
        synth->setParameter(byName(*synth, "Filter Env Amount"), 0.0f);
        synth->setParameter(byName(*synth, "Analog Character"), 0.0f);
        auto buffer = renderNote(synth, 57, 12000);
        VSM_ASSERT(peakAbs(buffer) > 0.02f); // chaque table sonne
        renders.push_back(std::move(buffer));
    }
    for (size_t a = 0; a < renders.size(); ++a) {
        for (size_t b = a + 1; b < renders.size(); ++b) {
            double correlation = 0.0, energyA = 0.0, energyB = 0.0;
            for (size_t i = 0; i < renders[a].size(); ++i) {
                correlation += static_cast<double>(renders[a][i]) * renders[b][i];
                energyA += static_cast<double>(renders[a][i]) * renders[a][i];
                energyB += static_cast<double>(renders[b][i]) * renders[b][i];
            }
            VSM_ASSERT(std::abs(correlation) / std::sqrt(std::max(1e-12, energyA * energyB)) < 0.95);
        }
    }
}

VSM_TEST(wavetable_synth_second_oscillator_thickens_without_doubling_level) {
    // Le second oscillateur doit ÉPAISSIR, pas doubler le volume : le niveau
    // est compensé, sinon chaque tour du réglage serait un tour de volume.
    auto single = makeWavetable();
    auto doubled = makeWavetable();
    for (auto* synth : {&single, &doubled}) (*synth)->setParameter(byName(**synth, "Wave Env Amount"), 0.0f);
    single->setParameter(byName(*single, "Osc B Level"), 0.0f);
    doubled->setParameter(byName(*doubled, "Osc B Level"), 1.0f);
    const double ratio = rms(renderNote(doubled, 57)) / std::max(1e-9, rms(renderNote(single, 57)));
    VSM_ASSERT(ratio > 0.55 && ratio < 1.45);
}

VSM_TEST(wavetable_synth_high_notes_do_not_alias) {
    // Vérifié à nouveau AU NIVEAU DE LA MACHINE, pas seulement de la brique :
    // c'est ici que le mauvais paramètre de limite passerait inaperçu.
    auto synth = makeWavetable();
    synth->setParameter(byName(*synth, "Position"), 1.0f);
    synth->setParameter(byName(*synth, "Wave Env Amount"), 0.0f);
    synth->setParameter(byName(*synth, "Filter Cutoff"), 18000.0f);
    synth->setParameter(byName(*synth, "Filter Env Amount"), 0.0f);
    synth->setParameter(byName(*synth, "Osc B Level"), 0.0f);
    synth->setParameter(byName(*synth, "Analog Character"), 0.0f);

    const auto buffer = renderNote(synth, 105, 16000); // La7, 3520 Hz
    const double fundamental = 3520.0;
    double harmonic = 0.0, inharmonic = 0.0;
    for (double hz = 60.0; hz < 23000.0; hz += 25.0) {
        double real = 0.0, imaginary = 0.0;
        for (size_t i = 0; i < buffer.size(); ++i) {
            const double angle = 2.0 * 3.14159265358979323846 * hz * static_cast<double>(i) / kSampleRate;
            real += static_cast<double>(buffer[i]) * std::cos(angle);
            imaginary += static_cast<double>(buffer[i]) * std::sin(angle);
        }
        const double power = (real * real + imaginary * imaginary);
        const double ratio = hz / fundamental;
        if (std::abs(ratio - std::round(ratio)) < 0.12 && std::round(ratio) >= 1.0) harmonic += power;
        else inharmonic += power;
    }
    VSM_ASSERT(inharmonic < harmonic * 0.1);
}

VSM_TEST(wavetable_synth_velocity_opens_the_filter) {
    auto synth = makeWavetable();
    synth->setParameter(byName(*synth, "Velocity to Filter"), 1.0f);
    synth->setParameter(byName(*synth, "Filter Cutoff"), 1200.0f);
    synth->setParameter(byName(*synth, "Filter Env Amount"), 0.0f);
    auto soft = makeWavetable();
    soft->setParameter(byName(*soft, "Velocity to Filter"), 1.0f);
    soft->setParameter(byName(*soft, "Filter Cutoff"), 1200.0f);
    soft->setParameter(byName(*soft, "Filter Env Amount"), 0.0f);
    VSM_ASSERT(brightness(renderNote(synth, 60, 16000, 127)) >
               brightness(renderNote(soft, 60, 16000, 20)) * 1.5);
}

VSM_TEST(wavetable_synth_is_deterministic) {
    auto first = makeWavetable();
    auto second = makeWavetable();
    const auto a = renderNote(first, 62, 16000);
    const auto b = renderNote(second, 62, 16000);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], b[i], 1e-9);
}

VSM_TEST(wavetable_synth_state_round_trips) {
    auto synth = makeWavetable();
    synth->setParameter(byName(*synth, "Wavetable"), 3.0f);
    synth->setParameter(byName(*synth, "Position"), 0.42f);
    const auto state = synth->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.wavetable"));
    auto restored = makeWavetable();
    restored->loadState(state);
    VSM_ASSERT_NEAR(restored->getParameter(byName(*restored, "Wavetable")), 3.0f, 1e-6);
    VSM_ASSERT_NEAR(restored->getParameter(byName(*restored, "Position")), 0.42f, 1e-6);
}

VSM_TEST(wavetable_synth_position_stays_inside_the_table) {
    // Réglage, enveloppe et LFO poussent TOUS la position vers le haut : le
    // total doit buter, jamais se replier. Un repliement s'entendrait comme un
    // saut de timbre en plein mouvement.
    auto synth = makeWavetable();
    synth->setParameter(byName(*synth, "Position"), 1.0f);
    synth->setParameter(byName(*synth, "Wave Env Amount"), 1.0f);
    synth->setParameter(byName(*synth, "LFO to Position"), 1.0f);
    synth->setParameter(byName(*synth, "LFO Rate"), 8.0f);
    synth->setParameter(byName(*synth, "Amp Sustain"), 1.0f);
    synth->setParameter(byName(*synth, "Amp Decay"), 8.0f);

    const auto buffer = renderNote(synth, 57, 40000);
    // Aucun saut brutal d'un échantillon à l'autre : un repliement de position
    // ferait passer d'une forme à son opposé en un échantillon.
    float largestJump = 0.0f;
    for (size_t i = 1; i < buffer.size(); ++i)
        largestJump = std::max(largestJump, std::abs(buffer[i] - buffer[i - 1]));
    VSM_ASSERT(largestJump < peakAbs(buffer) * 0.9f);
}
