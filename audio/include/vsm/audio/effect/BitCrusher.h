#pragma once
#include "IAudioEffect.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::audio::effect {

/// Bitcrusher : réduction de résolution en bits (quantification grossière) +
/// réduction de fréquence d'échantillonnage par sample & hold (on "gèle" la
/// valeur pendant N échantillons). Deux dégradations volontaires et
/// caractéristiques du son lo-fi/numérique. RT-safe, sans état alloué.
class BitCrusher : public IAudioEffect {
public:
    enum ParamIds : vsm::audio::plugin::ParamId {
        kBits = 0, kDownsample, kMix, kNumParams
    };

    BitCrusher() {
        parameterList_ = {
            {kBits, "Bits", 1.0f, 16.0f, 8.0f, ""},
            {kDownsample, "Downsample", 1.0f, 64.0f, 1.0f, "x"},
            {kMix, "Mix", 0.0f, 1.0f, 1.0f, ""},
        };
        for (const auto& p : parameterList_)
            params_[p.id].store(p.defaultValue, std::memory_order_relaxed);
    }

    void prepare(double /*sampleRate*/, int /*maxBlockSize*/) override { reset(); }

    void reset() override {
        holdL_ = holdR_ = 0.0f;
        holdCounter_ = 0;
    }

    void process(float* left, float* right, int numSamples) override {
        if (numSamples <= 0) return;

        const float bits = std::clamp(params_[kBits].load(std::memory_order_relaxed), 1.0f, 16.0f);
        const int downsample = std::max(1, static_cast<int>(std::lround(
            params_[kDownsample].load(std::memory_order_relaxed))));
        const float mix = params_[kMix].load(std::memory_order_relaxed);

        // Nombre de paliers = 2^bits ; pas de quantification correspondant.
        const float levels = std::pow(2.0f, bits);
        const float step = 2.0f / (levels - 1.0f);

        for (int i = 0; i < numSamples; ++i) {
            if (holdCounter_ == 0) {
                holdL_ = quantize(left[i], step);
                holdR_ = quantize(right[i], step);
            }
            if (++holdCounter_ >= downsample) holdCounter_ = 0;

            left[i] = left[i] * (1.0f - mix) + holdL_ * mix;
            right[i] = right[i] * (1.0f - mix) + holdR_ * mix;
        }
    }

    void setParameter(vsm::audio::plugin::ParamId id, float v) override {
        if (id < kNumParams) params_[id].store(v, std::memory_order_relaxed);
    }
    float getParameter(vsm::audio::plugin::ParamId id) const override {
        return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    const char* effectName() const override { return "BitCrusher"; }

private:
    static float quantize(float x, float step) {
        return std::round(x / step) * step;
    }

    float holdL_ = 0.0f, holdR_ = 0.0f;
    int holdCounter_ = 0;

    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
};

} // namespace vsm::audio::effect
