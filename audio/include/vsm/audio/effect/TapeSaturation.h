#pragma once
#include "IAudioEffect.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::audio::effect {

/// Saturation façon bande : saturation douce (tanh) légèrement asymétrique
/// (harmoniques paires, "chaleur") + rolloff des aigus (filtre 1 pôle,
/// paramètre Tone) + gain de compensation. RT-safe.
///
/// Approximation assumée (section 27) : pas de modèle physique de bande
/// (hystérésis, wow/flutter). Le tanh sature en douceur -> peu d'aliasing
/// aux niveaux usuels ; un suréchantillonnage (dsp/Oversampler) pourrait être
/// ajouté en Phase 6 pour les drives extrêmes.
class TapeSaturation : public IAudioEffect {
public:
    enum ParamIds : vsm::audio::plugin::ParamId { kDrive = 0, kTone, kMix, kNumParams };

    TapeSaturation() {
        parameterList_ = {
            {kDrive, "Drive", 1.0f, 12.0f, 2.0f, ""},
            {kTone, "Tone", 0.0f, 1.0f, 0.6f, ""},   // 0 = aigus atténués, 1 = ouvert
            {kMix, "Mix", 0.0f, 1.0f, 1.0f, ""},
        };
        for (const auto& p : parameterList_)
            params_[p.id].store(p.defaultValue, std::memory_order_relaxed);
    }

    void prepare(double sampleRate, int) override { sampleRate_ = sampleRate; reset(); }
    void reset() override { lpL_ = lpR_ = 0.0f; }

    void process(float* left, float* right, int numSamples) override {
        if (numSamples <= 0) return;
        vsm::audio::dsp::ScopedNoDenormals noDenormals;

        const float drive = params_[kDrive].load(std::memory_order_relaxed);
        const float tone = params_[kTone].load(std::memory_order_relaxed);
        const float mix = params_[kMix].load(std::memory_order_relaxed);

        const float norm = 1.0f / std::tanh(drive);          // compense le gain du tanh
        // Tone -> fréquence de coupure du 1 pôle : ~1.5 kHz (sombre) à ~18 kHz.
        const float cutoff = 1500.0f + tone * 16500.0f;
        const float lpCoeff = 1.0f - std::exp(-2.0f * static_cast<float>(vsm::audio::dsp::kPi)
                                               * cutoff / static_cast<float>(sampleRate_));

        for (int n = 0; n < numSamples; ++n) {
            left[n] = processSample(left[n], drive, norm, lpCoeff, lpL_, mix);
            right[n] = processSample(right[n], drive, norm, lpCoeff, lpR_, mix);
        }
    }

    void setParameter(vsm::audio::plugin::ParamId id, float v) override {
        if (id < kNumParams) params_[id].store(v, std::memory_order_relaxed);
    }
    float getParameter(vsm::audio::plugin::ParamId id) const override {
        return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    const char* effectName() const override { return "Tape Saturation"; }

private:
    float processSample(float in, float drive, float norm, float lpCoeff, float& lpState, float mix) {
        // Asymétrie douce : léger décalage avant saturation -> harmoniques paires.
        float sat = std::tanh((in + 0.05f * in * std::fabs(in)) * drive) * norm;
        lpState = vsm::audio::dsp::flushDenormalToZero(lpState + lpCoeff * (sat - lpState));
        return in * (1.0f - mix) + lpState * mix;
    }

    double sampleRate_ = 48000.0;
    float lpL_ = 0.0f, lpR_ = 0.0f;
    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
};

} // namespace vsm::audio::effect
