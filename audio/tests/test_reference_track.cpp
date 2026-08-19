#include "TestFramework.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/audio/engine/ReferenceTrack.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include <cmath>
#include <memory>
#include <vector>

using namespace vsm::audio::engine;
using vsm::audio::io::SampleBuffer;

namespace {

constexpr double kSampleRate = 48000.0;

/// Enregistrement de référence factice : un sinus continu à 1 kHz, facile à
/// reconnaître dans un mélange.
vsm::audio::io::SampleBufferPtr makeReference(double seconds = 2.0, double rate = kSampleRate,
                                               float amplitude = 0.5f) {
    auto buffer = std::make_shared<SampleBuffer>();
    buffer->sampleRate = rate;
    const auto frames = static_cast<size_t>(rate * seconds);
    buffer->left.resize(frames);
    for (size_t i = 0; i < frames; ++i)
        buffer->left[i] = amplitude * std::sin(2.0f * 3.14159265f * 1000.0f
                                                * static_cast<float>(i) / static_cast<float>(rate));
    return buffer;
}

float peakAbs(const std::vector<float>& buffer) {
    float peak = 0.0f;
    for (float sample : buffer) peak = std::max(peak, std::abs(sample));
    return peak;
}

/// Projet minimal : une piste, une machine, une note tenue.
vsm::sequencer::Project makeProject() {
    vsm::sequencer::Project project;
    vsm::sequencer::Track track;
    track.name = "Reconstruction";
    track.instrumentId = "vsm.minimoog";
    vsm::sequencer::Note note;
    note.startTick = 0;
    note.endTick = 1920;
    note.number = 48;
    note.velocity = 110;
    track.notes.push_back(note);
    project.tracks.push_back(track);
    return project;
}

std::vector<float> renderGraph(ProcessGraph& graph, int frames) {
    std::vector<float> left(static_cast<size_t>(frames), 0.0f);
    std::vector<float> right(static_cast<size_t>(frames), 0.0f);
    graph.seekSeconds(0.0);
    graph.setPlaying(true);
    const int block = 512;
    for (int start = 0; start < frames; start += block) {
        const int count = std::min(block, frames - start);
        graph.processBlock(left.data() + start, right.data() + start, count);
    }
    graph.setPlaying(false);
    return left;
}

} // namespace

VSM_TEST(reference_track_is_silent_until_it_is_given_audio_and_a_mode) {
    ReferenceTrack reference;
    reference.prepare(kSampleRate);
    std::vector<float> left(512, 0.0f), right(512, 0.0f);

    // Ni son ni mode : rien.
    reference.mixInto(left.data(), right.data(), 512, 0.0);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0f, 1e-9);

    // Du son, mais mode éteint : toujours rien. L'état par défaut est
    // « reconstruction seule », et il ne doit pas suffire de charger un
    // fichier pour l'entendre par surprise.
    reference.setAudio(makeReference());
    reference.mixInto(left.data(), right.data(), 512, 0.0);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0f, 1e-9);

    reference.setMode(ReferenceTrack::Mode::Mix);
    reference.mixInto(left.data(), right.data(), 512, 0.0);
    VSM_ASSERT(peakAbs(left) > 0.1f);
}

VSM_TEST(reference_track_follows_the_playhead) {
    // La référence doit être lue À LA POSITION DU TRANSPORT, sinon comparer
    // deux versions au même endroit du morceau serait impossible.
    ReferenceTrack reference;
    reference.prepare(kSampleRate);
    auto audio = std::make_shared<SampleBuffer>();
    audio->sampleRate = kSampleRate;
    audio->left.assign(static_cast<size_t>(kSampleRate * 2.0), 0.0f);
    // Une impulsion nette à exactement 1,0 s.
    audio->left[static_cast<size_t>(kSampleRate)] = 1.0f;
    reference.setAudio(audio);
    reference.setMode(ReferenceTrack::Mode::Solo);

    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    reference.mixInto(left.data(), right.data(), 512, 0.0);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0f, 1e-6); // rien au début

    std::fill(left.begin(), left.end(), 0.0f);
    reference.mixInto(left.data(), right.data(), 512, 1.0);
    VSM_ASSERT(peakAbs(left) > 0.5f); // l'impulsion est là, à une seconde
}

VSM_TEST(reference_track_resamples_a_recording_of_a_different_rate) {
    // Un enregistrement à 44,1 kHz lu tel quel à 48 kHz serait transposé d'un
    // demi-ton -- et l'on comparerait alors une erreur qu'on a introduite
    // soi-même. On vérifie par la DURÉE : deux secondes de source doivent
    // durer deux secondes.
    ReferenceTrack reference;
    reference.prepare(kSampleRate);
    auto audio = std::make_shared<SampleBuffer>();
    audio->sampleRate = 44100.0;
    audio->left.assign(static_cast<size_t>(44100.0 * 2.0), 0.0f);
    audio->left[static_cast<size_t>(44100.0 * 1.5)] = 1.0f; // impulsion à 1,5 s
    reference.setAudio(audio);
    reference.setMode(ReferenceTrack::Mode::Solo);

    std::vector<float> left(4800, 0.0f), right(4800, 0.0f); // 100 ms
    reference.mixInto(left.data(), right.data(), 4800, 1.45);
    VSM_ASSERT(peakAbs(left) > 0.4f); // l'impulsion tombe bien à 1,5 s de transport

    std::fill(left.begin(), left.end(), 0.0f);
    reference.mixInto(left.data(), right.data(), 4800, 1.60);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0f, 1e-6); // et pas ailleurs
}

