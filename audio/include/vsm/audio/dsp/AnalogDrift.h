#pragma once
#include "vsm/util/DeterministicRng.h"
#include <algorithm>
#include <cmath>

namespace vsm::audio::dsp {

/// Génère une dérive lente et corrélée (façon dérive thermique/vieillissement
/// d'un composant analogique réel), pilotée par le paramètre global ANALOG
/// CHARACTER de la section 8 du cahier des charges :
///
///   0%   -> parfaitement stable (sortie toujours 0)
///   25%  -> légère variation
///   50%  -> comportement "vintage" typique
///   75%  -> machine vieillissante
///   100% -> comportement très instable
///
/// Déterministe et SEEDÉ (vsm::util::DeterministicRng) : une session est
/// rejouable à l'identique avec la même graine, comme l'exige le cahier
/// des charges. Techniquement : bruit blanc passé dans un filtre passe-bas
/// du premier ordre, pour obtenir une dérive LENTE et corrélée dans le
/// temps -- pas du bruit haute fréquence, qui sonnerait comme un défaut
/// numérique plutôt qu'une dérive analogique.
class AnalogDrift {
public:
    void setSampleRate(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        updateSmoothingCoefficient();
    }
    void setSeed(uint64_t seed) { rng_ = vsm::util::DeterministicRng(seed); }

    /// 0..1, correspond à 0%..100% ANALOG CHARACTER.
    void setAmount(float amount) { amount_ = std::clamp(amount, 0.0f, 1.0f); }

    /// Fréquence de coupure de la dérive, en Hz. Volontairement très basse
    /// par défaut : une dérive audible doit être lente (dizaines de
    /// secondes), jamais un vibrato ou un LFO rapide.
    void setRateHz(float hz) {
        rateHz_ = std::max(0.001f, hz);
        updateSmoothingCoefficient();
    }

    /// Renvoie la déviation courante (bruit filtré, amplitude typique dans
    /// [-amount, +amount] sans borne stricte garantie). À l'appelant de
    /// mettre à l'échelle : demi-tons pour un pitch, octaves pour un
    /// cutoff, etc.
    float nextValue() {
        float target = rng_.nextBipolar() * amount_;
        state_ += (target - state_) * smoothingCoeff_;
        return state_;
    }

private:
    void updateSmoothingCoefficient() {
        float timeSamples = std::max(1.0f, static_cast<float>(sampleRate_) / rateHz_);
        smoothingCoeff_ = 1.0f - std::exp(-1.0f / timeSamples);
    }

    double sampleRate_ = 48000.0;
    float amount_ = 0.0f;
    float rateHz_ = 0.3f; // dérive lente par défaut (~qq secondes de constante de temps)
    float smoothingCoeff_ = 0.0001f;
    float state_ = 0.0f;
    vsm::util::DeterministicRng rng_{0};
};

} // namespace vsm::audio::dsp
