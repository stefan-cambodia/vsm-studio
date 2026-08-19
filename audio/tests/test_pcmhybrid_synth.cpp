#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/ISampleLoader.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include "../plugins/pcmhybrid/PcmHybridSynth.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;
using vsm::audio::io::SampleBuffer;

namespace {

constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeHybrid() {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.pcmhybrid");
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

} // namespace

VSM_TEST(pcmhybrid_is_registered) {
    auto synth = makeHybrid();
    VSM_ASSERT(synth != nullptr);
    VSM_ASSERT_EQ(std::string(synth->machineName()), std::string("PCM + Synth Hybrid"));
    VSM_ASSERT(synth->parameterList().size() >= 25);
}

VSM_TEST(pcmhybrid_produces_sound) {
    auto synth = makeHybrid();
    VSM_ASSERT(peakAbs(renderNote(synth, 60)) > 0.03f);
}

VSM_TEST(pcmhybrid_stays_within_headroom_on_a_full_chord) {
    auto synth = makeHybrid();
    MidiNoteEvent events[8]{};
    const uint8_t notes[8] = {48, 52, 55, 59, 60, 64, 67, 71};
    for (int i = 0; i < 8; ++i)
        events[i] = MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, 0, 0, notes[i], 110};
    std::vector<float> left(24000, 0.0f), right(24000, 0.0f);
    synth->process(events, 8, left.data(), right.data(), 24000);
    VSM_ASSERT(peakAbs(left) < 1.0f);
    VSM_ASSERT(peakAbs(left) > 0.1f);
}

VSM_TEST(pcmhybrid_attack_layer_is_loudest_at_the_very_start) {
    // Le trait de la famille : les premières millisecondes contiennent un
    // événement percussif que la couche entretenue ne peut pas produire.
    auto synth = makeHybrid();
    synth->setParameter(byName(*synth, "Attack Level"), 1.0f);
    synth->setParameter(byName(*synth, "Tone Level"), 0.35f);
    synth->setParameter(byName(*synth, "Amp Sustain"), 1.0f);
    synth->setParameter(byName(*synth, "Amp Decay"), 8.0f);
    synth->setParameter(byName(*synth, "Filter Env Amount"), 0.0f);
    const auto buffer = renderNote(synth, 60, 40000);
    // Fenêtre de 10 ms, ajustée à la durée réelle du transitoire : mesurer
    // sur 30 ms diluerait l'attaque dans le son tenu qui la suit et
    // rendrait le test insensible à ce qu'il prétend vérifier.
    VSM_ASSERT(rms(buffer, 0, 480) > rms(buffer, 30000, 40000) * 1.5);
}

VSM_TEST(pcmhybrid_attack_restarts_on_every_note) {
    // Une attaque qui ne repart pas donnerait des notes sans attaque dès la
    // deuxième -- le défaut le plus audible qu'une machine de ce type puisse
    // avoir.
    auto synth = makeHybrid();
    synth->setParameter(byName(*synth, "Attack Level"), 1.0f);
    synth->setParameter(byName(*synth, "Tone Level"), 0.0f); // attaque seule
    std::vector<float> left(48000, 0.0f), right(48000, 0.0f);
    MidiNoteEvent events[4]{};
    events[0] = MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, 0, 0, 60, 100};
    events[1] = MidiNoteEvent{MidiNoteEvent::Kind::NoteOff, 20000, 0, 60, 0};
    events[2] = MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, 24000, 0, 60, 100};
    events[3] = MidiNoteEvent{MidiNoteEvent::Kind::NoteOff, 44000, 0, 60, 0};
    synth->process(events, 4, left.data(), right.data(), 48000);

    const float firstAttack = peakAbs(left, 0, 3000);
    const float secondAttack = peakAbs(left, 24000, 27000);
    VSM_ASSERT(secondAttack > firstAttack * 0.7f); // la seconde attaque existe bel et bien
}

