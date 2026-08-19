#include "TestFramework.h"
#include "vsm/audio/io/WavFileWriter.h"
#include "vsm/audio/plugin/ISampleLoader.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include "../plugins/sampler/SamplerSynth.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

using namespace vsm::audio::plugin;
using vsm::plugins::sampler::SamplerSynth;
using vsm::audio::io::SampleBuffer;

namespace {

std::shared_ptr<SamplerSynth> makeSampler(double sr = 48000.0) {
    auto sampler = std::make_shared<SamplerSynth>();
    sampler->initialize(sr, 512);
    return sampler;
}

/// Échantillon synthétique reconnaissable : une rampe descendante, pour qu'on
/// puisse vérifier la POSITION de lecture et pas seulement la présence de son.
std::shared_ptr<const SampleBuffer> makeRampSample(size_t frames = 1000, double rate = 48000.0) {
    auto buffer = std::make_shared<SampleBuffer>();
    buffer->sampleRate = rate;
    buffer->left.resize(frames);
    for (size_t i = 0; i < frames; ++i)
        buffer->left[i] = 1.0f - static_cast<float>(i) / static_cast<float>(frames);
    return buffer;
}

MidiNoteEvent noteOn(int offset, uint8_t note, uint8_t velocity = 100) {
    return {MidiNoteEvent::Kind::NoteOn, offset, 0, note, velocity};
}

float peakAbs(const std::vector<float>& buffer) {
    float peak = 0.0f;
    for (float value : buffer) peak = std::max(peak, std::abs(value));
    return peak;
}

std::string writeTempWav(const std::string& name, size_t frames, double rate) {
    std::vector<float> data(frames);
    for (size_t i = 0; i < frames; ++i)
        data[i] = static_cast<float>(std::sin(0.05 * static_cast<double>(i))) * 0.7f;
    const auto path = (std::filesystem::temp_directory_path() / name).string();
    vsm::audio::io::WavFileWriter::writeFile(data.data(), nullptr, frames, rate,
                                              vsm::audio::io::SampleFormat::Float32, path);
    return path;
}

} // namespace

VSM_TEST(sampler_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.sampler"));
}

VSM_TEST(sampler_silent_with_no_events) {
    auto sampler = makeSampler();
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    sampler->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(sampler->activeVoiceCount(), 0);
}

