#include "vsm/audio/io/WaveformPeaks.h"

namespace vsm::audio::io {

std::vector<PeakBin> computePeaks(const float* left, const float* right, int64_t frames,
                                   int samplesPerBin) {
    std::vector<PeakBin> cache;
    if (left == nullptr || frames <= 0 || samplesPerBin <= 0) return cache;

    const size_t tranches = static_cast<size_t>((frames + samplesPerBin - 1) / samplesPerBin);
    cache.resize(tranches);
    for (size_t t = 0; t < tranches; ++t) {
        const int64_t debut = static_cast<int64_t>(t) * samplesPerBin;
        const int64_t fin = std::min(frames, debut + samplesPerBin);
        float mini = 0.0f, maxi = 0.0f;
        bool premier = true;
        for (int64_t i = debut; i < fin; ++i) {
            const float g = left[i];
            const float d = right != nullptr ? right[i] : g;
            const float bas = std::min(g, d);
            const float haut = std::max(g, d);
            if (premier) { mini = bas; maxi = haut; premier = false; }
            else { mini = std::min(mini, bas); maxi = std::max(maxi, haut); }
        }
        cache[t] = {mini, maxi};
    }
    return cache;
}

std::vector<PeakBin> peaksForRange(const std::vector<PeakBin>& cache, int64_t startFrame,
                                    int64_t endFrame, int columns, int samplesPerBin) {
    std::vector<PeakBin> colonnes;
    if (columns <= 0 || samplesPerBin <= 0) return colonnes;
    colonnes.assign(static_cast<size_t>(columns), PeakBin{});
    if (endFrame <= startFrame || cache.empty()) return colonnes;

    const double tranchesParColonne =
        static_cast<double>(endFrame - startFrame)
        / (static_cast<double>(samplesPerBin) * static_cast<double>(columns));

    for (int c = 0; c < columns; ++c) {
        const double depart = (static_cast<double>(startFrame) / samplesPerBin)
                            + tranchesParColonne * c;
        const double arrivee = depart + tranchesParColonne;
        int64_t premiere = static_cast<int64_t>(depart);
        int64_t derniere = static_cast<int64_t>(arrivee);
        // AU MOINS UNE TRANCHE PAR COLONNE : très zoomé, plusieurs colonnes
        // tombent dans la même tranche, et une colonne vide ferait un trou dans
        // le tracé là où le son est continu.
        if (derniere <= premiere) derniere = premiere + 1;

        float mini = 0.0f, maxi = 0.0f;
        bool trouve = false;
        for (int64_t t = premiere; t < derniere; ++t) {
            if (t < 0 || t >= static_cast<int64_t>(cache.size())) continue;
            const auto& tranche = cache[static_cast<size_t>(t)];
            if (!trouve) { mini = tranche.minimum; maxi = tranche.maximum; trouve = true; }
            else {
                mini = std::min(mini, tranche.minimum);
                maxi = std::max(maxi, tranche.maximum);
            }
        }
        colonnes[static_cast<size_t>(c)] = {mini, maxi};
    }
    return colonnes;
}

} // namespace vsm::audio::io
