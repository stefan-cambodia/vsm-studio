#include "TestFramework.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include "vsm/sequencer/Project.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace vsm::sequencer;
using namespace vsm::audio::engine;
using namespace vsm::audio::plugin;

// D18.7b de docs/ROADMAP-daw.md — PUBLIER LES SORTIES D'UNE MACHINE SUR DES
// PISTES.
//
// D18.7a a donné aux machines la CAPACITÉ de rendre leurs sorties séparément
// et l'a vérifiée au bit près sur le TR-808. Il restait à ce que le graphe les
// PUBLIE : qu'une piste puisse dire « je porte la caisse claire », et recevoir
// son propre fader, son propre panoramique et ses propres inserts. C'est ce
// qui sert la PARITÉ des pistes -- la chaîne d'analyse sépare la grosse caisse
// de la caisse claire, et les rejouer sur une seule piste les recollait.

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBloc = 512;
constexpr double kDuree = 1.0;

/// Une mesure de boîte à rythmes où PLUSIEURS pièces sonnent ensemble : c'est
/// le seul cas où une somme mal faite se verrait.
void poserLaMesure(Track& piste) {
    uint64_t compteur = 1;
    const int notes[6] = {36, 38, 42, 46, 39, 56};
    for (int i = 0; i < 6; ++i)
        piste.addNote(static_cast<Tick>(i * 240), static_cast<Tick>(i * 240 + 120),
                       static_cast<uint8_t>(notes[i]), 100, 0, compteur);
}

Project projetUnePiste() {
    Project projet;
    projet.tracks.emplace_back();
    projet.tracks[0].name = "TR-808";
    projet.tracks[0].instrumentId = "vsm.tr808";
    poserLaMesure(projet.tracks[0]);
    return projet;
}

/// Le MÊME projet, plus cinq pistes qui publient les sorties 1 à 5. La sortie
/// 0 reste sur la piste qui porte la machine : c'est le contrat.
Project projetPublie(int combien = 5) {
    Project projet = projetUnePiste();
    for (int k = 1; k <= combien; ++k) {
        projet.tracks.emplace_back();
        Track& piste = projet.tracks.back();
        piste.name = "sortie " + std::to_string(k);
        piste.outputSourceTrack = 0;
        piste.outputIndex = k;
    }
    return projet;
}

RenderedAudio rendre(const Project& projet) {
    ProcessGraph graphe;
    graphe.prepare(kSampleRate, kBloc);
    for (size_t t = 0; t < projet.tracks.size(); ++t)
        if (!projet.tracks[t].instrumentId.empty())
            graphe.setTrackInstrument(t, projet.tracks[t].instrumentId);
    graphe.setProject(projet);
    return OfflineRenderer::render(graphe, kSampleRate, kBloc, kDuree);
}

double ecartMaximal(const RenderedAudio& a, const RenderedAudio& b) {
    const size_t n = std::min(a.left.size(), b.left.size());
    double pire = 0.0;
    for (size_t i = 0; i < n; ++i) {
        pire = std::max(pire, std::abs(static_cast<double>(a.left[i]) - b.left[i]));
        pire = std::max(pire, std::abs(static_cast<double>(a.right[i]) - b.right[i]));
    }
    return pire;
}

double crete(const RenderedAudio& r) {
    double pic = 0.0;
    for (float v : r.left) pic = std::max(pic, std::abs(static_cast<double>(v)));
    for (float v : r.right) pic = std::max(pic, std::abs(static_cast<double>(v)));
    return pic;
}

} // namespace

VSM_TEST(publishing_the_outputs_on_tracks_does_not_change_what_one_hears) {
    vsm::audio::plugin::registerBuiltInPlugins();

    const auto temoin = rendre(projetUnePiste());
    const auto publie = rendre(projetPublie());

    // LA MESURE PORTE SUR DU SON, sans quoi « aucun écart » ne dirait rien :
    // deux silences sont toujours d'accord.
    const double pic = crete(temoin);
    VSM_ASSERT(pic > 0.05);

    const double ecart = ecartMaximal(temoin, publie);
    const double relatif = ecart / pic;
    std::printf("  [D18.7b] crete %.3f, ecart max %.3e (%.1f dBFS relatif)\n",
                pic, ecart, 20.0 * std::log10(std::max(relatif, 1e-30)));

    // LE CRITÈRE ÉCRIT D'AVANCE (feuille de route, D18.7b) : l'écart ne peut
    // pas être nul -- chaque piste traverse son propre fader et sa propre loi
    // de panoramique, donc le mélange calcule Σ(vₖ·g) là où la piste unique
    // calculait (Σvₖ)·g, et la multiplication flottante n'est pas distributive
    // sur l'addition. Il doit en revanche rester à hauteur d'ARRONDI : sous
    // -120 dBFS relatif. Au-delà, ce n'est plus de l'arrondi, c'est une erreur
    // de routage.
    VSM_ASSERT(relatif < 1.0e-6);
}

