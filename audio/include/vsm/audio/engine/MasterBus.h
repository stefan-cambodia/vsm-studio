#pragma once
#include "vsm/audio/dsp/Biquad.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Dynamics.h"
#include "vsm/audio/dsp/LufsMeter.h"
#include "vsm/audio/plugin/ParameterTypes.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::audio::engine {

/// Tranche master de la console (section 15) appliquée au bus stéréo final :
///
///   EQ 3 bandes -> Compresseur -> Saturation -> Largeur stéréo -> Limiteur
///                                                                   |
///                                                             Mètre LUFS + crête
///
/// Deux principes structurants :
///  - BYPASS PAR DÉFAUT (`enabled_ == false`) : tant que l'utilisateur ne
///    l'active pas, process() ne touche RIEN -> le rendu du ProcessGraph est
///    strictement identique à ce qu'il était avant l'ajout du bus master
///    (aucune régression des tests existants). C'est la façon d'ajouter un
///    étage master sans réécrire le chemin de rendu.
///  - RT-safe : tous les états (biquads, dynamique) sont membres, aucun buffer
///    alloué dans process(). Les paramètres arrivent du thread UI via des
///    std::atomic, relus une fois par bloc dans process().
///
/// Les paramètres sont exposés via le MÊME modèle que les instruments
/// (vsm::audio::plugin::ParameterList) : le bus master est ainsi, comme les
/// synthés, directement "ParameterDescriptor-ready" pour la future couche
/// d'interopérabilité (addon Phase 7) sans dépendre d'elle aujourd'hui.
class MasterBus {
public:
    enum ParamIds : vsm::audio::plugin::ParamId {
        kEnabled = 0,
        kLowShelfGainDb,
        kMidFreq,
        kMidGainDb,
        kMidQ,
        kHighShelfGainDb,
        kCompThresholdDb,
        kCompRatio,
        kCompAttackMs,
        kCompReleaseMs,
        kCompMakeupDb,
        kSaturationDrive,
        kStereoWidth,
        kLimiterCeilingDb,
        kNumParams
    };

    MasterBus() {
        parameterList_ = {
            {kEnabled, "Master Enabled", 0.0f, 1.0f, 0.0f, ""},
            {kLowShelfGainDb, "EQ Low Gain", -18.0f, 18.0f, 0.0f, "dB"},
            {kMidFreq, "EQ Mid Freq", 200.0f, 8000.0f, 1000.0f, "Hz"},
            {kMidGainDb, "EQ Mid Gain", -18.0f, 18.0f, 0.0f, "dB"},
            {kMidQ, "EQ Mid Q", 0.2f, 8.0f, 0.8f, ""},
            {kHighShelfGainDb, "EQ High Gain", -18.0f, 18.0f, 0.0f, "dB"},
            {kCompThresholdDb, "Comp Threshold", -48.0f, 0.0f, 0.0f, "dB"},
            {kCompRatio, "Comp Ratio", 1.0f, 20.0f, 2.0f, ""},
            {kCompAttackMs, "Comp Attack", 0.1f, 200.0f, 10.0f, "ms"},
            {kCompReleaseMs, "Comp Release", 5.0f, 1000.0f, 120.0f, "ms"},
            {kCompMakeupDb, "Comp Makeup", 0.0f, 24.0f, 0.0f, "dB"},
            {kSaturationDrive, "Saturation", 0.0f, 1.0f, 0.0f, ""},
            {kStereoWidth, "Stereo Width", 0.0f, 2.0f, 1.0f, ""},
            {kLimiterCeilingDb, "Limiter Ceiling", -12.0f, 0.0f, -0.3f, "dB"},
        };
        for (const auto& info : parameterList_)
            params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
    }

    void prepare(double sampleRate, int /*maxBlockSize*/) {
        sampleRate_ = sampleRate;
        for (auto* f : {&lowShelfL_, &lowShelfR_, &midL_, &midR_, &highShelfL_, &highShelfR_})
            f->setSampleRate(sampleRate);
        compressor_.setSampleRate(sampleRate);
        limiter_.setSampleRate(sampleRate);
        lufs_.prepare(sampleRate);
        reset();
    }

    void reset() {
        for (auto* f : {&lowShelfL_, &lowShelfR_, &midL_, &midR_, &highShelfL_, &highShelfR_})
            f->reset();
        compressor_.reset();
        limiter_.reset();
        lufs_.reset();
        outputPeak_.store(0.0f, std::memory_order_relaxed);
    }

