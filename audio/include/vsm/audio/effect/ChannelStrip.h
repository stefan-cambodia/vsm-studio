#pragma once
#include "IAudioEffect.h"
#include "vsm/audio/dsp/Biquad.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Dynamics.h"
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::audio::effect {

/// LES QUATRE EFFETS DE LA TRANCHE, ENFIN ENFICHABLES PAR PISTE (D4.1).
///
/// LE CONSTAT QUI A OUVERT LA PHASE D4 : le DSP existait déjà, entier et testé,
/// et n'était accessible QUE sur le master. Une console dont on ne peut pas
/// égaliser une piste n'est pas une console -- c'est un bus de sortie avec des
/// réglages. Le mixage se fait piste par piste ; le master ne fait que
/// terminer.
///
/// CE QUI EST RÉUTILISÉ, ET CE QUI NE L'EST PAS. Le DSP l'est intégralement :
/// `dsp::Biquad`, `dsp::Compressor`, `dsp::NoiseGate`, `dsp::Limiter` sont les
/// mêmes classes que celles du bus master, aux mêmes coefficients. Les rendre
/// enfichables ne demandait donc pas d'écrire du traitement, mais de l'HABILLER
/// en `IAudioEffect` : une liste de paramètres, des atomiques, un `process`.
/// Recopier le DSP dans quatre nouvelles classes aurait donné deux
/// compresseurs à faire coïncider, et ils auraient fini par diverger.
///
/// POURQUOI QUATRE EFFETS ET NON UNE SEULE « TRANCHE ». Une tranche unique
/// imposerait l'ordre égaliseur -> compresseur -> porte -> limiteur à qui n'en
/// veut qu'un, et cet ordre n'est pas celui qu'on veut toujours : une porte se
/// place avant le compresseur pour ne pas ouvrir sur le souffle qu'il vient de
/// remonter. Quatre inserts se rangent dans l'ordre qu'on décide, comme les
/// neuf autres effets du parc.

/// Égaliseur trois bandes : plateau grave, cloche paramétrique, plateau aigu.
class EqualiserEffect : public IAudioEffect {
public:
    enum ParamIds : vsm::audio::plugin::ParamId {
        kLowGainDb = 0, kLowFreq, kMidFreq, kMidGainDb, kMidQ, kHighGainDb, kHighFreq, kNumParams
    };

    EqualiserEffect() {
        parameterList_ = {
            {kLowGainDb, "Low Gain", -18.0f, 18.0f, 0.0f, "dB"},
            {kLowFreq, "Low Freq", 40.0f, 500.0f, 120.0f, "Hz"},
            {kMidFreq, "Mid Freq", 200.0f, 8000.0f, 1000.0f, "Hz"},
            {kMidGainDb, "Mid Gain", -18.0f, 18.0f, 0.0f, "dB"},
            {kMidQ, "Mid Q", 0.2f, 8.0f, 0.8f, ""},
            {kHighGainDb, "High Gain", -18.0f, 18.0f, 0.0f, "dB"},
            {kHighFreq, "High Freq", 2000.0f, 16000.0f, 8000.0f, "Hz"},
        };
        for (const auto& p : parameterList_) params_[p.id].store(p.defaultValue, std::memory_order_relaxed);
    }

    void prepare(double sampleRate, int) override {
        for (auto* f : filtres()) f->setSampleRate(sampleRate);
        reset();
    }
    void reset() override { for (auto* f : filtres()) f->reset(); }

    void process(float* left, float* right, int numSamples) override {
        if (numSamples <= 0) return;
        vsm::audio::dsp::ScopedNoDenormals noDenormals;
        using Type = vsm::audio::dsp::Biquad::Type;

        const float graveDb = params_[kLowGainDb].load(std::memory_order_relaxed);
        const float graveHz = params_[kLowFreq].load(std::memory_order_relaxed);
        const float mediumHz = params_[kMidFreq].load(std::memory_order_relaxed);
        const float mediumDb = params_[kMidGainDb].load(std::memory_order_relaxed);
        const float mediumQ = params_[kMidQ].load(std::memory_order_relaxed);
        const float aiguDb = params_[kHighGainDb].load(std::memory_order_relaxed);
        const float aiguHz = params_[kHighFreq].load(std::memory_order_relaxed);

        for (auto* f : {&graveL_, &graveR_}) f->set(Type::LowShelf, graveHz, 0.707f, graveDb);
        for (auto* f : {&mediumL_, &mediumR_}) f->set(Type::Peaking, mediumHz, mediumQ, mediumDb);
        for (auto* f : {&aiguL_, &aiguR_}) f->set(Type::HighShelf, aiguHz, 0.707f, aiguDb);

        for (int n = 0; n < numSamples; ++n) {
            left[n] = aiguL_.process(mediumL_.process(graveL_.process(left[n])));
            right[n] = aiguR_.process(mediumR_.process(graveR_.process(right[n])));
        }
    }

    void setParameter(vsm::audio::plugin::ParamId id, float v) override {
        if (id < kNumParams) params_[id].store(v, std::memory_order_relaxed);
    }
    float getParameter(vsm::audio::plugin::ParamId id) const override {
        return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    const char* effectName() const override { return "EQ"; }

private:
    std::array<vsm::audio::dsp::Biquad*, 6> filtres() {
        return {&graveL_, &graveR_, &mediumL_, &mediumR_, &aiguL_, &aiguR_};
    }
    vsm::audio::dsp::Biquad graveL_, graveR_, mediumL_, mediumR_, aiguL_, aiguR_;
    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
};

/// Compresseur d'insert : le `dsp::Compressor` du bus master, par piste.
class CompressorEffect : public IAudioEffect {
public:
    enum ParamIds : vsm::audio::plugin::ParamId {
        kThresholdDb = 0, kRatio, kAttackMs, kReleaseMs, kMakeupDb, kNumParams
    };

    CompressorEffect() {
        parameterList_ = {
            {kThresholdDb, "Threshold", -60.0f, 0.0f, -18.0f, "dB"},
            {kRatio, "Ratio", 1.0f, 20.0f, 3.0f, ""},
            {kAttackMs, "Attack", 0.1f, 200.0f, 10.0f, "ms"},
            {kReleaseMs, "Release", 5.0f, 1000.0f, 120.0f, "ms"},
            {kMakeupDb, "Makeup", 0.0f, 24.0f, 0.0f, "dB"},
        };
        for (const auto& p : parameterList_) params_[p.id].store(p.defaultValue, std::memory_order_relaxed);
    }

    void prepare(double sampleRate, int) override { compresseur_.setSampleRate(sampleRate); reset(); }
    void reset() override { compresseur_.reset(); }

    void process(float* left, float* right, int numSamples) override {
        if (numSamples <= 0) return;
        vsm::audio::dsp::ScopedNoDenormals noDenormals;
        compresseur_.setThresholdDb(params_[kThresholdDb].load(std::memory_order_relaxed));
        compresseur_.setRatio(params_[kRatio].load(std::memory_order_relaxed));
        compresseur_.setAttackMs(params_[kAttackMs].load(std::memory_order_relaxed));
        compresseur_.setReleaseMs(params_[kReleaseMs].load(std::memory_order_relaxed));
        compresseur_.setMakeupDb(params_[kMakeupDb].load(std::memory_order_relaxed));

        float reduction = 1.0f;
        for (int n = 0; n < numSamples; ++n)
            reduction = std::min(reduction, compresseur_.processStereo(left[n], right[n]));
        // LA RÉDUCTION DE GAIN EST PUBLIÉE. Un compresseur dont on ne voit pas
        // travailler est un compresseur qu'on règle au hasard : on monte le
        // seuil jusqu'à entendre quelque chose, et on ne sait jamais de combien.
        gainReduction_.store(reduction, std::memory_order_relaxed);
    }

    void setParameter(vsm::audio::plugin::ParamId id, float v) override {
        if (id < kNumParams) params_[id].store(v, std::memory_order_relaxed);
    }
    float getParameter(vsm::audio::plugin::ParamId id) const override {
        return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    const char* effectName() const override { return "Compressor"; }

    /// Gain appliqué au dernier bloc (1 = pas de réduction).
    float gainReduction() const { return gainReduction_.load(std::memory_order_relaxed); }

private:
    vsm::audio::dsp::Compressor compresseur_;
    std::atomic<float> gainReduction_{1.0f};
    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
};

/// Porte de bruit d'insert. Voir `dsp::NoiseGate` pour ses quatre temps.
class GateEffect : public IAudioEffect {
public:
    enum ParamIds : vsm::audio::plugin::ParamId {
        kThresholdDb = 0, kAttackMs, kHoldMs, kReleaseMs, kRangeDb, kNumParams
    };

    GateEffect() {
        parameterList_ = {
            {kThresholdDb, "Threshold", -80.0f, 0.0f, -40.0f, "dB"},
            {kAttackMs, "Attack", 0.05f, 100.0f, 1.0f, "ms"},
            {kHoldMs, "Hold", 0.0f, 500.0f, 50.0f, "ms"},
            {kReleaseMs, "Release", 5.0f, 2000.0f, 150.0f, "ms"},
            {kRangeDb, "Range", -80.0f, 0.0f, -60.0f, "dB"},
        };
        for (const auto& p : parameterList_) params_[p.id].store(p.defaultValue, std::memory_order_relaxed);
    }

    void prepare(double sampleRate, int) override { porte_.setSampleRate(sampleRate); reset(); }
    void reset() override { porte_.reset(); }

    void process(float* left, float* right, int numSamples) override {
        if (numSamples <= 0) return;
        vsm::audio::dsp::ScopedNoDenormals noDenormals;
        porte_.setThresholdDb(params_[kThresholdDb].load(std::memory_order_relaxed));
        porte_.setAttackMs(params_[kAttackMs].load(std::memory_order_relaxed));
        porte_.setHoldMs(params_[kHoldMs].load(std::memory_order_relaxed));
        porte_.setReleaseMs(params_[kReleaseMs].load(std::memory_order_relaxed));
        porte_.setRangeDb(params_[kRangeDb].load(std::memory_order_relaxed));

        float ouverture = 0.0f;
        for (int n = 0; n < numSamples; ++n)
            ouverture = std::max(ouverture, porte_.processStereo(left[n], right[n]));
        gateOpening_.store(ouverture, std::memory_order_relaxed);
    }

    void setParameter(vsm::audio::plugin::ParamId id, float v) override {
        if (id < kNumParams) params_[id].store(v, std::memory_order_relaxed);
    }
    float getParameter(vsm::audio::plugin::ParamId id) const override {
        return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    const char* effectName() const override { return "Gate"; }

    /// Ouverture maximale atteinte sur le dernier bloc (1 = grande ouverte).
    float gateOpening() const { return gateOpening_.load(std::memory_order_relaxed); }

private:
    vsm::audio::dsp::NoiseGate porte_;
    std::atomic<float> gateOpening_{1.0f};
    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
};

/// Limiteur d'insert. Voir `dsp::Limiter` : la garantie de plafond est stricte,
/// le lookahead true-peak reste un raffinement.
class LimiterEffect : public IAudioEffect {
public:
    enum ParamIds : vsm::audio::plugin::ParamId { kCeilingDb = 0, kReleaseMs, kNumParams };

    LimiterEffect() {
        parameterList_ = {
            {kCeilingDb, "Ceiling", -24.0f, 0.0f, -0.3f, "dB"},
            {kReleaseMs, "Release", 1.0f, 500.0f, 50.0f, "ms"},
        };
        for (const auto& p : parameterList_) params_[p.id].store(p.defaultValue, std::memory_order_relaxed);
    }

    void prepare(double sampleRate, int) override { limiteur_.setSampleRate(sampleRate); reset(); }
    void reset() override { limiteur_.reset(); }

    void process(float* left, float* right, int numSamples) override {
        if (numSamples <= 0) return;
        vsm::audio::dsp::ScopedNoDenormals noDenormals;
        const float plafondDb = params_[kCeilingDb].load(std::memory_order_relaxed);
        limiteur_.setCeiling(std::pow(10.0f, plafondDb / 20.0f));
        limiteur_.setReleaseMs(params_[kReleaseMs].load(std::memory_order_relaxed));
        for (int n = 0; n < numSamples; ++n) limiteur_.processStereo(left[n], right[n]);
    }

    void setParameter(vsm::audio::plugin::ParamId id, float v) override {
        if (id < kNumParams) params_[id].store(v, std::memory_order_relaxed);
    }
    float getParameter(vsm::audio::plugin::ParamId id) const override {
        return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    const char* effectName() const override { return "Limiter"; }

private:
    vsm::audio::dsp::Limiter limiteur_;
    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
};

} // namespace vsm::audio::effect
