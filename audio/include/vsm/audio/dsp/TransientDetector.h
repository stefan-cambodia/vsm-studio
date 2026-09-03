#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vsm::audio::dsp {

/// LA DÉTECTION DES TRANSITOIRES — les attaques d'un matériau, en trames,
/// pour le verrouillage de `TimeStretch` (D12.3, `docs/CDC-etirement-temporel.md`).
///
/// CE QU'ELLE FAIT. Elle découpe le matériau en blocs de `kBlock` trames
/// (5,3 ms à 48 kHz), mesure l'énergie de chacun, et retient les HAUSSES
/// d'énergie (le flux, demi-onde positive) qui dépassent un seuil ADAPTATIF :
/// un multiple de la hausse moyenne des ±`kContext` blocs voisins, plus un
/// plancher absolu pour ignorer le souffle. Deux attaques ne peuvent pas
/// être à moins de `kMinGapBlocks` l'une de l'autre (50 ms : la plus
/// serrée des doubles-croches à 150 BPM en fait 100). Chaque attaque
/// retenue est ensuite AFFINÉE à la trame : l'endroit de la plus forte pente
/// de l'énergie instantanée dans le bloc — un verrou posé au bloc près
/// laisserait 5 ms de flou, et le banc exige 1 ms.
///
/// CE QU'ELLE N'EST PAS. Ni un suiveur de temps, ni une détection de
/// hauteur : elle ne sait pas où sont les temps, seulement où le son
/// commence quelque chose. Elle tourne HORS du thread audio (à la publication
/// d'une piste, sur son matériau, avec des allocations) et son résultat est
/// une liste qu'on donne à `TimeStretch::setTransients`.
///
/// APPROXIMATIONS ASSUMÉES : une seule bande (l'énergie totale) — une attaque
/// de charleston sous une basse tenue peut lui échapper ; un flux spectral par
/// bandes viendra si un chiffre le demande. Les constantes sont celles d'un
/// premier réglage sur des clics et des sinus (le banc), pas sur un corpus.
class TransientDetector {
public:
    static constexpr int kBlock = 256;
    static constexpr int kContext = 20;        ///< ± 106 ms de voisinage
    static constexpr int kMinGapBlocks = 9;    ///< 48 ms
    static constexpr double kRatio = 3.0;      ///< le flux doit valoir trois fois la moyenne locale
    static constexpr double kFloor = 1e-4;     ///< en énergie (rms² d'un bloc) : sous ce plancher, c'est du souffle

    /// Analyse `[from, from + count)` de la source (`frameAt`, `requestRange`).
    /// Rend les attaques, en trames absolues de la source, triées.
    template <class Source>
    static std::vector<int64_t> detect(const Source& source, int64_t from, int64_t count) {
        std::vector<int64_t> attaques;
        if (count <= 0) return attaques;
        const int64_t blocs = (count + kBlock - 1) / kBlock;
        std::vector<double> energie(static_cast<size_t>(blocs), 0.0);
        source.requestRange(from, count);
        for (int64_t b = 0; b < blocs; ++b) {
            double e = 0.0;
            int n = 0;
            for (int i = 0; i < kBlock; ++i) {
                const int64_t t = from + b * kBlock + i;
                if (t >= from + count) break;
                float g = 0.0f, d = 0.0f;
                if (!source.frameAt(t, g, d)) continue;
                const double m = 0.5 * (static_cast<double>(g) + static_cast<double>(d));
                e += m * m;
                ++n;
            }
            energie[static_cast<size_t>(b)] = n > 0 ? e / n : 0.0;
        }
        // Le flux : la hausse d'énergie d'un bloc à l'autre, en racine (une
        // hausse de niveau, pas de puissance : le seuil parle en amplitude).
        std::vector<double> flux(static_cast<size_t>(blocs), 0.0);
        for (int64_t b = 1; b < blocs; ++b) {
            const double a = std::sqrt(energie[static_cast<size_t>(b - 1)]);
            const double c = std::sqrt(energie[static_cast<size_t>(b)]);
            flux[static_cast<size_t>(b)] = std::max(0.0, c - a);
        }
        int64_t dernier = -1000000;
        for (int64_t b = 1; b < blocs; ++b) {
            const double f = flux[static_cast<size_t>(b)];
            if (f <= 0.0 || energie[static_cast<size_t>(b)] < kFloor) continue;
            double somme = 0.0;
            int n = 0;
            for (int64_t c = std::max<int64_t>(0, b - kContext); c < std::min(blocs, b + kContext + 1); ++c) {
                if (c == b) continue;
                somme += flux[static_cast<size_t>(c)];
                ++n;
            }
            const double moyenne = n > 0 ? somme / n : 0.0;
            if (f < kRatio * moyenne + std::sqrt(kFloor)) continue;
            // Un maximum local du flux, pas un flanc.
            const bool max = f >= flux[static_cast<size_t>(b - 1)]
                             && (b + 1 >= blocs || f >= flux[static_cast<size_t>(b + 1)]);
            if (!max) continue;
            if (b - dernier < kMinGapBlocks) continue;
            attaques.push_back(affiner(source, from + (b - 1) * kBlock, 2 * kBlock));
            dernier = b;
        }
        return attaques;
    }

private:
    /// La trame (absolue) de la plus forte pente de l'énergie instantanée,
    /// lissée sur 16 trames, dans `[debut, debut + longueur)`.
    template <class Source>
    static int64_t affiner(const Source& source, int64_t debut, int64_t longueur) {
        constexpr int kLisse = 16;
        std::vector<double> e(static_cast<size_t>(longueur), 0.0);
        for (int64_t i = 0; i < longueur; ++i) {
            float g = 0.0f, d = 0.0f;
            if (!source.frameAt(debut + i, g, d)) continue;
            const double m = 0.5 * (static_cast<double>(g) + static_cast<double>(d));
            e[static_cast<size_t>(i)] = m * m;
        }
        double meilleure = -1.0;
        int64_t ou = 0;
        for (int64_t i = kLisse; i + kLisse <= longueur; ++i) {
            double avant = 0.0, apres = 0.0;
            for (int j = 0; j < kLisse; ++j) {
                avant += e[static_cast<size_t>(i - 1 - j)];
                apres += e[static_cast<size_t>(i + j)];
            }
            const double pente = apres - avant;
            if (pente > meilleure) { meilleure = pente; ou = i; }
        }
        return debut + ou;
    }
};

} // namespace vsm::audio::dsp
