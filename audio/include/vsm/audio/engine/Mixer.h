#pragma once
#include "vsm/audio/dsp/Constants.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>

namespace vsm::audio::engine {

/// Loi de panoramique à puissance constante (equal-power) : évite le creux
/// perçu au centre qu'une loi linéaire naïve produirait.
inline void equalPowerPan(float pan, float& gainL, float& gainR) {
    float p = std::clamp(pan, -1.0f, 1.0f);
    float angle = (p + 1.0f) * 0.25f * static_cast<float>(vsm::audio::dsp::kPi); // 0..pi/2
    gainL = std::cos(angle);
    gainR = std::sin(angle);
}

/// Mélange un bloc mono `input` (pondéré par `volume`/`pan`) dans le bus
/// stéréo `busL`/`busR` par ACCUMULATION (plusieurs pistes s'additionnent
/// sur le même bus, jamais d'écrasement).
///
/// Volontairement SANS struct "MixerChannelSettings" séparée : le
/// volume/pan/mute/solo d'une piste sont déjà portés par
/// vsm::sequencer::Track (Phase 1) -- les dupliquer ici créerait deux
/// sources de vérité pouvant diverger. `audible` (résolu par l'appelant à
/// partir de Track::muted/solo, via la même logique déjà testée dans
/// vsm::sequencer::PlaybackScheduler) découple ce calcul du reste.
///
/// Renvoie le pic absolu observé sur ce bloc (à reporter dans MeterBank).
inline float mixChannelInto(const float* input, int numSamples, float volume, float pan,
                             bool audible, float* busL, float* busR) {
    if (!audible || numSamples <= 0) return 0.0f;

    float gainL, gainR;
    equalPowerPan(pan, gainL, gainR);

    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        float s = input[i] * volume;
        busL[i] += s * gainL;
        busR[i] += s * gainR;
        peak = std::max(peak, std::abs(s));
    }
    return peak;
}

/// Banque de mètres crête, taille FIXE (aucune allocation, cohérent avec la
/// contrainte realtime du chemin audio). 128 canaux couvrent largement les
/// "100 pistes MIDI minimum" de la section 23 ; au-delà, silencieusement
/// ignoré côté report (le mixage lui-même n'est pas limité).
class MeterBank {
public:
    static constexpr size_t kMaxChannels = 128;

    void reportPeak(size_t channelIndex, float peak) {
        if (channelIndex < kMaxChannels)
            levels_[channelIndex].store(peak, std::memory_order_relaxed);
    }

    float readPeak(size_t channelIndex) const {
        return channelIndex < kMaxChannels ? levels_[channelIndex].load(std::memory_order_relaxed) : 0.0f;
    }

    void resetAll() {
        for (auto& level : levels_) level.store(0.0f, std::memory_order_relaxed);
    }

private:
    std::array<std::atomic<float>, kMaxChannels> levels_{};
};

/// Variante STÉRÉO : mélange une source stéréo (déjà passée par ses effets
/// d'insert) dans le bus, avec volume + pan equal-power appliqué comme
/// balance. Pour une source mono (L==R), le résultat est identique à
/// mixChannelInto -> aucune régression sur les pistes sans effet.
inline float mixStereoInto(const float* inL, const float* inR, int numSamples,
                            float volume, float pan, bool audible, float* busL, float* busR) {
    if (!audible || numSamples <= 0) return 0.0f;
    float gainL, gainR;
    equalPowerPan(pan, gainL, gainR);
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        float l = inL[i] * volume;
        float r = inR[i] * volume;
        busL[i] += l * gainL;
        busR[i] += r * gainR;
        peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
    }
    return peak;
}

} // namespace vsm::audio::engine
