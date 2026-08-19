#pragma once
#include "Constants.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace vsm::audio::dsp {

/// Chorus stéréo par ligne à retard modulée, façon BBD (bucket-brigade
/// device) des chorus analogiques vintage type Juno-106 / Boss CE-1.
///
/// Principe : UNE seule ligne à retard mono (l'entrée est la somme mono de
/// toutes les voix du synthé) lue à DEUX positions fractionnaires
/// différentes, modulées par deux LFO sinusoïdaux en quadrature de phase
/// (décalés de 90°, MÊME fréquence). C'est ce déphasage entre les deux
/// canaux -- pas un désaccord de fréquence -- qui crée la largeur stéréo
/// caractéristique d'un vrai chorus analogique : le Juno est monophonique
/// jusqu'à cet étage, c'est le chorus qui le rend stéréo.
///
/// Caractère BBD approximé par un léger passe-bas du premier ordre sur le
/// signal traité (les lignes BBD réelles, à cause du repliement de leur
/// horloge et du compander, roulent les aigus) -- suffisant pour retrouver
/// la douceur typique, sans modéliser le compander lui-même.
///
/// Réutilisable tel quel comme effet d'insert générique une fois le
/// Mixer/chaîne d'effets construits (section 16 : "chorus" y figure
/// explicitement). Aucune allocation dans process() : le buffer est
/// dimensionné une fois dans setSampleRate() (hors thread audio).
///
/// Simplification assumée (section 27) : les modes I / II / I+II du panneau
/// d'origine combinent deux LFO à fréquences fixes ; ici un seul LFO piloté
/// par des réglages continus (rate/depth/base/mix) reproduit le CARACTÈRE,
/// pas la topologie exacte à deux oscillateurs de l'électronique d'origine.
class Chorus {
public:
    void setSampleRate(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        const size_t maxSamples =
            static_cast<size_t>(sampleRate_ * (kMaxDelayMs / 1000.0)) + 4;
        buffer_.assign(maxSamples, 0.0f);
        writeIndex_ = 0;
        updateLfoIncrement();
        // Coefficient du passe-bas BBD (one-pole) : y += (x - y)*(1 - a),
        // a = exp(-2*pi*fc/fs). fc volontairement haut (~6 kHz) : on adoucit,
        // on n'étouffe pas.
        lpCoeff_ = std::exp(-2.0f * static_cast<float>(kPi) * kBbdRollOffHz /
                            static_cast<float>(sampleRate_));
    }

    void setRateHz(float hz) { rateHz_ = std::max(0.01f, hz); updateLfoIncrement(); }
    void setDepthMs(float ms) { depthMs_ = std::max(0.0f, ms); }
    void setBaseDelayMs(float ms) { baseDelayMs_ = std::max(0.1f, ms); }
    void setMix(float mix) { mix_ = std::clamp(mix, 0.0f, 1.0f); }

    void reset() {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        writeIndex_ = 0;
        phase_ = 0.0;
        lpStateL_ = lpStateR_ = 0.0f;
    }

    /// Traite un échantillon mono `in`, produit une paire stéréo.
    void process(float in, float& outL, float& outR) {
        if (buffer_.empty()) { outL = in; outR = in; return; }

        buffer_[writeIndex_] = in;

        const float lfoL = std::sin(static_cast<float>(phase_ * kTwoPi));
        const float lfoR = std::sin(static_cast<float>((phase_ + 0.25) * kTwoPi)); // +90°

        const float wetL = readTap(lfoL);
        const float wetR = readTap(lfoR);

        // Passe-bas BBD (one-pole).
        lpStateL_ += (wetL - lpStateL_) * (1.0f - lpCoeff_);
        lpStateR_ += (wetR - lpStateR_) * (1.0f - lpCoeff_);

        outL = in * (1.0f - mix_) + lpStateL_ * mix_;
        outR = in * (1.0f - mix_) + lpStateR_ * mix_;

        phase_ += lfoInc_;
        if (phase_ >= 1.0) phase_ -= 1.0;
        if (++writeIndex_ >= buffer_.size()) writeIndex_ = 0;
    }

private:
    float readTap(float lfo) const {
        // lfo dans [-1,1] -> retard dans [base, base+depth].
        const float delayMs = baseDelayMs_ + depthMs_ * (0.5f + 0.5f * lfo);
        const float delaySamples = delayMs * static_cast<float>(sampleRate_) / 1000.0f;

        float readPos = static_cast<float>(writeIndex_) - delaySamples;
        const float size = static_cast<float>(buffer_.size());
        while (readPos < 0.0f) readPos += size;

        const size_t i0 = static_cast<size_t>(readPos);
        const float frac = readPos - static_cast<float>(i0);
        size_t i1 = i0 + 1;
        if (i1 >= buffer_.size()) i1 = 0;

        return buffer_[i0] * (1.0f - frac) + buffer_[i1] * frac;
    }

    void updateLfoIncrement() { lfoInc_ = rateHz_ / sampleRate_; }

    static constexpr float kMaxDelayMs = 50.0f;
    static constexpr float kBbdRollOffHz = 6000.0f;

    double sampleRate_ = 48000.0;
    float rateHz_ = 0.5f;
    float depthMs_ = 2.7f;
    float baseDelayMs_ = 7.5f;
    float mix_ = 0.5f;

    std::vector<float> buffer_;
    size_t writeIndex_ = 0;
    double phase_ = 0.0;
    double lfoInc_ = 0.0;
    float lpCoeff_ = 0.0f;
    float lpStateL_ = 0.0f, lpStateR_ = 0.0f;
};

} // namespace vsm::audio::dsp
