#include "TestFramework.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include "vsm/sequencer/Project.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace vsm::sequencer;
using namespace vsm::audio::engine;

// D18.5 de docs/ROADMAP-daw.md — LA VITESSE DE LECTURE (varispeed).
//
// Deux tentatives ont échoué avant celle-ci, et la feuille de route dit
// pourquoi : le facteur avait été posé sur l'avance de BLOC, alors que
// `renderSpan` découpe chaque bloc en sous-segments qui recalculent leur
// position à raison d'un échantillon pour un échantillon. Ralentir la pendule
// sans ralentir la trotteuse ne ralentit rien.

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBloc = 512;

/// Une seule frappe de grosse caisse, posée à UNE SECONDE.
Project projetUneImpulsion() {
    Project projet;
    projet.tracks.emplace_back();
    projet.tracks[0].instrumentId = "vsm.tr808";
    uint64_t compteur = 1;
    // 120 BPM, 480 ticks la noire : une seconde = deux noires = 960 ticks.
    projet.tracks[0].addNote(960, 1080, 36, 110, 0, compteur);
    return projet;
}

RenderedAudio rendreA(double vitesse, double duree) {
    vsm::audio::plugin::registerBuiltInPlugins();
    ProcessGraph graphe;
    graphe.prepare(kSampleRate, kBloc);
    graphe.setTrackInstrument(0, "vsm.tr808");
    graphe.setProject(projetUneImpulsion());
    graphe.setPlaybackSpeed(vitesse);
    return OfflineRenderer::render(graphe, kSampleRate, kBloc, duree);
}

/// L'instant du premier échantillon qui dépasse le dixième de la crête : une
/// attaque de grosse caisse est franche, et ce seuil ne dépend pas du niveau.
double instantDeLAttaque(const RenderedAudio& rendu) {
    double pic = 0.0;
    for (float v : rendu.left) pic = std::max(pic, std::abs(static_cast<double>(v)));
    if (pic <= 0.0) return -1.0;
    for (size_t i = 0; i < rendu.left.size(); ++i)
        if (std::abs(static_cast<double>(rendu.left[i])) > pic * 0.1)
            return static_cast<double>(i) / kSampleRate;
    return -1.0;
}

} // namespace

VSM_TEST(at_half_speed_a_pulse_placed_at_one_second_comes_out_at_two) {
    // LE CRITÈRE DE L'ÉTAPE, mot pour mot depuis la feuille de route.
    const double normal = instantDeLAttaque(rendreA(1.0, 3.0));
    const double moitie = instantDeLAttaque(rendreA(0.5, 4.0));
    const double doublee = instantDeLAttaque(rendreA(2.0, 2.0));
    std::printf("  [D18.5] attaque : x1 %.4f s | x0,5 %.4f s | x2 %.4f s\n",
                normal, moitie, doublee);

    VSM_ASSERT(normal > 0.0);
    VSM_ASSERT_NEAR(normal, 1.0, 0.005);
    VSM_ASSERT_NEAR(moitie, 2.0, 0.005);
    VSM_ASSERT_NEAR(doublee, 0.5, 0.005);
}

VSM_TEST(speed_one_renders_bit_for_bit_what_it_rendered_before) {
    // CE QUI PROTÈGE TOUT LE RESTE : à 1,0, la vitesse ne doit pas exister.
    // Les expressions sont écrites `x * vitesse / sampleRate_` précisément
    // pour que la multiplication par 1,0 -- exacte en IEEE 754 -- laisse
    // l'arithmétique inchangée. Un facteur posé autrement (par exemple
    // `x * (vitesse / sampleRate_)`) changerait l'arrondi et déplacerait des
    // échantillons sans rien annoncer.
    const auto a = rendreA(1.0, 1.5);
    ProcessGraph temoin;
    vsm::audio::plugin::registerBuiltInPlugins();
    temoin.prepare(kSampleRate, kBloc);
    temoin.setTrackInstrument(0, "vsm.tr808");
    temoin.setProject(projetUneImpulsion());
    // Vitesse JAMAIS touchée : le graphe est dans son état d'usine.
    const auto b = OfflineRenderer::render(temoin, kSampleRate, kBloc, 1.5);

    VSM_ASSERT_EQ(a.left.size(), b.left.size());
    double pire = 0.0;
    for (size_t i = 0; i < a.left.size(); ++i) {
        pire = std::max(pire, std::abs(static_cast<double>(a.left[i]) - b.left[i]));
        pire = std::max(pire, std::abs(static_cast<double>(a.right[i]) - b.right[i]));
    }
    VSM_ASSERT_EQ(pire, 0.0);
}

