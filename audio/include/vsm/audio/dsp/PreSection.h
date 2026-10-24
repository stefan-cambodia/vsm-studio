#pragma once
#include "Biquad.h"

namespace vsm::audio::dsp {

/// D593 : LE COUPE-BAS ET LE COUPE-HAUT D'UNE TRANCHE — la section *Pre* de Cubase (*Low Cut*, *High Cut*), après le
/// trim et avant les inserts. Un Butterworth du deuxième ordre (RBJ, Q = 1/√2, 12 dB par octave) par filtre et par
/// canal.
///
/// ÉTEINT = 0 Hz, ET RIEN N'EST CALCULÉ : le bloc ressort au bit près, ce qui garde le son de tout projet d'avant. Les
/// coefficients ne se recalculent que quand la fréquence change ; un filtre qui S'ALLUME repart de mémoires vides — un
/// filtre rallumé ne rejoue pas l'état d'il y a dix minutes.
class PreSectionFilter {
public:
    void setSampleRate(double sampleRate) {
        if (sampleRate == sampleRate_) return;
        sampleRate_ = sampleRate;
        for (auto* f : {&bas_[0], &bas_[1], &haut_[0], &haut_[1]}) f->setSampleRate(sampleRate);
        basHz_ = hautHz_ = 0.0f;   // recalcul et remise à zéro au prochain bloc
    }

    /// Filtre `l` et `r` en place sur `n` échantillons.
    void process(float* l, float* r, int n, float lowCutHz, float highCutHz) {
        appliquer(bas_, basHz_, Biquad::Type::HighPass, lowCutHz, l, r, n);
        appliquer(haut_, hautHz_, Biquad::Type::LowPass, highCutHz, l, r, n);
    }

private:
    static constexpr float kQ = 0.70710678f;

    static void appliquer(Biquad (&f)[2], float& vu, Biquad::Type type, float hz, float* l, float* r, int n) {
        if (hz <= 0.0f) { vu = 0.0f; return; }
        if (hz != vu) {
            if (vu <= 0.0f) { f[0].reset(); f[1].reset(); }
            f[0].set(type, hz, kQ, 0.0f);
            f[1].set(type, hz, kQ, 0.0f);
            vu = hz;
        }
        for (int i = 0; i < n; ++i) {
            l[i] = f[0].process(l[i]);
            r[i] = f[1].process(r[i]);
        }
    }

    double sampleRate_ = 0.0;
    Biquad bas_[2], haut_[2];
    float basHz_ = 0.0f, hautHz_ = 0.0f;
};

} // namespace vsm::audio::dsp
