#pragma once
#include "IAudioEffect.h"
#include "vsm/audio/dsp/Chorus.h"
#include <algorithm>
#include <array>
#include <atomic>

namespace vsm::audio::effect {

/// Effet chorus d'insert : promotion de la brique dsp::Chorus (créée pour le
/// Juno-106) en effet réutilisable, comme prévu section 16 et dans l'addon.
/// L'entrée stéréo est sommée en mono, passée au chorus BBD (qui produit une
/// paire stéréo via ses deux LFO en quadrature), puis mélangée au signal sec.
class ChorusEffect : public IAudioEffect {
public:
    enum ParamIds : vsm::audio::plugin::ParamId {
        kRate = 0, kDepthMs, kMix, kNumParams
    };

    ChorusEffect() {
        parameterList_ = {
            {kRate, "Rate", 0.05f, 8.0f, 0.6f, "Hz"},
            {kDepthMs, "Depth", 0.5f, 8.0f, 3.0f, "ms"},
            {kMix, "Mix", 0.0f, 1.0f, 0.5f, ""},
        };
        for (const auto& p : parameterList_)
            params_[p.id].store(p.defaultValue, std::memory_order_relaxed);
    }

    void prepare(double sampleRate, int /*maxBlockSize*/) override {
        chorus_.setSampleRate(sampleRate);
        chorus_.setBaseDelayMs(8.0f);
        chorus_.setMix(1.0f); // full wet : le dry/wet est géré ici via kMix
        reset();
    }

    void reset() override { chorus_.reset(); }

    void process(float* left, float* right, int numSamples) override {
        if (numSamples <= 0) return;
        chorus_.setRateHz(params_[kRate].load(std::memory_order_relaxed));
        chorus_.setDepthMs(params_[kDepthMs].load(std::memory_order_relaxed));
        const float mix = params_[kMix].load(std::memory_order_relaxed);

        for (int i = 0; i < numSamples; ++i) {
            const float dryL = left[i];
            const float dryR = right[i];
            const float mono = 0.5f * (dryL + dryR);
            float wetL = 0.0f, wetR = 0.0f;
            chorus_.process(mono, wetL, wetR);
            left[i] = dryL * (1.0f - mix) + wetL * mix;
            right[i] = dryR * (1.0f - mix) + wetR * mix;
        }
    }

    void setParameter(vsm::audio::plugin::ParamId id, float v) override {
        if (id < kNumParams) params_[id].store(v, std::memory_order_relaxed);
    }
    float getParameter(vsm::audio::plugin::ParamId id) const override {
        return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    const char* effectName() const override { return "Chorus"; }

private:
    vsm::audio::dsp::Chorus chorus_;
    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
};

} // namespace vsm::audio::effect
