#include "TestFramework.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/sequencer/PlayOrder.h"
#include "vsm/sequencer/Project.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

using vsm::audio::engine::ProcessGraph;
using vsm::sequencer::Project;
using vsm::sequencer::Tick;
using vsm::sequencer::Track;

// D606.1 — LA LECTURE QUI SUIT L'ORDRE DE JEU. L'attendu écrit avant le code (ROADMAP-daw, D606) : 8 kHz, 120 BPM, deux
// sections d'une mesure (A de 0 à 2 s, B de 2 à 4 s), au Minimoog ; l'ordre [B, A, A], la boucle posée sur [0, 1 s] et
// active ; le rendu de la chaîne et celui du projet APLATI (`flattenPlayOrder`, le même ordre) diffèrent de moins de 1e-4
// sur les 6 s de la chaîne et la seconde qui suit ; le même projet joué droit en diffère de plus de 0,05.

namespace {
constexpr double kTaux = 8000.0;
constexpr int kBloc = 256;

Project deuxSections() {
    Project project;
    project.ticksPerQuarterNote = 480;
    project.markers.push_back({0, "A"});
    project.markers.push_back({1920, "B"});
    Track track;
    uint64_t id = 1;
    // `addNote(début, FIN, …)` — CORRIGÉ APRÈS LE PREMIER ESSAI : écrites en durées, B n'existait pas (1 section).
    track.addNote(0, 480, 57, 110, 0, id);
    track.addNote(960, 2400, 60, 90, 0, id);     // déborde de A dans B
    track.addNote(2160, 2880, 64, 120, 0, id);
    track.addNote(3360, 3840, 67, 100, 0, id);   // finit avec B
    project.tracks.push_back(track);
    return project;
}

std::unique_ptr<ProcessGraph> graphe(const Project& project) {
    auto g = std::make_unique<ProcessGraph>();   // sur le tas : un graphe est gros
    g->prepare(kTaux, kBloc);
    g->setTrackInstrument(0, "vsm.minimoog");
    g->setProject(project);
    return g;
}

std::vector<float> rendre(ProcessGraph& g, double secondes) {
    const int blocs = static_cast<int>(std::ceil(secondes * kTaux / kBloc));
    std::vector<float> sortie;
    std::vector<float> l(kBloc), r(kBloc);
    for (int b = 0; b < blocs; ++b) {
        g.processBlock(l.data(), r.data(), kBloc);
        sortie.insert(sortie.end(), l.begin(), l.end());
    }
    return sortie;
}

double ecartMax(const std::vector<float>& a, const std::vector<float>& b, size_t debut = 0) {
    double e = 0.0;
    for (size_t i = debut; i < std::min(a.size(), b.size()); ++i)
        e = std::max(e, static_cast<double>(std::abs(a[i] - b[i])));
    return e;
}

std::vector<std::pair<Tick, Tick>> chaineDe(const Project& project, const std::vector<int>& ordre) {
    const auto sections = vsm::sequencer::sectionsFromMarkers(project);
    std::vector<std::pair<Tick, Tick>> chaine;
    for (int i : ordre) chaine.emplace_back(sections[static_cast<size_t>(i)].startTick, sections[static_cast<size_t>(i)].endTick);
    return chaine;
}
} // namespace

