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

/// PORTE DE BRUIT stéréo-liée (D4.1). Elle n'existait pas : la tranche master
/// porte un égaliseur, un compresseur, une saturation et un limiteur, mais rien
/// pour faire TAIRE ce qui est en dessous d'un seuil.
///
/// LES QUATRE TEMPS D'UNE PORTE, et pourquoi il en faut quatre. Un simple
/// « si c'est faible, coupe » produit un hachage : le signal passe sans cesse
/// de part et d'autre du seuil, et la porte bat. Il faut donc :
///
///  - une ATTAQUE, pour ouvrir sans claquer ;
///  - un MAINTIEN (`hold`), qui garde la porte ouverte un temps minimum après
///    le dernier dépassement -- c'est lui qui empêche le hachage sur une note
///    tenue dont l'amplitude ondule autour du seuil ;
///  - un RELÂCHEMENT, pour fermer sans couper la queue d'une résonance. C'est
///    une CONSTANTE DE TEMPS et non un délai de fermeture -- il en faut trois
///    ou quatre pour atteindre le plancher --, comme pour le compresseur
///    ci-dessus : deux conventions dans le même fichier seraient un piège ;
///  - une PLAGE (`range`), qui dit de combien on atténue au lieu de couper
///    net. Une porte qui ferme complètement s'entend respirer ; une porte qui
///    atténue de 20 dB fait le travail sans se faire remarquer.
///
/// RT-safe : aucun état alloué, aucune branche coûteuse dans la boucle.
class NoiseGate {
public:
    void setSampleRate(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        updateCoeffs();
    }
    void setThresholdDb(float db) { thresholdLin_ = std::pow(10.0f, db / 20.0f); }
    void setAttackMs(float ms) { attackMs_ = std::max(0.01f, ms); updateCoeffs(); }
    void setHoldMs(float ms) { holdMs_ = std::max(0.0f, ms); updateCoeffs(); }
    void setReleaseMs(float ms) { releaseMs_ = std::max(1.0f, ms); updateCoeffs(); }
    /// Atténuation quand la porte est fermée, en dB (négatif). 0 = la porte ne
    /// fait rien ; -80 dB revient pratiquement à couper.
    void setRangeDb(float db) { floorGain_ = std::pow(10.0f, std::min(0.0f, db) / 20.0f); }

    void reset() { gain_ = 1.0f; holdRestant_ = 0; }

    /// Traite un échantillon stéréo en place. Renvoie le gain appliqué
    /// (1 = ouverte, `range` = fermée), pour un éventuel témoin.
    float processStereo(float& l, float& r) {
        const float detect = std::max(std::abs(l), std::abs(r));
        if (detect >= thresholdLin_) {
            holdRestant_ = holdEchantillons_;
        } else if (holdRestant_ > 0) {
            --holdRestant_;
        }
        // OUVERTE tant qu'on dépasse le seuil OU que le maintien court encore.
        const float cible = (detect >= thresholdLin_ || holdRestant_ > 0) ? 1.0f : floorGain_;
        const float coeff = (cible > gain_) ? attackCoeff_ : releaseCoeff_;
        gain_ = flushDenormalToZero(gain_ + (cible - gain_) * coeff);
        l *= gain_;
        r *= gain_;
        return gain_;
    }

private:
    void updateCoeffs() {
        attackCoeff_ = timeToCoeff(attackMs_);
        releaseCoeff_ = timeToCoeff(releaseMs_);
        holdEchantillons_ = static_cast<int>(sampleRate_ * holdMs_ / 1000.0);
    }
    float timeToCoeff(float ms) const {
        if (ms <= 0.0f) return 1.0f;
        const float samples = std::max(1.0f, static_cast<float>(sampleRate_) * ms / 1000.0f);
        return 1.0f - std::exp(-1.0f / samples);
    }

    double sampleRate_ = 48000.0;
    float thresholdLin_ = 0.01f;      // -40 dB
    float attackMs_ = 1.0f, holdMs_ = 50.0f, releaseMs_ = 100.0f;
    float floorGain_ = 0.0f;          // fermeture complète par défaut
    float attackCoeff_ = 0.5f, releaseCoeff_ = 0.01f;
    float gain_ = 1.0f;
    int holdEchantillons_ = 2400;
    int holdRestant_ = 0;
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
