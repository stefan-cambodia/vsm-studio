// D40 — LE DAW À 64 PISTES : CE QUI CASSE, ET À PARTIR DE COMBIEN.
//
// POURQUOI CE BANC EXISTE. L'objectif de parité de la reconstruction porte une
// seconde moitié : « la parité vaut à toute échelle, ce qui engage aussi le
// DAW, qui doit rester utilisable et vérifié à 64 pistes, pas seulement à
// huit ». Le plus gros projet du dépôt en compte SIX, et les quarante phases
// D0 à D39 ont toutes été mesurées et photographiées sur une à quatre pistes.
// Une reconstruction à parité rendra un projet que rien n'a jamais ouvert.
//
// CE QUE CE BANC CHERCHE, ET CE QU'IL NE CHERCHE PAS. Il ne cherche pas à
// rendre le DAW rapide : il cherche des CHIFFRES là où il n'y en a aucun. Un
// chiffre qu'on n'a pas est plus dangereux qu'un chiffre mauvais, parce qu'on
// le remplace par une impression.
//
// IL MESURE CE QUI SE MESURE SANS ÉCRAN : la construction des panneaux et la
// publication au moteur. Ce qui demande un écran -- la largeur du mélangeur,
// le défilement de la liste -- se photographie à part, et cela est dit plutôt
// que contourné.

#include <JuceHeader.h>
#include "ui/MixerComponent.h"
#include "ui/TrackListComponent.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include "vsm/sequencer/Project.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using vsm::sequencer::Project;
using vsm::sequencer::Track;

namespace {

/// Un projet de N pistes, chacune avec des notes et une machine : ce que rend
/// une reconstruction à parité, et non un projet vide qui ne mesurerait que la
/// boucle de construction.
Project projetDeNPistes(int n, const std::string& machine) {
    Project projet;
    projet.ticksPerQuarterNote = 480;
    projet.tempoMap.addTempoChange(0, 500000);
    uint64_t ids = 1;
    for (int i = 0; i < n; ++i) {
        Track t;
        t.name = "Piste " + std::to_string(i + 1);
        t.channel = static_cast<uint8_t>(i % 16);
        t.instrumentId = machine;
        t.colorRgba = 0xFF000000u | static_cast<uint32_t>(i * 3947);
        // Quatre mesures de croches : de quoi que le planning ait à trier.
        for (int k = 0; k < 32; ++k)
            t.addNote(k * 240, k * 240 + 200, static_cast<uint8_t>(36 + (i + k) % 48),
                       100, t.channel, ids);
        projet.tracks.push_back(std::move(t));
    }
    return projet;
}

/// LA MÉMOIRE RÉSIDENTE DU PROCESSUS, EN MÉGAOCTETS. Lue dans /proc, donc
/// Linux seulement -- rend -1 ailleurs, et le banc le DIT plutôt que d'écrire
/// un zéro qu'on prendrait pour une mesure.
///
/// POURQUOI CE CHIFFRE COMPTE ICI : une reconstruction à parité instancie
/// autant de machines qu'elle a de pistes. Si soixante-quatre en pesaient un
/// gigaoctet, le projet ne s'ouvrirait pas sur cette machine-ci (15 Go, dont
/// une séparation demucs prend déjà l'essentiel). C'est le chiffre que D40
/// avait annoncé ne pas prendre.
double memoireResidenteMo() {
    std::ifstream f("/proc/self/status");
    if (!f) return -1.0;
    std::string ligne;
    while (std::getline(f, ligne))
        if (ligne.rfind("VmRSS:", 0) == 0) {
            const size_t debut = ligne.find_first_of("0123456789");
            if (debut == std::string::npos) return -1.0;
            return std::stod(ligne.substr(debut)) / 1024.0;
        }
    return -1.0;
}

double millisecondes(std::function<void()> f) {
    const auto t0 = std::chrono::steady_clock::now();
    f();
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

/// Le rapport d'une taille.
struct Mesure {
    int pistes = 0;
    double liste = 0.0;
    double melangeur = 0.0;
    double notes = 0.0;
};

} // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit;
    vsm::audio::plugin::registerBuiltInPlugins();
    // LA MACHINE SE CHOISIT EN LIGNE DE COMMANDE, et le défaut est nommé dans
    // le rapport : mesurer une seule machine et conclure « les machines pèsent
    // tant » ferait passer la plus légère pour la règle. C'est une option de
    // banc, pas une constante éditée entre deux passes.
    const std::string machine = (argc > 1) ? argv[1] : "vsm.minimoog";
    // LES FILS DE RENDU SONT UNE OPTION DU BANC, ET C'EST UNE CORRECTION.
    //
    // La première version ne les réglait pas : la réserve restait à ZÉRO fil et
    // le banc mesurait le moteur sur UN cœur, alors que l'application le règle
    // sur « automatique » (jusqu'à huit, mesurés en D8). Les 125 % et 258 % du
    // budget publiés par D41 sont donc des chiffres mono-cœur présentés comme
    // ceux du logiciel.
    //
    // Passé en OPTION plutôt qu'en constante : le témoin (un fil) et la mesure
    // (huit) sortent du même binaire, comme l'exige la règle des A/B du projet.
    const size_t fils = (argc > 2)
        ? static_cast<size_t>(std::stoul(argv[2]))
        : vsm::audio::engine::ProcessGraph::recommendedRenderThreadCount();

