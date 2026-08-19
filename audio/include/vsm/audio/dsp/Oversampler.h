#pragma once
#include "Constants.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace vsm::audio::dsp {

/// FIR passe-bas linéaire, coefficients windowed-sinc (fenêtre de Hamming),
/// gain DC normalisé à 1. Buffer circulaire, aucune allocation hors design().
class FirLowpass {
public:
    /// cutoffNorm : fréquence de coupure normalisée (fs = 1, Nyquist = 0.5).
    void design(int numTaps, double cutoffNorm) {
        if (numTaps < 3) numTaps = 3;
        if ((numTaps % 2) == 0) ++numTaps; // impair -> phase linéaire symétrique
        coeffs_.assign(static_cast<size_t>(numTaps), 0.0f);
        hist_.assign(static_cast<size_t>(numTaps), 0.0f);
        pos_ = 0;

        const int M = numTaps - 1;
        double sum = 0.0;
        for (int n = 0; n < numTaps; ++n) {
            const double x = static_cast<double>(n) - static_cast<double>(M) / 2.0;
            const double sinc = (std::abs(x) < 1.0e-9)
                                    ? 2.0 * cutoffNorm
                                    : std::sin(2.0 * kPi * cutoffNorm * x) / (kPi * x);
            const double w = 0.54 - 0.46 * std::cos(2.0 * kPi * static_cast<double>(n) /
                                                    static_cast<double>(M));
            const double c = sinc * w;
            coeffs_[static_cast<size_t>(n)] = static_cast<float>(c);
            sum += c;
        }
        const float norm = static_cast<float>(1.0 / sum);
        for (auto& c : coeffs_) c *= norm;
    }

    void reset() { std::fill(hist_.begin(), hist_.end(), 0.0f); pos_ = 0; newest_ = 0; }

    /// Mémorise un échantillon SANS calculer la sortie. Utile quand on sait
    /// que cette sortie sera jetée -- c'est le cas de f-1 échantillons sur f
    /// dans l'étage de décimation d'un suréchantillonneur : la convolution y
    /// coûte N multiplications, l'historique doit être tenu à jour, mais le
    /// résultat part à la poubelle. Ne pas le calculer est une économie
    /// EXACTE (le signal restant est bit-identique), pas une approximation.
    void push(float x) {
        const size_t n = coeffs_.size();
        hist_[pos_] = x;
        newest_ = pos_;
        pos_ = (pos_ + 1) % n;
    }

    /// Convolution complète sur l'historique courant, le dernier échantillon
    /// poussé servant de plus récent. Même ordre de sommation que process().
    float compute() const {
        const size_t n = coeffs_.size();
        float acc = 0.0f;
        size_t idx = newest_;
        for (size_t k = 0; k < n; ++k) {
            acc += coeffs_[k] * hist_[idx];
            idx = (idx == 0) ? n - 1 : idx - 1;
        }
        return acc;
    }

    float process(float x) {
        push(x);
        return compute();
    }

    const std::vector<float>& coefficients() const { return coeffs_; }

private:
    std::vector<float> coeffs_, hist_;
    size_t pos_ = 0;
    size_t newest_ = 0;
};