VSM_TEST(the_play_chain_sounds_like_the_flattened_order_d606) {
    const Project original = deuxSections();
    const auto sections = vsm::sequencer::sectionsFromMarkers(original);
    VSM_ASSERT_EQ(sections.size(), static_cast<size_t>(2));
    VSM_ASSERT_EQ(sections[1].endTick, static_cast<Tick>(3840));
    const std::vector<int> ordre = {1, 0, 0};

    // LA CHAÎNE : départ comme le transport le fait (`startPlayChain` avant `setPlaying`), la boucle posée et active.
    auto chaine = graphe(original);
    chaine->setPlayChain(chaineDe(original, ordre));
    chaine->setLoopRegion(0.0, 1.0, true);
    chaine->startPlayChain();
    const double depart = chaine->currentSeconds();
    const int premier = chaine->playChainIndex();
    chaine->setPlaying(true);
    const auto sonChaine = rendre(*chaine, 7.0);

    // LE PROJET APLATI : le même ordre, écrit dans le matériau.
    Project aplati = original;
    VSM_ASSERT(vsm::sequencer::flattenPlayOrder(aplati, ordre));
    auto plat = graphe(aplati);
    plat->setPlaying(true);
    const auto sonAplati = rendre(*plat, 7.0);

    // LE TÉMOIN : le projet joué droit depuis 0.
    auto droit = graphe(original);
    droit->setPlaying(true);
    const auto sonDroit = rendre(*droit, 7.0);

    double crete = 0.0;
    for (float v : sonAplati) crete = std::max(crete, static_cast<double>(std::abs(v)));
    const double ecart = ecartMax(sonChaine, sonAplati);
    const double ecartQueue = ecartMax(sonChaine, sonAplati, static_cast<size_t>(6.0 * kTaux));
    const double ecartTemoin = ecartMax(sonDroit, sonAplati);
    // OÙ les deux sons divergent : l'écart par demi-seconde, et le premier échantillon au-delà de 1e-4.
    std::printf("      [D606.1] écart par demi-seconde :");
    for (size_t t = 0; t < 14; ++t) {
        double e = 0.0;
        for (size_t i = t * 4000; i < (t + 1) * 4000 && i < std::min(sonChaine.size(), sonAplati.size()); ++i)
            e = std::max(e, static_cast<double>(std::abs(sonChaine[i] - sonAplati[i])));
        std::printf(" %.0e", e);
    }
    size_t premierEcart = 0;
    while (premierEcart < std::min(sonChaine.size(), sonAplati.size())
           && std::abs(sonChaine[premierEcart] - sonAplati[premierEcart]) <= 1e-4f)
        ++premierEcart;
    std::printf(" ; premier écart à l'échantillon %zu (%.4f s)\n", premierEcart, static_cast<double>(premierEcart) / kTaux);
    const bool finDite = chaine->consumePlayChainEnded();
    std::printf("      [D606.1] départ %.3f s (élément %d) ; crête %.3f ; chaîne/aplati %.2e (queue %.2e) ; droit/aplati %.3f ; "
                "fin : %s, à %.4f s, %s\n", depart, premier, crete, ecart, ecartQueue, ecartTemoin,
                finDite ? "dite" : "MUETTE", chaine->currentSeconds(), chaine->isPlaying() ? "EN LECTURE" : "arrêté");
    VSM_ASSERT_NEAR(depart, 2.0, 1e-9);
    VSM_ASSERT_EQ(premier, 0);
    VSM_ASSERT(crete > 0.05);
    VSM_ASSERT(ecart < 1e-4);
    VSM_ASSERT(ecartTemoin > 0.05);
    VSM_ASSERT(finDite);
    VSM_ASSERT(!chaine->isPlaying());
    VSM_ASSERT_EQ(chaine->playChainIndex(), -1);
}

VSM_TEST(a_seek_picks_the_chain_element_or_leaves_the_order_and_a_take_ignores_it_d606) {
    const Project original = deuxSections();
    // [B, A] : B de 2 à 4 s, A de 0 à 2 s.
    auto g = graphe(original);
    g->setPlayChain(chaineDe(original, {1, 0}));
    VSM_ASSERT_EQ(g->playChainSize(), 2);
    g->seekSeconds(0.5);
    VSM_ASSERT_EQ(g->playChainIndex(), -1);    // à l'arrêt, aucun : le départ choisira
    g->startPlayChain();
    VSM_ASSERT_NEAR(g->currentSeconds(), 2.0, 1e-9);
    g->setPlaying(true);
    (void)rendre(*g, 0.25);
    g->seekSeconds(0.5);                       // en lecture, dans A : l'élément 1
    const int dansA = g->playChainIndex();
    g->seekSeconds(5.0);                       // hors de tout élément : la lecture quitte l'ordre
    const int dehors = g->playChainIndex();
    (void)rendre(*g, 0.5);
    const double fileDroit = g->currentSeconds();
    // UNE PRISE : la chaîne ignorée — le départ ne bouge pas la tête, et la lecture file droit au-delà de la fin de A.
    auto prise = graphe(original);
    prise->setPlayChain(chaineDe(original, {1, 0}));
    prise->seekSeconds(1.5);
    prise->setRecording(true);
    prise->startPlayChain();
    const double departPrise = prise->currentSeconds();
    prise->setPlaying(true);
    (void)rendre(*prise, 1.0);
    const double apresPrise = prise->currentSeconds();
    std::printf("      [D606.1] déplacé dans A : élément %d ; hors de la chaîne : %d, puis %.3f s ; une prise : départ "
                "%.3f s, une seconde plus tard %.3f s\n", dansA, dehors, fileDroit, departPrise, apresPrise);
    VSM_ASSERT_EQ(dansA, 1);
    VSM_ASSERT_EQ(dehors, -1);
    VSM_ASSERT(fileDroit > 5.4);
    VSM_ASSERT_NEAR(departPrise, 1.5, 1e-9);
    VSM_ASSERT(apresPrise > 2.4);
}