VSM_TEST(pcmhybrid_all_five_attacks_are_reachable_and_distinct) {
    std::vector<std::vector<float>> renders;
    for (int index = 0; index < 5; ++index) {
        auto synth = makeHybrid();
        synth->setParameter(byName(*synth, "Attack Sample"), static_cast<float>(index));
        synth->setParameter(byName(*synth, "Attack Level"), 1.0f);
        synth->setParameter(byName(*synth, "Tone Level"), 0.0f);
        synth->setParameter(byName(*synth, "Attack Tone"), 1.0f);
        synth->setParameter(byName(*synth, "Filter Cutoff"), 18000.0f);
        synth->setParameter(byName(*synth, "Filter Env Amount"), 0.0f);
        auto buffer = renderNote(synth, 60, 12000);
        VSM_ASSERT(peakAbs(buffer) > 0.01f);
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
            VSM_ASSERT(std::abs(correlation) / std::sqrt(std::max(1e-12, energyA * energyB)) < 0.9);
        }
    }
}

VSM_TEST(pcmhybrid_attack_follows_the_keyboard) {
    // Contrairement au sampler du parc, où la touche SÉLECTIONNE un
    // emplacement, l'attaque est ici mélodique : elle doit monter avec le
    // clavier, sinon elle se décolle du corps du son dès qu'on change
    // d'octave. Une octave plus haut = attaque deux fois plus courte.
    auto low = makeHybrid();
    auto high = makeHybrid();
    for (auto* synth : {&low, &high}) {
        (*synth)->setParameter(byName(**synth, "Attack Level"), 1.0f);
        (*synth)->setParameter(byName(**synth, "Tone Level"), 0.0f);
        (*synth)->setParameter(byName(**synth, "Attack Decay"), 2.0f); // laisse jouer l'échantillon
        (*synth)->setParameter(byName(**synth, "Attack Sample"), 1.0f); // PLUCK, bien tonal
    }
    auto duration = [](const std::vector<float>& buffer) {
        const float threshold = peakAbs(buffer) * 0.05f;
        size_t last = 0;
        for (size_t i = 0; i < buffer.size(); ++i)
            if (std::abs(buffer[i]) > threshold) last = i;
        return last;
    };
    const size_t lowDuration = duration(renderNote(low, 48, 40000));
    const size_t highDuration = duration(renderNote(high, 60, 40000));
    VSM_ASSERT(highDuration < lowDuration); // lue plus vite, donc plus courte
}

VSM_TEST(pcmhybrid_ring_modulation_changes_the_spectrum) {
    // La modulation en anneau ne doit pas être un simple changement de
    // niveau : elle produit des fréquences qui n'existent dans AUCUNE des
    // deux couches. On le vérifie par la corrélation entre les deux rendus.
    auto parallel = makeHybrid();
    auto ring = makeHybrid();
    for (auto* synth : {&parallel, &ring}) {
        (*synth)->setParameter(byName(**synth, "Attack Sample"), 4.0f); // BELL, bien inharmonique
        (*synth)->setParameter(byName(**synth, "Attack Decay"), 1.5f);
        (*synth)->setParameter(byName(**synth, "Analog Character"), 0.0f);
    }
    ring->setParameter(byName(*ring, "Structure"), 1.0f);

    const auto a = renderNote(parallel, 60, 16000);
    const auto b = renderNote(ring, 60, 16000);
    double correlation = 0.0, energyA = 0.0, energyB = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        correlation += static_cast<double>(a[i]) * b[i];
        energyA += static_cast<double>(a[i]) * a[i];
        energyB += static_cast<double>(b[i]) * b[i];
    }
    VSM_ASSERT(std::abs(correlation) / std::sqrt(std::max(1e-12, energyA * energyB)) < 0.7);
    VSM_ASSERT(peakAbs(b) > 0.01f); // ...et ça sonne toujours
}

VSM_TEST(pcmhybrid_velocity_acts_first_on_the_attack) {
    // Sur l'instrument imité, jouer plus fort change surtout le bruit du
    // contact -- pas seulement le volume général.
    auto synth = makeHybrid();
    synth->setParameter(byName(*synth, "Velocity to Attack"), 1.0f);
    synth->setParameter(byName(*synth, "Velocity to Filter"), 0.0f);
    synth->setParameter(byName(*synth, "Tone Level"), 0.7f);
    synth->setParameter(byName(*synth, "Attack Level"), 1.0f);
    synth->setParameter(byName(*synth, "Amp Sustain"), 1.0f);
    synth->setParameter(byName(*synth, "Amp Decay"), 8.0f);

    const auto loud = renderNote(synth, 60, 40000, 127);
    const auto soft = renderNote(synth, 60, 40000, 20);
    // L'attaque change beaucoup... (fenêtre de 10 ms, cf. plus haut)
    const double attackRatio = rms(loud, 0, 480) / std::max(1e-9, rms(soft, 0, 480));
    // ...le régime établi, beaucoup moins.
    const double steadyRatio = rms(loud, 30000, 40000) / std::max(1e-9, rms(soft, 30000, 40000));
    VSM_ASSERT(attackRatio > steadyRatio * 1.5);
}

