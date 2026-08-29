#include "vsm/audio/engine/OfflineRenderer.h"
#include <algorithm>
#include <chrono>
#include <thread>
#include <cmath>

namespace vsm::audio::engine {

RenderedAudio OfflineRenderer::render(ProcessGraph& graph, double sampleRate, int blockSize,
                                       double durationSeconds, bool realTimePace) {
    RenderedAudio result;
    result.sampleRate = sampleRate;

    size_t totalFrames = static_cast<size_t>(std::llround(durationSeconds * sampleRate));
    result.left.assign(totalFrames, 0.0f);
    result.right.assign(totalFrames, 0.0f);

    // LA RÉFÉRENCE NE PART JAMAIS DANS UN RENDU. Le rendu hors ligne partage
    // le `processBlock` de la lecture temps réel -- c'est voulu, il ne doit
    // exister qu'un seul chemin de calcul -- mais exporter la reconstruction
    // avec l'enregistrement d'origine mélangé dedans produirait un fichier qui
    // n'est ni l'un ni l'autre. On la coupe donc explicitement, et on remet
    // l'utilisateur dans l'état où il l'avait laissée à la fin.
    const auto modeReference = graph.referenceTrack().mode();
    graph.referenceTrack().setMode(vsm::audio::engine::ReferenceTrack::Mode::Off);

    graph.seekSeconds(0.0);
    graph.setPlaying(true);

    std::vector<float> blockL(static_cast<size_t>(blockSize), 0.0f);
    std::vector<float> blockR(static_cast<size_t>(blockSize), 0.0f);

    // LE PAS DU TEMPS RÉEL (D6.5). L'attente se calcule depuis le DÉBUT, pas
    // bloc par bloc : additionner des attentes courtes accumule l'erreur de
    // chaque réveil, et un rendu de neuf minutes finirait sensiblement en
    // retard sur ce qu'il prétend imiter.
    const auto depart = std::chrono::steady_clock::now();

    size_t framesRendered = 0;
    while (framesRendered < totalFrames) {
        int thisBlock = static_cast<int>(std::min<size_t>(static_cast<size_t>(blockSize), totalFrames - framesRendered));
        graph.processBlock(blockL.data(), blockR.data(), thisBlock);
        std::copy(blockL.begin(), blockL.begin() + thisBlock, result.left.begin() + static_cast<long>(framesRendered));
        std::copy(blockR.begin(), blockR.begin() + thisBlock, result.right.begin() + static_cast<long>(framesRendered));
        framesRendered += static_cast<size_t>(thisBlock);

        if (realTimePace) {
            const auto echeance = depart + std::chrono::duration<double>(
                                                static_cast<double>(framesRendered) / sampleRate);
            std::this_thread::sleep_until(echeance);
        }
    }

    graph.setPlaying(false);
    graph.referenceTrack().setMode(modeReference);
    return result;
}

} // namespace vsm::audio::engine
