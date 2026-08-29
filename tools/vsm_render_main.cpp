#include "vsm/interchange/OfflineReconstruction.h"
#if VSM_WITH_CLAP
#include "ClapPluginHost.h"
#endif
#if VSM_WITH_VST3
#include "Vst3PluginHost.h"
#endif
#include "vsm/interchange/PatchRenderService.h"
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// `vsm-render` : rend un dossier de projet en WAV, sans interface ni carte son.
//
// C'est le point d'entrée du « Mode A » (docs/ROADMAP-interop.md § 7) : un
// script Python écrit un dossier de projet, appelle ce programme, relit le
// WAV. Aucun protocole, aucun serveur, aucune session à maintenir -- ce qui
// rend la boucle d'optimisation reproductible et débogable, et permet de la
// rejouer des mois plus tard à partir des seuls fichiers.
//
// Conventions de ligne de commande respectées parce qu'un outil piloté par un
// script en dépend : code de sortie 0 en cas de succès et non nul en cas
// d'échec, diagnostics sur stderr, résumé exploitable sur stdout.

namespace {

void printUsage() {
    std::fprintf(stderr,
        "Usage : vsm-render <dossier-projet> <sortie.wav> [options]\n"
        "\n"
        "  Rend un projet VSM (project.json + midi/ + instruments/) en WAV.\n"
        "\n"
        "Options :\n"
        "  --sample-rate <Hz>    fréquence d'échantillonnage (défaut : 48000)\n"
        "  --block-size <n>      taille de bloc du rendu (défaut : 512)\n"
        "  --tail <secondes>     silence ajouté après la dernière note (défaut : 2)\n"
        "  --temps-reel          rend au pas du temps réel. INUTILE aux machines de\n"
        "                        ce projet, qui sont déterministes : neuf minutes\n"
        "                        prendront neuf minutes pour le même résultat. Existe\n"
        "                        pour les plugins tiers qui l'exigent -- ceux-là\n"
        "                        l'obtiennent d'eux-mêmes, sans cette option.\n"
        "  --stems <dossier>     un WAV par piste au lieu d'un mixage ; le second\n"
        "                        argument est alors ce dossier. La somme des stems\n"
        "                        redonne le mixage AVANT la tranche master.\n"
        "  --stems-par <mode>    piste (défaut) | groupe\n"
        "  --start <secondes>    début de la plage exportée (défaut : 0). Le rendu\n"
        "                        part toujours de zéro et la plage est découpée\n"
        "                        ensuite, pour que les queues et les compresseurs\n"
        "                        soient dans l'état où l'oreille les attend.\n"
        "  --duration <secondes> durée imposée (défaut : déduite du projet)\n"
        "  --format <f>          float32 | int24 | int16 (défaut : float32)\n"
        "  --quiet               n'affiche que les erreurs\n"
        "\n"
        "Mode service (pour une boucle d'optimisation) :\n"
        "  vsm-render --serve    lit des requêtes JSON (une par ligne) sur l'entrée\n"
        "                        standard et répond une ligne JSON par requête.\n"
        "                        Les machines restent chargées entre les requêtes :\n"
        "                        c'est ce qui rend des milliers de rendus possibles.\n"
        "\n"
        "Codes de sortie : 0 succès, 1 erreur d'utilisation, 2 échec du rendu.\n");
}

bool parseDouble(const char* text, double& out) {
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text || *end != '\0') return false;
    out = value;
    return true;
}

} // namespace

