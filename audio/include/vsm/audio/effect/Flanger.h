#pragma once
#include "IAudioEffect.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace vsm::audio::effect {

/// Flanger : ligne à retard courte (~1..7 ms) modulée par un LFO, avec
/// feedback. LFO en quadrature entre L et R pour un balayage stéréo. Lecture
/// interpolée linéairement (pas de zipper). RT-safe.
class Flanger : public IAudioEffect {
public:
    enum ParamIds : vsm::audio::plugin::ParamId { kRate = 0, kDepth, kFeedback, kMix, kNumParams };

    Flanger() {
        parameterList_ = {
            {kRate, "Rate", 0.05f, 5.0f, 0.3f, "Hz"},
            {kDepth, "Depth", 0.0f, 1.0f, 0.7f, ""},
            {kFeedback, "Feedback", 0.0f, 0.95f, 0.3f, ""},
            {kMix, "Mix", 0.0f, 1.0f, 0.5f, ""},
        };
        for (const auto& p : parameterList_)
            params_[p.id].store(p.defaultValue, std::memory_order_relaxed);
    }

    void prepare(double sampleRate, int) override {
        sampleRate_ = sampleRate;
        maxDelay_ = static_cast<size_t>(0.012 * sampleRate) + 4;
        bufL_.assign(maxDelay_, 0.0f);
        bufR_.assign(maxDelay_, 0.0f);
        writeIdx_ = 0;
        phase_ = 0.0;
    }
    void reset() override {
        std::fill(bufL_.begin(), bufL_.end(), 0.0f);
        std::fill(bufR_.begin(), bufR_.end(), 0.0f);
        writeIdx_ = 0; phase_ = 0.0;
    }

    void process(float* left, float* right, int numSamples) override {
        if (numSamples <= 0 || bufL_.empty()) return;
        vsm::audio::dsp::ScopedNoDenormals noDenormals;

        const float rate = params_[kRate].load(std::memory_order_relaxed);
        const float depth = params_[kDepth].load(std::memory_order_relaxed);
        const float feedback = params_[kFeedback].load(std::memory_order_relaxed);
        const float mix = params_[kMix].load(std::memory_order_relaxed);

        const float baseDelay = 0.001f * static_cast<float>(sampleRate_);
        const float sweep = depth * 0.006f * static_cast<float>(sampleRate_);
        const double inc = static_cast<double>(rate) / sampleRate_;

        for (int n = 0; n < numSamples; ++n) {
            const float lfoL = 0.5f * (1.0f + std::sin(static_cast<float>(vsm::audio::dsp::kTwoPi * phase_)));
            const float lfoR = 0.5f * (1.0f + std::sin(static_cast<float>(vsm::audio::dsp::kTwoPi * (phase_ + 0.25))));

            const float readL = readInterp(bufL_, baseDelay + lfoL * sweep);
            const float readR = readInterp(bufR_, baseDelay + lfoR * sweep);

            bufL_[writeIdx_] = left[n] + feedback * readL;
            bufR_[writeIdx_] = right[n] + feedback * readR;

            left[n] = left[n] * (1.0f - mix) + readL * mix;
            right[n] = right[n] * (1.0f - mix) + readR * mix;

            if (++writeIdx_ >= maxDelay_) writeIdx_ = 0;
            phase_ += inc; if (phase_ >= 1.0) phase_ -= 1.0;
        }
    }

    void setParameter(vsm::audio::plugin::ParamId id, float v) override {
        if (id < kNumParams) params_[id].store(v, std::memory_order_relaxed);
    }
    float getParameter(vsm::audio::plugin::ParamId id) const override {
        return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    const char* effectName() const override { return "Flanger"; }

private:
    float readInterp(const std::vector<float>& buf, float delaySamples) const {
        float readPos = static_cast<float>(writeIdx_) - delaySamples;
        while (readPos < 0.0f) readPos += static_cast<float>(maxDelay_);
        size_t i0 = static_cast<size_t>(readPos);
        size_t i1 = (i0 + 1) % maxDelay_;
        float frac = readPos - static_cast<float>(i0);
        return buf[i0] * (1.0f - frac) + buf[i1] * frac;
    }

    double sampleRate_ = 48000.0;
    size_t maxDelay_ = 0, writeIdx_ = 0;
    double phase_ = 0.0;
    std::vector<float> bufL_, bufR_;
    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
};

} // namespace vsm::audio::effect
