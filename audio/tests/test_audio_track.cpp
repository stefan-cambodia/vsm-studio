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

// ---------------------------------------------------------------------------
// D12.5 — LE MOTEUR SUIT LE TEMPO (docs/CDC-etirement-temporel.md, § 4 et § 5).
// La règle du § 0 d'abord : un clip qui ne suit pas le tempo ne change pas
// d'un bit, et un clip étiré au rapport un non plus.
// ---------------------------------------------------------------------------

namespace {

using vsm::audio::engine::MemorySampleStore;

/// Une piste audio d'une seconde, un sinus à 220 Hz, 48 kHz.
std::shared_ptr<vsm::audio::engine::AudioTrackSource> pisteSinus(double hz, double secondes,
                                                                  double sr = 48000.0) {
    auto source = std::make_shared<vsm::audio::engine::AudioTrackSource>();
    const auto n = static_cast<size_t>(secondes * sr);
    std::vector<float> l(n), r(n);
    for (size_t i = 0; i < n; ++i) {
        l[i] = 0.5f * static_cast<float>(std::sin(2.0 * M_PI * hz * static_cast<double>(i) / sr));
        r[i] = l[i];
    }
    source->setMemorySamples(std::move(l), std::move(r));
    return source;
}
/// Un tick vaut 1/960 s (480 ppq, 120 BPM).
double enSecondes960(int64_t tick) { return static_cast<double>(tick) / 960.0; }

std::pair<std::vector<float>, std::vector<float>> jouer(
    const vsm::audio::engine::AudioTrackSource& source, int64_t trames, int bloc) {
    std::vector<float> l(static_cast<size_t>(trames), 0.0f), r(static_cast<size_t>(trames), 0.0f);
    for (int64_t pos = 0; pos < trames; pos += bloc) {
        const int n = static_cast<int>(std::min<int64_t>(bloc, trames - pos));
        source.mixInto(l.data() + pos, r.data() + pos, pos, n);
    }
    return {std::move(l), std::move(r)};
}
double picHz960(const std::vector<float>& x, size_t from, size_t count, double lo, double hi) {
    double meilleur = lo, m = -1.0;
    for (double f = lo; f <= hi; f += 0.2) {
        double re = 0.0, im = 0.0;
        for (size_t i = 0; i < count && from + i < x.size(); ++i) {
            const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / static_cast<double>(count));
            const double ph = 2.0 * M_PI * f * static_cast<double>(i) / 48000.0;
            re += w * x[from + i] * std::cos(ph);
            im += w * x[from + i] * std::sin(ph);
        }
        const double v = std::sqrt(re * re + im * im);
        if (v > m) { m = v; meilleur = f; }
    }
    return meilleur;
}

/// Une piste d'une seconde avec UN clip, étiré ou non.
vsm::sequencer::Track pisteAvecClip(vsm::sequencer::WarpMode mode, double secondesSource,
                                     vsm::midi::Tick longueurTicks) {
    vsm::sequencer::Track piste;
    piste.kind = vsm::sequencer::Track::Kind::Audio;
    piste.audio.path = "audio/prise.wav";
    piste.audio.sampleRate = 48000.0;
    piste.audio.frames = static_cast<int64_t>(secondesSource * 48000.0);
    vsm::sequencer::Clip clip;
    clip.id = 1;
    clip.startTick = 0;
    clip.length = longueurTicks;
    clip.sourceLength = longueurTicks;
    clip.warpMode = mode;
    if (mode != vsm::sequencer::WarpMode::Off)
        clip.warpMarkers = {{0.0, 0}, {secondesSource, longueurTicks}};
    piste.clips.push_back(clip);
    return piste;
}

} // namespace

VSM_TEST(a_clip_warped_at_ratio_one_plays_the_original_bit_for_bit) {
    // LA RÈGLE DU § 0 DU CDC : allumer le suivi de tempo sans rien caler ne
    // change pas un échantillon. C'est le court-circuit de `TimeStretch`.
    auto source = pisteSinus(220.0, 1.0);
    auto nu = pisteSinus(220.0, 1.0);
    // Une seconde de matériau sur 960 ticks = une seconde : rapport un.
    source->clips = spansFromTrack(pisteAvecClip(vsm::sequencer::WarpMode::KeepPitch, 1.0, 960),
                                    48000.0, enSecondes960);
    nu->clips = spansFromTrack(pisteAvecClip(vsm::sequencer::WarpMode::Off, 1.0, 960),
                                48000.0, enSecondes960);
    VSM_ASSERT_EQ(source->clips.size(), size_t(1));
    VSM_ASSERT(source->clips[0].warp != nullptr);
    prepareWarpedSpans(*source);
    prepareWarpedSpans(*nu);
    VSM_ASSERT(nu->clips[0].warp == nullptr);

    auto [wl, wr] = jouer(*source, 48000, 512);
    auto [nl, nr] = jouer(*nu, 48000, 512);
    VSM_ASSERT(wl == nl);
    VSM_ASSERT(wr == nr);
}