/// Suréchantillonneur mono à facteur entier (1, 2, 4 ou 8). Zero-stuffing +
/// FIR passe-bas pour la montée, FIR passe-bas + décimation pour la descente.
/// Sert à faire tourner un traitement NON LINÉAIRE (saturation, distorsion,
/// waveshaping) à une fréquence plus élevée pour repousser le repliement
/// (aliasing) au-dessus de la bande audible, puis à redescendre proprement
/// (sections 13 et 14). factor = 1 -> passe-plat.
///
/// Un seul étage FIR (pas de cascade halfband) : simple à vérifier et
/// suffisant. Aucune allocation dans processBlock().
///
/// OPTIMISATION POLYPHASE (Phase 6, après mesure -- voir ARCHITECTURE.md
/// § 9 ter). L'implémentation naïve faisait deux fois le même travail inutile,
/// et le banc CPU l'a montré sans ambiguïté : la Distortion (seul effet
/// suréchantillonné) coûtait 0,40 ms par bloc contre 0,015 ms pour l'effet
/// suivant, soit 27 fois plus cher que n'importe quel autre.
///   1. À la MONTÉE, f-1 échantillons sur f entrant dans le FIR sont des
///      zéros insérés : le produit c[m]*0 était calculé puis additionné. La
///      décomposition polyphase ne garde, pour chaque phase k, que les
///      coefficients c[k], c[k+f], c[k+2f]... appliqués à l'historique des
///      VRAIS échantillons d'entrée.
///   2. À la DESCENTE, une convolution complète était calculée pour chaque
///      échantillon suréchantillonné alors qu'un seul sur f est conservé. Les
///      autres sont désormais simplement poussés dans l'historique (push()).
/// Les deux économies sont EXACTES, pas des approximations : les termes
/// supprimés valent zéro ou ne sont jamais lus, et le facteur de compensation
/// de gain (f, toujours une puissance de deux, donc multiplication exacte en
/// binaire) est intégré aux coefficients sans changer le moindre bit. Le test
/// `oversampler_polyphase_matches_naive_reference` le vérifie contre une
/// implémentation naïve de référence écrite exprès dans les tests.
class Oversampler {
public:
    void prepare(int factor, int maxBlockSize) {
        factor_ = (factor >= 8) ? 8 : (factor >= 4) ? 4 : (factor >= 2) ? 2 : 1;
        maxBlock_ = std::max(1, maxBlockSize);
        if (factor_ == 1) return;

        const int taps = 16 * factor_ + 1;
        const double cutoff = 0.5 / static_cast<double>(factor_); // Nyquist de base
        up_.design(taps, cutoff);
        down_.design(taps, cutoff);

        // Décomposition polyphase du filtre de montée : phase k -> les
        // coefficients d'indice k, k+f, k+2f... Le facteur de compensation de
        // gain est absorbé ici plutôt qu'appliqué à l'échantillon porteur.
        const auto& c = up_.coefficients();
        phaseLength_ = (static_cast<int>(c.size()) + factor_ - 1) / factor_;
        upPhases_.assign(static_cast<size_t>(factor_ * phaseLength_), 0.0f);
        for (size_t m = 0; m < c.size(); ++m) {
            const size_t phase = m % static_cast<size_t>(factor_);
            const size_t tap = m / static_cast<size_t>(factor_);
            upPhases_[phase * static_cast<size_t>(phaseLength_) + tap] = c[m] * static_cast<float>(factor_);
        }
        inputHistory_.assign(static_cast<size_t>(phaseLength_), 0.0f);

        over_.assign(static_cast<size_t>(maxBlock_) * static_cast<size_t>(factor_), 0.0f);
        reset();
    }

    void reset() {
        up_.reset();
        down_.reset();
        std::fill(inputHistory_.begin(), inputHistory_.end(), 0.0f);
        inputPos_ = 0;
    }

    int factor() const { return factor_; }

    /// Traite un bloc mono en place : monte à factor_, applique `fn` à chaque
    /// échantillon suréchantillonné, redescend. `fn` : float(float).
    template <typename Fn>
    void processBlock(float* data, int numSamples, Fn fn) {
        if (factor_ == 1) {
            for (int i = 0; i < numSamples; ++i) data[i] = fn(data[i]);
            return;
        }
        const int count = std::min(numSamples, maxBlock_);
        const int f = factor_;

        // Montée, en polyphase : aucun zéro n'est multiplié ni additionné.
        // y[i*f + k] = somme sur n de upPhases_[k][n] * x[i-n].
        const size_t histLen = inputHistory_.size();
        for (int i = 0; i < count; ++i) {
            inputHistory_[inputPos_] = data[i];
            for (int k = 0; k < f; ++k) {
                const float* phase = &upPhases_[static_cast<size_t>(k) * static_cast<size_t>(phaseLength_)];
                float acc = 0.0f;
                size_t idx = inputPos_;
                for (int n = 0; n < phaseLength_; ++n) {
                    acc += phase[n] * inputHistory_[idx];
                    idx = (idx == 0) ? histLen - 1 : idx - 1;
                }
                over_[static_cast<size_t>(i * f + k)] = acc;
            }
            inputPos_ = (inputPos_ + 1) % histLen;
        }

        // Non-linéarité au taux suréchantillonné.
        for (int j = 0; j < count * f; ++j)
            over_[static_cast<size_t>(j)] = fn(over_[static_cast<size_t>(j)]);

        // Descente : l'historique est alimenté par TOUS les échantillons (le
        // filtre doit les voir), mais la convolution n'est calculée que pour
        // celui qu'on garde.
        for (int i = 0; i < count; ++i) {
            for (int k = 0; k < f - 1; ++k)
                down_.push(over_[static_cast<size_t>(i * f + k)]);
            down_.push(over_[static_cast<size_t>(i * f + f - 1)]);
            data[i] = down_.compute();
        }
    }

private:
    int factor_ = 1;
    int maxBlock_ = 1;
    FirLowpass up_, down_;
    std::vector<float> over_;

    // Étage de montée en polyphase : `factor_` sous-filtres de `phaseLength_`
    // coefficients, appliqués à l'historique des échantillons d'ENTRÉE (pas
    // du signal suréchantillonné).
    std::vector<float> upPhases_;
    std::vector<float> inputHistory_;
    int phaseLength_ = 0;
    size_t inputPos_ = 0;
};

} // namespace vsm::audio::dsp
