#pragma once
#include "Biquad.h"
#include <cmath>

namespace vsm::audio::dsp {

/// Mètre de loudness intégré, pré-filtrage "K-weighting" d'après l'ITU-R
/// BS.1770 : un high-shelf (~+4 dB au-dessus de ~1,5 kHz) suivi d'un
/// passe-haut du 2e ordre (~38 Hz, "RLB"), puis loudness = -0.691 +
/// 10*log10(somme des moyennes quadratiques K-pondérées des canaux).
///
/// Le K-weighting est ici réalisé avec les biquads RBJ (Biquad) réglés sur
/// les fréquences/gain/Q de référence de BS.1770 -- très proche de la
/// bilinéaire exacte de la norme, suffisant pour un mètre d'aide au mixage.
///
/// Simplification assumée (section 27) : c'est une mesure INTÉGRÉE non
/// gatée (pas de porte absolue -70 LUFS ni de porte relative -10 LU de la
/// norme). Le gating et la mesure momentanée/court-terme fenêtrée sont un
/// raffinement Phase 6. reset() redémarre l'intégration.
class LufsMeter {
public:
    void prepare(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        // Étage 1 : high-shelf ~1681 Hz, +4 dB, Q ~0.707 (pré-filtre BS.1770).
        shelfL_.setSampleRate(sampleRate_);
        shelfR_.setSampleRate(sampleRate_);
        shelfL_.set(Biquad::Type::HighShelf, 1681.97f, 0.7071752f, 3.99984f);
        shelfR_.set(Biquad::Type::HighShelf, 1681.97f, 0.7071752f, 3.99984f);
        // Étage 2 : passe-haut ~38 Hz, Q ~0.5 (RLB).
        hpL_.setSampleRate(sampleRate_);
        hpR_.setSampleRate(sampleRate_);
        hpL_.set(Biquad::Type::HighPass, 38.13f, 0.5003270f, 0.0f);
        hpR_.set(Biquad::Type::HighPass, 38.13f, 0.5003270f, 0.0f);
        reset();
    }

    void reset() {
        shelfL_.reset(); shelfR_.reset();
        hpL_.reset(); hpR_.reset();
        sumSquares_ = 0.0;
        count_ = 0;
    }

    /// Accumule un échantillon stéréo dans l'intégration.
    void processStereo(float l, float r) {
        const float kl = hpL_.process(shelfL_.process(l));
        const float kr = hpR_.process(shelfR_.process(r));
        sumSquares_ += static_cast<double>(kl) * kl + static_cast<double>(kr) * kr;
        ++count_;
    }

    /// Loudness intégrée en LUFS. Renvoie kSilence si rien n'a été accumulé
    /// ou si le niveau est sous le plancher de silence.
    double integratedLufs() const {
        if (count_ == 0) return kSilence;
        const double meanSquare = sumSquares_ / static_cast<double>(count_);
        if (meanSquare <= 1.0e-12) return kSilence;
        return -0.691 + 10.0 * std::log10(meanSquare);
    }

    static constexpr double kSilence = -120.0;

private:
    double sampleRate_ = 48000.0;
    Biquad shelfL_, shelfR_, hpL_, hpR_;
    double sumSquares_ = 0.0;
    unsigned long long count_ = 0;
};

} // namespace vsm::audio::dsp
