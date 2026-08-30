#include "vsm/audio/io/WaveformPeaks.h"
#include <cmath>

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

std::vector<PeakBin> computePeaksFromFile(WavStreamReader& reader, double sessionSampleRate,
                                           int samplesPerBin) {
    std::vector<PeakBin> cache;
    if (samplesPerBin <= 0 || sessionSampleRate <= 0.0 || reader.frames() <= 0) return cache;

    // Trames du FICHIER par trame de session. Le cache s'indexe en trames de
    // session : c'est là que le dessin cherchera.
    const double ratio = reader.sampleRate() / sessionSampleRate;
    const int64_t trames = static_cast<int64_t>(
        std::llround(static_cast<double>(reader.frames()) / (ratio > 0.0 ? ratio : 1.0)));
    if (trames <= 0) return cache;

    const size_t tranches = static_cast<size_t>((trames + samplesPerBin - 1) / samplesPerBin);
    cache.assign(tranches, PeakBin{});
    // UNE TRANCHE VIDE N'EST PAS UNE TRANCHE À ZÉRO. Sans ce drapeau, le
    // minimum et le maximum partiraient de 0 et ne pourraient jamais devenir
    // tous deux négatifs : un passage entièrement sous l'axe -- une basse, une
    // asymétrie de voix -- se dessinerait comme s'il touchait le zéro.
    std::vector<char> vue(tranches, 0);

    // UN MORCEAU À LA FOIS, et jamais plus. Quatre secondes à 48 kHz font
    // 1,5 Mo : c'est la mémoire que coûte l'aperçu d'un fichier de neuf
    // minutes, quelle que soit sa durée.
    constexpr int64_t kMorceau = 1 << 18;  // 262 144 trames de fichier
    std::vector<float> gauche(static_cast<size_t>(kMorceau)), droite(static_cast<size_t>(kMorceau));

    for (int64_t depart = 0; depart < reader.frames(); depart += kMorceau) {
        const int64_t combien = std::min(kMorceau, reader.frames() - depart);
        const int64_t lues = reader.readFrames(depart, combien, gauche.data(), droite.data());
        if (lues <= 0) break;
        for (int64_t i = 0; i < lues; ++i) {
            // La tranche est celle de la position de SESSION correspondante.
            const int64_t trameSession = ratio > 0.0
                ? static_cast<int64_t>(static_cast<double>(depart + i) / ratio) : depart + i;
            const int64_t t = trameSession / samplesPerBin;
            if (t < 0 || t >= static_cast<int64_t>(cache.size())) continue;
            auto& tranche = cache[static_cast<size_t>(t)];
            const float g = gauche[static_cast<size_t>(i)];
            const float d = droite[static_cast<size_t>(i)];
            const float bas = std::min(g, d);
            const float haut = std::max(g, d);
            if (!vue[static_cast<size_t>(t)]) {
                tranche.minimum = bas;
                tranche.maximum = haut;
                vue[static_cast<size_t>(t)] = 1;
            } else {
                tranche.minimum = std::min(tranche.minimum, bas);
                tranche.maximum = std::max(tranche.maximum, haut);
            }
        }
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
