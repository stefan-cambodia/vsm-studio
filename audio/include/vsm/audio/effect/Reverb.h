#pragma once
#include "IAudioEffect.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <vector>

namespace vsm::audio::effect {

/// Reverb algorithmique de type Freeverb (Schroeder-Moorer) : 8 filtres en
/// peigne (comb) parallèles avec amortissement, suivis de 4 passe-tout en
/// série, par canal. Écart de tuning gauche/droite pour la largeur stéréo.
/// Réglages : size (taille de pièce), damping (amortissement des aigus),
/// width (largeur stéréo), mix. RT-safe : buffers dimensionnés dans prepare().
///
/// Tunings de référence Freeverb (à 44,1 kHz) mis à l'échelle selon le sample
/// rate. Approximation assumée : ce n'est pas un modèle de pièce physique,
/// mais l'algorithme de réverbération numérique classique du domaine public.
class Reverb : public IAudioEffect {
public:
    enum ParamIds : vsm::audio::plugin::ParamId { kSize = 0, kDamping, kWidth, kMix, kNumParams };

    Reverb() {
        parameterList_ = {
            {kSize, "Size", 0.0f, 1.0f, 0.6f, ""},
            {kDamping, "Damping", 0.0f, 1.0f, 0.5f, ""},
            {kWidth, "Width", 0.0f, 1.0f, 1.0f, ""},
            {kMix, "Mix", 0.0f, 1.0f, 0.3f, ""},
        };
        for (const auto& p : parameterList_)
            params_[p.id].store(p.defaultValue, std::memory_order_relaxed);
    }

    void prepare(double sampleRate, int) override {
        const double scale = sampleRate / 44100.0;
        static constexpr int combTuning[kNumCombs] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        static constexpr int apTuning[kNumAllpass] = {556, 441, 341, 225};
        const int spread = static_cast<int>(23 * scale);
        for (size_t i = 0; i < kNumCombs; ++i) {
            combL_[i].setSize(static_cast<size_t>(combTuning[i] * scale));
            combR_[i].setSize(static_cast<size_t>(combTuning[i] * scale) + static_cast<size_t>(spread));
        }
        for (size_t i = 0; i < kNumAllpass; ++i) {
            apL_[i].setSize(static_cast<size_t>(apTuning[i] * scale));
            apR_[i].setSize(static_cast<size_t>(apTuning[i] * scale) + static_cast<size_t>(spread));
        }
        reset();
    }

    void reset() override {
        for (auto& c : combL_) c.clear();
        for (auto& c : combR_) c.clear();
        for (auto& a : apL_) a.clear();
        for (auto& a : apR_) a.clear();
    }

    void process(float* left, float* right, int numSamples) override {
        if (numSamples <= 0) return;
        vsm::audio::dsp::ScopedNoDenormals noDenormals;

        const float size = params_[kSize].load(std::memory_order_relaxed);
        const float damping = params_[kDamping].load(std::memory_order_relaxed);
        const float width = params_[kWidth].load(std::memory_order_relaxed);
        const float mix = params_[kMix].load(std::memory_order_relaxed);

        const float feedback = size * 0.28f + 0.7f;   // 0.7..0.98
        const float damp = damping * 0.4f;
        const float wet1 = width * 0.5f + 0.5f;
        const float wet2 = (1.0f - width) * 0.5f;
        constexpr float kInputGain = 0.015f;

        for (int n = 0; n < numSamples; ++n) {
            const float dryL = left[n];
            const float dryR = right[n];
            const float input = (dryL + dryR) * kInputGain;

            float outL = 0.0f, outR = 0.0f;
            for (size_t i = 0; i < kNumCombs; ++i) {
                outL += combL_[i].process(input, feedback, damp);
                outR += combR_[i].process(input, feedback, damp);
            }
            for (size_t i = 0; i < kNumAllpass; ++i) {
                outL = apL_[i].process(outL);
                outR = apR_[i].process(outR);
            }

            const float wetL = outL * wet1 + outR * wet2;
            const float wetR = outR * wet1 + outL * wet2;
            left[n] = dryL * (1.0f - mix) + wetL * mix;
            right[n] = dryR * (1.0f - mix) + wetR * mix;
        }
    }

    void setParameter(vsm::audio::plugin::ParamId id, float v) override {
        if (id < kNumParams) params_[id].store(v, std::memory_order_relaxed);
    }
    float getParameter(vsm::audio::plugin::ParamId id) const override {
        return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    const char* effectName() const override { return "Reverb"; }

private:
    static constexpr size_t kNumCombs = 8;
    static constexpr size_t kNumAllpass = 4;

    struct Comb {
        std::vector<float> buf;
        size_t idx = 0;
        float store = 0.0f;
        void setSize(size_t n) { buf.assign(std::max<size_t>(1, n), 0.0f); idx = 0; store = 0.0f; }
        void clear() { std::fill(buf.begin(), buf.end(), 0.0f); idx = 0; store = 0.0f; }
        float process(float in, float feedback, float damp) {
            float out = buf[idx];
            store = vsm::audio::dsp::flushDenormalToZero(out * (1.0f - damp) + store * damp);
            buf[idx] = in + store * feedback;
            if (++idx >= buf.size()) idx = 0;
            return out;
        }
    };
    struct Allpass {
        std::vector<float> buf;
        size_t idx = 0;
        void setSize(size_t n) { buf.assign(std::max<size_t>(1, n), 0.0f); idx = 0; }
        void clear() { std::fill(buf.begin(), buf.end(), 0.0f); idx = 0; }
        float process(float in) {
            float bufout = buf[idx];
            float out = -in + bufout;
            buf[idx] = in + bufout * 0.5f;
            if (++idx >= buf.size()) idx = 0;
            return out;
        }
    };

    std::array<Comb, kNumCombs> combL_, combR_;
    std::array<Allpass, kNumAllpass> apL_, apR_;
    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
};

} // namespace vsm::audio::effect