VSM_TEST(a_project_where_nobody_publishes_takes_exactly_the_path_it_always_took) {
    vsm::audio::plugin::registerBuiltInPlugins();
    // AU BIT PRÈS, et pas « à peu près » : tant qu'aucune piste ne publie, la
    // table de routage n'est même pas allouée et le rendu doit être celui
    // d'avant l'étape. C'est ce qui rend l'addition sans risque pour les
    // projets qui existent.
    const auto a = rendre(projetUnePiste());
    const auto b = rendre(projetUnePiste());
    VSM_ASSERT_EQ(ecartMaximal(a, b), 0.0);
}

VSM_TEST(each_published_track_carries_its_own_piece_and_not_the_others) {
    vsm::audio::plugin::registerBuiltInPlugins();
    // CE QUI PROUVE QUE LA PUBLICATION SÉPARE VRAIMENT : on coupe toutes les
    // pistes sauf une, et ce qui sort doit être la pièce de CETTE sortie --
    // pas le mélange, pas le silence.
    //
    // La sortie 1 du TR-808 est la caisse claire (note 38), rendue seule ici :
    // le résultat doit être NON NUL et DIFFÉRENT du rendu de la sortie 2
    // (charley fermé), sans quoi les six pistes porteraient la même chose.
    auto seule = [](int sortie) {
        Project projet = projetPublie();
        for (size_t t = 0; t < projet.tracks.size(); ++t)
            projet.tracks[t].muted = !(projet.tracks[t].outputIndex == sortie
                                       && projet.tracks[t].publishesInstrumentOutput());
        return rendre(projet);
    };
    const auto caisseClaire = seule(1);
    const auto charley = seule(2);
    VSM_ASSERT(crete(caisseClaire) > 0.001);
    VSM_ASSERT(crete(charley) > 0.001);
    VSM_ASSERT(ecartMaximal(caisseClaire, charley) > 0.001);
}

VSM_TEST(the_sum_of_the_published_tracks_is_the_whole_machine) {
    vsm::audio::plugin::registerBuiltInPlugins();
    // L'AUTRE MOITIÉ DE LA PREUVE : chaque piste ne porte pas seulement
    // QUELQUE CHOSE de différent, elle porte SA part -- la somme des six
    // rendus solo doit refaire le rendu complet.
    auto seule = [](int sortie) {
        Project projet = projetPublie();
        for (size_t t = 0; t < projet.tracks.size(); ++t) {
            const bool cestElle = projet.tracks[t].publishesInstrumentOutput()
                                      ? projet.tracks[t].outputIndex == sortie
                                      : sortie == 0;
            projet.tracks[t].muted = !cestElle;
        }
        return rendre(projet);
    };
    const auto complet = rendre(projetPublie());
    std::vector<double> sommeL(complet.left.size(), 0.0), sommeR(complet.right.size(), 0.0);
    for (int k = 0; k <= 5; ++k) {
        const auto part = seule(k);
        for (size_t i = 0; i < sommeL.size() && i < part.left.size(); ++i) {
            sommeL[i] += part.left[i];
            sommeR[i] += part.right[i];
        }
    }
    double pire = 0.0, pic = 0.0;
    for (size_t i = 0; i < sommeL.size(); ++i) {
        pire = std::max(pire, std::abs(sommeL[i] - complet.left[i]));
        pire = std::max(pire, std::abs(sommeR[i] - complet.right[i]));
        pic = std::max(pic, std::abs(static_cast<double>(complet.left[i])));
    }
    std::printf("  [D18.7b] somme des six solos : ecart max %.3e sur une crete de %.3f\n",
                pire, pic);
    VSM_ASSERT(pic > 0.05);
    VSM_ASSERT(pire / pic < 1.0e-6);
}

