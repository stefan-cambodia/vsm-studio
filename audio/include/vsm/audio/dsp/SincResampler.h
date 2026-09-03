#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vsm::audio::dsp {

/// LE RÉÉCHANTILLONNAGE À NOYAU FENÊTRÉ — un sinc sous fenêtre de Kaiser,
/// écrit pour D12.1 (`docs/CDC-etirement-temporel.md`, § 3) et pour remplacer
/// l'interpolation linéaire de D2.3, qui atténuait l'aigu et laissait du
/// repliement (approximation assumée à l'époque, chiffrée : 10⁻³ sous 10 kHz).
///
/// CE QU'IL FAIT. Lire un signal à une position FRACTIONNAIRE, c'est
/// reconstruire le signal continu entre ses échantillons ; la reconstruction
/// exacte d'un signal à bande limitée est une somme de sinc, infinie. On la
/// tronque à `kTaps` échantillons de part et d'autre sous une fenêtre de
/// Kaiser (paramètre β), qui règle le compromis entre la raideur de la coupure
/// et l'ondulation. En SOUS-ÉCHANTILLONNAGE (rapport > 1 : le fichier est plus
/// rapide que la session), la coupure descend à la moitié de la fréquence de
/// SESSION — c'est l'anti-repliement, que l'interpolation linéaire n'avait pas.
///
/// LA TABLE DE PHASES. Le noyau est tabulé sur `kPhases` positions
/// fractionnaires et interpolé linéairement entre deux phases : le coût par
/// échantillon est de `2·kTaps` multiplications, sans sinus ni Bessel, et le
/// résultat est déterministe au bit près (la table est construite dans l'ordre,
/// en double, une fois par rapport).
///
/// SANS ÉTAT : la sortie à la position p ne dépend que des échantillons
/// autour de p. C'est ce qui rend une fenêtre de diffusion lisible sans avoir
/// lu les précédentes — la propriété sur laquelle D8.2 repose.
///
/// APPROXIMATIONS ASSUMÉES : β = 8, et une longueur de noyau que le banc a
/// CHOISIE plutôt que promise — le CDC disait 32 points ; mesuré, 32 points
/// laissent 20 kHz rouler (4 × 10⁻²) et n'atténuent un 23 kHz replié que de
/// 12 dB ; la longueur par défaut est celle que le banc a retenue (voir
/// `test_sinc_resampler.cpp`). La bande de transition reste celle d'un noyau
/// fini, mesurée fréquence par fréquence.
class SincResampler {
public:
    static constexpr int kDefaultTaps = 64;    ///< points du noyau (voir le banc : 32 était l'attendu, 64 le mesuré)
    static constexpr int kPhases = 512;
    static constexpr double kBeta = 8.0;

    /// Construit la table pour un rapport `ratio` = trames de fichier par
    /// trame de session, avec `taps` points (pair). Alloue : hors thread audio.
    void prepare(double ratio, int taps = kDefaultTaps) {
        ratio_ = ratio > 0.0 ? ratio : 1.0;
        taps_ = std::max(4, taps + (taps % 2));
        const int kTaps = taps_;
        const int kHalf = taps_ / 2;
        // La coupure, en cycles par trame de FICHIER : la moitié, ou moins si
        // la session est plus lente que le fichier (anti-repliement).
        const double coupure = 0.5 * std::min(1.0, 1.0 / ratio_);
        table_.assign(static_cast<size_t>((kPhases + 1) * kTaps), 0.0f);
        const double i0beta = besselI0(kBeta);
        for (int ph = 0; ph <= kPhases; ++ph) {
            const double frac = static_cast<double>(ph) / kPhases;
            double somme = 0.0;
            for (int k = 0; k < kTaps; ++k) {
                // Le point k du noyau est à la distance (k - kHalf + 1) - frac
                // de la position demandée.
                const double t = static_cast<double>(k - kHalf + 1) - frac;
                const double x = 2.0 * coupure * t;
                const double sinc = std::abs(x) < 1e-12 ? 1.0 : std::sin(M_PI * x) / (M_PI * x);
                const double u = t / kHalf;                       // -1..1 sur la fenêtre
                const double w = std::abs(u) >= 1.0 ? 0.0 : besselI0(kBeta * std::sqrt(1.0 - u * u)) / i0beta;
                const double h = 2.0 * coupure * sinc * w;
                table_[static_cast<size_t>(ph * kTaps + k)] = static_cast<float>(h);
                somme += h;
            }
            // Gain unité en continu, phase par phase : sans cela, une constante
            // ondulerait au rythme de la fraction.
            if (somme > 1e-9)
                for (int k = 0; k < kTaps; ++k)
                    table_[static_cast<size_t>(ph * kTaps + k)] = static_cast<float>(
                        static_cast<double>(table_[static_cast<size_t>(ph * kTaps + k)]) / somme);
        }
    }
    bool isPrepared() const { return !table_.empty(); }
    double ratio() const { return ratio_; }
    int taps() const { return taps_; }

    /// La valeur du signal `x` (de `count` échantillons, indices 0..count-1) à
    /// la position fractionnaire `position` ; hors du signal, zéro. Sans
    /// allocation.
    float at(const float* x, int64_t count, double position) const {
        const double plancher = std::floor(position);
        const auto n0 = static_cast<int64_t>(plancher);
        const double frac = position - plancher;
        const double phaseExacte = frac * kPhases;
        const auto ph = static_cast<int>(phaseExacte);
        const float mix = static_cast<float>(phaseExacte - ph);
        const float* a = table_.data() + static_cast<size_t>(ph * taps_);
        const float* b = a + taps_;
        const int half = taps_ / 2;
        float somme = 0.0f;
        for (int k = 0; k < taps_; ++k) {
            const int64_t i = n0 + (k - half + 1);
            if (i < 0 || i >= count) continue;
            const float h = a[k] + (b[k] - a[k]) * mix;
            somme += x[i] * h;
        }
        return somme;
    }

    /// Un canal entier vers `framesCibles` trames de session. Alloue.
    std::vector<float> resample(const std::vector<float>& source, size_t framesCibles) const {
        std::vector<float> sortie(framesCibles, 0.0f);
        if (source.empty() || !isPrepared()) return sortie;
        for (size_t i = 0; i < framesCibles; ++i)
            sortie[i] = at(source.data(), static_cast<int64_t>(source.size()), static_cast<double>(i) * ratio_);
        return sortie;
    }

    /// Bessel modifiée d'ordre zéro, par sa série : elle converge vite pour
    /// les β qu'on utilise, et elle est écrite ici plutôt qu'importée.
    static double besselI0(double x) {
        double somme = 1.0, terme = 1.0;
        const double q = x * x / 4.0;
        for (int k = 1; k < 60; ++k) {
            terme *= q / (static_cast<double>(k) * k);
            somme += terme;
            if (terme < somme * 1e-16) break;
        }
        return somme;
    }

private:
    double ratio_ = 1.0;
    int taps_ = kDefaultTaps;
    std::vector<float> table_;
};

} // namespace vsm::audio::dsp
