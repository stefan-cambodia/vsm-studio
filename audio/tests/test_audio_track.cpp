#include "TestFramework.h"
#include "vsm/audio/engine/AudioTrackSource.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/sequencer/Project.h"
#include <cmath>

using namespace vsm::audio::engine;
using namespace vsm::sequencer;

// D2 de docs/ROADMAP-daw.md — LA PISTE AUDIO.
//
// Ce qu'elle débloque tient en un fait : dans la reconstruction de *Sky and
// Sand*, la piste « Voix » porte UNE note MIDI, qui déclenche 8 min 52 et 47 Mo
// dans l'emplacement d'un sampler de percussions. Ce n'était pas un choix de
// production, c'était le seul moyen de faire entrer de l'audio dans un DAW qui
// n'avait pas de piste pour en porter.

namespace {

/// Un fichier reconnaissable : la valeur d'un échantillon est son indice
/// divisé par mille. On sait donc, en lisant la sortie, EXACTEMENT quel
/// échantillon du fichier a été joué et à quel endroit.
std::shared_ptr<AudioTrackSource> materiauReperable(int64_t frames) {
    auto source = std::make_shared<AudioTrackSource>();
    std::vector<float> gauche(static_cast<size_t>(frames)), droite(static_cast<size_t>(frames));
    for (int64_t i = 0; i < frames; ++i) {
        gauche[static_cast<size_t>(i)] = static_cast<float>(i) / 1000.0f;
        droite[static_cast<size_t>(i)] = -static_cast<float>(i) / 1000.0f;
    }
    source->setMemorySamples(std::move(gauche), std::move(droite));
    return source;
}

} // namespace

VSM_TEST(an_audio_clip_lands_where_the_timeline_says) {
    auto source = materiauReperable(1000);
    AudioClipSpan span;
    span.startFrame = 100;      // posé à l'échantillon 100 de la ligne de temps
    span.lengthFrames = 50;
    span.sourceStartFrame = 400; // et il commence à l'échantillon 400 du fichier
    source->clips.push_back(span);

    std::vector<float> gauche(300, 0.0f), droite(300, 0.0f);
    source->mixInto(gauche.data(), droite.data(), 0, 300);

    VSM_ASSERT_NEAR(gauche[99], 0.0f, 1e-9);            // rien avant
    VSM_ASSERT_NEAR(gauche[100], 400.0f / 1000.0f, 1e-6); // le bon échantillon
    VSM_ASSERT_NEAR(gauche[149], 449.0f / 1000.0f, 1e-6);
    VSM_ASSERT_NEAR(gauche[150], 0.0f, 1e-9);            // rien après
    VSM_ASSERT_NEAR(droite[120], -420.0f / 1000.0f, 1e-6); // et la droite est bien la droite
}

VSM_TEST(a_block_that_starts_inside_a_clip_reads_from_the_right_place) {
    // LE PIÈGE QUE CE TEST INTERDIT : compter les blocs au lieu de lire la
    // position sur la ligne de temps. Tant qu'on lit les blocs dans l'ordre,
    // les deux se valent -- puis un bouclage ou un saut de tête de lecture
    // arrive, et le son part d'un autre endroit du fichier sans que rien ne le
    // dise.
    auto source = materiauReperable(1000);
    AudioClipSpan span;
    span.startFrame = 0;
    span.lengthFrames = 800;
    span.sourceStartFrame = 0;
    source->clips.push_back(span);

    std::vector<float> gauche(64, 0.0f), droite(64, 0.0f);
    source->mixInto(gauche.data(), droite.data(), 500, 64);   // on saute à 500
    VSM_ASSERT_NEAR(gauche[0], 500.0f / 1000.0f, 1e-6);
    VSM_ASSERT_NEAR(gauche[63], 563.0f / 1000.0f, 1e-6);
}

