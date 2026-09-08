#include "TestFramework.h"
#include "vsm/audio/engine/AudioTrackSource.h"
#include "vsm/sequencer/Track.h"
#include <algorithm>
#include <cmath>
#include <vector>

// LE BANC DE D54 — TRANSPOSER UN CLIP AUDIO, écrit AVANT la première mesure.
//
// L'élément que D21 avait reporté (« demande un rendu différent dans le
// moteur »), puis D22, D23 et D24 après lui, chaque fois parce qu'une campagne
// tournait et qu'on ne recompile pas `vsm-render` pendant qu'elle court.
//
// CE QUE LA TRANSPOSITION DOIT FAIRE, et c'est tout ce qui la distingue du
// mode « vinyle » (`Repitch`) qui existe depuis D12 :
//
//   1. la HAUTEUR change du facteur demandé — +12 demi-tons double la
//      fréquence, -12 la divise, +7 la multiplie par 1,4983 ;
//   2. la DURÉE ne bouge pas — c'est là que `Repitch` échoue par construction ;
//   3. zéro demi-ton laisse le clip sur le chemin de lecture d'avant, au bit
//      près : un réglage neutre qui change le son est un réglage cassé ;
//   4. le rendu est INDÉPENDANT DE LA TAILLE DE BLOC (invariant n° 3), sans
//      quoi le temps réel et `vsm-render` ne rendraient pas le même fichier.

namespace {

using namespace vsm::audio::engine;
using vsm::sequencer::Track;
using vsm::sequencer::Clip;
constexpr double kSr = 48000.0;

/// Une piste audio d'un seul clip, portant un sinus.
Track pisteAvecSinus(double hz, double secondes, double demiTons) {
    Track piste;
    piste.kind = Track::Kind::Audio;
    piste.name = "Banc D54";
    piste.audio.path = "banc.wav";
    piste.audio.sampleRate = kSr;
    piste.audio.frames = static_cast<int64_t>(secondes * kSr);
    piste.audio.channels = 2;
    Clip clip;
    clip.startTick = 0;
    clip.length = 0;                  // jusqu'au bout du matériau
    clip.pitchSemitones = demiTons;
    piste.clips.push_back(clip);
    (void)hz;
    return piste;
}

std::shared_ptr<MemorySampleStore> sinus(double hz, double secondes) {
    const auto n = static_cast<size_t>(secondes * kSr);
    std::vector<float> l(n), r(n);
    for (size_t i = 0; i < n; ++i) {
        l[i] = 0.5f * static_cast<float>(std::sin(2.0 * M_PI * hz * static_cast<double>(i) / kSr));
        r[i] = l[i];
    }
    return std::make_shared<MemorySampleStore>(std::move(l), std::move(r));
}

/// La piste publiée comme l'application le fait : portées, miroir, hauteur,
/// étireur.
AudioTrackSource publier(const Track& piste, std::shared_ptr<const SampleStore> magasin) {
    AudioTrackSource source;
    source.samples = std::move(magasin);
    source.clips = spansFromTrack(piste, kSr,
                                   [](int64_t tick) { return static_cast<double>(tick) / 960.0 * 0.5; },
                                   vsm::sequencer::FadeShape::EqualPower);
    prepareWarpedSpans(source);
    return source;
}

std::vector<float> rendre(const AudioTrackSource& source, int64_t trames, int bloc) {
    std::vector<float> g(static_cast<size_t>(trames), 0.0f), d(static_cast<size_t>(trames), 0.0f);
    for (int64_t p = 0; p < trames; p += bloc) {
        const int n = static_cast<int>(std::min<int64_t>(bloc, trames - p));
        source.mixInto(g.data() + p, d.data() + p, p, n);
    }
    return g;
}

double magnitudeAt(const std::vector<float>& x, size_t from, size_t count, double hz) {
    double re = 0.0, im = 0.0, norm = 0.0;
    for (size_t i = 0; i < count && from + i < x.size(); ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / static_cast<double>(count));
        const double ph = 2.0 * M_PI * hz * static_cast<double>(i) / kSr;
        re += w * static_cast<double>(x[from + i]) * std::cos(ph);
        im += w * static_cast<double>(x[from + i]) * std::sin(ph);
        norm += w;
    }
    return std::sqrt(re * re + im * im) / std::max(1.0, norm);
}

