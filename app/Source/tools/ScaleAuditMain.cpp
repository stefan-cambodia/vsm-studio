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
    (void)argc; (void)argv;
    vsm::audio::plugin::registerBuiltInPlugins();

    const size_t machines = vsm::audio::plugin::PluginRegistry::instance().listAvailable().size();
    std::printf("=== D40 : LE DAW À %zu MACHINES, MESURÉ DE 8 À 64 PISTES ===\n", machines);
    std::printf("  (chaque ligne de piste remplit un sélecteur avec TOUT le registre)\n\n");

    std::printf("  %-8s %12s %12s %12s\n", "pistes", "liste (ms)", "mixeur (ms)", "notes");
    std::vector<Mesure> mesures;
    for (int n : { 8, 16, 32, 64 }) {
        Project projet = projetDeNPistes(n, "vsm.minimoog");
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
        Project projet = projetDeNPistes(64, "vsm.minimoog");
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
            Project projet = projetDeNPistes(n, "vsm.minimoog");
            vsm::audio::engine::ProcessGraph graphe;
            graphe.prepare(kSampleRate, kBlock);
            const double montage = millisecondes([&] {
                graphe.setProject(projet);
                for (int i = 0; i < n; ++i)
                    graphe.setTrackInstrument(static_cast<size_t>(i), "vsm.minimoog");
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
            // LA CRÊTE EST LA PREUVE QUE ÇA JOUE. Sans elle, un temps de calcul
            // ridicule passerait pour une performance.
            std::printf("  %-8d %8.1f ms %12.3f %9.1f %%   crête %.3f%s\n",
                        n, montage, parBloc, 100.0 * parBloc / budgetMs, crete,
                        crete < 1.0e-6 ? "  <- SILENCE : LE BANC NE MESURE RIEN" : "");
        }
    }

    return 0;
}