VSM_TEST(fades_gain_and_phase_do_what_they_say) {
    auto source = materiauReperable(1000);
    AudioClipSpan span;
    span.startFrame = 0;
    span.lengthFrames = 100;
    span.sourceStartFrame = 500;   // valeur constante autour de 0,5
    span.fadeInFrames = 10;
    span.fadeOutFrames = 10;
    span.gain = 0.5f;
    span.invertPhase = true;
    source->clips.push_back(span);

    std::vector<float> gauche(100, 0.0f), droite(100, 0.0f);
    source->mixInto(gauche.data(), droite.data(), 0, 100);

    VSM_ASSERT_NEAR(gauche[0], 0.0f, 1e-9);                   // début du fondu
    VSM_ASSERT(gauche[50] < 0.0f);                            // phase inversée
    VSM_ASSERT_NEAR(std::abs(gauche[50]), 0.5f * 550.0f / 1000.0f, 1e-6); // gain 0,5
    VSM_ASSERT(std::abs(gauche[95]) < std::abs(gauche[50]));  // fondu de sortie
    // LE DERNIER ÉCHANTILLON N'EST PAS NUL, ET C'EST JUSTE. Le fondu atteint
    // zéro à la FRONTIÈRE du clip, c'est-à-dire un échantillon après le
    // dernier : à l'échantillon 99 d'un clip de 100 avec dix de fondu, il
    // reste un dixième. Sur un fondu réaliste -- quelques milliers
    // d'échantillons -- ce résidu vaut un demi-millième et ne s'entend pas.
    // Exiger zéro ici obligerait à tordre le calcul pour satisfaire un test.
    VSM_ASSERT(std::abs(gauche[99]) < std::abs(gauche[95]));
    VSM_ASSERT_NEAR(std::abs(gauche[99]), 0.1f * 0.5f * 599.0f / 1000.0f, 1e-4);
}

VSM_TEST(reading_past_the_end_of_the_material_is_silence_not_noise) {
    auto source = materiauReperable(100);
    AudioClipSpan span;
    span.startFrame = 0;
    span.lengthFrames = 500;      // bien plus long que le matériau
    span.sourceStartFrame = 0;
    source->clips.push_back(span);

    std::vector<float> gauche(500, 0.0f), droite(500, 0.0f);
    source->mixInto(gauche.data(), droite.data(), 0, 500);
    VSM_ASSERT_NEAR(gauche[99], 99.0f / 1000.0f, 1e-6);
    for (size_t i = 100; i < 500; ++i)
        VSM_ASSERT_NEAR(gauche[i], 0.0f, 1e-9);
}

VSM_TEST(an_audio_track_without_a_clip_plays_its_whole_file) {
    // La même règle que pour une piste MIDI : « pas de clip » veut dire « pas
    // de découpe », pas « rien ».
    Track piste;
    piste.kind = Track::Kind::Audio;
    piste.audio = {"samples/voix.wav", 44100.0, 44100, 2};
    const auto spans = spansFromTrack(piste, 44100.0, [](int64_t t) { return t / 960.0; });
    VSM_ASSERT_EQ(spans.size(), size_t(1));
    VSM_ASSERT_EQ(spans[0].startFrame, int64_t(0));
    VSM_ASSERT_EQ(spans[0].lengthFrames, int64_t(44100));
}

VSM_TEST(a_muted_audio_clip_produces_no_span_at_all) {
    Track piste;
    piste.kind = Track::Kind::Audio;
    piste.audio = {"samples/voix.wav", 44100.0, 44100, 2};
    Clip clip;
    clip.startTick = 0;
    clip.length = 480;
    clip.muted = true;
    piste.clips.push_back(clip);
    VSM_ASSERT(spansFromTrack(piste, 44100.0, [](int64_t t) { return t / 960.0; }).empty());
}

