#pragma once
#include "IAudioEffect.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Filter.h"
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::audio::effect {

/// Filtre d'insert multimode (passe-bas / passe-haut / passe-bande / réjecteur)
/// stéréo, à partir du StateVariableFilter du moteur. Réglages : cutoff,
/// resonance, mode, mix. RT-safe.
class FilterEffect : public IAudioEffect {
public:
    enum ParamIds : vsm::audio::plugin::ParamId { kCutoff = 0, kResonance, kMode, kMix, kNumParams };

    FilterEffect() {
        parameterList_ = {
            {kCutoff, "Cutoff", 20.0f, 20000.0f, 2000.0f, "Hz"},
            {kResonance, "Resonance", 0.5f, 20.0f, 1.0f, ""},
            {kMode, "Mode", 0.0f, 3.0f, 0.0f, ""}, // 0 LP 1 HP 2 BP 3 Notch
            {kMix, "Mix", 0.0f, 1.0f, 1.0f, ""},
        };
        for (const auto& p : parameterList_)
            params_[p.id].store(p.defaultValue, std::memory_order_relaxed);
    }

    void prepare(double sampleRate, int) override {
        filterL_.setSampleRate(sampleRate);
        filterR_.setSampleRate(sampleRate);
        reset();
    }
    void reset() override { filterL_.reset(); filterR_.reset(); }

    void process(float* left, float* right, int numSamples) override {
        if (numSamples <= 0) return;
        vsm::audio::dsp::ScopedNoDenormals noDenormals;

        const float cutoff = params_[kCutoff].load(std::memory_order_relaxed);
        const float reso = params_[kResonance].load(std::memory_order_relaxed);
        const int mode = static_cast<int>(std::lround(params_[kMode].load(std::memory_order_relaxed)));
        const float mix = params_[kMix].load(std::memory_order_relaxed);

        using Mode = vsm::audio::dsp::StateVariableFilter::Mode;
        Mode m = Mode::LowPass;
        switch (mode) { case 1: m = Mode::HighPass; break; case 2: m = Mode::BandPass; break;
                        case 3: m = Mode::Notch; break; default: m = Mode::LowPass; }

        for (auto* f : {&filterL_, &filterR_}) { f->setMode(m); f->setCutoffHz(cutoff); f->setResonance(reso); }

        for (int n = 0; n < numSamples; ++n) {
            left[n] = left[n] * (1.0f - mix) + filterL_.process(left[n]) * mix;
            right[n] = right[n] * (1.0f - mix) + filterR_.process(right[n]) * mix;
        }
    }

    void setParameter(vsm::audio::plugin::ParamId id, float v) override {
        if (id < kNumParams) params_[id].store(v, std::memory_order_relaxed);
    }
    float getParameter(vsm::audio::plugin::ParamId id) const override {
        return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    const char* effectName() const override { return "Filter"; }

private:
    vsm::audio::dsp::StateVariableFilter filterL_, filterR_;
    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
};

} // namespace vsm::audio::effect
