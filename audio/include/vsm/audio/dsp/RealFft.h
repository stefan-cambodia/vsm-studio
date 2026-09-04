#pragma once
#include <array>
#include <cmath>
#include <cstddef>

namespace vsm::audio::dsp {

/// TRANSFORMÉE DE FOURIER INVERSE, radix-2, taille fixée à la compilation.
///
/// POURQUOI DANS LE PARC ET NON UNE BIBLIOTHÈQUE. Le dépôt n'a aucune
/// dépendance de calcul, et une IFFT de mille vingt-quatre points tient en
/// cinquante lignes : en prendre une dehors coûterait plus (une dépendance de
/// plus à figer, à porter, à expliquer) que de l'écrire. Elle est ici parce
/// que `vsm.spectral` en a besoin, et elle est écrite pour être relue.
///
/// CE QU'ELLE GARANTIT, ET C'EST CE QUI COMPTE POUR LE TEMPS RÉEL :
///  - **aucune allocation** : les tables de rotation sont construites au
///    `prepare`, tout le reste travaille en place dans des tableaux fixes ;
///  - **déterminisme** : que des opérations flottantes, dans un ordre fixe ;
///  - **taille connue à la compilation**, donc pas d'indirection.
///
/// La convention est celle d'un signal RÉEL : on remplit `kBins` cases
/// complexes (la moitié du spectre plus une), la symétrie hermitienne est
/// reconstruite ici, et `inverse()` rend `kSize` échantillons réels.
template <size_t kSizeT>
class RealIfft {
public:
    static constexpr size_t kSize = kSizeT;
    static constexpr size_t kBins = kSizeT / 2 + 1;

    RealIfft() {
        for (size_t i = 0; i < kSize; ++i) {
            const double a = -2.0 * M_PI * static_cast<double>(i) / static_cast<double>(kSize);
            cos_[i] = static_cast<float>(std::cos(a));
            sin_[i] = static_cast<float>(std::sin(a));
        }
        for (size_t i = 0; i < kSize; ++i) {
            size_t j = 0, x = i;
            for (size_t b = 1; b < kSize; b <<= 1) { j = (j << 1) | (x & 1u); x >>= 1; }
            miroir_[i] = j;
        }
    }

    /// LA TRANSFORMÉE DIRECTE (D12.8) : `kSize` échantillons réels dans `in`,
    /// `kBins` cases complexes dans `re`/`im`. Écrite ici pour la même raison
    /// que l'inverse -- le vocodeur de phase en a besoin, et elle tient dans
    /// le même noyau radix-2 : une copie, une transformation, une lecture.
    void forward(const float* in, float* re, float* im) {
        for (size_t k = 0; k < kSize; ++k) { reT_[k] = in[k]; imT_[k] = 0.0f; }
        transformer();
        for (size_t k = 0; k < kBins; ++k) { re[k] = reT_[k]; im[k] = imT_[k]; }
    }

    /// `re`/`im` : `kBins` cases. Écrit `kSize` échantillons réels dans `out`.
    void inverse(const float* re, const float* im, float* out) {
        // Symétrie hermitienne : un signal réel a `X[N-k] = conj(X[k])`. On la
        // reconstruit plutôt que de la demander à l'appelant, qui l'oublierait.
        for (size_t k = 0; k < kBins; ++k) {
            reT_[k] = re[k];
            imT_[k] = im[k];
        }
        for (size_t k = kBins; k < kSize; ++k) {
            reT_[k] = re[kSize - k];
            imT_[k] = -im[kSize - k];
        }
        // Une IFFT est une FFT conjuguée : on inverse le signe des parties
        // imaginaires, on transforme, on réinverse — et on divise par N.
        for (size_t k = 0; k < kSize; ++k) imT_[k] = -imT_[k];
        transformer();
        const float echelle = 1.0f / static_cast<float>(kSize);
        for (size_t k = 0; k < kSize; ++k) out[k] = reT_[k] * echelle;
    }

private:
    /// FFT en place, décimation en temps (Cooley-Tukey).
    void transformer() {
        for (size_t i = 0; i < kSize; ++i) {
            const size_t j = miroir_[i];
            if (j > i) {
                std::swap(reT_[i], reT_[j]);
                std::swap(imT_[i], imT_[j]);
            }
        }
        for (size_t pas = 2; pas <= kSize; pas <<= 1) {
            const size_t demi = pas / 2;
            const size_t saut = kSize / pas;
            for (size_t debut = 0; debut < kSize; debut += pas) {
                for (size_t k = 0; k < demi; ++k) {
                    const size_t angle = k * saut;
                    const float wr = cos_[angle], wi = sin_[angle];
                    const size_t a = debut + k, b = a + demi;
                    const float tr = wr * reT_[b] - wi * imT_[b];
                    const float ti = wr * imT_[b] + wi * reT_[b];
                    reT_[b] = reT_[a] - tr;
                    imT_[b] = imT_[a] - ti;
                    reT_[a] += tr;
                    imT_[a] += ti;
                }
            }
        }
    }

    std::array<float, kSize> cos_{}, sin_{};
    std::array<size_t, kSize> miroir_{};
    std::array<float, kSize> reT_{}, imT_{};
};

} // namespace vsm::audio::dsp