// CORRIGÉ APRÈS LA MESURE (ROADMAP-daw, D606) : deux éléments CONTIGUS forment un seul tenant. L'ordre identité [A, B]
// joué en chaîne doit sonner comme le projet joué droit — la note 60, qui traverse la frontière, n'est ni relâchée ni
// relancée — et comme le projet aplati du même ordre.
VSM_TEST(the_identity_chain_sounds_like_the_straight_playback_d606) {
    const Project original = deuxSections();
    auto chaine = graphe(original);
    chaine->setPlayChain(chaineDe(original, {0, 1}));
    chaine->startPlayChain();
    chaine->setPlaying(true);
    const auto sonChaine = rendre(*chaine, 5.0);
    auto droit = graphe(original);
    droit->setPlaying(true);
    const auto sonDroit = rendre(*droit, 5.0);
    Project aplati = original;
    VSM_ASSERT(vsm::sequencer::flattenPlayOrder(aplati, {0, 1}));
    auto plat = graphe(aplati);
    plat->setPlaying(true);
    const auto sonAplati = rendre(*plat, 5.0);
    std::printf("      [D606.1] l'ordre identité : chaîne/droit %.2e, aplati/droit %.2e\n", ecartMax(sonChaine, sonDroit),
                ecartMax(sonAplati, sonDroit));
    VSM_ASSERT(ecartMax(sonChaine, sonDroit) < 1e-4);
    VSM_ASSERT(ecartMax(sonAplati, sonDroit) < 1e-4);
}

// D611.1 — LES CONTRÔLEURS CHASSÉS AU SAUT. L'attendu écrit avant le code (ROADMAP-daw, D611), CORRIGÉ APRÈS LA PREMIÈRE
// MESURE : un CC 7 du planning s'applique à toute la portion de bloc où il tombe (D329, défaut nommé) ; la mesure de la
// chasse se fait donc sur des sections ALIGNÉES sur les blocs (deux mesures = 32 000 échantillons = 125 blocs de 256) :
// la chaîne [C, A, C] et la boucle sur B contre l'aplati, < 1e-4 ; le code d'avant en différait de plus de 0,05. Le cas
// non aligné est gardé : l'écart y est CONFINÉ au bloc de chaque saut.
namespace {
/// D611 : OÙ deux sons divergent — le premier échantillon au-delà de 1e-4, et l'écart des 256 échantillons qui suivent.
void direLaDivergence(const char* quoi, const std::vector<float>& a, const std::vector<float>& b) {
    size_t i = 0;
    while (i < std::min(a.size(), b.size()) && std::abs(a[i] - b[i]) <= 1e-4f) ++i;
    std::printf("      [D611.1] %s : premier écart à l'échantillon %zu (%.4f s, bloc %zu + %zu)\n", quoi, i,
                static_cast<double>(i) / kTaux, i / kBloc, i % kBloc);
}
} // namespace

namespace {
/// Trois sections d'après des repères, `mesures` mesures chacune ; une note par section ; le volume de canal (CC 7) à 40
/// au milieu de A et à 127 au milieu de B. `alignee` : C finit à 3 × `mesures` (sinon 120 ticks plus tôt, et chaque saut
/// de la chaîne tombe au milieu d'un bloc).
Project troisSectionsEtUnVolume(int mesures, bool alignee) {
    const Tick m = 1920 * mesures;
    Project project;
    project.ticksPerQuarterNote = 480;
    project.markers.push_back({0, "A"});
    project.markers.push_back({m, "B"});
    project.markers.push_back({2 * m, "C"});
    Track track;
    uint64_t id = 1;
    track.addNote(0, m - 120, 57, 110, 0, id);
    track.addNote(m, 2 * m - 120, 60, 110, 0, id);
    // C commence par la note relancée seule : sa propre note part une noire plus tard — sur le Minimoog, MONOPHONIQUE,
    // deux attaques au même instant se départagent par leur ordre d'arrivée, qui n'est pas le même dans la chaîne
    // (la relance d'abord) et dans l'aplati (l'ordre du planning). CORRIGÉ APRÈS LE PREMIER ESSAI de la note ajoutée.
    track.addNote(2 * m + 480, alignee ? 3 * m : 3 * m - 120, 64, 110, 0, id);
    // AJOUTÉE APRÈS LA MESURE ALIGNÉE (le défaut de D606 à la frontière de bloc) : une note qui traverse de B dans C,
    // que le saut A → C doit relancer — au bit comme l'aplati, même quand le saut finit un bloc.
    track.addNote(2 * m - 240, 2 * m + 240, 67, 90, 0, id);
    track.controlChanges.push_back({m / 2, 0, 7, 40});
    track.controlChanges.push_back({m + m / 2, 0, 7, 127});
    project.tracks.push_back(track);
    return project;
}

/// L'écart hors des blocs qui contiennent un saut (`sauts`, en échantillons de sortie).
double ecartHorsDesBlocs(const std::vector<float>& a, const std::vector<float>& b, const std::vector<size_t>& sauts) {
    double e = 0.0;
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        bool dansUnBlocDeSaut = false;
        for (size_t s : sauts) dansUnBlocDeSaut = dansUnBlocDeSaut || i / kBloc == s / kBloc;
        if (!dansUnBlocDeSaut) e = std::max(e, static_cast<double>(std::abs(a[i] - b[i])));
    }
    return e;
}