    const size_t machines = vsm::audio::plugin::PluginRegistry::instance().listAvailable().size();
    std::printf("=== D40 : LE DAW À %zu MACHINES, MESURÉ DE 8 À 64 PISTES ===\n", machines);
    std::printf("  machine mesurée : %s ; fils de rendu : %zu (recommandé : %zu)\n",
                machine.c_str(), fils,
                vsm::audio::engine::ProcessGraph::recommendedRenderThreadCount());
    std::printf("  (chaque ligne de piste remplit un sélecteur avec TOUT le registre)\n\n");

    std::printf("  %-8s %12s %12s %12s\n", "pistes", "liste (ms)", "mixeur (ms)", "notes");
    std::vector<Mesure> mesures;
    for (int n : { 8, 16, 32, 64 }) {
        Project projet = projetDeNPistes(n, machine);
        Mesure m;
        m.pistes = n;
        for (const auto& t : projet.tracks) m.notes += static_cast<double>(t.notes.size());

        TrackListComponent liste;
        liste.setBounds(0, 0, 300, 900);
        m.liste = millisecondes([&] { liste.loadProject(projet); });

        MixerComponent melangeur;
        melangeur.setBounds(0, 0, 1200, 300);
        m.melangeur = millisecondes([&] { melangeur.setProject(&projet); });

        std::printf("  %-8d %12.1f %12.1f %12.0f\n", m.pistes, m.liste, m.melangeur, m.notes);
        mesures.push_back(m);
    }

    // CE QUI CROÎT PLUS VITE QUE N. Doubler le nombre de pistes doit doubler le
    // temps ; un facteur nettement supérieur à 2 dit qu'une boucle en cache une
    // autre. On l'écrit plutôt que de le deviner à l'oeil sur quatre nombres.
    std::printf("\n  facteur en doublant le nombre de pistes (2,0 = linéaire) :\n");
    for (size_t i = 1; i < mesures.size(); ++i) {
        const double fl = mesures[i - 1].liste > 0.01 ? mesures[i].liste / mesures[i - 1].liste : 0.0;
        const double fm = mesures[i - 1].melangeur > 0.01 ? mesures[i].melangeur / mesures[i - 1].melangeur : 0.0;
        std::printf("    %2d -> %2d   liste x%.2f   mixeur x%.2f\n",
                    mesures[i - 1].pistes, mesures[i].pistes, fl, fm);
    }

