#include "vsm/audio/engine/OfflineRenderer.h"
#include <algorithm>
#include <cmath>

namespace vsm::audio::engine {

RenderedAudio OfflineRenderer::render(ProcessGraph& graph, double sampleRate, int blockSize, double durationSeconds) {
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

    size_t framesRendered = 0;
    while (framesRendered < totalFrames) {
        int thisBlock = static_cast<int>(std::min<size_t>(static_cast<size_t>(blockSize), totalFrames - framesRendered));
        graph.processBlock(blockL.data(), blockR.data(), thisBlock);
        std::copy(blockL.begin(), blockL.begin() + thisBlock, result.left.begin() + static_cast<long>(framesRendered));
        std::copy(blockR.begin(), blockR.begin() + thisBlock, result.right.begin() + static_cast<long>(framesRendered));
        framesRendered += static_cast<size_t>(thisBlock);
    }

    graph.setPlaying(false);
    graph.referenceTrack().setMode(modeReference);
    return result;
}

} // namespace vsm::audio::engine