VSM_TEST(reference_offset_shifts_the_recording) {
    // L'enregistrement peut avoir du silence en tête. Sans réglage de
    // décalage, on comparerait deux sons désalignés et tout paraîtrait faux.
    ReferenceTrack reference;
    reference.prepare(kSampleRate);
    auto audio = std::make_shared<SampleBuffer>();
    audio->sampleRate = kSampleRate;
    audio->left.assign(static_cast<size_t>(kSampleRate * 2.0), 0.0f);
    audio->left[static_cast<size_t>(kSampleRate * 0.5)] = 1.0f;
    reference.setAudio(audio);
    reference.setMode(ReferenceTrack::Mode::Solo);
    reference.setOffsetSeconds(0.5); // l'impulsion doit remonter à t=0

    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    reference.mixInto(left.data(), right.data(), 512, 0.0);
    VSM_ASSERT(peakAbs(left) > 0.5f);
}

VSM_TEST(solo_mode_silences_the_reconstruction) {
    vsm::audio::plugin::registerBuiltInPlugins();
    ProcessGraph graph;
    graph.prepare(kSampleRate, 512);
    graph.setProject(makeProject());
    graph.setTrackInstrument(0, "vsm.minimoog");

    const auto reconstruction = renderGraph(graph, 24000);
    VSM_ASSERT(peakAbs(reconstruction) > 0.01f); // la reconstruction sonne

    graph.referenceTrack().setAudio(makeReference());
    graph.referenceTrack().setMode(ReferenceTrack::Mode::Solo);
    const auto original = renderGraph(graph, 24000);
    VSM_ASSERT(peakAbs(original) > 0.1f);

    // En mode « original seul », ce qu'on entend est la RÉFÉRENCE : son niveau
    // doit être celui du sinus (0,5), pas la somme des deux.
    VSM_ASSERT_NEAR(peakAbs(original), 0.5f, 0.05f);
}

VSM_TEST(mix_mode_lets_both_be_heard) {
    vsm::audio::plugin::registerBuiltInPlugins();
    ProcessGraph graph;
    graph.prepare(kSampleRate, 512);
    graph.setProject(makeProject());
    graph.setTrackInstrument(0, "vsm.minimoog");
    graph.referenceTrack().setAudio(makeReference());
    graph.referenceTrack().setMode(ReferenceTrack::Mode::Mix);

    const auto ensemble = renderGraph(graph, 24000);
    // Plus fort que l'un ou l'autre seul : les deux sont bien présents.
    VSM_ASSERT(peakAbs(ensemble) > 0.5f);
}

VSM_TEST(an_export_never_carries_the_reference) {
    // LE test de cette étape. Le rendu hors ligne passe par le MÊME
    // processBlock que la lecture ; sans précaution, exporter avec la
    // référence active produirait un fichier contenant l'enregistrement
    // d'origine mélangé à la reconstruction -- c'est-à-dire ni l'un ni
    // l'autre, et personne ne s'en apercevrait avant de l'écouter.
    //
    // DEUX GRAPHES NEUFS, et c'est nécessaire : rendre deux fois le même
    // projet sur LE MÊME graphe ne donne pas le même son, parce que les
    // instruments gardent leur état d'un rendu à l'autre (phases
    // d'oscillateurs, dérive analogique seedée qui a avancé). Une première
    // version de ce test comparait deux rendus successifs et voyait un écart
    // qui n'avait rien à voir avec la référence.
    vsm::audio::plugin::registerBuiltInPlugins();

    auto prepare = [](ProcessGraph& graph) {
        graph.prepare(kSampleRate, 512);
        graph.setProject(makeProject());
        graph.setTrackInstrument(0, "vsm.minimoog");
    };

    ProcessGraph sansReference;
    prepare(sansReference);
    const RenderedAudio sans = OfflineRenderer::render(sansReference, kSampleRate, 512, 0.5);

    ProcessGraph avecReference;
    prepare(avecReference);
    avecReference.referenceTrack().setAudio(makeReference());
    avecReference.referenceTrack().setMode(ReferenceTrack::Mode::Mix);
    const RenderedAudio avec = OfflineRenderer::render(avecReference, kSampleRate, 512, 0.5);

    VSM_ASSERT_EQ(sans.left.size(), avec.left.size());
    VSM_ASSERT(peakAbs(sans.left) > 0.01f); // il y a bien de la reconstruction à comparer
    for (size_t i = 0; i < sans.left.size(); ++i)
        VSM_ASSERT_NEAR(avec.left[i], sans.left[i], 1e-9); // rigoureusement le même rendu

    // ...et le mode de l'utilisateur est RESTITUÉ : exporter ne doit pas
    // éteindre en douce son écoute comparative.
    VSM_ASSERT(avecReference.referenceTrack().mode() == ReferenceTrack::Mode::Mix);
}

VSM_TEST(two_fresh_graphs_render_identically) {
    // La propriété sur laquelle repose le test précédent, vérifiée pour
    // elle-même : c'est le graphe NEUF qui est reproductible, pas le graphe
    // rejoué.
    vsm::audio::plugin::registerBuiltInPlugins();
    auto rendre = []() {
        ProcessGraph graph;
        graph.prepare(kSampleRate, 512);
        graph.setProject(makeProject());
        graph.setTrackInstrument(0, "vsm.minimoog");
        return OfflineRenderer::render(graph, kSampleRate, 512, 0.3);
    };
    const RenderedAudio a = rendre(), b = rendre();
    VSM_ASSERT_EQ(a.left.size(), b.left.size());
    for (size_t i = 0; i < a.left.size(); ++i) VSM_ASSERT_NEAR(a.left[i], b.left[i], 1e-9);
}