    // LE COÛT D'UN RAFRAÎCHISSEMENT, celui qui part à chaque geste de mixage.
    {
        Project projet = projetDeNPistes(64, machine);
        TrackListComponent liste;
        liste.setBounds(0, 0, 300, 900);
        liste.loadProject(projet);
        MixerComponent melangeur;
        melangeur.setBounds(0, 0, 1200, 300);
        melangeur.setProject(&projet);

        const double muet = millisecondes([&] { for (int k = 0; k < 100; ++k) melangeur.refreshMuteSolo(); });
        const double mix = millisecondes([&] { for (int k = 0; k < 100; ++k) melangeur.refreshFromTracks(); });
        const double lignes = millisecondes([&] { for (int k = 0; k < 100; ++k) liste.refreshFromTracks(); });
        std::printf("\n  à 64 pistes, cent rafraîchissements :\n");
        std::printf("    mixeur refreshMuteSolo  %7.1f ms  (%.3f ms l'unité)\n", muet, muet / 100.0);
        std::printf("    mixeur refreshFromTracks%7.1f ms  (%.3f ms l'unité)\n", mix, mix / 100.0);
        std::printf("    liste  refreshFromTracks%7.1f ms  (%.3f ms l'unité)\n", lignes, lignes / 100.0);
        std::printf("\n  UN GESTE DE FADER EN APPELLE UN PAR PIXEL : au-delà de 16 ms l'unité,\n"
                    "  un glissé ne suivrait plus la souris (60 images par seconde).\n");
    }

    // ------------------------------------------------------------------
    // LE MOTEUR — le chiffre que je voulais le plus
    // ------------------------------------------------------------------
    // 64 machines instanciées, c'est 64 fois la mémoire d'une machine, et rien
    // dans les quarante phases précédentes ne dit ce que cela pèse. On mesure
    // aussi le temps d'un bloc : c'est lui qui décide si le morceau joue ou
    // craque, et il se compare au BUDGET du bloc (512 échantillons à 48 kHz
    // font 10,67 ms de son ; les calculer doit prendre moins que cela).
    {
        constexpr double kSampleRate = 48000.0;
        constexpr int kBlock = 512;
        const double budgetMs = 1000.0 * kBlock / kSampleRate;
        std::printf("\n=== LE MOTEUR : un bloc de %d échantillons à %.0f kHz vaut %.2f ms de son ===\n",
                    kBlock, kSampleRate / 1000.0, budgetMs);
        std::printf("  %-8s %10s %14s %10s\n", "pistes", "montage", "un bloc (ms)", "du budget");
        for (int n : { 8, 16, 32, 64 }) {
            Project projet = projetDeNPistes(n, machine);
            vsm::audio::engine::ProcessGraph graphe;
            graphe.prepare(kSampleRate, kBlock);
            graphe.setRenderThreadCount(fils);
            const double montage = millisecondes([&] {
                graphe.setProject(projet);
                for (int i = 0; i < n; ++i)
                    graphe.setTrackInstrument(static_cast<size_t>(i), machine);
            });

            std::vector<float> gauche(kBlock, 0.0f), droite(kBlock, 0.0f);
            graphe.seekSeconds(0.0);
            // IL FAUT LE METTRE EN LECTURE, sans quoi il ne déclenche aucune
            // note et l'on mesure soixante-quatre machines au repos. La
            // première version de ce banc rendait 0,002 ms par bloc pour
            // 64 Minimoog -- deux microsecondes pour 512 échantillons, ce qui
            // est impossible, et c'est cette impossibilité qui a dénoncé la
            // mesure. Un chiffre trop beau se vérifie avant de se publier.
            graphe.setPlaying(true);
            // Un tour à vide d'abord : le PREMIER bloc paie ce que les suivants
            // ne paient plus, et le compter fausserait la moyenne dans le sens
            // qui arrange.
            graphe.processBlock(gauche.data(), droite.data(), kBlock);
            constexpr int kBlocs = 200;
            double crete = 0.0;
            const double total = millisecondes([&] {
                for (int b = 0; b < kBlocs; ++b) {
                    graphe.processBlock(gauche.data(), droite.data(), kBlock);
                    for (int i = 0; i < kBlock; ++i)
                        crete = std::max(crete, static_cast<double>(std::abs(gauche[i])));
                }
            });
            const double parBloc = total / kBlocs;
            // D42 : LE RENDU PARALLÈLE SERT-IL EN LECTURE ? Le moteur a une
            // réserve de fils (`RenderThreadPool`) et compte les portées qu'il
            // rend en parallèle. Si ce compteur reste à zéro pendant que le
            // transport joue, le DAW calcule sur UN cœur pendant que la machine
            // en a douze -- et les 125 % du budget de D41 en seraient 21 %.
            const unsigned long long portees = graphe.parallelSpansRendered();
            // LA CRÊTE EST LA PREUVE QUE ÇA JOUE. Sans elle, un temps de calcul
            // ridicule passerait pour une performance.
            std::printf("  %-8d %8.1f ms %12.3f %9.1f %%   crête %.3f   fils=%zu portées//=%llu%s\n",
                        n, montage, parBloc, 100.0 * parBloc / budgetMs, crete,
                        graphe.renderThreadCount(), portees,
                        crete < 1.0e-6 ? "  <- SILENCE : LE BANC NE MESURE RIEN" : "");
        }
    }