/// La fréquence du pic entre `lo` et `hi`, au dixième de hertz.
double picHz(const std::vector<float>& x, size_t from, size_t count, double lo, double hi) {
    double meilleur = lo, m = -1.0;
    for (double f = lo; f <= hi; f += 0.1) {
        const double v = magnitudeAt(x, from, count, f);
        if (v > m) { m = v; meilleur = f; }
    }
    return meilleur;
}

double rms(const std::vector<float>& x, size_t from, size_t count) {
    double s = 0.0; size_t n = 0;
    for (size_t i = from; i < from + count && i < x.size(); ++i) { s += x[i] * x[i]; ++n; }
    return n ? std::sqrt(s / static_cast<double>(n)) : 0.0;
}

/// L'écart en CENTS entre deux fréquences — l'unité dans laquelle une erreur
/// de hauteur se juge, et la seule qui ait le même sens à 220 et à 880 Hz.
double cents(double mesuree, double attendue) {
    return 1200.0 * std::log2(mesuree / attendue);
}

} // namespace

/// BANC 1 : la hauteur suit le demi-ton demandé, et la durée ne bouge pas.
VSM_TEST(a_clip_transposed_up_an_octave_doubles_its_pitch_and_keeps_its_length) {
    const auto magasin = sinus(440.0, 3.0);
    const Track piste = pisteAvecSinus(440.0, 3.0, 12.0);
    const AudioTrackSource source = publier(piste, magasin);
    VSM_ASSERT_EQ(source.clips.size(), size_t(1));

    // LA DURÉE EST CELLE DU MATÉRIAU, pas celle du matériau divisé par deux :
    // c'est exactement ce que `Repitch` ne sait pas faire.
    VSM_ASSERT_EQ(source.clips[0].lengthFrames, static_cast<int64_t>(3.0 * kSr));

    const auto rendu = rendre(source, static_cast<int64_t>(3.0 * kSr), 512);
    // La fenêtre d'analyse est prise au MILIEU : les bords portent le fondu de
    // sécurité et l'amorçage du vocodeur, et mesurer là dirait autre chose.
    const double pic = picHz(rendu, static_cast<size_t>(1.0 * kSr), 16384, 700.0, 1100.0);
    VSM_ASSERT(std::abs(cents(pic, 880.0)) < 15.0);
    // ET IL Y A DU SON : sans cette ligne, un rendu silencieux passerait le
    // test précédent en donnant le premier point balayé.
    VSM_ASSERT(rms(rendu, static_cast<size_t>(1.0 * kSr), 16384) > 0.05);
}

VSM_TEST(a_clip_transposed_down_an_octave_halves_its_pitch) {
    const auto magasin = sinus(440.0, 3.0);
    const AudioTrackSource source = publier(pisteAvecSinus(440.0, 3.0, -12.0), magasin);
    const auto rendu = rendre(source, static_cast<int64_t>(3.0 * kSr), 512);
    const double pic = picHz(rendu, static_cast<size_t>(1.0 * kSr), 16384, 150.0, 350.0);
    VSM_ASSERT(std::abs(cents(pic, 220.0)) < 15.0);
    VSM_ASSERT(rms(rendu, static_cast<size_t>(1.0 * kSr), 16384) > 0.05);
}

/// BANC 2 : une quinte, c'est-à-dire un rapport qui n'est PAS une puissance de
/// deux. Un rapport entier passerait par un chemin trop simple pour prouver
/// quoi que ce soit.
VSM_TEST(a_clip_transposed_a_fifth_lands_on_the_tempered_ratio) {
    const auto magasin = sinus(440.0, 3.0);
    const AudioTrackSource source = publier(pisteAvecSinus(440.0, 3.0, 7.0), magasin);
    const auto rendu = rendre(source, static_cast<int64_t>(3.0 * kSr), 512);
    const double attendue = 440.0 * std::pow(2.0, 7.0 / 12.0);   // 659,26 Hz
    const double pic = picHz(rendu, static_cast<size_t>(1.0 * kSr), 16384, 550.0, 800.0);
    VSM_ASSERT(std::abs(cents(pic, attendue)) < 15.0);
}

