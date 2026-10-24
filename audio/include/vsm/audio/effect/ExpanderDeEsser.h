#pragma once
#include "IAudioEffect.h"
#include "vsm/audio/dsp/Biquad.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::audio::effect {

/// D596 : DEUX DYNAMIQUES QUE LE PARC N'AVAIT PAS — l'expandeur et le de-esser de Cubase (*Expander*, *DeEsser*).
///
/// LA PORTE N'EST PAS UN EXPANDEUR. Elle bascule entre ouverte et sa plage : un souffle sous une voix se coupe ou ne se
/// coupe pas. L'expandeur BAISSE EN PROPORTION : sous le seuil, chaque décibel d'écart en retire `rapport − 1`.
///
/// Une constante de temps en coefficient d'un filtre à un pôle (la convention de `dsp::NoiseGate`).
inline float coefficientDeTemps(double sampleRate, float ms) {
    if (ms <= 0.0f) return 1.0f;
    const float echantillons = std::max(1.0f, static_cast<float>(sampleRate) * ms / 1000.0f);
    return 1.0f - std::exp(-1.0f / echantillons);
}

/// Expandeur vers le bas, stéréo lié. DÉTECTION : la crête (montée immédiate, descente au relâchement). GAIN : en dB,
/// `−min(−plage, (seuil − niveau)·(rapport − 1))` sous le seuil, 0 au-dessus ; lissé par l'attaque quand il remonte
/// (l'expandeur s'ouvre) et par le relâchement quand il descend. À l'attaque d'un son, l'expandeur part FERMÉ (le silence
/// d'avant est sous le seuil) et s'ouvre en `attaque` ; le gain qui touche sa cible à 10⁻⁴ dB près s'y POSE : en régime
/// établi au-dessus du seuil, le gain vaut 0 dB exactement, et le signal sort au bit près.
class ExpanderEffect : public IAudioEffect {
public:
    enum ParamIds : vsm::audio::plugin::ParamId {
        kThresholdDb = 0, kRatio, kRangeDb, kAttackMs, kReleaseMs, kNumParams
    };

    ExpanderEffect() {
        parameterList_ = {
            {kThresholdDb, "Threshold", -80.0f, 0.0f, -40.0f, "dB"},
            {kRatio, "Ratio", 1.0f, 8.0f, 2.0f, ":1"},
            {kRangeDb, "Range", -80.0f, 0.0f, -40.0f, "dB"},
            {kAttackMs, "Attack", 0.1f, 100.0f, 1.0f, "ms"},
            {kReleaseMs, "Release", 5.0f, 2000.0f, 100.0f, "ms"},
        };
        for (const auto& p : parameterList_) params_[p.id].store(p.defaultValue, std::memory_order_relaxed);
    }

    void prepare(double sampleRate, int) override { sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0; reset(); }
    void reset() override { enveloppe_ = 0.0f; gainDb_ = 0.0f; }

    void process(float* left, float* right, int numSamples) override {
        if (numSamples <= 0) return;
        vsm::audio::dsp::ScopedNoDenormals noDenormals;
        const float seuil = params_[kThresholdDb].load(std::memory_order_relaxed);
        const float rapport = std::max(1.0f, params_[kRatio].load(std::memory_order_relaxed));
        const float plage = std::min(0.0f, params_[kRangeDb].load(std::memory_order_relaxed));
        const float attaque = coefficientDeTemps(sampleRate_, params_[kAttackMs].load(std::memory_order_relaxed));
        const float relache = coefficientDeTemps(sampleRate_, params_[kReleaseMs].load(std::memory_order_relaxed));
        const float descente = 1.0f - relache;   // la crête retombe au rythme du relâchement
        float reduction = 0.0f;
        for (int n = 0; n < numSamples; ++n) {
            const float crete = std::max(std::abs(left[n]), std::abs(right[n]));
            enveloppe_ = crete > enveloppe_ ? crete : vsm::audio::dsp::flushDenormalToZero(enveloppe_ * descente);
            const float niveau = 20.0f * std::log10(std::max(enveloppe_, 1.0e-9f));
            const float cible = niveau >= seuil ? 0.0f : std::max(plage, -(seuil - niveau) * (rapport - 1.0f));
            if (cible != gainDb_) {
                gainDb_ += (cible - gainDb_) * (cible > gainDb_ ? attaque : relache);
                if (std::abs(cible - gainDb_) < 1.0e-4f) gainDb_ = cible;   // posé : 0 dB redevient EXACT
            }
            if (gainDb_ != 0.0f) {
                const float g = std::pow(10.0f, gainDb_ / 20.0f);
                left[n] *= g;
                right[n] *= g;
            }
            reduction = std::min(reduction, gainDb_);
        }
        reductionDb_.store(reduction, std::memory_order_relaxed);
    }

