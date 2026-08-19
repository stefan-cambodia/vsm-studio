#pragma once
#include "IAudioEffect.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::audio::effect {

/// Phaser : cascade de N passe-tout du 1er ordre dont le coefficient est
/// balayé par un LFO, avec feedback. LFO en quadrature L/R. RT-safe.
class Phaser : public IAudioEffect {
public:
    enum ParamIds : vsm::audio::plugin::ParamId { kRate = 0, kDepth, kFeedback, kMix, kNumParams };

    Phaser() {
        parameterList_ = {
            {kRate, "Rate", 0.05f, 8.0f, 0.5f, "Hz"},
            {kDepth, "Depth", 0.0f, 1.0f, 0.7f, ""},
            {kFeedback, "Feedback", 0.0f, 0.95f, 0.4f, ""},
            {kMix, "Mix", 0.0f, 1.0f, 0.5f, ""},
        };
        for (const auto& p : parameterList_)
            params_[p.id].store(p.defaultValue, std::memory_order_relaxed);
    }

    void prepare(double sampleRate, int) override { sampleRate_ = sampleRate; reset(); }
    void reset() override {
        stateL_.fill(0.0f); stateR_.fill(0.0f);
        lastL_ = lastR_ = 0.0f; phase_ = 0.0;
    }

    void process(float* left, float* right, int numSamples) override {
        if (numSamples <= 0) return;
        vsm::audio::dsp::ScopedNoDenormals noDenormals;

        const float rate = params_[kRate].load(std::memory_order_relaxed);
        const float depth = params_[kDepth].load(std::memory_order_relaxed);
        const float feedback = params_[kFeedback].load(std::memory_order_relaxed);
        const float mix = params_[kMix].load(std::memory_order_relaxed);
        const double inc = static_cast<double>(rate) / sampleRate_;

        for (int n = 0; n < numSamples; ++n) {
            const float lfoL = 0.5f * (1.0f + std::sin(static_cast<float>(vsm::audio::dsp::kTwoPi * phase_)));
            const float lfoR = 0.5f * (1.0f + std::sin(static_cast<float>(vsm::audio::dsp::kTwoPi * (phase_ + 0.25))));
            // Coefficient allpass balayé (0.1..0.1+depth*0.8).
            const float gL = 0.1f + lfoL * depth * 0.85f;
            const float gR = 0.1f + lfoR * depth * 0.85f;

            left[n] = processChannel(left[n], stateL_, gL, feedback, lastL_, mix);
            right[n] = processChannel(right[n], stateR_, gR, feedback, lastR_, mix);

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
    const char* effectName() const override { return "Phaser"; }

private:
    static constexpr int kStages = 6;

    float processChannel(float in, std::array<float, kStages>& state, float g,
                         float feedback, float& last, float mix) {
        float x = in + feedback * last;
        for (int s = 0; s < kStages; ++s) {
            // Passe-tout 1er ordre : y = -g*x + state ; state = x + g*y
            float y = -g * x + state[static_cast<size_t>(s)];
            state[static_cast<size_t>(s)] = vsm::audio::dsp::flushDenormalToZero(x + g * y);
            x = y;
        }
        last = x;
        return in * (1.0f - mix) + x * mix;
    }

    double sampleRate_ = 48000.0, phase_ = 0.0;
    std::array<float, kStages> stateL_{}, stateR_{};
    float lastL_ = 0.0f, lastR_ = 0.0f;
    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
};

} // namespace vsm::audio::effect
