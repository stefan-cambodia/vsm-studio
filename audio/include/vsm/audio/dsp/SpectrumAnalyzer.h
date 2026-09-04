#pragma once
#include "vsm/audio/dsp/RealFft.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace vsm::audio::dsp {

/// L'ANALYSE ELLE-MÊME (D15.3), sans rien de l'interface : `kSize`
/// échantillons entrent, `kBins` niveaux en dB sortent. Fenêtre de Hann, la
/// transformée de D12.8, et une échelle où UN SINUS PLEIN-ÉCHELLE LIT 0 dB --
/// c'est l'échelle de Live (Spectrum) et de SuperVision, et c'est ce qui
/// permet de lire un niveau sur la courbe plutôt qu'une forme.
///
/// Pourquoi 4 096 par défaut : à 48 kHz, une case fait 11,7 Hz, c'est-à-dire
/// un cinquième de demi-ton à 1 kHz et un demi-ton à 200 Hz -- assez pour
/// nommer une note dans le médium, et une image toutes les 85 ms, assez
/// vive pour suivre un mixage. Le banc le mesure (test `audio/`).
template <size_t kSizeT = 4096>
class SpectrumAnalyzer {
public:
    static constexpr size_t kSize = kSizeT;
    static constexpr size_t kBins = kSizeT / 2 + 1;
    static constexpr float kFloorDb = -120.0f;

    SpectrumAnalyzer() {
        for (size_t i = 0; i < kSize; ++i)
            window_[i] = 0.5f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * static_cast<float>(i)
                                                / static_cast<float>(kSize));
        db_.fill(kFloorDb);
    }

    /// `in` : `kSize` échantillons. Remplit les `kBins` niveaux.
    void analyze(const float* in) {
        for (size_t i = 0; i < kSize; ++i) in_[i] = in[i] * window_[i];
        fft_.forward(in_.data(), re_.data(), im_.data());
        // Un sinus d'amplitude A donne, après Hann (gain cohérent 0,5), une
        // raie de module A · kSize / 4 ; on ramène cette raie à A.
        const float norme = 4.0f / static_cast<float>(kSize);
        for (size_t k = 0; k < kBins; ++k) {
            const float module = std::sqrt(re_[k] * re_[k] + im_[k] * im_[k]) * norme;
            db_[k] = module > 1e-6f ? std::max(kFloorDb, 20.0f * std::log10(module)) : kFloorDb;
        }
    }

    float magnitudeDb(size_t bin) const { return bin < kBins ? db_[bin] : kFloorDb; }
    const std::array<float, kBins>& magnitudesDb() const { return db_; }

    static double binFrequency(size_t bin, double sampleRate) {
        return static_cast<double>(bin) * sampleRate / static_cast<double>(kSize);
    }

    size_t peakBin() const {
        size_t meilleur = 0;
        for (size_t k = 1; k < kBins; ++k) if (db_[k] > db_[meilleur]) meilleur = k;
        return meilleur;
    }

    /// La fréquence de la crête, affinée par interpolation parabolique sur
    /// les trois cases autour du maximum (en dB) : à 1 kHz sur 4 096 points,
    /// l'erreur passe d'une demi-case (5,9 Hz) à moins d'un hertz.
    double peakFrequency(double sampleRate) const {
        const size_t k = peakBin();
        return (static_cast<double>(k) + static_cast<double>(peakOffset(k)))
               * sampleRate / static_cast<double>(kSize);
    }

    /// LE NIVEAU DE LA CRÊTE, CORRIGÉ DE LA FENÊTRE. La case seule ment de
    /// jusqu'à 1,4 dB quand la raie tombe entre deux cases (la fenêtre de
    /// Hann la creuse : 1 kHz lu -0,63 dB sur la case de 996 Hz), et le
    /// sommet de la parabole en dB ne suffit pas (+0,15 et +0,27 dB au banc,
    /// la parabole n'est pas la forme du lobe). La forme du lobe de Hann est
    /// connue : à `delta` cases du centre, le module vaut
    /// sinc(delta) / (1 - delta²) fois celui du centre. On lit la case, on
    /// sait de combien elle est décalée, on rend ce que le lobe a enlevé.
    float peakDb() const {
        const size_t k = peakBin();
        if (k == 0 || k + 1 >= kBins) return db_[k];
        const float delta = peakOffset(k);
        const float d = std::abs(delta);
        if (d < 1e-4f) return db_[k];
        const float sinc = std::sin(static_cast<float>(M_PI) * d) / (static_cast<float>(M_PI) * d);
        const float lobe = sinc / (1.0f - d * d);
        return db_[k] - 20.0f * std::log10(std::max(lobe, 1e-3f));
    }

private:
    /// Le décalage de la crête vraie par rapport à la case `k`, en cases,
    /// dans [-0,5 ; 0,5].
    float peakOffset(size_t k) const {
        if (k == 0 || k + 1 >= kBins) return 0.0f;
        const float a = db_[k - 1], b = db_[k], c = db_[k + 1];
        const float denominateur = a - 2.0f * b + c;
        const float delta = std::abs(denominateur) > 1e-9f ? 0.5f * (a - c) / denominateur : 0.0f;
        return std::clamp(delta, -0.5f, 0.5f);
    }

public:

private:
    RealIfft<kSize> fft_;
    std::array<float, kSize> window_{}, in_{};
    std::array<float, kBins> re_{}, im_{}, db_{};
};

} // namespace vsm::audio::dsp
