#include "TestFramework.h"
#include "vsm/audio/dsp/PreSection.h"
#include "vsm/audio/engine/AudioTrackSource.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/sequencer/Project.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace vsm::audio;
using namespace vsm::sequencer;

// D593 — LE COUPE-BAS ET LE COUPE-HAUT DE LA TRANCHE. L'attendu écrit avant le code (ROADMAP-daw, D593) : la réponse
// d'un biquad de RBJ à 48 kHz, calculée avant tout code — coupe-bas à 200 Hz : 50 Hz à −24,10 dB, 200 Hz à −3,01 dB,
// 1 kHz à −0,007 dB ; coupe-haut à 2 kHz : 8 kHz à −25,69 dB, 2 kHz à −3,01 dB, 1 kHz à −0,26 dB ; à ±0,05 dB.

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<float> sinus(double hz, int frames, float amplitude = 0.1f) {
    std::vector<float> x(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i)
        x[static_cast<size_t>(i)] = amplitude * static_cast<float>(std::sin(2.0 * kPi * hz * i / 48000.0));
    return x;
}

double rmsDb(const std::vector<float>& x, size_t from, size_t count) {
    double s = 0.0;
    for (size_t i = from; i < from + count; ++i) s += static_cast<double>(x[i]) * x[i];
    return 10.0 * std::log10(s / static_cast<double>(count));
}

/// Le gain en régime établi : une seconde de mise en route jetée, la seconde suivante mesurée (un nombre entier de
/// périodes pour toutes les fréquences essayées), par blocs de 512 comme dans le moteur.
double gainDb(double hz, float lowCutHz, float highCutHz) {
    const int frames = 96000;
    std::vector<float> l = sinus(hz, frames), r = sinus(hz, frames);
    const std::vector<float> entree = l;
    dsp::PreSectionFilter pre;
    pre.setSampleRate(48000.0);
    for (int debut = 0; debut < frames; debut += 512)
        pre.process(l.data() + debut, r.data() + debut, std::min(512, frames - debut), lowCutHz, highCutHz);
    return rmsDb(l, 48000, 48000) - rmsDb(entree, 48000, 48000);
}

} // namespace

VSM_TEST(the_low_cut_at_200_hz_answers_like_the_rbj_high_pass) {
    std::printf("    D593 coupe-bas 200 Hz : 50 Hz %.3f dB, 200 Hz %.3f dB, 1 kHz %.4f dB\n", gainDb(50, 200, 0),
                gainDb(200, 200, 0), gainDb(1000, 200, 0));
    VSM_ASSERT_NEAR(gainDb(50, 200.0f, 0.0f), -24.10, 0.05);
    VSM_ASSERT_NEAR(gainDb(200, 200.0f, 0.0f), -3.01, 0.05);
    VSM_ASSERT_NEAR(gainDb(1000, 200.0f, 0.0f), -0.007, 0.05);
}

VSM_TEST(the_high_cut_at_2_khz_answers_like_the_rbj_low_pass) {
    std::printf("    D593 coupe-haut 2 kHz : 8 kHz %.3f dB, 2 kHz %.3f dB, 1 kHz %.3f dB\n", gainDb(8000, 0, 2000),
                gainDb(2000, 0, 2000), gainDb(1000, 0, 2000));
    VSM_ASSERT_NEAR(gainDb(8000, 0.0f, 2000.0f), -25.69, 0.05);
    VSM_ASSERT_NEAR(gainDb(2000, 0.0f, 2000.0f), -3.01, 0.05);
    VSM_ASSERT_NEAR(gainDb(1000, 0.0f, 2000.0f), -0.26, 0.05);
}

VSM_TEST(both_cuts_off_leave_the_signal_bit_for_bit) {
    std::vector<float> l = sinus(440, 4096), r = sinus(660, 4096);
    const std::vector<float> l0 = l, r0 = r;
    dsp::PreSectionFilter pre;
    pre.setSampleRate(48000.0);
    pre.process(l.data(), r.data(), 4096, 0.0f, 0.0f);
    VSM_ASSERT(l == l0);
    VSM_ASSERT(r == r0);
    // Allumé puis éteint : éteint, il ne touche de nouveau à rien.
    pre.process(l.data(), r.data(), 4096, 300.0f, 0.0f);
    std::vector<float> l1 = sinus(440, 4096), r1 = sinus(660, 4096);
    const std::vector<float> l2 = l1;
    pre.process(l1.data(), r1.data(), 4096, 0.0f, 0.0f);
    VSM_ASSERT(l1 == l2);
}

VSM_TEST(the_graph_cuts_the_low_end_of_a_track_after_its_trim_and_leaves_the_witness_alone) {
    // Le CHEMIN RÉEL : une piste audio dans le graphe (D2), un sinus de 50 Hz ; le témoin, la même piste coupe-bas
    // éteint — même code, une seule valeur change.
    auto rendre = [](float lowCutHz) {
        Project project;
        project.ticksPerQuarterNote = 480;
        Track piste;
        piste.kind = Track::Kind::Audio;
        piste.name = "Grave";
        piste.lowCutHz = lowCutHz;
        project.tracks.push_back(piste);
        auto graph = std::make_unique<engine::ProcessGraph>();
        graph->prepare(48000.0, 512);
        graph->setProject(project);
        auto source = std::make_shared<engine::AudioTrackSource>();
        source->setMemorySamples(sinus(50.0, 96000), sinus(50.0, 96000));
        engine::AudioClipSpan span;
        span.startFrame = 0;
        span.lengthFrames = 96000;
        source->clips.push_back(span);
        graph->setTrackAudio(0, source);
        return engine::OfflineRenderer::render(*graph, 48000.0, 512, 2.0).left;
    };
    const auto temoin = rendre(0.0f);
    const auto coupe = rendre(200.0f);
    VSM_ASSERT(temoin.size() >= 96000 && coupe.size() >= 96000);
    const double ecart = rmsDb(coupe, 48000, 48000) - rmsDb(temoin, 48000, 48000);
    std::printf("    D593 graphe : 50 Hz coupé à 200 Hz, %.3f dB sous le témoin\n", ecart);
    VSM_ASSERT_NEAR(ecart, -24.10, 0.05);
}