VSM_TEST(the_speed_is_bounded_rather_than_left_to_produce_nonsense) {
    // Une vitesse nulle ou négative ferait une lecture qui n'avance pas, ou
    // qui recule : le moteur borne plutôt que de la subir, et le dit par son
    // accesseur.
    ProcessGraph graphe;
    graphe.prepare(kSampleRate, kBloc);
    VSM_ASSERT_NEAR(graphe.playbackSpeed(), 1.0, 1e-12);
    graphe.setPlaybackSpeed(0.0);
    VSM_ASSERT_NEAR(graphe.playbackSpeed(), 0.25, 1e-12);
    graphe.setPlaybackSpeed(-3.0);
    VSM_ASSERT_NEAR(graphe.playbackSpeed(), 0.25, 1e-12);
    graphe.setPlaybackSpeed(99.0);
    VSM_ASSERT_NEAR(graphe.playbackSpeed(), 4.0, 1e-12);
}

VSM_TEST(the_pitch_of_a_computed_instrument_does_not_change_with_the_speed) {
    // CE QUI DISTINGUE UN VARISPEED D'UNE BANDE : ce que le moteur CALCULE
    // suit l'horloge mais garde sa hauteur -- un oscillateur à 440 Hz reste à
    // 440 Hz, il dure simplement plus longtemps. Ce sont les fichiers LUS qui
    // changent de hauteur, et c'est mesuré ailleurs.
    vsm::audio::plugin::registerBuiltInPlugins();
    auto rendre = [](double vitesse) {
        Project projet;
        projet.tracks.emplace_back();
        projet.tracks[0].instrumentId = "vsm.minimoog";
        uint64_t compteur = 1;
        projet.tracks[0].addNote(0, 1920, 69, 100, 0, compteur);   // A4 = 440 Hz
        ProcessGraph g;
        g.prepare(kSampleRate, kBloc);
        g.setTrackInstrument(0, "vsm.minimoog");
        g.setProject(projet);
        g.setPlaybackSpeed(vitesse);
        return OfflineRenderer::render(g, kSampleRate, kBloc, 1.0);
    };
    auto hauteur = [](const RenderedAudio& r) {
        // Autocorrélation, la même méthode que test_control_events : compter
        // les passages par zéro ment dès qu'un filtre coupe des harmoniques.
        const size_t debut = static_cast<size_t>(kSampleRate * 0.2);
        if (r.left.size() <= debut + 2000) return 0.0;
        const size_t n = r.left.size() - debut;
        const float* s = r.left.data() + debut;
        const auto lagMin = static_cast<size_t>(kSampleRate / 2000.0);
        const auto lagMax = std::min(n / 2, static_cast<size_t>(kSampleRate / 50.0));
        double meilleur = -1.0; size_t meilleurLag = lagMin;
        for (size_t lag = lagMin; lag <= lagMax; ++lag) {
            double somme = 0.0;
            for (size_t i = 0; i + lag < n; ++i) somme += static_cast<double>(s[i]) * s[i + lag];
            if (somme > meilleur) { meilleur = somme; meilleurLag = lag; }
        }
        return kSampleRate / static_cast<double>(meilleurLag);
    };
    const double f1 = hauteur(rendre(1.0));
    const double fLent = hauteur(rendre(0.5));
    std::printf("  [D18.5] hauteur d'un instrument calcule : x1 %.1f Hz | x0,5 %.1f Hz\n", f1, fLent);
    VSM_ASSERT(f1 > 100.0);
    // La MÊME hauteur, à 2 % près : c'est la définition retenue.
    VSM_ASSERT(std::abs(fLent - f1) / f1 < 0.02);
}

// ---------------------------------------------------------------------------
// L'AUTRE MOITIÉ : LE FICHIER LU.
//
// Le second examen de D18.5 l'avait isolée : `AudioTrackSource::mixInto` lit
// des trames CONSÉCUTIVES, et ralentir l'horloge ne ralentit pas cette
// lecture. Un varispeed audio n'est pas un décalage de position, c'est un
// changement de PAS -- donc un rééchantillonnage.