/// BANC 3 : zéro demi-ton ne touche à RIEN — ni au chemin, ni à un échantillon.
/// Un réglage neutre qui change le son est un réglage cassé, et c'est la
/// première chose qu'on vérifie d'un réglage qui s'éteint.
VSM_TEST(a_clip_at_zero_semitones_is_the_untouched_path_bit_for_bit) {
    const auto magasin = sinus(440.0, 1.0);
    const AudioTrackSource avec = publier(pisteAvecSinus(440.0, 1.0, 0.0), magasin);
    // Aucune portée transposée ne doit avoir reçu d'étireur ni de magasin
    // enveloppant : le chemin est celui d'avant D54, sans un test de plus.
    VSM_ASSERT(avec.clips[0].warp == nullptr);
    VSM_ASSERT(avec.clips[0].source == nullptr);

    const auto rendu = rendre(avec, static_cast<int64_t>(1.0 * kSr), 512);
    // Comparé au matériau lui-même, fondu de sécurité mis à part.
    float g = 0.0f, d = 0.0f;
    size_t compares = 0;
    for (size_t i = 2400; i < static_cast<size_t>(1.0 * kSr) - 2400; ++i) {
        VSM_ASSERT(magasin->frameAt(static_cast<int64_t>(i), g, d));
        VSM_ASSERT_EQ(rendu[i], g);
        ++compares;
    }
    VSM_ASSERT(compares > 40000);
}

/// BANC 4 : l'invariant n° 3. Le même clip transposé rend le MÊME signal à
/// 128, 512 et 2048 échantillons par bloc — sans quoi la lecture et
/// `vsm-render` ne produiraient pas le même fichier, ce qui est la propriété
/// sur laquelle tout le projet repose.
VSM_TEST(a_transposed_clip_renders_the_same_at_every_block_size) {
    const auto magasin = sinus(440.0, 2.0);
    const int64_t trames = static_cast<int64_t>(2.0 * kSr);
    const auto a = rendre(publier(pisteAvecSinus(440.0, 2.0, 5.0), magasin), trames, 512);
    const auto b = rendre(publier(pisteAvecSinus(440.0, 2.0, 5.0), magasin), trames, 128);
    const auto c = rendre(publier(pisteAvecSinus(440.0, 2.0, 5.0), magasin), trames, 2048);
    VSM_ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        VSM_ASSERT_EQ(a[i], b[i]);
        VSM_ASSERT_EQ(a[i], c[i]);
    }
}

/// BANC 5 : le mode « vinyle » garde sa hauteur de vinyle. `Repitch` dit « la
/// hauteur suit la durée » ; lui ajouter une hauteur indépendante demanderait
/// une seconde étape d'étirement, c'est-à-dire ce que ce mode existe pour
/// éviter. Le refus est une DÉCISION, elle est écrite, et elle est vérifiée
/// ici plutôt que supposée.
VSM_TEST(a_repitch_clip_ignores_the_transposition_by_design) {
    const auto magasin = sinus(440.0, 2.0);
    Track piste = pisteAvecSinus(440.0, 2.0, 12.0);
    piste.clips[0].warpMode = vsm::sequencer::WarpMode::Repitch;
    piste.clips[0].warpMarkers = {{0.0, 0}, {2.0, 3840}};
    piste.clips[0].length = 3840;
    const AudioTrackSource source = publier(piste, magasin);
    VSM_ASSERT_EQ(source.clips.size(), size_t(1));
    VSM_ASSERT(source.clips[0].warp != nullptr);
    VSM_ASSERT(source.clips[0].warp->repitch);
    // Le magasin n'a PAS été enveloppé : la transposition n'a rien fait.
    VSM_ASSERT(source.clips[0].source == nullptr);
}
