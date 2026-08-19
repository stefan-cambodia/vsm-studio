#pragma once
#include "IAudioEffect.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace vsm::audio::effect {

/// Delay stéréo avec feedback, mode ping-pong (le feedback traverse les
/// canaux) et un passe-bas dans la boucle de réinjection ("tone" : chaque
/// répétition s'assombrit, comme un écho à bande). Retard entier (réglé par
/// bloc) : le retard fractionnaire interpolé est un raffinement simple.
/// RT-safe : buffers dimensionnés dans prepare(), rien alloué dans process().
class Delay : public IAudioEffect {
public:
    enum ParamIds : vsm::audio::plugin::ParamId {
        kTimeMs = 0, kFeedback, kMix, kPingPong, kToneHz, kNumParams
    };

    Delay() {
        parameterList_ = {
            {kTimeMs, "Time", 1.0f, kMaxDelayMs, 350.0f, "ms"},
            {kFeedback, "Feedback", 0.0f, 0.95f, 0.35f, ""},
            {kMix, "Mix", 0.0f, 1.0f, 0.3f, ""},
            {kPingPong, "Ping-Pong", 0.0f, 1.0f, 0.0f, ""},
            {kToneHz, "Tone", 500.0f, 18000.0f, 6000.0f, "Hz"},
        };
        for (const auto& p : parameterList_)
            params_[p.id].store(p.defaultValue, std::memory_order_relaxed);
    }

    void prepare(double sampleRate, int /*maxBlockSize*/) override {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        const size_t maxSamples =
            static_cast<size_t>(sampleRate_ * (kMaxDelayMs / 1000.0)) + 4;
        bufL_.assign(maxSamples, 0.0f);
        bufR_.assign(maxSamples, 0.0f);
        reset();
    }

    void reset() override {
        std::fill(bufL_.begin(), bufL_.end(), 0.0f);
        std::fill(bufR_.begin(), bufR_.end(), 0.0f);
        writeIndex_ = 0;
        toneStateL_ = toneStateR_ = 0.0f;
    }

    void process(float* left, float* right, int numSamples) override {
        if (bufL_.empty() || numSamples <= 0) return;
        vsm::audio::dsp::ScopedNoDenormals noDenormals;

        const float timeMs = params_[kTimeMs].load(std::memory_order_relaxed);
        const float feedback = params_[kFeedback].load(std::memory_order_relaxed);
        const float mix = params_[kMix].load(std::memory_order_relaxed);
        const bool pingPong = params_[kPingPong].load(std::memory_order_relaxed) >= 0.5f;
        const float toneHz = params_[kToneHz].load(std::memory_order_relaxed);

        const size_t size = bufL_.size();
        size_t delaySamples = static_cast<size_t>(std::max(1.0f, timeMs * static_cast<float>(sampleRate_) / 1000.0f));
        if (delaySamples >= size) delaySamples = size - 1;

        // Coefficient one-pole du passe-bas de la boucle.
        const float toneCoeff = std::exp(-2.0f * static_cast<float>(vsm::audio::dsp::kPi) *
                                         toneHz / static_cast<float>(sampleRate_));

        for (int i = 0; i < numSamples; ++i) {
            const size_t readIndex = (writeIndex_ + size - delaySamples) % size;
            const float delayedL = bufL_[readIndex];
            const float delayedR = bufR_[readIndex];

            // Passe-bas dans la réinjection.
            toneStateL_ += (delayedL - toneStateL_) * (1.0f - toneCoeff);
            toneStateR_ += (delayedR - toneStateR_) * (1.0f - toneCoeff);

            const float dryL = left[i];
            const float dryR = right[i];

            if (pingPong) {
                bufL_[writeIndex_] = dryL + toneStateR_ * feedback;
                bufR_[writeIndex_] = dryR + toneStateL_ * feedback;
            } else {
                bufL_[writeIndex_] = dryL + toneStateL_ * feedback;
                bufR_[writeIndex_] = dryR + toneStateR_ * feedback;
            }

            left[i] = dryL * (1.0f - mix) + delayedL * mix;
            right[i] = dryR * (1.0f - mix) + delayedR * mix;

            writeIndex_ = (writeIndex_ + 1) % size;
        }
    }

    void setParameter(vsm::audio::plugin::ParamId id, float v) override {
        if (id < kNumParams) params_[id].store(v, std::memory_order_relaxed);
    }
    float getParameter(vsm::audio::plugin::ParamId id) const override {
        return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    const char* effectName() const override { return "Delay"; }

private:
    static constexpr float kMaxDelayMs = 2000.0f;

    double sampleRate_ = 48000.0;
    std::vector<float> bufL_, bufR_;
    size_t writeIndex_ = 0;
    float toneStateL_ = 0.0f, toneStateR_ = 0.0f;

    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
};

} // namespace vsm::audio::effect