    void setParameter(vsm::audio::plugin::ParamId id, float v) override {
        if (id < kNumParams) params_[id].store(v, std::memory_order_relaxed);
    }
    float getParameter(vsm::audio::plugin::ParamId id) const override {
        return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    const char* effectName() const override { return "Expander"; }

    /// La plus forte réduction du dernier bloc, en dB (0 = rien retiré).
    float reductionDb() const { return reductionDb_.load(std::memory_order_relaxed); }

private:
    double sampleRate_ = 48000.0;
    float enveloppe_ = 0.0f;
    float gainDb_ = 0.0f;
    std::atomic<float> reductionDb_{0.0f};
    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
};

/// De-esser LARGE BANDE. DÉTECTION : un passe-bande de RBJ (Q = 1,4, gain de crête 0 dB) à la FRÉQUENCE, sur la moyenne
/// des deux canaux ; son niveau est la valeur efficace lissée sur 5 ms, rapportée à la CRÊTE d'un sinus de même valeur
/// (+3,01 dB) — une crête échantillonnée d'un sinus à 8 kHz (six échantillons par période) peut manquer le sommet de
/// 1,25 dB, la valeur efficace non. Au-dessus du seuil, la réduction vaut `(niveau − seuil)·(1 − 1/4)`, bornée par la
/// réduction maximale ; temps fixes (1 ms pour réduire, 60 ms pour rendre), ceux d'une consonne. Le partage par bande est
/// remis (ROADMAP-daw, D596). Sous le seuil depuis le départ, le signal sort au bit près.
class DeEsserEffect : public IAudioEffect {
public:
    enum ParamIds : vsm::audio::plugin::ParamId { kFrequencyHz = 0, kThresholdDb, kMaxReductionDb, kNumParams };

    DeEsserEffect() {
        parameterList_ = {
            {kFrequencyHz, "Frequency", 2000.0f, 12000.0f, 6000.0f, "Hz"},
            {kThresholdDb, "Threshold", -60.0f, 0.0f, -30.0f, "dB"},
            {kMaxReductionDb, "Max Reduction", 0.0f, 24.0f, 12.0f, "dB"},
        };
        for (const auto& p : parameterList_) params_[p.id].store(p.defaultValue, std::memory_order_relaxed);
    }

    void prepare(double sampleRate, int) override {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        frequenceVue_ = -1.0f;
        reset();
    }
    void reset() override { bande_.reset(); energie_ = 0.0f; gainDb_ = 0.0f; }

    void process(float* left, float* right, int numSamples) override {
        if (numSamples <= 0) return;
        vsm::audio::dsp::ScopedNoDenormals noDenormals;
        const float frequence = params_[kFrequencyHz].load(std::memory_order_relaxed);
        if (frequence != frequenceVue_) poserLaBande(frequence);
        const float seuil = params_[kThresholdDb].load(std::memory_order_relaxed);
        const float maximum = std::max(0.0f, params_[kMaxReductionDb].load(std::memory_order_relaxed));
        const float lissage = coefficientDeTemps(sampleRate_, 5.0f);
        const float reduire = coefficientDeTemps(sampleRate_, 1.0f);
        const float rendre = coefficientDeTemps(sampleRate_, 60.0f);
        float reduction = 0.0f;
        for (int n = 0; n < numSamples; ++n) {
            const float b = bande_.process(0.5f * (left[n] + right[n]));
            energie_ = vsm::audio::dsp::flushDenormalToZero(energie_ + (b * b - energie_) * lissage);
            const float niveau = 10.0f * std::log10(std::max(2.0f * energie_, 1.0e-18f));   // crête d'un sinus
            const float cible = niveau > seuil ? -std::min(maximum, (niveau - seuil) * 0.75f) : 0.0f;
            if (cible != gainDb_) {
                gainDb_ += (cible - gainDb_) * (cible < gainDb_ ? reduire : rendre);
                if (std::abs(cible - gainDb_) < 1.0e-4f) gainDb_ = cible;   // posé : 0 dB redevient EXACT
            }
            if (gainDb_ != 0.0f) {
                const float g = std::pow(10.0f, gainDb_ / 20.0f);
                left[n] *= g;
                right[n] *= g;
            }
            reduction = std::min(reduction, gainDb_);
        }
        reductionDb_.store(reduction, std::memory_order_relaxed);
    }

    void setParameter(vsm::audio::plugin::ParamId id, float v) override {
        if (id < kNumParams) params_[id].store(v, std::memory_order_relaxed);
    }
    float getParameter(vsm::audio::plugin::ParamId id) const override {
        return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    const char* effectName() const override { return "De-esser"; }

    float reductionDb() const { return reductionDb_.load(std::memory_order_relaxed); }

private:
    void poserLaBande(float frequence) {
        // RBJ, passe-bande à gain de crête de 0 dB : b0 = α, b1 = 0, b2 = −α ; a0 = 1 + α, a1 = −2 cos ω0, a2 = 1 − α.
        const double f0 = std::clamp(static_cast<double>(frequence), 20.0, sampleRate_ * 0.45);
        const double w0 = 2.0 * 3.14159265358979323846 * f0 / sampleRate_;
        const double alpha = std::sin(w0) / (2.0 * 1.4);
        const double a0 = 1.0 + alpha;
        bande_.setCoefficients(static_cast<float>(alpha / a0), 0.0f, static_cast<float>(-alpha / a0),
                               static_cast<float>(-2.0 * std::cos(w0) / a0), static_cast<float>((1.0 - alpha) / a0));
        frequenceVue_ = frequence;
    }

    double sampleRate_ = 48000.0;
    vsm::audio::dsp::Biquad bande_;
    float frequenceVue_ = -1.0f;
    float energie_ = 0.0f;
    float gainDb_ = 0.0f;
    std::atomic<float> reductionDb_{0.0f};
    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
};

} // namespace vsm::audio::effect
