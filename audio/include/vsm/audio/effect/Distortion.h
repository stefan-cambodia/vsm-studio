#pragma once
#include "IAudioEffect.h"
#include "vsm/audio/dsp/Biquad.h"
#include "vsm/audio/dsp/Oversampler.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace vsm::audio::effect {

/// Distorsion / overdrive avec anti-aliasing par suréchantillonnage (4x).
/// Le waveshaping non linéaire tourne au taux suréchantillonné pour repousser
/// le repliement hors bande, puis on redescend (sections 13/14). Deux modes :
/// soft (tanh, overdrive) et hard (écrêtage, distorsion). Un passe-bas "tone"
/// en sortie adoucit le haut du spectre. RT-safe.
class Distortion : public IAudioEffect {
public:
    enum ParamIds : vsm::audio::plugin::ParamId {
        kDrive = 0, kMode, kToneHz, kMix, kOutputDb, kNumParams
    };
    enum Mode { Soft = 0, Hard = 1 };

    Distortion() {
        parameterList_ = {
            {kDrive, "Drive", 0.0f, 1.0f, 0.4f, ""},
            {kMode, "Mode", 0.0f, 1.0f, 0.0f, ""}, // 0=soft 1=hard
            {kToneHz, "Tone", 800.0f, 18000.0f, 9000.0f, "Hz"},
            {kMix, "Mix", 0.0f, 1.0f, 1.0f, ""},
            {kOutputDb, "Output", -24.0f, 6.0f, 0.0f, "dB"},
        };
        for (const auto& p : parameterList_)
            params_[p.id].store(p.defaultValue, std::memory_order_relaxed);
    }

    void prepare(double sampleRate, int maxBlockSize) override {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        maxBlock_ = std::max(1, maxBlockSize);
        osL_.prepare(4, maxBlock_);
        osR_.prepare(4, maxBlock_);
        toneL_.setSampleRate(sampleRate_);
        toneR_.setSampleRate(sampleRate_);
        dryL_.assign(static_cast<size_t>(maxBlock_), 0.0f);
        dryR_.assign(static_cast<size_t>(maxBlock_), 0.0f);
        reset();
    }

    void reset() override {
        osL_.reset(); osR_.reset();
        toneL_.reset(); toneR_.reset();
    }

    void process(float* left, float* right, int numSamples) override {
        if (numSamples <= 0) return;
        const int count = std::min(numSamples, maxBlock_);

        const float drive = params_[kDrive].load(std::memory_order_relaxed);
        const bool hard = params_[kMode].load(std::memory_order_relaxed) >= 0.5f;
        const float toneHz = params_[kToneHz].load(std::memory_order_relaxed);
        const float mix = params_[kMix].load(std::memory_order_relaxed);
        const float outGain = std::pow(10.0f, params_[kOutputDb].load(std::memory_order_relaxed) / 20.0f);

        const float pre = 1.0f + drive * 30.0f;
        const float norm = hard ? 1.0f : (1.0f / std::tanh(pre)); // garde ~unitaire à pleine échelle

        auto shaper = [pre, norm, hard](float x) -> float {
            const float d = x * pre;
            if (hard) return std::clamp(d, -1.0f, 1.0f);
            return std::tanh(d) * norm;
        };

        for (int i = 0; i < count; ++i) { dryL_[static_cast<size_t>(i)] = left[i]; dryR_[static_cast<size_t>(i)] = right[i]; }

        osL_.processBlock(left, count, shaper);
        osR_.processBlock(right, count, shaper);

        toneL_.set(vsm::audio::dsp::Biquad::Type::LowPass, toneHz, 0.707f, 0.0f);
        toneR_.set(vsm::audio::dsp::Biquad::Type::LowPass, toneHz, 0.707f, 0.0f);

        for (int i = 0; i < count; ++i) {
            const float wetL = toneL_.process(left[i]);
            const float wetR = toneR_.process(right[i]);
            left[i] = (dryL_[static_cast<size_t>(i)] * (1.0f - mix) + wetL * mix) * outGain;
            right[i] = (dryR_[static_cast<size_t>(i)] * (1.0f - mix) + wetR * mix) * outGain;
        }
    }

    void setParameter(vsm::audio::plugin::ParamId id, float v) override {
        if (id < kNumParams) params_[id].store(v, std::memory_order_relaxed);
    }
    float getParameter(vsm::audio::plugin::ParamId id) const override {
        return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    const char* effectName() const override { return "Distortion"; }

    /// Il SURÉCHANTILLONNE (facteur 4), donc il retarde : seize échantillons,
    /// que le graphe compense désormais (D4.5). Voir `Oversampler`.
    int latencySamples() const override { return osL_.latencySamples(); }

private:
    double sampleRate_ = 48000.0;
    int maxBlock_ = 1;
    vsm::audio::dsp::Oversampler osL_, osR_;
    vsm::audio::dsp::Biquad toneL_, toneR_;
    std::vector<float> dryL_, dryR_;

    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
};

} // namespace vsm::audio::effect