VSM_TEST(pcmhybrid_loaded_sample_replaces_the_generated_attack) {
    // Le point qui rend cette machine utile à la reconstruction : la chaîne
    // d'analyse dépose l'attaque réelle du son à reproduire, et c'est ELLE
    // qu'on entend, pas le transitoire engendré.
    auto synth = makeHybrid();
    auto* loader = dynamic_cast<ISampleLoader*>(synth.get());
    VSM_ASSERT(loader != nullptr);
    VSM_ASSERT_EQ(loader->slotCount(), 1);

    const auto generated = renderNote(synth, 60, 12000);

    // Une attaque reconnaissable : une salve à 1 kHz, que rien dans la banque
    // engendrée ne produit.
    auto sample = std::make_shared<SampleBuffer>();
    sample->sampleRate = kSampleRate;
    sample->sourcePath = "attaque-de-test";
    sample->left.resize(4800);
    for (size_t i = 0; i < sample->left.size(); ++i)
        sample->left[i] = 0.8f * std::sin(2.0f * 3.14159265f * 1000.0f
                                          * static_cast<float>(i) / static_cast<float>(kSampleRate));

    auto* hybrid = dynamic_cast<ISynthPlugin*>(synth.get());
    VSM_ASSERT(hybrid != nullptr);
    // Passage par le chemin « déjà en mémoire », sans fichier.
    static_cast<vsm::plugins::pcmhybrid::PcmHybridSynth*>(synth.get())->setAttackSample(sample);

    const auto loaded = renderNote(synth, 60, 12000);
    double correlation = 0.0, energyA = 0.0, energyB = 0.0;
    for (size_t i = 0; i < generated.size(); ++i) {
        correlation += static_cast<double>(generated[i]) * loaded[i];
        energyA += static_cast<double>(generated[i]) * generated[i];
        energyB += static_cast<double>(loaded[i]) * loaded[i];
    }
    VSM_ASSERT(std::abs(correlation) / std::sqrt(std::max(1e-12, energyA * energyB)) < 0.8);
    VSM_ASSERT_EQ(loader->samplePath(0), std::string("attaque-de-test"));

    // ...et retirer l'échantillon rend la banque engendrée.
    loader->clearSample(0);
    VSM_ASSERT_EQ(loader->samplePath(0), std::string());
}

VSM_TEST(pcmhybrid_missing_file_is_reported_not_substituted) {
    auto synth = makeHybrid();
    auto* loader = dynamic_cast<ISampleLoader*>(synth.get());
    std::string error;
    VSM_ASSERT(!loader->loadSample(0, "/ce/fichier/n/existe/pas.wav", error));
    VSM_ASSERT(!error.empty());
    // Emplacement inexistant : refusé aussi, avec un message.
    error.clear();
    VSM_ASSERT(!loader->loadSample(3, "/peu/importe.wav", error));
    VSM_ASSERT(!error.empty());
}

VSM_TEST(pcmhybrid_is_deterministic) {
    auto first = makeHybrid();
    auto second = makeHybrid();
    const auto a = renderNote(first, 62, 16000);
    const auto b = renderNote(second, 62, 16000);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], b[i], 1e-9);
}

VSM_TEST(pcmhybrid_state_round_trips) {
    auto synth = makeHybrid();
    synth->setParameter(byName(*synth, "Attack Sample"), 3.0f);
    synth->setParameter(byName(*synth, "Structure"), 1.0f);
    const auto state = synth->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.pcmhybrid"));
    auto restored = makeHybrid();
    restored->loadState(state);
    VSM_ASSERT_NEAR(restored->getParameter(byName(*restored, "Attack Sample")), 3.0f, 1e-6);
    VSM_ASSERT_NEAR(restored->getParameter(byName(*restored, "Structure")), 1.0f, 1e-6);
}