VSM_TEST(the_graph_plays_an_audio_track_that_has_no_instrument_at_all) {
    // LA CONDITION QUI FAISAIT TOUT SAUTER : le graphe passait une piste sans
    // instrument, en silence. Une piste audio n'en a pas et n'en aura jamais.
    Project project;
    project.ticksPerQuarterNote = 480;
    Track voix;
    voix.kind = Track::Kind::Audio;
    voix.name = "Voix";
    project.tracks.push_back(voix);

    ProcessGraph graph;
    graph.prepare(48000.0, 512);
    graph.setProject(project);

    auto source = materiauReperable(48000);
    AudioClipSpan span;
    span.startFrame = 0;
    span.lengthFrames = 48000;
    source->clips.push_back(span);
    graph.setTrackAudio(0, source);

    const auto rendu = OfflineRenderer::render(graph, 48000.0, 512, 0.5);
    float crete = 0.0f;
    for (float s : rendu.left) crete = std::max(crete, std::abs(s));
    VSM_ASSERT(crete > 0.01f);   // la piste a bien sonné
}

// --- D5.2 : la boucle de clip vaut aussi pour l'audio -----------------------

VSM_TEST(an_audio_clip_longer_than_its_window_repeats_it) {
    // LE GESTE DOIT VOULOIR DIRE LA MÊME CHOSE SUR LES DEUX SORTES DE PISTE.
    // Le planning MIDI répétait déjà sa fenêtre quand la durée jouée la
    // dépasse ; l'audio, lui, lisait tout droit et continuait dans le fichier.
    // Étirer un clip aurait donc bouclé une batterie MIDI et révélé la suite
    // d'une prise audio -- deux réponses pour un seul geste.
    vsm::sequencer::Track piste;
    piste.kind = vsm::sequencer::Track::Kind::Audio;
    piste.audio.path = "audio/prise.wav";
    piste.audio.sampleRate = 48000.0;
    piste.audio.frames = 48000 * 8;
    piste.audio.channels = 2;

    vsm::sequencer::Clip clip;
    clip.startTick = 0;
    clip.sourceLength = 960;    // une seconde de fenêtre à 120 BPM / ppq 480
    clip.length = 3840;         // quatre fois plus long que la fenêtre
    piste.clips.push_back(clip);

    // 480 ticks par noire, 120 BPM : un tick vaut 1/960 s.
    const auto spans = spansFromTrack(piste, 48000.0,
                                       [](int64_t t) { return static_cast<double>(t) / 960.0; });
    VSM_ASSERT_EQ(spans.size(), size_t(4));
    for (size_t i = 0; i < spans.size(); ++i) {
        VSM_ASSERT_EQ(spans[i].startFrame, static_cast<int64_t>(i) * 48000);
        VSM_ASSERT_EQ(spans[i].lengthFrames, int64_t(48000));
        // CHAQUE TOUR REPART DU MÊME ENDROIT DU FICHIER : c'est ce qui fait
        // une boucle plutôt qu'une lecture continue.
        VSM_ASSERT_EQ(spans[i].sourceStartFrame, int64_t(0));
    }
}

VSM_TEST(the_fades_of_a_looped_clip_belong_to_the_clip_and_not_to_each_turn) {
    // Les répéter ferait un trou à chaque tour de boucle.
    vsm::sequencer::Track piste;
    piste.kind = vsm::sequencer::Track::Kind::Audio;
    piste.audio.path = "audio/prise.wav";
    piste.audio.sampleRate = 48000.0;
    piste.audio.frames = 48000 * 8;

    vsm::sequencer::Clip clip;
    clip.sourceLength = 960;
    clip.length = 2880;         // trois tours
    clip.fadeInSeconds = 0.05;
    clip.fadeOutSeconds = 0.10;
    piste.clips.push_back(clip);

    const auto spans = spansFromTrack(piste, 48000.0,
                                       [](int64_t t) { return static_cast<double>(t) / 960.0; });
    VSM_ASSERT_EQ(spans.size(), size_t(3));
    VSM_ASSERT_EQ(spans[0].fadeInFrames, int64_t(48000 * 0.05));
    VSM_ASSERT_EQ(spans[0].fadeOutFrames, int64_t(0));
    VSM_ASSERT_EQ(spans[1].fadeInFrames, int64_t(0));
    VSM_ASSERT_EQ(spans[1].fadeOutFrames, int64_t(0));
    VSM_ASSERT_EQ(spans[2].fadeInFrames, int64_t(0));
    VSM_ASSERT_EQ(spans[2].fadeOutFrames, int64_t(48000 * 0.10));
}