    // ------------------------------------------------------------------
    // LA MÉMOIRE — le chiffre que D40 avait annoncé ne pas prendre
    // ------------------------------------------------------------------
    {
        const double base = memoireResidenteMo();
        if (base < 0.0) {
            std::printf("\n=== LA MÉMOIRE : non mesurable ici (/proc absent) ===\n");
        } else {
            std::printf("\n=== LA MÉMOIRE : ce que pèsent N machines instanciées ===\n");
            std::printf("  au départ, le processus tient %.0f Mo\n", base);
            std::printf("  %-8s %14s %16s\n", "pistes", "total (Mo)", "par machine (Mo)");
            // LES GRAPHES SONT GARDÉS VIVANTS ENSEMBLE : les détruire entre deux
            // tailles rendrait la mémoire au tas et l'on mesurerait le tas, pas
            // les machines. Chaque ligne dit donc le CUMUL, et la colonne « par
            // machine » divise l'écart par les machines ajoutées.
            std::vector<std::unique_ptr<vsm::audio::engine::ProcessGraph>> graphes;
            double avant = base;
            int cumulPistes = 0;
            for (int n : { 8, 16, 32, 64 }) {
                Project projet = projetDeNPistes(n, machine);
                auto graphe = std::make_unique<vsm::audio::engine::ProcessGraph>();
                graphe->prepare(48000.0, 512);
                graphe->setProject(projet);
                for (int i = 0; i < n; ++i)
                    graphe->setTrackInstrument(static_cast<size_t>(i), machine);
                graphes.push_back(std::move(graphe));
                cumulPistes += n;
                const double apres = memoireResidenteMo();
                std::printf("  +%-7d %14.0f %16.2f\n", n, apres, (apres - avant) / n);
                avant = apres;
            }
            std::printf("  -> %d machines vivantes, %.0f Mo au total\n",
                        cumulPistes, memoireResidenteMo());
            std::printf("  (cette machine a 15 Go, dont une séparation demucs prend l'essentiel)\n");
        }
    }