namespace {

using vsm::audio::engine::AudioClipSpan;
using vsm::audio::engine::AudioTrackSource;

std::shared_ptr<AudioTrackSource> sinusDUneSeconde(double hz) {
    auto source = std::make_shared<AudioTrackSource>();
    const auto n = static_cast<size_t>(kSampleRate * 2.0);
    std::vector<float> l(n), r(n);
    for (size_t i = 0; i < n; ++i) {
        l[i] = 0.5f * static_cast<float>(
                          std::sin(2.0 * M_PI * hz * static_cast<double>(i) / kSampleRate));
        r[i] = l[i];
    }
    source->setMemorySamples(std::move(l), std::move(r));
    AudioClipSpan span;
    span.startFrame = 0;
    span.lengthFrames = static_cast<int64_t>(kSampleRate * 2.0);
    source->clips.push_back(span);
    return source;
}

/// Hauteur par autocorrélation, sur la partie stable du rendu.
double hauteurDe(const std::vector<float>& signal, size_t depuis, size_t combien) {
    if (signal.size() < depuis + combien) return 0.0;
    const float* s = signal.data() + depuis;
    const auto lagMin = static_cast<size_t>(kSampleRate / 2000.0);
    const auto lagMax = std::min(combien / 2, static_cast<size_t>(kSampleRate / 50.0));
    if (lagMax <= lagMin) return 0.0;
    double meilleur = -1.0;
    size_t meilleurLag = lagMin;
    for (size_t lag = lagMin; lag <= lagMax; ++lag) {
        double somme = 0.0;
        for (size_t i = 0; i + lag < combien; ++i) somme += static_cast<double>(s[i]) * s[i + lag];
        if (somme > meilleur) { meilleur = somme; meilleurLag = lag; }
    }
    return kSampleRate / static_cast<double>(meilleurLag);
}

RenderedAudio rendreLeFichierA(double vitesse, double duree) {
    Project projet;
    projet.tracks.emplace_back();
    projet.tracks[0].kind = Track::Kind::Audio;
    ProcessGraph graphe;
    graphe.prepare(kSampleRate, kBloc);
    graphe.setProject(projet);
    graphe.setTrackAudio(0, sinusDUneSeconde(440.0));
    graphe.setPlaybackSpeed(vitesse);
    return OfflineRenderer::render(graphe, kSampleRate, kBloc, duree);
}

} // namespace

VSM_TEST(a_file_read_at_half_speed_drops_an_octave_like_a_tape) {
    // LA DÉFINITION D'UN VARISPEED, et ce qui le distingue d'un étirement :
    // 440 Hz lus à mi-vitesse font 220 Hz. Si le moteur se contentait de
    // décaler la position, la hauteur ne bougerait pas -- et l'on aurait un
    // varispeed qui n'en est pas un.
    const double normal = hauteurDe(rendreLeFichierA(1.0, 1.0).left,
                                     static_cast<size_t>(kSampleRate * 0.2),
                                     static_cast<size_t>(kSampleRate * 0.5));
    const double lent = hauteurDe(rendreLeFichierA(0.5, 1.5).left,
                                   static_cast<size_t>(kSampleRate * 0.2),
                                   static_cast<size_t>(kSampleRate * 0.5));
    const double vite = hauteurDe(rendreLeFichierA(2.0, 0.6).left,
                                   static_cast<size_t>(kSampleRate * 0.1),
                                   static_cast<size_t>(kSampleRate * 0.3));
    std::printf("  [D18.5] fichier a 440 Hz : x1 %.1f Hz | x0,5 %.1f Hz | x2 %.1f Hz\n",
                normal, lent, vite);
    VSM_ASSERT_NEAR(normal, 440.0, 6.0);
    VSM_ASSERT_NEAR(lent, 220.0, 5.0);
    VSM_ASSERT_NEAR(vite, 880.0, 12.0);
}

VSM_TEST(a_file_at_speed_one_is_read_bit_for_bit_by_the_old_path) {
    // `mixIntoAtSpeed` DÉLÈGUE à `mixInto` quand la vitesse vaut un : ce n'est
    // pas une approximation qui se trouve juste, c'est le même code.
    auto source = sinusDUneSeconde(440.0);
    const int n = 4096;
    std::vector<float> aL(n, 0.0f), aR(n, 0.0f), bL(n, 0.0f), bR(n, 0.0f);
    vsm::audio::dsp::SincResampler noyau;
    noyau.prepare(1.0);
    source->mixInto(aL.data(), aR.data(), 1000, n);
    source->mixIntoAtSpeed(bL.data(), bR.data(), 1000.0, n, 1.0, noyau);
    double pire = 0.0;
    for (int i = 0; i < n; ++i) {
        pire = std::max(pire, std::abs(static_cast<double>(aL[i]) - bL[i]));
        pire = std::max(pire, std::abs(static_cast<double>(aR[i]) - bR[i]));
    }
    VSM_ASSERT_EQ(pire, 0.0);
}

VSM_TEST(a_file_read_slowly_lasts_proportionally_longer) {
    // ET IL DURE PLUS LONGTEMPS : un clip de deux secondes lu à mi-vitesse
    // occupe quatre secondes de sortie. Sans quoi on aurait changé la hauteur
    // sans changer la vitesse, ce qui est un transpositeur, pas un varispeed.
    auto derniereSortie = [](const RenderedAudio& r) {
        for (size_t i = r.left.size(); i > 0; --i)
            if (std::abs(static_cast<double>(r.left[i - 1])) > 1.0e-4)
                return static_cast<double>(i - 1) / kSampleRate;
        return -1.0;
    };
    const double fin1 = derniereSortie(rendreLeFichierA(1.0, 3.0));
    const double finLent = derniereSortie(rendreLeFichierA(0.5, 6.0));
    std::printf("  [D18.5] fin du clip de 2 s : x1 %.3f s | x0,5 %.3f s\n", fin1, finLent);
    VSM_ASSERT_NEAR(fin1, 2.0, 0.02);
    VSM_ASSERT_NEAR(finLent, 4.0, 0.02);
}
