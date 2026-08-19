#include "vsm/interchange/OfflineReconstruction.h"
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
    std::vector<std::string> positional;
    vsm::interchange::RenderOptions options;
    bool quiet = false;

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