int main(int argc, char** argv) {
#if VSM_WITH_CLAP
    // D7.1 : les identifiants `clap:` deviennent chargeables. Une seule ligne,
    // et le rendu accepte les mêmes projets que l'application.
    vsm::clap::installClapResolver();
#endif
#if VSM_WITH_VST3
    vsm::vst3::installVst3Resolver();
#endif
    std::vector<std::string> positional;
    vsm::interchange::RenderOptions options;
    bool quiet = false;
    std::string stems;
    vsm::interchange::StemGranularity granularite = vsm::interchange::StemGranularity::Tracks;

    // Mode service : détecté avant tout le reste, il ne partage aucun argument
    // avec le rendu de projet.
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--serve")
            return vsm::interchange::runPatchRenderLoop(std::cin, std::cout, std::cerr);
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "vsm-render : valeur manquante après %s\n", name);
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h") { printUsage(); return 0; }
        else if (arg == "--quiet") { quiet = true; }
        else if (arg == "--temps-reel") { options.realTimeRender = true; }
        else if (arg == "--sample-rate") {
            const char* value = next("--sample-rate");
            if (!value || !parseDouble(value, options.sampleRate) || options.sampleRate <= 0.0) {
                std::fprintf(stderr, "vsm-render : fréquence d'échantillonnage invalide\n");
                return 1;
            }
        } else if (arg == "--block-size") {
            const char* value = next("--block-size");
            double parsed = 0.0;
            if (!value || !parseDouble(value, parsed) || parsed < 1.0) {
                std::fprintf(stderr, "vsm-render : taille de bloc invalide\n");
                return 1;
            }
            options.blockSize = static_cast<int>(parsed);
        } else if (arg == "--tail") {
            const char* value = next("--tail");
            if (!value || !parseDouble(value, options.tailSeconds) || options.tailSeconds < 0.0) {
                std::fprintf(stderr, "vsm-render : durée de queue invalide\n");
                return 1;
            }
        } else if (arg == "--stems") {
            const char* value = next("--stems");
            if (!value) return 1;
            stems = value;
        } else if (arg == "--stems-par") {
            const char* value = next("--stems-par");
            if (!value) return 1;
            const std::string mode = value;
            if (mode == "piste") granularite = vsm::interchange::StemGranularity::Tracks;
            else if (mode == "groupe") granularite = vsm::interchange::StemGranularity::Groups;
            else {
                std::fprintf(stderr, "vsm-render : granularité inconnue \"%s\" (piste|groupe)\n", value);
                return 1;
            }
        } else if (arg == "--start") {
            const char* value = next("--start");
            if (!value || !parseDouble(value, options.startSeconds) || options.startSeconds < 0.0) {
                std::fprintf(stderr, "vsm-render : début de plage invalide\n");
                return 1;
            }
        } else if (arg == "--duration") {
            const char* value = next("--duration");
            if (!value || !parseDouble(value, options.durationSeconds) || options.durationSeconds < 0.0) {
                std::fprintf(stderr, "vsm-render : durée invalide\n");
                return 1;
            }
        } else if (arg == "--format") {
            const char* value = next("--format");
            if (!value) return 1;
            const std::string format = value;
            if (format == "float32") options.format = vsm::audio::io::SampleFormat::Float32;
            else if (format == "int24") options.format = vsm::audio::io::SampleFormat::Int24;
            else if (format == "int16") options.format = vsm::audio::io::SampleFormat::Int16;
            else {
                std::fprintf(stderr, "vsm-render : format inconnu \"%s\" (float32|int24|int16)\n", value);
                return 1;
            }
        } else if (!arg.empty() && arg[0] == '-') {
            std::fprintf(stderr, "vsm-render : option inconnue \"%s\"\n", arg.c_str());
            printUsage();
            return 1;
        } else {
            positional.push_back(arg);
        }
    }

    if (positional.size() != 2) { printUsage(); return 1; }

    // EXPORT PAR STEMS (D6.2) : le second argument devient un DOSSIER, un WAV
    // par piste. Même moteur, même options de plage et de format -- il n'y a
    // toujours qu'un seul rendu dans ce projet.
    if (!stems.empty()) {
        const auto chargé = vsm::interchange::loadProjectBundle(positional[0]);
        if (!chargé.success) {
            std::fprintf(stderr, "vsm-render : %s\n", chargé.error.c_str());
            return 2;
        }
        const vsm::interchange::StemResult sortie =
            vsm::interchange::renderStemsToFolder(chargé.bundle, positional[1], granularite, options);
        for (const auto& warning : chargé.warnings)
            std::fprintf(stderr, "avertissement : %s\n", warning.c_str());
        for (const auto& warning : sortie.warnings)
            std::fprintf(stderr, "avertissement : %s\n", warning.c_str());
        if (!sortie.success) {
            std::fprintf(stderr, "vsm-render : %s\n", sortie.error.c_str());
            return 2;
        }
        if (!quiet)
            std::printf("%zu stems écrits dans %s (%.2f s)\n", sortie.stems.size(),
                         positional[1].c_str(), sortie.renderedSeconds);
        return 0;
    }

    const vsm::interchange::RenderResult result =
        vsm::interchange::renderProjectFolderToWav(positional[0], positional[1], options);

    // Les avertissements vont sur stderr même en mode silencieux : un
    // instrument manquant change le rendu, et le taire donnerait un WAV faux
    // que le script appelant croirait bon.
    for (const auto& warning : result.warnings)
        std::fprintf(stderr, "avertissement : %s\n", warning.c_str());

    if (!result.success) {
        std::fprintf(stderr, "vsm-render : %s\n", result.error.c_str());
        return 2;
    }
    if (!quiet) std::printf("%s\n", result.summary().c_str());
    return 0;
}
