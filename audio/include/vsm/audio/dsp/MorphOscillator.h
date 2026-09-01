#pragma once
#include "vsm/audio/dsp/Constants.h"
#include <algorithm>
#include <cmath>

namespace vsm::audio::dsp {

/// Oscillateur MORPHABLE : sinus → triangle → scie → carré, en CONTINU.
///
/// Né dans `vsm.generic`, où la continuité de la forme d'onde est l'exigence
/// centrale (une falaise dans la fonction de coût bloque une recherche par
/// descente), et promu ici le jour où une seconde machine en a eu besoin
/// (`vsm.vector`, 02/09/2026) — la règle du § 8.4 : une brique se partage,
/// elle ne se recopie pas, parce que deux copies divergent toujours à la
/// longue. La preuve de la fidélité de l'extraction n'est pas une relecture,
/// c'est l'empreinte audio de `vsm.generic`, inchangée au bit à travers le
/// déplacement — exactement le service qu'a rendu `StringWaveguide` en son
/// temps.
///
/// Les quatre formes sont calculées à LA MÊME phase, ce qui permet de les
/// fondre (quatre oscillateurs indépendants se peigneraient), et la scie
/// comme le carré portent une correction polyBLEP aux discontinuités.
class MorphOscillator {
public:
    void setSampleRate(double sampleRate) { sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0; }
    void setFrequency(float hz) { frequencyHz_ = hz > 0.0f ? hz : 1.0f; }
    void reset(double phase = 0.0) { phase_ = phase - std::floor(phase); }

    /// `shape` de 0 (sinus) à 3 (carré), continu.
    float nextSample(float shape, float pulseWidth) {
        const double dt = static_cast<double>(frequencyHz_) / sampleRate_;
        const double t = phase_;

        // Les quatre formes, toutes calculées à LA MÊME phase. C'est ce qui
        // permet de les fondre : quatre oscillateurs indépendants se
        // peigneraient.
        const auto sinus = static_cast<float>(std::sin(t * kTwoPi));
        const auto triangle = static_cast<float>(4.0 * std::abs(t - 0.5) - 1.0);

        float dent = static_cast<float>(2.0 * t - 1.0);
        dent -= polyBlep(t, dt);

        const double largeur = std::clamp(static_cast<double>(pulseWidth), 0.05, 0.95);
        float carre = (t < largeur) ? 1.0f : -1.0f;
        carre += polyBlep(t, dt);
        carre -= polyBlep(std::fmod(t + (1.0 - largeur), 1.0), dt);

        phase_ += dt;
        if (phase_ >= 1.0) phase_ -= 1.0;

        // FONDU ENTRE LES DEUX FORMES VOISINES. La sortie est continue en
        // `shape`, sans le moindre palier.
        const float position = std::clamp(shape, 0.0f, 3.0f);
        const auto index = static_cast<int>(position);
        const float fraction = position - static_cast<float>(index);
        const float formes[4] = {sinus, triangle, dent, carre};
        const float a = formes[std::min(index, 3)];
        const float b = formes[std::min(index + 1, 3)];
        return a + (b - a) * fraction;
    }

private:
    static float polyBlep(double t, double dt) {
        if (t < dt) {
            const double x = t / dt;
            return static_cast<float>(x + x - x * x - 1.0);
        }
        if (t > 1.0 - dt) {
            const double x = (t - 1.0) / dt;
            return static_cast<float>(x * x + x + x + 1.0);
        }
        return 0.0f;
    }

    double sampleRate_ = 48000.0;
    double phase_ = 0.0;
    float frequencyHz_ = 440.0f;
};

} // namespace vsm::audio::dsp
