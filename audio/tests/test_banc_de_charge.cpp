#include "TestFramework.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/sequencer/Project.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace vsm::audio;
using namespace vsm::audio::engine;
using namespace vsm::sequencer;

// D8.4 de docs/ROADMAP-daw.md — LE BANC DE CHARGE, DANS LA SUITE DE TESTS.
//
// POURQUOI ICI ET PAS SEULEMENT DANS `audio/bench/`. Le banc CPU de la Phase 6
// est un exécutable à part, qu'on lance à la main quand on y pense. Il a servi
// exactement une fois par optimisation, ce qui veut dire qu'entre deux
// optimisations personne ne regardait : une régression de performance entrait
// dans le dépôt et n'en ressortait qu'au moment où quelqu'un se plaignait d'un
// clic. La suite de tests, elle, tourne à chaque fois.
//
// CE QU'UN TEST DE PERFORMANCE PEUT ET NE PEUT PAS AFFIRMER. Il ne peut pas
// dire « ce bloc coûte 0,42 ms » : la même ligne de code donne des chiffres
// différents selon le cœur, la fréquence, ce que fait le reste de la machine.
// Il PEUT dire deux choses, et ce sont les deux qui comptent :
//
//   1. **Des RAPPORTS**, qui ne dépendent d'aucune de ces variables. « Doubler
//      les pistes double le coût » et « la densité du planning ne coûte rien »
//      sont des propriétés de l'algorithme, pas de la machine. C'est là que
//      vivent les vraies régressions -- une quadratique qu'on introduit sans le
//      voir --, et un rapport les attrape sur n'importe quel ordinateur.
//   2. **Un CHIFFRE, exprimé en étalons**, c'est-à-dire en « combien
//      d'enveloppes ADSR coûte une piste ». Il ne prouve rien à lui seul, mais
//      il est comparable d'une exécution à l'autre, et il est IMPRIMÉ : c'est
//      lui que le critère de la phase appelle « chiffré et suivi ».
//
// Les seuils sont donc larges à dessein. Un test de performance qui échoue
// parce que la machine a éternué est un test qu'on finit par ignorer, et un
// test qu'on ignore ne protège rien.

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

/// Coût moyen, en nanosecondes, d'une opération DSP triviale et stable.
/// Mesuré JUSTE AVANT chaque ligne du banc, il capture l'état réel du cœur à
/// cet instant -- fréquence, throttling, cœur P ou E -- et permet d'exprimer
/// les coûts en unités qui ne dépendent pas de cet état. Même étalon que le
/// banc CPU de la Phase 6, pour que les deux chiffres se comparent.
double etalonNs() {
    dsp::AdsrEnvelope env;
    env.setSampleRate(kSampleRate);
    env.noteOn();
    constexpr int kIterations = 100000;
    float acc = 0.0f;
    for (int i = 0; i < 20000; ++i) acc += env.nextSample();   // rodage
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIterations; ++i) acc += env.nextSample();
    const auto t1 = std::chrono::steady_clock::now();
    volatile float puits = acc;
    (void)puits;
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / kIterations;
}

/// `trackCount` pistes jouant chacune un accord tenu, plus `planningNotes`
/// notes COURTES ET LOINTAINES par piste : celles-là ne sonnent presque jamais,
/// mais elles grossissent le planning que le rendu doit traverser. C'est la
/// dimension que le banc CPU ne mesurait pas, et une piste de batterie
/// reconstruite en porte quatre mille.
Project projetDeCharge(size_t trackCount, int planningNotes) {
    Project projet;
    const uint16_t ppq = 480;
    projet.ticksPerQuarterNote = ppq;
    uint64_t ids = 1;
    for (size_t t = 0; t < trackCount; ++t) {
        Track piste;
        piste.channel = static_cast<uint8_t>(t % 16);
        for (int n = 0; n < 4; ++n)
            piste.addNote(0, ppq * 64, static_cast<uint8_t>(48 + 4 * n), 100, 0, ids);
        for (int n = 0; n < planningNotes; ++n) {
            const vsm::midi::Tick debut =
                static_cast<vsm::midi::Tick>(ppq) * 128 + static_cast<vsm::midi::Tick>(n) * 8;
            piste.addNote(debut, debut + 4, static_cast<uint8_t>(48 + (n % 24)), 100, 0, ids);
        }
        projet.tracks.push_back(piste);
    }
    return projet;
}

