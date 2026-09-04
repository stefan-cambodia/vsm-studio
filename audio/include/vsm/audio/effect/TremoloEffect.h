#pragma once
#include "IAudioEffect.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::audio::effect {

/// TRÉMOLO ET AUTO-PAN (D13.8) — un LFO sur le gain, une voie contre l'autre.
///
/// Le même effet fait les deux : à phase stéréo nulle, les deux voies montent
/// et descendent ensemble (le trémolo d'un ampli de guitare) ; à 180°, l'une
/// monte quand l'autre descend (l'auto-pan). *Shape* passe continûment du
/// sinus (0) au carré (1) — le carré est le trémolo « optique » des amplis
/// Fender, une porte plus qu'une ondulation. Le LFO repart de zéro à
/// `reset()` : deux rendus sont identiques au bit près.
class TremoloEffect : public IAudioEffect {
public:
    enum ParamIds : vsm::audio::plugin::ParamId { kRate = 0, kDepth, kShape, kStereoPhase, kNumParams };

    TremoloEffect() {
        parameterList_ = {
            {kRate, "Rate", 0.1f, 20.0f, 4.0f, "Hz"},
            {kDepth, "Depth", 0.0f, 1.0f, 0.5f, ""},
            {kShape, "Shape", 0.0f, 1.0f, 0.0f, ""},
            {kStereoPhase, "Stereo Phase", 0.0f, 180.0f, 0.0f, "deg"},
        };
        for (const auto& p : parameterList_) params_[p.id].store(p.defaultValue, std::memory_order_relaxed);
    }
    void prepare(double sampleRate, int) override { sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0; reset(); }
    void reset() override { phase_ = 0.0; }
    void process(float* left, float* right, int numSamples) override {
        const double rate = params_[kRate].load(std::memory_order_relaxed);
        const float depth = std::clamp(params_[kDepth].load(std::memory_order_relaxed), 0.0f, 1.0f);
        const float shape = std::clamp(params_[kShape].load(std::memory_order_relaxed), 0.0f, 1.0f);
        const double decalage = params_[kStereoPhase].load(std::memory_order_relaxed) / 360.0;
        const double pas = rate / sampleRate_;
        // La forme : un sinus dont la pente est durcie par une tangente
        // hyperbolique -- à 1, presque un carré, sans discontinuité.
        const float durete = 1.0f + 24.0f * shape;
        auto onde = [&](double ph) {
            const float s = static_cast<float>(std::sin(2.0 * M_PI * ph));
            const float d = std::tanh(s * durete) / std::tanh(durete);
            return 0.5f * (1.0f + d);   // 0..1
        };
        for (int i = 0; i < numSamples; ++i) {
            const float gL = 1.0f - depth * (1.0f - onde(phase_));
            const float gR = 1.0f - depth * (1.0f - onde(phase_ + decalage));
            left[i] *= gL;
            right[i] *= gR;
            phase_ += pas;
            if (phase_ >= 1.0) phase_ -= 1.0;
        }
    }
    void setParameter(vsm::audio::plugin::ParamId id, float v) override { if (id < kNumParams) params_[id].store(v, std::memory_order_relaxed); }
    float getParameter(vsm::audio::plugin::ParamId id) const override { return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f; }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    const char* effectName() const override { return "Tremolo / Auto-pan"; }

private:
    double sampleRate_ = 48000.0, phase_ = 0.0;
    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
};

} // namespace vsm::audio::effect