VSM_TEST(keep_pitch_uses_the_phase_vocoder_and_the_witness_uses_wsola) {
    // D12.8 : le choix d'algorithme est celui du CLIP, écrit dans le projet.
    auto a = pisteSinus(220.0, 1.0), b = pisteSinus(220.0, 1.0);
    a->clips = spansFromTrack(pisteAvecClip(vsm::sequencer::WarpMode::KeepPitch, 1.0, 1920), 48000.0, enSecondes960);
    b->clips = spansFromTrack(pisteAvecClip(vsm::sequencer::WarpMode::KeepPitchWsola, 1.0, 1920), 48000.0, enSecondes960);
    prepareWarpedSpans(*a);
    prepareWarpedSpans(*b);
    VSM_ASSERT(a->clips[0].warp && a->clips[0].warp->vocoder && !a->clips[0].warp->repitch);
    VSM_ASSERT(b->clips[0].warp && !b->clips[0].warp->vocoder && !b->clips[0].warp->repitch);
    // Les deux jouent deux secondes de la3 ; ils ne sont pas identiques (deux
    // algorithmes), mais tous deux tiennent la hauteur.
    auto [al, ar] = jouer(*a, 100000, 512);
    auto [bl, br] = jouer(*b, 100000, 512);
    VSM_ASSERT(al != bl);
    for (const auto* x : {&al, &bl}) {
        const double pic = picHz960(*x, 24000, 48000, 210.0, 230.0);
        VSM_ASSERT(std::abs(1200.0 * std::log2(pic / 220.0)) <= 5.0);
    }
}

VSM_TEST(a_warped_clip_plays_longer_and_keeps_its_pitch) {
    // Une seconde de matériau déclarée durer DEUX secondes (1920 ticks) : le
    // clip joue deux secondes, et le la3 reste un la3.
    auto source = pisteSinus(220.0, 1.0);
    source->clips = spansFromTrack(pisteAvecClip(vsm::sequencer::WarpMode::KeepPitch, 1.0, 1920),
                                    48000.0, enSecondes960);
    prepareWarpedSpans(*source);
    VSM_ASSERT_EQ(source->clips[0].lengthFrames, int64_t(96000));

    auto [l, r] = jouer(*source, 100000, 512);
    const double pic = picHz960(l, 24000, 48000, 210.0, 230.0);
    double rmsFin = 0.0;
    for (size_t i = 80000; i < 94000; ++i) rmsFin += l[i] * l[i];
    rmsFin = std::sqrt(rmsFin / 14000.0);
    double rmsApres = 0.0;
    for (size_t i = 97000; i < 100000; ++i) rmsApres += l[i] * l[i];
    rmsApres = std::sqrt(rmsApres / 3000.0);
    std::printf("    [banc moteur étiré] ×2 : pic %.1f Hz, rms avant la fin %.4f, après %.6f\n",
                pic, rmsFin, rmsApres);
    VSM_ASSERT(std::abs(1200.0 * std::log2(pic / 220.0)) <= 5.0);
    VSM_ASSERT(rmsFin > 0.2);
    VSM_ASSERT(rmsApres < 0.01);
}

VSM_TEST(a_repitched_clip_drops_its_pitch_with_its_speed) {
    // LE VINYLE : une seconde étalée sur deux, la hauteur suit — 220 Hz
    // deviennent 110 Hz. C'est ce qui distingue les deux modes, et le seul
    // moyen de le vérifier est de mesurer la hauteur.
    auto source = pisteSinus(220.0, 1.0);
    source->clips = spansFromTrack(pisteAvecClip(vsm::sequencer::WarpMode::Repitch, 1.0, 1920),
                                    48000.0, enSecondes960);
    prepareWarpedSpans(*source);
    VSM_ASSERT(source->clips[0].warp && source->clips[0].warp->repitch);
    auto [l, r] = jouer(*source, 96000, 512);
    const double pic = picHz960(l, 24000, 48000, 100.0, 240.0);
    std::printf("    [banc moteur étiré] rééchantillonné ×2 : pic %.1f Hz (110 attendu)\n", pic);
    VSM_ASSERT(std::abs(1200.0 * std::log2(pic / 110.0)) <= 5.0);
}

