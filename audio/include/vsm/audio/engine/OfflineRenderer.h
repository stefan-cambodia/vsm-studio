#pragma once
#include "vsm/audio/engine/ProcessGraph.h"
#include <vector>

namespace vsm::audio::engine {

struct RenderedAudio {
    std::vector<float> left;
    std::vector<float> right;
    double sampleRate = 48000.0;
    size_t numFrames() const { return left.size(); }
};

/// Rejoue un projet hors temps réel (pas d'horloge, pas de thread dédié --
/// simple boucle synchrone) en utilisant EXACTEMENT le même
/// ProcessGraph::processBlock() que le futur callback audio temps réel :
/// le résultat est donc garanti bit-identique à ce qu'on entendrait en
/// lecture live (voir ARCHITECTURE.md section 5 -- "un seul et même calcul
/// pour la lecture live et l'export"), propriété vérifiée par test
/// (déterminisme : deux rendus successifs produisent des octets identiques).
class OfflineRenderer {
public:
    /// `realTimePace` fait attendre le rendu entre deux blocs pour suivre le
    /// temps réel (D6.5). CE N'EST JAMAIS UTILE AUX MACHINES DE CE PROJET, qui
    /// sont déterministes : le résultat est identique, seule la durée du calcul
    /// change. Cela existe pour les plugins des autres (phase D7) qui lisent
    /// une horloge ou font tourner leur propre thread, et qui rendraient
    /// autrement autre chose que ce qu'on a entendu.
    static RenderedAudio render(ProcessGraph& graph, double sampleRate, int blockSize,
                                 double durationSeconds, bool realTimePace = false);
};

} // namespace vsm::audio::engine