    // ------------------------------------------------------------------
    // D41.1 — LE COÛT DE CHAQUE MACHINE DU PARC
    // ------------------------------------------------------------------
    // POURQUOI PARCOURIR LE REGISTRE ET NON UNE LISTE ÉCRITE À LA MAIN : une
    // machine ajoutée demain doit entrer dans la mesure sans qu'on y pense.
    // C'est la discipline que `regression_every_registered_machine_has_a_reference`
    // applique déjà aux empreintes audio.
    //
    // ET POURQUOI CETTE TABLE COMPTE AU-DELÀ DU DAW : la reconstruction CHOISIT
    // les machines qu'elle assigne. Savoir laquelle coûte cinq fois une autre
    // est une donnée de la parité autant que du confort.
    if (argc <= 1 || std::string(argv[1]) == "--table") {
        constexpr double kSampleRate = 48000.0;
        constexpr int kBlock = 512;
        constexpr int kPistes = 16;   // assez pour que l'écart se voie, assez peu
                                       // pour que la table entière tienne en une minute
        const double budgetMs = 1000.0 * kBlock / kSampleRate;
        std::printf("\n=== D41.1 : LE PRIX DE CHAQUE MACHINE (%d pistes, bloc de %d) ===\n",
                    kPistes, kBlock);

        struct Prix { std::string id; double ms = 0.0; double crete = 0.0; };
        std::vector<Prix> prix;
        for (const auto& [id, nom] : vsm::audio::plugin::PluginRegistry::instance().listAvailable()) {
            if (id.rfind("vsm.", 0) != 0) continue;
            Project projet = projetDeNPistes(kPistes, id);
            vsm::audio::engine::ProcessGraph graphe;
            graphe.prepare(kSampleRate, kBlock);
            graphe.setProject(projet);
            for (int i = 0; i < kPistes; ++i)
                graphe.setTrackInstrument(static_cast<size_t>(i), id);
            std::vector<float> g(kBlock, 0.0f), d(kBlock, 0.0f);
            graphe.seekSeconds(0.0);
            graphe.setPlaying(true);
            graphe.processBlock(g.data(), d.data(), kBlock);
            Prix p;
            p.id = id;
            constexpr int kBlocs = 60;
            const double total = millisecondes([&] {
                for (int b = 0; b < kBlocs; ++b) {
                    graphe.processBlock(g.data(), d.data(), kBlock);
                    for (int i = 0; i < kBlock; ++i)
                        p.crete = std::max(p.crete, static_cast<double>(std::abs(g[i])));
                }
            });
            p.ms = total / kBlocs;
            prix.push_back(std::move(p));
        }
        std::sort(prix.begin(), prix.end(), [](const Prix& a, const Prix& b) { return a.ms < b.ms; });
        for (const auto& p : prix)
            std::printf("  %-22s %7.3f ms  %5.1f %% du budget%s\n", p.id.c_str(), p.ms,
                        100.0 * p.ms / budgetMs,
                        p.crete < 1.0e-6 ? "   <- SILENCE : ne joue pas, prix non mesure" : "");
        if (prix.size() >= 2) {
            const double rapport = prix.back().ms / std::max(1.0e-9, prix.front().ms);
            std::printf("  -> %zu machines ; de %s (%.3f ms) a %s (%.3f ms), un rapport de %.1f\n",
                        prix.size(), prix.front().id.c_str(), prix.front().ms,
                        prix.back().id.c_str(), prix.back().ms, rapport);
            // LA PLUS CHÈRE EST REMESURÉE À 64 PISTES, ET NON EXTRAPOLÉE.
            // Multiplier par quatre le prix de seize pistes DONNAIT UN CHIFFRE
            // FAUX, et faux dans le sens rassurant : `vsm.plate` annonçait
            // ainsi 100 % du budget quand la mesure directe en rend 258. Le
            // coût ne suit pas le nombre de pistes, parce que le nombre de
            // VOIX simultanées, lui, ne le suit pas non plus.
            Project gros = projetDeNPistes(64, prix.back().id);
            vsm::audio::engine::ProcessGraph g64;
            g64.prepare(kSampleRate, kBlock);
            g64.setProject(gros);
            for (int i = 0; i < 64; ++i) g64.setTrackInstrument(static_cast<size_t>(i), prix.back().id);
            std::vector<float> gg(kBlock, 0.0f), dd(kBlock, 0.0f);
            g64.seekSeconds(0.0);
            g64.setPlaying(true);
            g64.processBlock(gg.data(), dd.data(), kBlock);
            constexpr int kB64 = 60;
            const double t64 = millisecondes([&] {
                for (int b = 0; b < kB64; ++b) g64.processBlock(gg.data(), dd.data(), kBlock);
            }) / kB64;
            std::printf("     %s a 64 pistes, MESURE : %.3f ms, soit %.1f %% du budget%s\n",
                        prix.back().id.c_str(), t64, 100.0 * t64 / budgetMs,
                        t64 > budgetMs ? "  <- LE MOTEUR NE TIENT PLUS" : "");
        }
    }