    // --- API paramètres (mêmes contrats que ISynthPlugin) ----------------
    void setParameter(vsm::audio::plugin::ParamId id, float value) {
        if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
    }
    float getParameter(vsm::audio::plugin::ParamId id) const {
        return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const { return parameterList_; }

    void setEnabled(bool on) { params_[kEnabled].store(on ? 1.0f : 0.0f, std::memory_order_relaxed); }
    bool isEnabled() const { return params_[kEnabled].load(std::memory_order_relaxed) >= 0.5f; }

    /// Mesures pour l'UI (thread UI). Valides après au moins un process().
    double integratedLufs() const { return lufs_.integratedLufs(); }
    float outputPeak() const { return outputPeak_.load(std::memory_order_relaxed); }
    /// Valeur efficace du bus final sur le dernier bloc (D4.7).
    float outputRms() const { return outputRms_.load(std::memory_order_relaxed); }
    /// Corrélation de phase du bus final, de -1 à +1. Voir `phaseCorrelation`.
    float outputCorrelation() const { return outputCorrelation_.load(std::memory_order_relaxed); }

    /// Traite le bus stéréo EN PLACE. No-op complet si désactivé.
    void process(float* left, float* right, int numSamples) {
        if (!isEnabled() || numSamples <= 0) return;

        vsm::audio::dsp::ScopedNoDenormals noDenormals;

        // Coefficients EQ recalculés une fois par bloc (hors boucle sample).
        const float lowGain = params_[kLowShelfGainDb].load(std::memory_order_relaxed);
        const float midFreq = params_[kMidFreq].load(std::memory_order_relaxed);
        const float midGain = params_[kMidGainDb].load(std::memory_order_relaxed);
        const float midQ = params_[kMidQ].load(std::memory_order_relaxed);
        const float highGain = params_[kHighShelfGainDb].load(std::memory_order_relaxed);
        using B = vsm::audio::dsp::Biquad;
        lowShelfL_.set(B::Type::LowShelf, 120.0f, 0.707f, lowGain);
        lowShelfR_.set(B::Type::LowShelf, 120.0f, 0.707f, lowGain);
        midL_.set(B::Type::Peaking, midFreq, midQ, midGain);
        midR_.set(B::Type::Peaking, midFreq, midQ, midGain);
        highShelfL_.set(B::Type::HighShelf, 8000.0f, 0.707f, highGain);
        highShelfR_.set(B::Type::HighShelf, 8000.0f, 0.707f, highGain);

        compressor_.setThresholdDb(params_[kCompThresholdDb].load(std::memory_order_relaxed));
        compressor_.setRatio(params_[kCompRatio].load(std::memory_order_relaxed));
        compressor_.setAttackMs(params_[kCompAttackMs].load(std::memory_order_relaxed));
        compressor_.setReleaseMs(params_[kCompReleaseMs].load(std::memory_order_relaxed));
        compressor_.setMakeupDb(params_[kCompMakeupDb].load(std::memory_order_relaxed));

        const float drive = params_[kSaturationDrive].load(std::memory_order_relaxed);
        const float width = params_[kStereoWidth].load(std::memory_order_relaxed);
        const float ceilingDb = params_[kLimiterCeilingDb].load(std::memory_order_relaxed);
        limiter_.setCeiling(std::pow(10.0f, ceilingDb / 20.0f));

        // Saturation : drive 0 -> transparent. tanh normalisé pour garder un
        // gain ~unitaire à faible niveau. Sans oversampling ici -> le drive
        // reste modéré (repliement Phase 6 via l'oversampler à venir).
        const float satAmount = drive;
        const float satPre = 1.0f + satAmount * 6.0f;
        const float satNorm = (satAmount > 0.0f) ? (1.0f / std::tanh(satPre)) : 1.0f;

        float blockPeak = 0.0f;
        double sommeL2 = 0.0, sommeR2 = 0.0, sommeLR = 0.0;
        for (int i = 0; i < numSamples; ++i) {
            float l = left[i];
            float r = right[i];

            // EQ
            l = highShelfL_.process(midL_.process(lowShelfL_.process(l)));
            r = highShelfR_.process(midR_.process(lowShelfR_.process(r)));

            // Compresseur (stéréo-lié)
            compressor_.processStereo(l, r);

            // Saturation douce
            if (satAmount > 0.0f) {
                l = std::tanh(l * satPre) * satNorm;
                r = std::tanh(r * satPre) * satNorm;
            }

            // Largeur stéréo (mid/side). Court-circuit quand la largeur est
            // neutre : comparaison par tolérance, pas d'égalité flottante
            // exacte (sinon -Wfloat-equal sous les flags stricts de JUCE).
            if (std::abs(width - 1.0f) > 1.0e-6f) {
                const float mid = 0.5f * (l + r);
                const float side = 0.5f * (l - r) * width;
                l = mid + side;
                r = mid - side;
            }

            // Limiteur brickwall
            limiter_.processStereo(l, r);

            left[i] = l;
            right[i] = r;
            blockPeak = std::max(blockPeak, std::max(std::abs(l), std::abs(r)));
            sommeL2 += static_cast<double>(l) * l;
            sommeR2 += static_cast<double>(r) * r;
            sommeLR += static_cast<double>(l) * r;

            lufs_.processStereo(l, r);
        }
        outputPeak_.store(blockPeak, std::memory_order_relaxed);
        // CORRÉLATION DE PHASE DU MASTER (D4.7) : ce qu'il reste du mixage en
        // mono. Négative, le morceau se vide dès qu'on l'écoute sur un
        // téléphone -- et rien ne le disait.
        outputRms_.store(numSamples > 0
                             ? static_cast<float>(std::sqrt((sommeL2 + sommeR2) / (2.0 * numSamples)))
                             : 0.0f,
                          std::memory_order_relaxed);
        const double denominateur = std::sqrt(sommeL2 * sommeR2);
        outputCorrelation_.store(denominateur > 1.0e-20
                                     ? static_cast<float>(std::clamp(sommeLR / denominateur, -1.0, 1.0))
                                     : 1.0f,
                                  std::memory_order_relaxed);
    }

private:
    double sampleRate_ = 48000.0;

    vsm::audio::dsp::Biquad lowShelfL_, lowShelfR_, midL_, midR_, highShelfL_, highShelfR_;
    vsm::audio::dsp::Compressor compressor_;
    vsm::audio::dsp::Limiter limiter_;
    vsm::audio::dsp::LufsMeter lufs_;

    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
    std::atomic<float> outputPeak_{0.0f};
    std::atomic<float> outputRms_{0.0f};
    std::atomic<float> outputCorrelation_{1.0f};
};

} // namespace vsm::audio::engine