/// Coût MÉDIAN d'un bloc, en millisecondes. La médiane et non la moyenne :
/// un seul bloc retardé par l'ordonnanceur suffirait à décaler une moyenne, et
/// ce banc-ci mesure le coût habituel, pas le pire cas (que le banc CPU de la
/// Phase 6 mesure, lui, avec sa colonne p99).
double coutMedianMs(size_t trackCount, int planningNotes, int blocs = 120) {
    ProcessGraph graphe;
    graphe.prepare(kSampleRate, kBlockSize);
    for (size_t t = 0; t < trackCount; ++t) graphe.setTrackInstrument(t, "vsm.testtone");
    graphe.setProject(projetDeCharge(trackCount, planningNotes));
    graphe.seekSeconds(0.0);
    graphe.setPlaying(true);

    std::vector<float> gauche(kBlockSize, 0.0f), droite(kBlockSize, 0.0f);
    for (int i = 0; i < 20; ++i) graphe.processBlock(gauche.data(), droite.data(), kBlockSize);

    std::vector<double> mesures;
    mesures.reserve(static_cast<size_t>(blocs));
    for (int b = 0; b < blocs; ++b) {
        const auto t0 = std::chrono::steady_clock::now();
        graphe.processBlock(gauche.data(), droite.data(), kBlockSize);
        const auto t1 = std::chrono::steady_clock::now();
        mesures.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::sort(mesures.begin(), mesures.end());
    return mesures[mesures.size() / 2];
}

} // namespace

// ---------------------------------------------------------------------------
// 1. LE CHIFFRE : combien coûte UNE piste.
// ---------------------------------------------------------------------------

VSM_TEST(load_bench_publishes_the_cost_of_one_track) {
    const double etalon = etalonNs();
    const double une = coutMedianMs(1, 0);
    const double seize = coutMedianMs(16, 0);
    // LE COÛT MARGINAL, et non le coût total divisé par le nombre de pistes :
    // un bloc porte des frais fixes -- le bus master, les mètres, le
    // métronome -- qui n'appartiennent à aucune piste et fausseraient la
    // division.
    const double parPisteMs = (seize - une) / 15.0;
    const double parPisteEtalons = parPisteMs * 1.0e6 / etalon;

    std::printf("\n  [banc de charge D8.4] étalon = %.2f ns/enveloppe\n", etalon);
    std::printf("  [banc de charge D8.4]  1 piste  : %.4f ms  (%.1f %% du budget)\n",
                une, 100.0 * une / (1000.0 * kBlockSize / kSampleRate));
    std::printf("  [banc de charge D8.4] 16 pistes : %.4f ms  (%.1f %% du budget)\n",
                seize, 100.0 * seize / (1000.0 * kBlockSize / kSampleRate));
    std::printf("  [banc de charge D8.4] COÛT PAR PISTE : %.4f ms = %.0f étalons\n",
                parPisteMs, parPisteEtalons);

    // Le seul garde-fou possible sur une valeur absolue : elle doit rester dans
    // un ordre de grandeur plausible. Une piste qui coûterait un quart du
    // budget d'un bloc à elle seule serait une régression que personne ne
    // pourrait rater ; une piste « gratuite » voudrait dire qu'on ne mesure
    // rien du tout.
    VSM_ASSERT(parPisteMs > 0.0);
    VSM_ASSERT(parPisteMs < 0.25 * (1000.0 * kBlockSize / kSampleRate));
}

// ---------------------------------------------------------------------------
// 2. LES RAPPORTS : ce qui ne dépend d'aucune machine.
// ---------------------------------------------------------------------------

VSM_TEST(the_cost_grows_LINEARLY_with_the_number_of_tracks) {
    // TRENTE-DEUX PISTES DOIVENT COÛTER DEUX FOIS SEIZE, et non quatre fois.
    // Un jour, quelqu'un écrira dans le rendu d'une piste une boucle sur toutes
    // les autres -- pour chercher une chaîne latérale, un groupe, un routage --
    // et personne ne le verra avant d'avoir un vrai projet sous la main. Ce
    // rapport-ci le verra tout de suite.
    const double une = coutMedianMs(1, 0);
    const double seize = coutMedianMs(16, 0);
    const double trenteDeux = coutMedianMs(32, 0);

    const double parPisteBas = (seize - une) / 15.0;
    const double parPisteHaut = (trenteDeux - seize) / 16.0;
    const double rapport = parPisteBas > 0.0 ? parPisteHaut / parPisteBas : 0.0;
    std::printf("  [banc de charge D8.4] coût marginal 1->16 : %.5f ms ; 16->32 : %.5f ms"
                "  (rapport %.2f)\n", parPisteBas, parPisteHaut, rapport);

    // Large à dessein : entre seize et trente-deux pistes, les caches se
    // remplissent et la piste marginale coûte un peu plus. Un rapport de deux
    // reste linéaire ; un rapport de dix ne l'est pas.
    VSM_ASSERT(rapport > 0.3);
    VSM_ASSERT(rapport < 3.0);
}

