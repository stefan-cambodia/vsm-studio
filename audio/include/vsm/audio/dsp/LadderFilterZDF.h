#pragma once
#include "Constants.h"
#include "SimdFloat4.h" // pour fastTanh, partagé avec la version 4 lignes
#include "DenormalGuard.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace vsm::audio::dsp {

/// Filtre ladder en cascade de N étages one-pole (2 à 4), topologie
/// zero-delay-feedback : même principe TPT que StateVariableFilter (voir
/// Filter.h), généralisé à N étages avec un rebouclage global. Le nombre
/// de pôles n'est PAS un détail cosmétique : c'est ce qui distingue le
/// ladder Moog (4 pôles, 24 dB/oct) du filtre du TB-303 (3 pôles,
/// 18 dB/oct) -- les utiliser l'un pour l'autre serait une approximation
/// de trop. Voir setPoleCount().
///
/// C'est LA brique qui donne aux machines analogiques leur caractère,
/// contrairement au StateVariableFilter générique de la Phase 2 :
///  - auto-oscillation stable (résonance proche du maximum), amplitude
///    BORNÉE par une saturation tanh() dans le chemin de feedback plutôt
///    que de diverger numériquement ;
///  - une saturation d'entrée ("drive") qui fait que le comportement du
///    filtre change réellement avec le niveau du signal entrant (section 9).
///
/// Dérivation : chaque étage est un one-pole TPT (y = G*u + (1-G)*s, mise
/// à jour d'état s_new = y + (y-s), voir onePole()). Le rebouclage global
/// (u1 = drive(x) - k*yN) crée une boucle algébrique résolue en 2 passes :
/// (1) une estimation LINÉAIRE de yN en supposant le feedback non saturé,
/// résoluble directement : yN = (G^N*driven + S) / (1 + k*G^N), où S est la
/// contribution des états existants des N étages (indépendante de
/// l'entrée) ; (2) le feedback RÉEL utilisé pour propager le signal est
/// celui de cette estimation passée dans tanh(), ce qui borne
/// l'auto-oscillation. Cette dérivation se généralise directement de 4 à
/// N étages -- seule la boucle d'accumulation de S et l'exposant final
/// changent, la logique reste identique.
///
/// Simplification assumée et documentée : la saturation n'est appliquée
/// qu'à l'entrée et au chemin de feedback global, pas à chacun des étages
/// individuellement (contrairement à un modèle "Huovilainen" complet, qui
/// nécessiterait une résolution implicite par itération de Newton à chaque
/// échantillon). Suffisant pour un comportement stable, borné et
/// audiblement non-linéaire ; un modèle par étage plus fidèle reste un
/// raffinement possible si une comparaison avec du hardware réel
/// (section 27) le justifie.
class LadderFilterZDF {
public:
    static constexpr int kMaxPoles = 4;

    /// Coefficients dérivés cohérents dès la construction, sans setter
    /// préalable (même garantie que StateVariableFilter).
    LadderFilterZDF() { updateDerived(); }

    void setSampleRate(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        updateCoefficients();
    }
    void setCutoffHz(float hz) {
        if (isSameValue(hz, cutoffHz_)) return; // mêmes entrées -> mêmes coefficients, rien à recalculer
        cutoffHz_ = hz;
        updateCoefficients();
    }

    /// 2 = 12 dB/oct, 3 = 18 dB/oct (TB-303-style), 4 = 24 dB/oct
    /// (Moog/Minimoog-style, valeur par défaut -- comportement inchangé
    /// pour tout code existant qui n'appelle jamais cette méthode).
    void setPoleCount(int poles) {
        const int clamped = std::clamp(poles, 2, kMaxPoles);
        if (clamped == poleCount_) return;
        poleCount_ = clamped;
        updateDerived();
    }
    int poleCount() const { return poleCount_; }

    /// Plage utile ~0..~4.2 : au-delà du seuil d'auto-oscillation (qui
    /// dépend du nombre de pôles), le filtre auto-oscille (comportement
    /// recherché, pas un bug -- c'est la signature sonore d'un ladder).
    void setResonance(float amount) {
        const float clamped = std::clamp(amount, 0.0f, 4.2f);
        if (isSameValue(clamped, resonance_)) return;
        resonance_ = clamped;
        updateDerived(); // la résonance entre dans le dénominateur précalculé
    }

    /// Saturation d'entrée ("drive"), >= 0.1. 1.0 ~= quasi linéaire pour un
    /// signal d'amplitude normale ; au-delà, sature audiblement plus fort.
    void setDrive(float drive) { drive_ = std::max(0.1f, drive); }

