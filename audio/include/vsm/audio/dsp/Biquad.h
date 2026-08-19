#pragma once
#include "Constants.h"
#include "DenormalGuard.h"
#include <algorithm>
#include <cmath>

namespace vsm::audio::dsp {

/// Biquad du second ordre, coefficients "RBJ Audio EQ Cookbook" (Robert
/// Bristow-Johnson). Forme directe transposée II (TDF-II) : bon comportement
/// numérique, un seul jeu d'états z1/z2. Sert de brique commune à l'EQ du bus
/// master (section 15), au futur EQ d'effet (section 16) et au filtrage
/// K-weighting du mètre LUFS (LufsMeter.h).
///
/// Les coefficients sont recalculés hors du chemin audio (setters appelés
/// par bloc) ; process() ne fait que la récurrence, sans allocation.
class Biquad {
public:
    enum class Type { LowPass, HighPass, Peaking, LowShelf, HighShelf };

    void setSampleRate(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    }

    void set(Type type, float freqHz, float q, float gainDb) {
        const double fs = sampleRate_;
        const double f0 = std::clamp(static_cast<double>(freqHz), 1.0, fs * 0.49);
        const double w0 = kTwoPi * f0 / fs;
        const double cosw = std::cos(w0);
        const double sinw = std::sin(w0);
        const double qq = q > 1.0e-4f ? static_cast<double>(q) : 1.0e-4;
        const double A = std::pow(10.0, static_cast<double>(gainDb) / 40.0);

        double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

        switch (type) {
            case Type::LowPass: {
                const double alpha = sinw / (2.0 * qq);
                b0 = (1.0 - cosw) * 0.5; b1 = 1.0 - cosw; b2 = (1.0 - cosw) * 0.5;
                a0 = 1.0 + alpha; a1 = -2.0 * cosw; a2 = 1.0 - alpha;
                break;
            }
            case Type::HighPass: {
                const double alpha = sinw / (2.0 * qq);
                b0 = (1.0 + cosw) * 0.5; b1 = -(1.0 + cosw); b2 = (1.0 + cosw) * 0.5;
                a0 = 1.0 + alpha; a1 = -2.0 * cosw; a2 = 1.0 - alpha;
                break;
            }
            case Type::Peaking: {
                const double alpha = sinw / (2.0 * qq);
                b0 = 1.0 + alpha * A; b1 = -2.0 * cosw; b2 = 1.0 - alpha * A;
                a0 = 1.0 + alpha / A; a1 = -2.0 * cosw; a2 = 1.0 - alpha / A;
                break;
            }
            case Type::LowShelf: {
                const double alpha = sinw / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / qq - 1.0) + 2.0);
                const double sq = 2.0 * std::sqrt(A) * alpha;
                b0 =        A * ((A + 1.0) - (A - 1.0) * cosw + sq);
                b1 =  2.0 * A * ((A - 1.0) - (A + 1.0) * cosw);
                b2 =        A * ((A + 1.0) - (A - 1.0) * cosw - sq);
                a0 =            (A + 1.0) + (A - 1.0) * cosw + sq;
                a1 = -2.0 *     ((A - 1.0) + (A + 1.0) * cosw);
                a2 =            (A + 1.0) + (A - 1.0) * cosw - sq;
                break;
            }
            case Type::HighShelf: {
                const double alpha = sinw / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / qq - 1.0) + 2.0);
                const double sq = 2.0 * std::sqrt(A) * alpha;
                b0 =        A * ((A + 1.0) + (A - 1.0) * cosw + sq);
                b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw);
                b2 =        A * ((A + 1.0) + (A - 1.0) * cosw - sq);
                a0 =            (A + 1.0) - (A - 1.0) * cosw + sq;
                a1 =  2.0 *     ((A - 1.0) - (A + 1.0) * cosw);
                a2 =            (A + 1.0) - (A - 1.0) * cosw - sq;
                break;
            }
        }

        const double inv = 1.0 / a0;
        b0_ = static_cast<float>(b0 * inv);
        b1_ = static_cast<float>(b1 * inv);
        b2_ = static_cast<float>(b2 * inv);
        a1_ = static_cast<float>(a1 * inv);
        a2_ = static_cast<float>(a2 * inv);
    }

    /// Coefficients directs (utile pour le K-weighting du LufsMeter, dont les
    /// valeurs de référence sont fournies déjà normalisées par a0).
    void setCoefficients(float b0, float b1, float b2, float a1, float a2) {
        b0_ = b0; b1_ = b1; b2_ = b2; a1_ = a1; a2_ = a2;
    }

    void reset() { z1_ = z2_ = 0.0f; }

    float process(float x) {
        const float y = b0_ * x + z1_;
        z1_ = flushDenormalToZero(b1_ * x - a1_ * y + z2_);
        z2_ = flushDenormalToZero(b2_ * x - a2_ * y);
        return y;
    }

private:
    double sampleRate_ = 48000.0;
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f;
    float z1_ = 0.0f, z2_ = 0.0f;
};

} // namespace vsm::audio::dsp