    // ------------------------------------------------------------------
    // D42 — LA CHARGE PAR PISTE
    // ------------------------------------------------------------------
    {
        constexpr double kSampleRate = 48000.0;
        constexpr int kBlock = 512;
        const double budgetMs = 1000.0 * kBlock / kSampleRate;
        std::printf("\n=== D42 : LE TEMPS PAR PISTE (%s, 16 pistes) ===\n", machine.c_str());
        // UNE PISTE CHÈRE PARMI DES LÉGÈRES : c'est le cas qui donne son sens
        // au chiffre. Un banc où toutes les pistes coûtent pareil ne dirait pas
        // si la mesure DISTINGUE, seulement si elle mesure.
        Project mixte = projetDeNPistes(16, machine);
        mixte.tracks[3].instrumentId = "vsm.additive";
        for (size_t fils : { size_t{0}, size_t{8} }) {
            vsm::audio::engine::ProcessGraph graphe;
            graphe.prepare(kSampleRate, kBlock);
            graphe.setRenderThreadCount(fils);
            graphe.setProject(mixte);
            for (size_t i = 0; i < mixte.tracks.size(); ++i)
                graphe.setTrackInstrument(i, mixte.tracks[i].instrumentId);
            std::vector<float> g(kBlock, 0.0f), d(kBlock, 0.0f);
            graphe.seekSeconds(0.0);
            graphe.setPlaying(true);
            for (int b = 0; b < 200; ++b) graphe.processBlock(g.data(), d.data(), kBlock);

            double somme = 0.0;
            size_t muettes = 0;
            double chere = 0.0, legere = 1.0e9;
            for (size_t i = 0; i < mixte.tracks.size(); ++i) {
                const double us = graphe.readTrackRenderMicros(i);
                somme += us;
                if (us <= 0.0) ++muettes;
                if (i == 3) chere = us; else legere = std::min(legere, us);
            }
            const double totalBloc = millisecondes([&] {
                for (int b = 0; b < 200; ++b) graphe.processBlock(g.data(), d.data(), kBlock);
            }) / 200.0;
            std::printf("  %zu fil(s) : somme des pistes %.3f ms, bloc %.3f ms (%.0f %% du bloc)\n",
                        fils, somme / 1000.0, totalBloc, 100.0 * (somme / 1000.0) / totalBloc);
            std::printf("            piste chere (additive) %.1f us, la plus legere %.1f us,"
                        " rapport %.1f ; pistes sans temps : %zu\n",
                        chere, legere, chere / std::max(1.0, legere), muettes);
            if (muettes > 0)
                std::printf("            <- UNE PISTE SANS TEMPS EST UNE PISTE NON MESUREE\n");
        }
        std::printf("  RAPPEL : ce temps se compare aux AUTRES PISTES, pas au budget --\n"
                    "  en parallele huit pistes tournent ensemble et leur somme depasse le bloc.\n");
        (void)budgetMs;
    }

    return 0;
}
