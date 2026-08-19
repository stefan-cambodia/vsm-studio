#pragma once
#include <algorithm>
#include <cmath>

namespace vsm::audio::dsp {

/// Lisse un changement de paramètre (ex : cutoff automatisé) pour éviter le
/// "zipper noise" d'un saut brutal (section 14 du cahier des charges).
/// Convergence exponentielle (one-pole) vers la cible : simple, stable,
/// jamais de dépassement (overshoot) pour un changement en échelon.
class ParameterSmoother {
public:
    void setSampleRate(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        updateCoefficient();
    }
    void setSmoothingTimeMs(float ms) {
        smoothingTimeMs_ = std::max(0.0f, ms);
        updateCoefficient();
    }

    void reset(float value) { current_ = value; target_ = value; }
    void setTarget(float value) { target_ = value; }
    float target() const { return target_; }

    float nextValue() {
        current_ += (target_ - current_) * coefficient_;
        return current_;
    }
    float currentValue() const { return current_; }
    bool isSmoothing() const { return std::abs(target_ - current_) > 1.0e-5f; }

private:
    void updateCoefficient() {
        if (smoothingTimeMs_ <= 0.0f) { coefficient_ = 1.0f; return; }
        float timeSamples = std::max(1.0f, static_cast<float>(sampleRate_) * smoothingTimeMs_ / 1000.0f);
        coefficient_ = 1.0f - std::exp(-1.0f / timeSamples);
    }

    double sampleRate_ = 48000.0;
    float smoothingTimeMs_ = 20.0f;
    float coefficient_ = 0.1f;
    float current_ = 0.0f;
    float target_ = 0.0f;
};

} // namespace vsm::audio::dsp