VSM_TEST(a_warped_clip_renders_the_same_whatever_the_block_size) {
    // LA CONDITION DE D2.6, portée à l'étirement : le rendu hors ligne (gros
    // blocs) et le temps réel (petits blocs) doivent donner le même fichier.
    auto a = pisteSinus(220.0, 1.0), b = pisteSinus(220.0, 1.0);
    const auto piste = pisteAvecClip(vsm::sequencer::WarpMode::KeepPitch, 1.0, 1500);
    a->clips = spansFromTrack(piste, 48000.0, enSecondes960);
    b->clips = spansFromTrack(piste, 48000.0, enSecondes960);
    prepareWarpedSpans(*a);
    prepareWarpedSpans(*b);
    auto [al, ar] = jouer(*a, 75000, 256);
    auto [bl, br] = jouer(*b, 75000, 4096);
    VSM_ASSERT(al == bl);
    VSM_ASSERT(ar == br);
}

VSM_TEST(the_fades_and_the_gain_of_a_warped_clip_still_apply) {
    auto source = pisteSinus(220.0, 1.0);
    auto piste = pisteAvecClip(vsm::sequencer::WarpMode::KeepPitch, 1.0, 1920);
    piste.clips[0].fadeInSeconds = 0.5;
    piste.clips[0].gain = 0.5f;
    source->clips = spansFromTrack(piste, 48000.0, enSecondes960);
    prepareWarpedSpans(*source);
    VSM_ASSERT_EQ(source->clips[0].fadeInFrames, int64_t(24000));
    auto [l, r] = jouer(*source, 96000, 512);
    double debut = 0.0, milieu = 0.0;
    for (size_t i = 1000; i < 3000; ++i) debut += l[i] * l[i];
    for (size_t i = 40000; i < 42000; ++i) milieu += l[i] * l[i];
    debut = std::sqrt(debut / 2000.0);
    milieu = std::sqrt(milieu / 2000.0);
    std::printf("    [banc moteur étiré] fondu de 0,5 s et gain 0,5 : rms à 40 ms %.4f, à 0,85 s %.4f\n",
                debut, milieu);
    VSM_ASSERT(debut < milieu * 0.2);
    VSM_ASSERT(milieu > 0.1 && milieu < 0.3);   // 0,5 × 0,354 = 0,177
}

// ---------------------------------------------------------------------------
// D13.1 — DEUX CLIPS QUI SE CHEVAUCHENT SE FONDENT, ILS NE S'ADDITIONNENT PAS.
// ---------------------------------------------------------------------------

VSM_TEST(overlapping_audio_clips_crossfade_instead_of_summing) {
    // Un fichier CONSTANT (0,5) : quelle que soit la fenêtre lue, la valeur
    // est la même, si bien que « la somme reste à un » se mesure au bit.
    auto source = std::make_shared<vsm::audio::engine::AudioTrackSource>();
    std::vector<float> l(48000 * 4, 0.5f), r(48000 * 4, 0.5f);
    source->setMemorySamples(std::move(l), std::move(r));
    vsm::sequencer::Track piste;
    piste.kind = vsm::sequencer::Track::Kind::Audio;
    piste.audio.path = "audio/prise.wav";
    piste.audio.sampleRate = 48000.0;
    piste.audio.frames = 48000 * 4;
    // Clip A : 0 -> 2 s ; clip B : 1 s -> 3 s. Une seconde de chevauchement.
    vsm::sequencer::Clip a, b;
    a.id = 1; a.startTick = 0; a.length = 1920; a.sourceLength = 1920;
    b.id = 2; b.startTick = 960; b.length = 1920; b.sourceLength = 1920; b.sourceStartSeconds = 1.0;
    piste.clips = {a, b};
    source->clips = spansFromTrack(piste, 48000.0, enSecondes960);
    VSM_ASSERT_EQ(source->clips.size(), size_t(2));
    VSM_ASSERT_EQ(source->clips[0].fadeOutFrames, int64_t(48000));
    VSM_ASSERT_EQ(source->clips[1].fadeInFrames, int64_t(48000));
    auto [ol, orr] = jouer(*source, 48000 * 3, 512);
    double pire = 0.0;
    for (size_t i = 0; i < ol.size(); ++i) pire = std::max(pire, std::abs(static_cast<double>(ol[i]) - 0.5));
    // Au milieu du chevauchement (1,5 s), chacun est à demi : la somme fait 0,5.
    std::printf("    [banc fondu enchaîné] niveau à 0,5 s %.4f, à 1,5 s %.4f, à 2,5 s %.4f ; pire écart à 0,5 : %.5f\n",
                ol[24000], ol[72000], ol[120000], pire);
    VSM_ASSERT(pire < 1e-4);

    // Un fondu réglé PLUS LONG que le chevauchement est gardé.
    piste.clips[0].fadeOutSeconds = 1.5;
    auto spans = spansFromTrack(piste, 48000.0, enSecondes960);
    VSM_ASSERT_EQ(spans[0].fadeOutFrames, int64_t(72000));
    VSM_ASSERT_EQ(spans[1].fadeInFrames, int64_t(48000));
}