VSM_TEST(sampler_empty_slot_stays_silent) {
    // Un emplacement vide ne joue RIEN -- pas un son de repli, pas un bip.
    auto sampler = makeSampler();
    const auto event = noteOn(0, 36);
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    sampler->process(&event, 1, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(sampler->activeVoiceCount(), 0);
}

VSM_TEST(sampler_plays_the_sample_of_the_triggered_slot) {
    auto sampler = makeSampler();
    sampler->setSample(0, makeRampSample());
    const auto event = noteOn(0, 36); // note par défaut de l'emplacement 0

    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    sampler->process(&event, 1, left.data(), right.data(), 512);
    VSM_ASSERT(peakAbs(left) > 0.5f);
    // La rampe descend : le début doit être plus fort que la fin.
    VSM_ASSERT(std::abs(left[0]) > std::abs(left[500]));
    for (float value : left) VSM_ASSERT(std::isfinite(value));
}

VSM_TEST(sampler_note_selects_the_slot_and_does_not_transpose) {
    // Convention de boîte à rythmes : la note CHOISIT le son, elle ne le
    // transpose pas. Transposer un coup de caisse claire selon la touche
    // produirait n'importe quoi.
    auto sampler = makeSampler();
    sampler->setSample(0, makeRampSample());
    sampler->setSample(1, makeRampSample());

    auto render = [&](uint8_t note) {
        auto fresh = makeSampler();
        fresh->setSample(0, makeRampSample());
        const auto event = noteOn(0, note);
        std::vector<float> left(400, 0.0f), right(400, 0.0f);
        fresh->process(&event, 1, left.data(), right.data(), 400);
        return left;
    };

    const auto atDefaultNote = render(36);
    const auto atOtherNote = render(38);  // note de l'emplacement 1, vide ici
    VSM_ASSERT(peakAbs(atDefaultNote) > 0.5f);
    VSM_ASSERT_NEAR(peakAbs(atOtherNote), 0.0, 1e-9); // l'emplacement 1 n'a pas d'échantillon
}

VSM_TEST(sampler_tune_changes_playback_speed) {
    auto sampler = makeSampler();
    sampler->setSample(0, makeRampSample(1000));
    // Une octave au-dessus : l'échantillon est lu deux fois plus vite, donc
    // épuisé deux fois plus tôt.
    sampler->setParameter(SamplerSynth::slotParam(0, SamplerSynth::kSlotTune), 12.0f);

    const auto event = noteOn(0, 36);
    std::vector<float> left(1000, 0.0f), right(1000, 0.0f);
    sampler->process(&event, 1, left.data(), right.data(), 1000);
    VSM_ASSERT(peakAbs(left) > 0.4f);
    // Après 500 échantillons de sortie, on a consommé les 1000 du fichier.
    VSM_ASSERT_EQ(sampler->activeVoiceCount(), 0);
}

VSM_TEST(sampler_compensates_the_file_sample_rate) {
    // Un échantillon 44,1 kHz joué sur un moteur à 48 kHz doit garder sa
    // hauteur : sans compensation, il sonnerait un demi-ton trop grave.
    auto sampler = makeSampler(48000.0);
    sampler->setSample(0, makeRampSample(4800, 44100.0));

    const auto event = noteOn(0, 36);
    std::vector<float> left(6000, 0.0f), right(6000, 0.0f);
    sampler->process(&event, 1, left.data(), right.data(), 6000);

    // 4800 trames à 44,1 kHz durent 108,8 ms, soit ~5224 trames à 48 kHz.
    // La voix doit donc s'être arrêtée entre 5000 et 5400 échantillons.
    size_t lastNonZero = 0;
    for (size_t i = 0; i < left.size(); ++i) if (std::abs(left[i]) > 1e-6f) lastNonZero = i;
    VSM_ASSERT(lastNonZero > 5000 && lastNonZero < 5400);
}

VSM_TEST(sampler_decay_shortens_the_sound) {
    auto sampler = makeSampler();
    sampler->setSample(0, makeRampSample(20000));
    sampler->setParameter(SamplerSynth::slotParam(0, SamplerSynth::kSlotDecay), 0.05f);

    const auto event = noteOn(0, 36);
    std::vector<float> left(10000, 0.0f), right(10000, 0.0f);
    sampler->process(&event, 1, left.data(), right.data(), 10000);

    // 0,05 s à 48 kHz = 2400 échantillons : au-delà, silence.
    VSM_ASSERT(std::abs(left[100]) > 0.1f);
    VSM_ASSERT_NEAR(std::abs(left[5000]), 0.0f, 1e-6);
}

VSM_TEST(sampler_choke_group_cuts_the_previous_voice) {
    // Le charleston fermé étouffe l'ouvert : seul couplage entre voix d'une
    // boîte à rythmes, et il est indispensable.
    auto sampler = makeSampler();
    sampler->setSample(0, makeRampSample(20000));
    sampler->setSample(1, makeRampSample(20000));
    sampler->setParameter(SamplerSynth::slotParam(0, SamplerSynth::kSlotChoke), 1.0f);
    sampler->setParameter(SamplerSynth::slotParam(1, SamplerSynth::kSlotChoke), 1.0f);

    const MidiNoteEvent events[2] = { noteOn(0, 36), noteOn(200, 38) };
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    sampler->process(events, 2, left.data(), right.data(), 512);
    // Une seule voix survit : la seconde a coupé la première.
    VSM_ASSERT_EQ(sampler->activeVoiceCount(), 1);
}

VSM_TEST(sampler_loads_a_wav_file_and_reports_failures) {
    auto sampler = makeSampler();
    vsm::audio::plugin::ISampleLoader& loader = *sampler;

    const auto path = writeTempWav("vsm_sampler_test.wav", 2000, 48000.0);
    std::string error;
    VSM_ASSERT(loader.loadSample(0, path, error));
    VSM_ASSERT(error.empty());
    VSM_ASSERT_EQ(loader.samplePath(0), path);

    // Fichier absent : ÉCHEC signalé, et l'emplacement garde ce qu'il avait --
    // aucun son de substitution.
    std::string missingError;
    VSM_ASSERT(!loader.loadSample(0, "/chemin/absent.wav", missingError));
    VSM_ASSERT(!missingError.empty());
    VSM_ASSERT_EQ(loader.samplePath(0), path);

    const auto event = noteOn(0, 36);
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    sampler->process(&event, 1, left.data(), right.data(), 512);
    VSM_ASSERT(peakAbs(left) > 0.1f); // l'échantillon d'origine joue toujours

    loader.clearSample(0);
    VSM_ASSERT(loader.samplePath(0).empty());
    std::filesystem::remove(path);
}

VSM_TEST(sampler_is_deterministic) {
    auto render = [] {
        auto sampler = makeSampler();
        sampler->setSample(0, makeRampSample());
        sampler->setParameter(SamplerSynth::slotParam(0, SamplerSynth::kSlotTune), 3.0f);
        const auto event = noteOn(0, 36);
        std::vector<float> left(2000, 0.0f), right(2000, 0.0f);
        sampler->process(&event, 1, left.data(), right.data(), 2000);
        return left;
    };
    const auto first = render(), second = render();
    for (size_t i = 0; i < first.size(); ++i) VSM_ASSERT_NEAR(first[i], second[i], 1e-9);
}

VSM_TEST(sampler_save_load_roundtrip) {
    auto source = makeSampler();
    source->setParameter(SamplerSynth::slotParam(2, SamplerSynth::kSlotTune), -5.0f);
    source->setParameter(SamplerSynth::slotParam(2, SamplerSynth::kSlotPan), 0.6f);
    const auto state = source->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.sampler"));

    auto target = makeSampler();
    target->loadState(state);
    VSM_ASSERT_NEAR(target->getParameter(SamplerSynth::slotParam(2, SamplerSynth::kSlotTune)), -5.0f, 1e-6);
    VSM_ASSERT_NEAR(target->getParameter(SamplerSynth::slotParam(2, SamplerSynth::kSlotPan)), 0.6f, 1e-6);
}

VSM_TEST(sampler_parameter_list_size) {
    auto sampler = makeSampler();
    // 1 global + 16 emplacements x 7 paramètres.
    VSM_ASSERT_EQ(sampler->parameterList().size(), size_t{1 + 16 * 7});
    VSM_ASSERT_EQ(size_t{SamplerSynth::kSlotCount}, size_t{16});
}

VSM_TEST(sampler_default_notes_follow_general_midi_drums) {
    // Un MIDI de batterie transcrit par l'analyse doit tomber sur les bons
    // emplacements sans réglage préalable.
    auto sampler = makeSampler();
    VSM_ASSERT_NEAR(sampler->getParameter(SamplerSynth::slotParam(0, SamplerSynth::kSlotNote)), 36.0f, 0.5f);
    VSM_ASSERT_NEAR(sampler->getParameter(SamplerSynth::slotParam(1, SamplerSynth::kSlotNote)), 38.0f, 0.5f);
    VSM_ASSERT_NEAR(sampler->getParameter(SamplerSynth::slotParam(2, SamplerSynth::kSlotNote)), 42.0f, 0.5f);
}