VSM_TEST(the_size_of_the_schedule_costs_nothing_to_play) {
    // LA RÉGRESSION QUI EXISTAIT VRAIMENT, ET QUE CE BANC A TROUVÉE.
    //
    // Le planning était trié par TEMPS, et chaque piste le parcourait EN ENTIER,
    // à chaque sous-segment, pour n'en garder que ce qui la concernait. Le coût
    // d'un bloc valait donc « pistes x événements ». Mesuré avant le
    // rangement par piste : trente-deux pistes de quatre mille notes coûtaient
    // 10,4 ms par bloc contre 3,9 ms à vide -- 99,5 % du budget, dont
    // l'essentiel passé à ÉCARTER des notes qui ne sonnaient pas encore.
    //
    // Ces notes-là sont à deux minutes de la tête de lecture. Les jouer ne
    // coûte rien, puisqu'on ne les joue pas. Le seul coût légitime est celui de
    // ne pas les regarder.
    const double vide = coutMedianMs(16, 0);
    const double dense = coutMedianMs(16, 4000);   // 64 000 événements au planning
    const double surcout = vide > 0.0 ? dense / vide : 0.0;
    std::printf("  [banc de charge D8.4] 16 pistes à vide : %.4f ms ; avec 4000 notes"
                " chacune : %.4f ms  (x%.2f)\n", vide, dense, surcout);

    // Un planning soixante-quatre mille fois plus fourni ne doit pas coûter
    // 50 % de plus. Avant le rangement par piste, le rapport était de 2,6.
    VSM_ASSERT(surcout < 1.5);
}

VSM_TEST(seeking_into_a_dense_schedule_stays_immediate) {
    // LE MÊME PIÈGE, DE L'AUTRE CÔTÉ : entrer dans le planning au milieu du
    // morceau doit se CHERCHER et non se PARCOURIR. Sans dichotomie, sauter à
    // la fin d'un morceau dense coûterait d'autant plus cher qu'on saute loin,
    // ce qui est exactement l'inverse de ce qu'on attend d'un DAW.
    ProcessGraph graphe;
    graphe.prepare(kSampleRate, kBlockSize);
    for (size_t t = 0; t < 16; ++t) graphe.setTrackInstrument(t, "vsm.testtone");
    const Project projet = projetDeCharge(16, 4000);
    graphe.setProject(projet);
    graphe.setPlaying(true);

    std::vector<float> gauche(kBlockSize, 0.0f), droite(kBlockSize, 0.0f);
    auto coutDepuis = [&](double secondes) {
        graphe.seekSeconds(secondes);
        for (int i = 0; i < 10; ++i) graphe.processBlock(gauche.data(), droite.data(), kBlockSize);
        std::vector<double> mesures;
        for (int b = 0; b < 80; ++b) {
            graphe.seekSeconds(secondes);
            const auto t0 = std::chrono::steady_clock::now();
            graphe.processBlock(gauche.data(), droite.data(), kBlockSize);
            const auto t1 = std::chrono::steady_clock::now();
            mesures.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
        std::sort(mesures.begin(), mesures.end());
        return mesures[mesures.size() / 2];
    };

    const double auDebut = coutDepuis(0.0);
    const double aLaFin = coutDepuis(projet.ticksToSeconds(projet.lastSoundingTick()) - 1.0);
    std::printf("  [banc de charge D8.4] bloc au début : %.4f ms ; bloc en fin de morceau :"
                " %.4f ms  (x%.2f)\n", auDebut, aLaFin, auDebut > 0.0 ? aLaFin / auDebut : 0.0);

    VSM_ASSERT(auDebut > 0.0);
    VSM_ASSERT(aLaFin < auDebut * 2.0);
}
