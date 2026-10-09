#include "TestFramework.h"
#include "vsm/audio/engine/AudioTrackSource.h"
#include "vsm/sequencer/Track.h"
#include <algorithm>
#include <cmath>
#include <vector>

// D545.1 de docs/ROADMAP-daw.md — LA COURBE DE GAIN D'UN CLIP, DANS LE MOTEUR.
//
// Les points sont des secondes du FICHIER ; `spansFromTrack` les pose sur chaque portée (sa carte, sa
// boucle, son sens), et le mixage les lit à la trame qu'il joue. Un signal CONSTANT de 0,5 rend la
// courbe lisible telle quelle : la sortie vaut 0,5 × le gain. Le banc de l'application mesure le clip
// simple dans l'export ; ces tests gardent les trois correspondances qu'il ne voit pas — la boucle,
// l'envers, l'étirement.
namespace {
using namespace vsm::audio::engine;
using vsm::sequencer::Clip;
using vsm::sequencer::Track;
constexpr double kSr = 48000.0;
double enSecondes(int64_t tick) { return static_cast<double>(tick) / 1920.0; }   // 1920 ticks par seconde

Track piste(const Clip& clip, double secondes) {
    Track t;
    t.kind = Track::Kind::Audio;
    t.name = "Banc D545.1";
    t.audio.path = "banc.wav";
    t.audio.sampleRate = kSr;
    t.audio.frames = static_cast<int64_t>(secondes * kSr);
    t.audio.channels = 2;
    t.clips.push_back(clip);
    return t;
}
std::vector<float> rendre(const Track& t, double secondes) {
    const auto n = static_cast<size_t>(secondes * kSr);
    AudioTrackSource source;
    source.samples = std::make_shared<MemorySampleStore>(std::vector<float>(n, 0.5f), std::vector<float>(n, 0.5f));
    source.clips = spansFromTrack(t, kSr, enSecondes, vsm::sequencer::FadeShape::EqualPower);
    prepareWarpedSpans(source);
    std::vector<float> g(n, 0.0f), d(n, 0.0f);
    for (size_t p = 0; p < n; p += 512) {
        const int k = static_cast<int>(std::min<size_t>(512, n - p));
        source.mixInto(g.data() + p, d.data() + p, static_cast<int64_t>(p), k);
    }
    return g;
}
float a(const std::vector<float>& x, double secondes) { return x[static_cast<size_t>(secondes * kSr)]; }
}

VSM_TEST(the_engine_plays_the_gain_curve_at_the_second_of_the_file) {
    Clip c;
    c.gainEnvelope = {{1.0, 1.0f}, {3.0, 0.0f}};
    const auto x = rendre(piste(c, 4.0), 4.0);
    VSM_ASSERT_NEAR(a(x, 0.5), 0.5, 1e-4);
    VSM_ASSERT_NEAR(a(x, 2.0), 0.25, 1e-3);
    VSM_ASSERT_NEAR(a(x, 3.5), 0.0, 1e-4);
}

VSM_TEST(a_looped_clip_plays_its_curve_at_every_turn) {
    // Une fenêtre d'une seconde, jouée quatre fois : la courbe la suit à chaque tour.
    Clip c;
    c.sourceLength = 1920;
    c.length = 4 * 1920;
    c.gainEnvelope = {{0.25, 1.0f}, {0.75, 0.0f}};
    const auto x = rendre(piste(c, 4.0), 4.0);
    for (int tour = 0; tour < 4; ++tour) {
        VSM_ASSERT_NEAR(a(x, tour + 0.5), 0.25, 1e-3);
        VSM_ASSERT_NEAR(a(x, tour + 0.9), 0.0, 1e-4);
        VSM_ASSERT_NEAR(a(x, tour + 0.1), 0.5, 1e-4);
    }
}

VSM_TEST(a_reversed_clip_plays_its_curve_backwards_with_its_material) {
    Clip c;
    c.reversed = true;
    c.gainEnvelope = {{1.0, 1.0f}, {3.0, 0.0f}};
    const auto x = rendre(piste(c, 4.0), 4.0);
    VSM_ASSERT_NEAR(a(x, 0.5), 0.0, 1e-4);    // à l'envers, 0,5 s joue la seconde 3,5 du fichier
    VSM_ASSERT_NEAR(a(x, 2.0), 0.25, 1e-3);
    VSM_ASSERT_NEAR(a(x, 3.5), 0.5, 1e-4);    // et 3,5 s, la seconde 0,5
}

VSM_TEST(a_stretched_clip_plays_its_curve_through_its_map) {
    // La première seconde du fichier, étirée sur deux secondes (« réchantillonné » : exact sur une
    // constante) ; la courbe va de 1 à 0 sur cette seconde du FICHIER.
    Clip c;
    c.length = 2 * 1920;
    c.warpMode = vsm::sequencer::WarpMode::Repitch;
    c.warpMarkers = {{0.0, 0}, {1.0, 2 * 1920}};
    c.gainEnvelope = {{0.0, 1.0f}, {1.0, 0.0f}};
    const auto x = rendre(piste(c, 4.0), 2.0);
    VSM_ASSERT_NEAR(a(x, 1.0), 0.25, 2e-3);   // la seconde 0,5 du fichier : gain 0,5
    VSM_ASSERT_NEAR(a(x, 0.5), 0.375, 2e-3);  // la seconde 0,25 : gain 0,75
}