VSM_TEST(an_output_that_does_not_exist_is_silent_AND_counted) {
    vsm::audio::plugin::registerBuiltInPlugins();
    // PANNE MUETTE INTERDITE. Une piste qui réclame la sortie n° 9 d'une
    // machine qui n'en a que six sort silencieuse -- c'est inévitable -- mais
    // le moteur le COMPTE. Sans ce compteur, le seul symptôme serait une piste
    // qui ne fait pas de bruit, et c'est le genre de silence qu'on met une
    // soirée à ne pas comprendre.
    Project projet = projetUnePiste();
    projet.tracks.emplace_back();
    projet.tracks[1].name = "sortie inexistante";
    projet.tracks[1].outputSourceTrack = 0;
    projet.tracks[1].outputIndex = 9;

    ProcessGraph graphe;
    graphe.prepare(kSampleRate, kBloc);
    graphe.setTrackInstrument(0, "vsm.tr808");
    graphe.setProject(projet);
    OfflineRenderer::render(graphe, kSampleRate, kBloc, kDuree);
    VSM_ASSERT(graphe.droppedInstrumentOutputs() > 0);
}

VSM_TEST(a_publication_that_points_nowhere_is_refused_and_counted) {
    vsm::audio::plugin::registerBuiltInPlugins();
    // Les trois refus qui protègent l'ordre de rendu : une source hors du
    // projet, une piste qui se publie elle-même, et une chaîne de publications
    // (publier la sortie d'une piste qui publie déjà) -- cette dernière n'a pas
    // d'ordre de rendu qui finisse.
    for (int cas = 0; cas < 3; ++cas) {
        Project projet = projetUnePiste();
        projet.tracks.emplace_back();
        projet.tracks[1].outputSourceTrack = cas == 0 ? 42 : (cas == 1 ? 1 : 2);
        projet.tracks[1].outputIndex = 1;
        if (cas == 2) {
            projet.tracks.emplace_back();
            projet.tracks[2].outputSourceTrack = 0;
            projet.tracks[2].outputIndex = 1;
        }
        ProcessGraph graphe;
        graphe.prepare(kSampleRate, kBloc);
        graphe.setTrackInstrument(0, "vsm.tr808");
        graphe.setProject(projet);
        OfflineRenderer::render(graphe, kSampleRate, kBloc, 0.2);
        VSM_ASSERT(graphe.droppedInstrumentOutputs() > 0);
    }
}

VSM_TEST(two_tracks_asking_for_the_same_output_share_it_rather_than_double_it) {
    vsm::audio::plugin::registerBuiltInPlugins();
    // Deux pistes qui réclament la MÊME sortie lisent le même tampon : la
    // machine ne la rend qu'une fois, et l'on entend donc cette pièce DEUX
    // FOIS plus fort -- ce qui est le comportement attendu de deux tranches
    // portant le même signal, et non un doublement du calcul.
    Project projet = projetUnePiste();
    for (int i = 0; i < 2; ++i) {
        projet.tracks.emplace_back();
        projet.tracks.back().outputSourceTrack = 0;
        projet.tracks.back().outputIndex = 1;
        projet.tracks.back().muted = false;
    }
    // On coupe la piste porteuse : ne restent que les deux copies.
    projet.tracks[0].muted = true;

    Project une = projet;
    une.tracks.pop_back();

    ProcessGraph g1;
    g1.prepare(kSampleRate, kBloc);
    g1.setTrackInstrument(0, "vsm.tr808");
    g1.setProject(une);
    const auto simple = OfflineRenderer::render(g1, kSampleRate, kBloc, kDuree);

    ProcessGraph g2;
    g2.prepare(kSampleRate, kBloc);
    g2.setTrackInstrument(0, "vsm.tr808");
    g2.setProject(projet);
    const auto double_ = OfflineRenderer::render(g2, kSampleRate, kBloc, kDuree);

    VSM_ASSERT(crete(simple) > 0.001);
    VSM_ASSERT_EQ(g2.droppedInstrumentOutputs(), uint64_t(0));
    // Deux fois le même signal : la crête double, à l'arrondi près.
    const double rapport = crete(double_) / crete(simple);
    std::printf("  [D18.7b] deux pistes sur la meme sortie : rapport de crete %.4f\n", rapport);
    VSM_ASSERT(std::abs(rapport - 2.0) < 1.0e-4);
}
