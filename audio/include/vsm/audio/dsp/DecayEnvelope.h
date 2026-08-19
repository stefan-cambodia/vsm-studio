#pragma once
#include <algorithm>
#include <cmath>

namespace vsm::audio::dsp {

/// Enveloppe percussive à décroissance exponentielle (one-shot). Forme
/// typique d'un ampli de boîte à rythmes analogique : attaque quasi
/// instantanée, décroissance exponentielle, ni sustain ni note-off.
///
/// Brique partagée : d'abord écrite pour le TR-808-style, extraite ici pour
/// être réutilisée par toutes les machines de percussion (TR-909, etc.) sans
/// duplication ni couplage entre plugins.
class DecayEnvelope {
public:
    void setSampleRate(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    }
    void setDecaySeconds(float seconds) {
        const float s = std::max(0.002f, seconds);
        coeff_ = std::exp(-1.0f / (s * static_cast<float>(sampleRate_)));
    }
    void trigger() { level_ = 1.0f; active_ = true; }
    /// Coupe rapidement (choke group : ex. charleston fermé coupant l'ouvert).
    void choke() { coeff_ = std::exp(-1.0f / (0.005f * static_cast<float>(sampleRate_))); }
    bool isActive() const { return active_; }

    float next() {
        if (!active_) return 0.0f;
        const float v = level_;
        level_ *= coeff_;
        if (level_ < 1.0e-4f) { level_ = 0.0f; active_ = false; }
        return v;
    }

private:
    double sampleRate_ = 48000.0;
    float level_ = 0.0f, coeff_ = 0.0f;
    bool active_ = false;
};

} // namespace vsm::audio::dsp
