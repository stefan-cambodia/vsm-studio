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

/// Ce qu'on mesure d'une piste (D4.7).
///
/// LA CRÊTE SEULE NE DIT PAS GRAND-CHOSE, et c'est ce que le mixeur affichait.
/// Elle dit si ça écrête ; elle ne dit pas si c'est FORT. Deux pistes de même
/// crête peuvent être séparées de quinze décibels perçus selon qu'elles sont
/// denses ou pleines de silences — et c'est la seconde qu'on cherche quand on
/// équilibre un mixage.
///
/// LA CORRÉLATION DE PHASE, elle, répond à une question qu'aucune des deux
/// autres ne pose : « qu'est-ce qu'il reste de cette piste en mono ? ». Elle
/// vaut +1 pour un signal identique sur les deux canaux, 0 pour deux canaux
/// sans rapport, et -1 quand ils sont en opposition — auquel cas la piste
/// DISPARAÎT dès qu'on somme, ce qui arrive à qui écoute sur un téléphone.
/// Rien dans ce logiciel ne le signalait.
struct TrackMeasurement {
    float peak = 0.0f;
    float rms = 0.0f;
    /// Entre -1 et +1. Vaut +1 pour une piste mono, par convention : un signal
    /// identique des deux côtés EST parfaitement corrélé.
    float correlation = 1.0f;
};

/// Banque de mètres, taille FIXE (aucune allocation, cohérent avec la
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

    /// Publie les trois mesures d'un bloc. Trois atomiques indépendantes
    /// plutôt qu'une structure : elles ne sont lues que pour être AFFICHÉES,
    /// et une valeur d'un bloc mêlée à celle du suivant se voit d'autant moins
    /// qu'un mètre est déjà lissé à l'œil.
    void reportMeasurement(size_t channelIndex, const TrackMeasurement& m) {
        if (channelIndex >= kMaxChannels) return;
        levels_[channelIndex].store(m.peak, std::memory_order_relaxed);
        rms_[channelIndex].store(m.rms, std::memory_order_relaxed);
        correlation_[channelIndex].store(m.correlation, std::memory_order_relaxed);
    }

    float readPeak(size_t channelIndex) const {
        return channelIndex < kMaxChannels ? levels_[channelIndex].load(std::memory_order_relaxed) : 0.0f;
    }
    float readRms(size_t channelIndex) const {
        return channelIndex < kMaxChannels ? rms_[channelIndex].load(std::memory_order_relaxed) : 0.0f;
    }
    float readCorrelation(size_t channelIndex) const {
        return channelIndex < kMaxChannels ? correlation_[channelIndex].load(std::memory_order_relaxed) : 1.0f;
    }

    void resetAll() {
        for (auto& level : levels_) level.store(0.0f, std::memory_order_relaxed);
        for (auto& v : rms_) v.store(0.0f, std::memory_order_relaxed);
        for (auto& v : correlation_) v.store(1.0f, std::memory_order_relaxed);
    }

private:
    std::array<std::atomic<float>, kMaxChannels> levels_{};
    std::array<std::atomic<float>, kMaxChannels> rms_{};
    std::array<std::atomic<float>, kMaxChannels> correlation_{};
};

/// La corrélation de phase d'un bloc stéréo : Σ(L·R) / √(ΣL²·ΣR²).
///
/// Fonction PURE, donc testable sans moteur -- et c'est ce qui compte, parce
/// qu'un mètre faux ne se voit pas : il affiche un chiffre plausible.
///
/// Le silence rend +1 et non zéro : deux canaux vides ne sont pas « sans
/// rapport », ils sont identiques. Afficher zéro sur une piste qui se tait
/// ferait clignoter l'aiguille au milieu à chaque blanc.
inline float phaseCorrelation(const float* left, const float* right, int numSamples) {
    if (numSamples <= 0) return 1.0f;
    double sommeLR = 0.0, sommeL2 = 0.0, sommeR2 = 0.0;
    for (int i = 0; i < numSamples; ++i) {
        const double l = left[i], r = right[i];
        sommeLR += l * r;
        sommeL2 += l * l;
        sommeR2 += r * r;
    }
    const double denominateur = std::sqrt(sommeL2 * sommeR2);
    if (denominateur <= 1.0e-20) return 1.0f;
    return static_cast<float>(std::clamp(sommeLR / denominateur, -1.0, 1.0));
}

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

/// BALANCE stéréo : le pendant du panoramique pour un signal DÉJÀ stéréo.
///
/// LA DIFFÉRENCE N'EST PAS UN DÉTAIL, et c'est un test qui l'a montrée. La loi
/// à puissance constante ci-dessus vaut 0,707 sur les DEUX canaux au centre :
/// c'est ce qu'il faut pour placer une source dans un espace stéréo sans creux
/// perçu. Mais un GROUPE reçoit un signal qui a déjà traversé cette loi, et la
/// lui appliquer une seconde fois lui coûte encore 3 dB : router deux pistes
/// dans un groupe neutre les rendait plus faibles que sans groupe. Grouper
/// serait alors un choix qu'on paie, ce qui est inacceptable.
///
/// Une balance, elle, vaut UN au centre et ne fait qu'atténuer le canal opposé
/// quand on tourne -- exactement ce que fait le potentiomètre de balance d'une
/// tranche stéréo sur une console.
inline void stereoBalance(float pan, float& gainL, float& gainR) {
    const float p = std::clamp(pan, -1.0f, 1.0f);
    gainL = p <= 0.0f ? 1.0f : 1.0f - p;
    gainR = p >= 0.0f ? 1.0f : 1.0f + p;
}

/// Mélange un bloc stéréo dans un bus stéréo par BALANCE (voir ci-dessus) :
/// neutre au centre, donc traversable sans perte.
inline float mixStereoBalancedInto(const float* inL, const float* inR, int numSamples,
                                    float volume, float pan, bool audible,
                                    float* busL, float* busR) {
    if (!audible || numSamples <= 0) return 0.0f;
    float gainL, gainR;
    stereoBalance(pan, gainL, gainR);
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        const float l = inL[i] * volume;
        const float r = inR[i] * volume;
        busL[i] += l * gainL;
        busR[i] += r * gainR;
        peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
    }
    return peak;
}

} // namespace vsm::audio::engine
