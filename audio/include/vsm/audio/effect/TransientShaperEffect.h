#pragma once
#include "IAudioEffect.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::audio::effect {

/// TRANSIENT SHAPER (D13.8) — l'attaque et la tenue, sans seuil.
///
/// Un compresseur agit au-dessus d'un seuil ; celui-ci agit sur la FORME. Deux
/// suiveurs d'enveloppe sur le même signal, l'un rapide (1 ms de montée,
/// 20 ms de descente), l'autre lent (30 ms, 200 ms) : leur différence, quand le
/// rapide dépasse le lent, EST l'attaque ; ce qui reste quand ils se rejoignent
/// est la tenue. *Attack* pousse ou retient la première, *Sustain* la seconde,
/// de −1 à +1. C'est la recette de la SPL Transient Designer, sans le nom.
class TransientShaperEffect : public IAudioEffect {
public:
    enum ParamIds : vsm::audio::plugin::ParamId { kAttack = 0, kSustain, kOutput, kNumParams };

    TransientShaperEffect() {
        parameterList_ = {
            {kAttack, "Attack", -1.0f, 1.0f, 0.3f, ""},
            {kSustain, "Sustain", -1.0f, 1.0f, 0.0f, ""},
            {kOutput, "Output Level", 0.0f, 2.0f, 1.0f, ""},
        };
        for (const auto& p : parameterList_) params_[p.id].store(p.defaultValue, std::memory_order_relaxed);
    }
    void prepare(double sampleRate, int) override {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        auto coef = [&](double ms) { return static_cast<float>(std::exp(-1.0 / (ms * 0.001 * sampleRate_))); };
        rapideMontee_ = coef(1.0); rapideDescente_ = coef(20.0);
        lentMontee_ = coef(30.0); lentDescente_ = coef(200.0);
        lissage_ = coef(2.0);
        reset();
    }
    void reset() override { rapide_ = lent_ = lisse_ = 0.0f; }
    void process(float* left, float* right, int numSamples) override {
        const float attack = std::clamp(params_[kAttack].load(std::memory_order_relaxed), -1.0f, 1.0f);
        const float sustain = std::clamp(params_[kSustain].load(std::memory_order_relaxed), -1.0f, 1.0f);
        const float sortie = params_[kOutput].load(std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i) {
            // L'ENVELOPPE, PAS LA FORME D'ONDE : le redressé est lissé à 2 ms
            // avant les suiveurs. Sans cela, le suiveur lent ne monte que sous
            // les crêtes d'un son tenu et s'installe en dessous du rapide --
            // mesuré, un son tenu prenait +12 % avec Attack +1.
            lisse_ = std::abs(0.5f * (left[i] + right[i])) * (1.0f - lissage_) + lisse_ * lissage_;
            const float x = lisse_;
            rapide_ = x > rapide_ ? x + rapideMontee_ * (rapide_ - x) : x + rapideDescente_ * (rapide_ - x);
            lent_ = x > lent_ ? x + lentMontee_ * (lent_ - x) : x + lentDescente_ * (lent_ - x);
            // L'attaque : de combien le rapide dépasse le lent, rapporté au lent.
            const float a = std::clamp((rapide_ - lent_) / (lent_ + 1e-4f), 0.0f, 1.0f);
            // Le gain : jusqu'à +12 dB / -12 dB sur l'attaque, ± 9 dB sur la tenue.
            const float gainAttaque = std::pow(4.0f, attack * a);
            const float gainTenue = std::pow(2.8f, sustain * (1.0f - a));
            const float g = gainAttaque * gainTenue * sortie;
            left[i] *= g;
            right[i] *= g;
        }
    }
    void setParameter(vsm::audio::plugin::ParamId id, float v) override { if (id < kNumParams) params_[id].store(v, std::memory_order_relaxed); }
    float getParameter(vsm::audio::plugin::ParamId id) const override { return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f; }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    const char* effectName() const override { return "Transient Shaper"; }

private:
    double sampleRate_ = 48000.0;
    float rapide_ = 0.0f, lent_ = 0.0f, lisse_ = 0.0f, lissage_ = 0.0f;
    float rapideMontee_ = 0.0f, rapideDescente_ = 0.0f, lentMontee_ = 0.0f, lentDescente_ = 0.0f;
    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
};

} // namespace vsm::audio::effect