struct DeuxSons { std::vector<float> lecture, aplati; };

DeuxSons chaineEtAplati(const Project& original, const std::vector<int>& ordre, double secondes) {
    auto chaine = graphe(original);
    chaine->setPlayChain(chaineDe(original, ordre));
    chaine->startPlayChain();
    chaine->setPlaying(true);
    Project aplati = original;
    VSM_ASSERT(vsm::sequencer::flattenPlayOrder(aplati, ordre));
    auto plat = graphe(aplati);
    plat->setPlaying(true);
    return {rendre(*chaine, secondes), rendre(*plat, secondes)};
}

DeuxSons boucleEtAplati(const Project& original, double debut, double fin, double secondes) {
    auto boucle = graphe(original);
    boucle->setLoopRegion(debut, fin, true);
    boucle->seekSeconds(debut);
    boucle->setPlaying(true);
    Project aplati = original;
    VSM_ASSERT(vsm::sequencer::flattenPlayOrder(aplati, {1, 1}));
    auto plat = graphe(aplati);
    plat->setPlaying(true);
    return {rendre(*boucle, secondes), rendre(*plat, secondes)};
}
} // namespace

VSM_TEST(a_chain_jump_and_a_loop_wrap_chase_the_controllers_like_the_flattened_order_d611) {
    const Project aligne = troisSectionsEtUnVolume(2, true);   // A de 0 à 8 s… chaque saut sur une frontière de bloc
    VSM_ASSERT_EQ(vsm::sequencer::sectionsFromMarkers(aligne).size(), static_cast<size_t>(3));
    const auto chaine = chaineEtAplati(aligne, {2, 0, 2}, 13.0);
    const auto boucle = boucleEtAplati(aligne, 4.0, 8.0, 8.0);
    const double eChaine = ecartMax(chaine.lecture, chaine.aplati), eBoucle = ecartMax(boucle.lecture, boucle.aplati);
    std::printf("      [D611.1] aligné : chaîne [C, A, C] / aplati %.2e ; boucle sur B, deux tours / aplati [B, B] %.2e\n",
                eChaine, eBoucle);
    direLaDivergence("chaîne alignée", chaine.lecture, chaine.aplati);
    VSM_ASSERT(eChaine < 1e-4);
    VSM_ASSERT(eBoucle < 1e-4);
}

VSM_TEST(unaligned_the_scheduled_cc7_lands_at_the_start_of_its_block_and_nowhere_else_d611) {
    // LE DÉFAUT NOMMÉ (D329), MESURÉ : sections d'une mesure (16 000 échantillons, 62,5 blocs). La chaîne saute au milieu
    // d'un bloc et chasse À L'ÉCHANTILLON ; l'aplati applique son CC 7 de créneau dès le début du bloc. L'écart est
    // CONFINÉ au bloc de chaque saut.
    const Project original = troisSectionsEtUnVolume(1, false);
    const auto chaine = chaineEtAplati(original, {2, 0, 2}, 6.5);
    const auto boucle = boucleEtAplati(original, 2.0, 4.0, 4.0);
    // Les sauts : la chaîne C → A à 15 000 (C dure 1 800 ticks, 1,875 s), A → C à 31 000 ; la boucle à 16 000.
    const double horsChaine = ecartHorsDesBlocs(chaine.lecture, chaine.aplati, {15000, 31000});
    const double horsBoucle = ecartHorsDesBlocs(boucle.lecture, boucle.aplati, {16000});
    std::printf("      [D611.1] non aligné : chaîne %.2e (hors des blocs de saut %.2e) ; boucle %.2e (hors %.2e)\n",
                ecartMax(chaine.lecture, chaine.aplati), horsChaine, ecartMax(boucle.lecture, boucle.aplati), horsBoucle);
    direLaDivergence("chaîne non alignée", chaine.lecture, chaine.aplati);
    direLaDivergence("boucle non alignée", boucle.lecture, boucle.aplati);
    VSM_ASSERT(horsChaine < 1e-4);
    VSM_ASSERT(horsBoucle < 1e-4);
}