    void reset() { states_.fill(0.0f); }

    float process(float input) {
        // fastTanh plutôt que std::tanh : la version 4 lignes (LadderFilterZDFx4)
        // ne peut pas appeler std::tanh -- il n'en existe pas de version SIMD --
        // et il serait indéfendable qu'une machine sonne différemment selon
        // qu'elle a été vectorisée ou non. Les deux chemins partagent donc la
        // même approximation, dont l'écart avec std::tanh est de 1,5e-5 au pire
        // (~-96 dBFS). Voir SimdFloat4.h et ARCHITECTURE.md § 9 sexies.
        float driven = fastTanh(input * drive_) / kDriveNorm;

        // Passe 1 : estimation linéaire de yN (feedback non saturé),
        // résoluble algébriquement -- voir dérivation dans le commentaire
        // de classe. La boucle accumule S en parcourant les étages du
        // DERNIER vers le PREMIER, gPow finissant à G^poleCount_ (= gN)
        // une fois la boucle terminée -- réutilisé directement ensuite,
        // aucun calcul redondant.
        float S = 0.0f;
        for (int i = 0; i < poleCount_; ++i)
            S += stateWeights_[static_cast<size_t>(i)] * states_[static_cast<size_t>(i)];
        float yNEstimate = (gN_ * driven + S) * invFeedbackDenominator_;

        // Passe 2 : le feedback RÉELLEMENT utilisé est saturé -- c'est ce
        // qui borne l'auto-oscillation en amplitude au lieu de diverger.
        float u = driven - fastTanh(resonance_ * yNEstimate);

        for (int i = 0; i < poleCount_; ++i)
            u = onePole(u, states_[static_cast<size_t>(i)]);

        for (int i = 0; i < poleCount_; ++i)
            states_[static_cast<size_t>(i)] = flushDenormalToZero(states_[static_cast<size_t>(i)]);

        return u; // après la boucle, u contient la sortie du dernier étage (yN)
    }

private:
    float onePole(float x, float& state) const {
        float v = (x - state) * G_;
        float y = v + state;
        state = y + v;
        return y;
    }

    void updateCoefficients() {
        float nyquist = static_cast<float>(sampleRate_) * 0.5f;
        float clampedCutoff = std::clamp(cutoffHz_, 10.0f, nyquist * 0.95f);
        float g = std::tan(static_cast<float>(kPi) * clampedCutoff / static_cast<float>(sampleRate_));
        G_ = g / (1.0f + g);
        oneMinusG_ = 1.0f - G_;
        updateDerived();
    }

    /// Tout ce qui ne dépend QUE des paramètres (coupure, résonance, nombre de
    /// pôles) est calculé ici, une fois, au lieu d'être refait à chaque
    /// échantillon dans process() :
    ///   - stateWeights_[i] = (1-G) * G^(poleCount-1-i), les poids de la
    ///     contribution des états dans l'estimation linéaire de yN ;
    ///   - gN_ = G^poleCount ;
    ///   - invFeedbackDenominator_ = 1 / (1 + resonance * G^poleCount), pour
    ///     remplacer une DIVISION par une multiplication dans la boucle.
    /// La division était particulièrement coûteuse ici : chaque échantillon du
    /// filtre dépend du précédent, donc rien ne peut se recouvrir -- c'est une
    /// chaîne de latence, où une division vaut une quinzaine de cycles pleins.
    void updateDerived() {
        float gPow = 1.0f;
        for (int i = poleCount_ - 1; i >= 0; --i) {
            stateWeights_[static_cast<size_t>(i)] = gPow * oneMinusG_;
            gPow *= G_;
        }
        gN_ = gPow;
        invFeedbackDenominator_ = 1.0f / (1.0f + resonance_ * gN_);
    }

    double sampleRate_ = 48000.0;
    float cutoffHz_ = 1000.0f;
    float resonance_ = 0.0f;
    float drive_ = 1.0f;
    int poleCount_ = 4; // défaut = comportement Moog/Minimoog inchangé
    static constexpr float kDriveNorm = 0.7615941559557649f; // tanh(1.0), normalise drive=1.0 -> ~gain unitaire

    float G_ = 0.0f, oneMinusG_ = 1.0f;
    float gN_ = 0.0f, invFeedbackDenominator_ = 1.0f;
    std::array<float, kMaxPoles> stateWeights_{};
    std::array<float, kMaxPoles> states_{};
};

} // namespace vsm::audio::dsp
