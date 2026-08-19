#pragma once
#include "DenormalGuard.h"
#include <algorithm>
#include <cmath>

namespace vsm::audio::dsp {

/// Compresseur feed-forward stéréo-lié (la détection utilise le max des deux
/// canaux -> l'image stéréo ne se déplace pas quand ça compresse). Détection
/// crête suivie d'un lissage attaque/relâchement en domaine linéaire de gain.
/// Genou dur (hard knee) : un genou progressif (soft knee) est un raffinement
/// simple à ajouter ensuite. RT-safe : aucun état alloué dynamiquement.
class Compressor {
public:
    void setSampleRate(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        updateTimeCoeffs();
    }
    void setThresholdDb(float db) { thresholdDb_ = db; }
    void setRatio(float ratio) { ratio_ = std::max(1.0f, ratio); }
    void setAttackMs(float ms) { attackMs_ = std::max(0.0f, ms); updateTimeCoeffs(); }
    void setReleaseMs(float ms) { releaseMs_ = std::max(1.0f, ms); updateTimeCoeffs(); }
    void setMakeupDb(float db) { makeupGain_ = dbToGain(db); }

    void reset() { gain_ = 1.0f; }

    /// Traite un échantillon stéréo en place. Renvoie la réduction de gain
    /// appliquée (0..1, 1 = pas de réduction) pour un éventuel mètre GR.
    float processStereo(float& l, float& r) {
        const float detect = std::max(std::abs(l), std::abs(r));
        const float detectDb = gainToDb(std::max(detect, 1.0e-9f));

        // Cible de gain (linéaire) d'après la courbe statique hard-knee.
        float targetGain = 1.0f;
        if (detectDb > thresholdDb_) {
            const float overDb = detectDb - thresholdDb_;
            const float compressedOverDb = overDb / ratio_;
            const float reductionDb = compressedOverDb - overDb; // <= 0
            targetGain = dbToGain(reductionDb);
        }

        // Attaque quand on doit RÉDUIRE (targetGain < gain_), relâchement sinon.
        const float coeff = (targetGain < gain_) ? attackCoeff_ : releaseCoeff_;
        gain_ = flushDenormalToZero(gain_ + (targetGain - gain_) * coeff);

        l *= gain_ * makeupGain_;
        r *= gain_ * makeupGain_;
        return gain_;
    }

private:
    static float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }
    static float gainToDb(float g) { return 20.0f * std::log10(g); }

    void updateTimeCoeffs() {
        attackCoeff_ = timeToCoeff(attackMs_);
        releaseCoeff_ = timeToCoeff(releaseMs_);
    }
    float timeToCoeff(float ms) const {
        if (ms <= 0.0f) return 1.0f;
        const float samples = std::max(1.0f, static_cast<float>(sampleRate_) * ms / 1000.0f);
        return 1.0f - std::exp(-1.0f / samples);
    }

    double sampleRate_ = 48000.0;
    float thresholdDb_ = 0.0f;
    float ratio_ = 4.0f;
    float attackMs_ = 10.0f, releaseMs_ = 100.0f;
    float makeupGain_ = 1.0f;
    float attackCoeff_ = 0.1f, releaseCoeff_ = 0.01f;
    float gain_ = 1.0f;
};

/// Limiteur brickwall stéréo-lié. Garantit |sortie| <= ceiling : l'attaque
/// est instantanée (le gain est calculé sur l'échantillon courant et appliqué
/// à CE MÊME échantillon), le relâchement est lissé pour éviter le pompage.
///
/// Simplification assumée (section 27) : pas de lookahead ni de détection
/// true-peak (inter-sample) dans cette première version -> un pic isolé subit
/// un léger écrêtage de forme d'onde plutôt qu'une réduction anticipée. Le
/// lookahead + oversampling true-peak sont un raffinement Phase 6. La
/// GARANTIE de plafond, elle, est déjà stricte et testée.
class Limiter {
public:
    void setSampleRate(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        updateReleaseCoeff();
    }
    void setCeiling(float ceilingLinear) { ceiling_ = std::max(1.0e-4f, ceilingLinear); }
    void setReleaseMs(float ms) { releaseMs_ = std::max(1.0f, ms); updateReleaseCoeff(); }

    void reset() { gain_ = 1.0f; }

    void processStereo(float& l, float& r) {
        const float peak = std::max(std::abs(l), std::abs(r));
        const float limitGain = (peak > ceiling_) ? ceiling_ / peak : 1.0f; // <= 1

        if (limitGain < gain_)
            gain_ = limitGain;                             // attaque instantanée
        else
            gain_ += (limitGain - gain_) * releaseCoeff_;  // relâchement lissé

        // Verrou dur : quoi qu'ait fait le relâchement, la sortie ne peut pas
        // franchir le plafond sur cet échantillon. Inerte tant que peak <= ceiling.
        if (peak > 0.0f)
            gain_ = std::min(gain_, ceiling_ / peak);

        l *= gain_;
        r *= gain_;
    }

private:
    void updateReleaseCoeff() {
        const float samples = std::max(1.0f, static_cast<float>(sampleRate_) * releaseMs_ / 1000.0f);
        releaseCoeff_ = 1.0f - std::exp(-1.0f / samples);
    }

    double sampleRate_ = 48000.0;
    float ceiling_ = 1.0f;
    float releaseMs_ = 50.0f;
    float releaseCoeff_ = 0.01f;
    float gain_ = 1.0f;
};

} // namespace vsm::audio::dsp
