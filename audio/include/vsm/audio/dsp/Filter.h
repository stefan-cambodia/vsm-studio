#pragma once
#include "Constants.h"
#include "DenormalGuard.h"
#include <algorithm>
#include <cmath>

namespace vsm::audio::dsp {

/// Filtre multimode State Variable, topologie "zero-delay feedback" (TPT,
/// d'après Vadim Zavalishin / Andrew Simper) : stable même à forte
/// résonance, comportement correct près de l'auto-oscillation.
///
/// C'est le filtre GÉNÉRIQUE de la Phase 2, utilisé par le synthé de
/// référence (TestToneSynth) pour valider toute la chaîne. Les topologies
/// fidèles à chaque machine (ladder Moog, diode MS-20, OTA Roland...)
/// arrivent en Phase 3 avec leur propre non-linéarité de saturation interne
/// -- voir ARCHITECTURE.md section 7. Ce filtre-ci reste linéaire (pas de
/// saturation interne), volontairement : il ne prétend imiter aucune
/// machine précise.
class StateVariableFilter {
public:
    enum class Mode { LowPass, HighPass, BandPass, Notch };

    StateVariableFilter() { updateCoefficients(); } // coefficients cohérents dès la construction, sans setter préalable

    void setSampleRate(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        updateCoefficients();
    }
    // Sortie anticipée si la valeur ne change pas : un plugin qui réécrit la
    // même coupure à chaque échantillon (paramètre non modulé) déclenchait un
    // std::tan par échantillon pour rien. Mesuré : 21,5 ns par échantillon
    // avec le setter, 6,8 ns sans -- les deux tiers du coût du filtre étaient
    // un recalcul de coefficients identiques.
    void setCutoffHz(float hz) {
        if (isSameValue(hz, cutoffHz_)) return;
        cutoffHz_ = hz;
        updateCoefficients();
    }
    void setResonance(float q) {
        const float clamped = std::max(0.05f, q);
        if (isSameValue(clamped, q_)) return;
        q_ = clamped;
        updateCoefficients();
    }
    void setMode(Mode m) { mode_ = m; }

    void reset() { ic1eq_ = ic2eq_ = 0.0f; }

    float process(float input) {
        float v3 = input - ic2eq_;
        float v1 = a1_ * ic1eq_ + a2_ * v3;
        float v2 = ic2eq_ + a2_ * ic1eq_ + a3_ * v3;
        ic1eq_ = flushDenormalToZero(2.0f * v1 - ic1eq_);
        ic2eq_ = flushDenormalToZero(2.0f * v2 - ic2eq_);

        switch (mode_) {
            case Mode::LowPass:  return v2;
            case Mode::HighPass: return input - k_ * v1 - v2;
            case Mode::BandPass: return v1;
            case Mode::Notch:    return (input - k_ * v1 - v2) + v2;
        }
        return v2;
    }

private:
    void updateCoefficients() {
        float nyquist = static_cast<float>(sampleRate_) * 0.5f;
        float clampedCutoff = std::clamp(cutoffHz_, 10.0f, nyquist * 0.99f);
        g_ = std::tan(static_cast<float>(kPi) * clampedCutoff / static_cast<float>(sampleRate_));
        k_ = 1.0f / q_;
        a1_ = 1.0f / (1.0f + g_ * (g_ + k_));
        a2_ = g_ * a1_;
        a3_ = g_ * a2_;
    }

    double sampleRate_ = 48000.0;
    float cutoffHz_ = 1000.0f;
    float q_ = 0.707f;
    Mode mode_ = Mode::LowPass;

    float g_ = 0.0f, k_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f;
    float ic1eq_ = 0.0f, ic2eq_ = 0.0f;
};

} // namespace vsm::audio::dsp
