#pragma once
#include "Constants.h"
#include <cmath>

namespace vsm::audio::dsp {

enum class Waveform { Sine, Saw, Square, Triangle };

/// Oscillateur à bande limitée par correction PolyBLEP aux discontinuités
/// (section 14 : "PolyBLEP, band-limited oscillators"). Volontairement
/// générique (pas encore la modélisation fine d'un oscillateur de synthé
/// précis, qui viendra Phase 3 avec les machines de référence) : c'est le
/// bloc de base réutilisable par TOUTES les futures émulations.
class BandLimitedOscillator {
public:
    void setSampleRate(double sampleRate) { sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0; }
    void setFrequency(float hz) { frequencyHz_ = hz; }
    void setWaveform(Waveform w) { waveform_ = w; }
    void setPulseWidth(float pw) { pulseWidth_ = clamp01(pw, 0.02f, 0.98f); }

    void reset(double phase = 0.0) {
        phase_ = phase - std::floor(phase);
    }

    /// Produit un échantillon et avance la phase d'un pas.
    float nextSample() {
        double dt = frequencyHz_ / sampleRate_;
        float sample = 0.0f;

        switch (waveform_) {
            case Waveform::Sine:
                sample = static_cast<float>(std::sin(phase_ * kTwoPi));
                break;

            case Waveform::Saw: {
                sample = static_cast<float>(2.0 * phase_ - 1.0);
                sample -= polyBlep(phase_, dt);
                break;
            }

            case Waveform::Square: {
                sample = (phase_ < pulseWidth_) ? 1.0f : -1.0f;
                sample += polyBlep(phase_, dt);
                double t2 = std::fmod(phase_ + (1.0 - pulseWidth_), 1.0);
                sample -= polyBlep(t2, dt);
                break;
            }

            case Waveform::Triangle: {
                // Formule directe (pas d'intégration) : bornée à [-1, 1] par
                // construction, donc sans aucun risque de dépassement ou de
                // dérive transitoire. Compromis Phase 2 assumé : le triangle
                // n'est PAS anti-aliasé ici (contrairement à saw/square) --
                // ses harmoniques décroissent bien plus vite (en 1/n²
                // contre 1/n), l'aliasing y est nettement moins perceptible
                // en pratique. Une version anti-aliasée (DPW - Differentiated
                // Polynomial Waveform, plus robuste qu'un intégrateur "leaky"
                // naïf) est un raffinement Phase 6 si une machine précise
                // l'exige (voir ARCHITECTURE.md, section Qualité audio).
                sample = static_cast<float>(4.0 * std::abs(phase_ - 0.5) - 1.0);
                break;
            }
        }

        phase_ += dt;
        if (phase_ >= 1.0) phase_ -= 1.0;
        else if (phase_ < 0.0) phase_ += 1.0; // sécurité si fréquence négative

        return sample;
    }

private:
    static float clamp01(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    /// Correction PolyBLEP (2 segments) appliquée de part et d'autre d'une
    /// discontinuité, pour supprimer l'aliasing haute fréquence qu'une onde
    /// en dents de scie/carré "brute" produirait.
    static float polyBlep(double t, double dt) {
        if (dt <= 0.0) return 0.0f;
        if (t < dt) {
            double x = t / dt;
            return static_cast<float>(x + x - x * x - 1.0);
        }
        if (t > 1.0 - dt) {
            double x = (t - 1.0) / dt;
            return static_cast<float>(x * x + x + x + 1.0);
        }
        return 0.0f;
    }

    double sampleRate_ = 48000.0;
    float frequencyHz_ = 440.0f;
    Waveform waveform_ = Waveform::Saw;
    float pulseWidth_ = 0.5f;
    double phase_ = 0.0;
};

} // namespace vsm::audio::dsp
