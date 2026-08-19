#pragma once
#include "Constants.h"
#include "SimdFloat4.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace vsm::audio::dsp {

/// QUATRE filtres ladder ZDF indépendants, traités en parallèle sur des lignes
/// SIMD. Même topologie et même dérivation que `LadderFilterZDF` (voir son
/// commentaire de classe pour la théorie) : chaque ligne a sa propre fréquence
/// de coupure, sa propre résonance et son propre état.
///
/// À QUOI ÇA SERT, ET POURQUOI CE N'EST PAS UNE OPTIMISATION GRATUITE : un
/// filtre récursif est une chaîne de dépendances -- l'échantillon n dépend du
/// n-1, donc le processeur passe son temps à attendre. Mesuré sur ce projet :
/// la version scalaire coûte ~70 ns par échantillon alors que son arithmétique
/// seule en vaut ~17. Supprimer du travail ne règle rien (retirer les deux
/// tanh ET dérouler les boucles ne fait gagner que 30 %). En revanche, QUATRE
/// filtres indépendants remplissent ces attentes sans se gêner : c'est le
/// principe de cette classe, et c'est pourquoi elle ne sert qu'aux plugins
/// POLYPHONIQUES, où quatre voix ont chacune leur filtre.
///
/// DIFFÉRENCE ASSUMÉE avec la version scalaire : la saturation utilise
/// `fastTanh` (approximation rationnelle) au lieu de `std::tanh`, qui n'a pas
/// d'équivalent SIMD. L'écart maximal est de 1,5e-5, soit ~-96 dBFS. La
/// version scalaire utilise la MÊME approximation, pour que les deux chemins
/// donnent le même son -- sans quoi une machine sonnerait différemment selon
/// qu'elle a été vectorisée ou non, ce qui serait indéfendable.
class LadderFilterZDFx4 {
public:
    static constexpr int kMaxPoles = 4;
    static constexpr size_t kLanes = SimdFloat4::kLanes;

    LadderFilterZDFx4() { updateCoefficients(); }

    void setSampleRate(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        updateCoefficients();
    }

    /// Fréquence de coupure d'UNE ligne (donc d'une voix).
    void setCutoffHz(size_t lane, float hz) {
        if (lane >= kLanes) return;
        if (!(hz < cutoffHz_[lane] || hz > cutoffHz_[lane])) return; // identique -> rien à recalculer
        cutoffHz_[lane] = hz;
        updateCoefficients();
    }

    /// Règle les QUATRE coupures et ne recalcule les coefficients qu'une fois.
    /// Passer par setCutoffHz() ligne par ligne relancerait le calcul des
    /// quatre tangentes à chaque appel, soit seize par échantillon : de quoi
    /// annuler tout le bénéfice de la vectorisation. C'est l'entrée à
    /// utiliser dans une boucle de voix.
    void setCutoffsHz(const float (&hz)[kLanes]) {
        bool changed = false;
        for (size_t lane = 0; lane < kLanes; ++lane) {
            if (hz[lane] < cutoffHz_[lane] || hz[lane] > cutoffHz_[lane]) {
                cutoffHz_[lane] = hz[lane];
                changed = true;
            }
        }
        if (changed) updateCoefficients();
    }

    void setResonance(size_t lane, float amount) {
        if (lane >= kLanes) return;
        resonance_[lane] = std::clamp(amount, 0.0f, 4.2f);
    }

    void setDrive(size_t lane, float drive) {
        if (lane >= kLanes) return;
        drive_[lane] = std::max(0.1f, drive);
    }

    void setPoleCount(int poles) { poleCount_ = std::clamp(poles, 2, kMaxPoles); }
    int poleCount() const { return poleCount_; }

    void reset() { for (auto& state : states_) state = SimdFloat4(0.0f); }

    /// Remet à zéro l'état d'UNE ligne (une voix qu'on réattribue à une autre
    /// note ne doit pas hériter de la résonance de la précédente).
    void resetLane(size_t lane) {
        if (lane >= kLanes) return;
        for (auto& state : states_) {
            float tmp[kLanes];
            state.store(tmp);
            tmp[lane] = 0.0f;
            state = SimdFloat4::load(tmp);
        }
    }

    /// Traite un échantillon sur chacune des 4 lignes.
    SimdFloat4 process(const SimdFloat4& input) {
        const SimdFloat4 drive = SimdFloat4::load(drive_.data());
        const SimdFloat4 resonance = SimdFloat4::load(resonance_.data());
        const SimdFloat4 driven = fastTanh(input * drive) * SimdFloat4(kInvDriveNorm);

        SimdFloat4 S(0.0f);
        SimdFloat4 gPow(1.0f);
        for (int i = poleCount_ - 1; i >= 0; --i) {
            S += gPow * oneMinusG_ * states_[static_cast<size_t>(i)];
            gPow *= G_;
        }
        const SimdFloat4 gN = gPow;
        const SimdFloat4 yNEstimate = (gN * driven + S) / (SimdFloat4(1.0f) + resonance * gN);

        SimdFloat4 u = driven - fastTanh(resonance * yNEstimate);
        for (int i = 0; i < poleCount_; ++i) u = onePole(u, states_[static_cast<size_t>(i)]);
        for (int i = 0; i < poleCount_; ++i)
            states_[static_cast<size_t>(i)] = flushDenormals(states_[static_cast<size_t>(i)]);
        return u;
    }

private:
    SimdFloat4 onePole(const SimdFloat4& x, SimdFloat4& state) const {
        const SimdFloat4 v = (x - state) * G_;
        const SimdFloat4 y = v + state;
        state = y + v;
        return y;
    }

    /// Les dénormales ne peuvent pas être testées par branchement en SIMD sans
    /// masques : on ajoute puis retranche une constante minuscule, ce qui les
    /// absorbe (x + 1e-25 - 1e-25 == 0 pour x dénormal, et laisse un signal
    /// normal inchangé). Le mode FTZ/DAZ du CPU (ScopedNoDenormals, activé
    /// pour tout le bloc audio) reste la protection principale.
    static SimdFloat4 flushDenormals(const SimdFloat4& x) {
        const SimdFloat4 tiny(1.0e-25f);
        return (x + tiny) - tiny;
    }

    void updateCoefficients() {
        const float nyquist = static_cast<float>(sampleRate_) * 0.5f;
        std::array<float, kLanes> g{}, gG{}, oneMinus{};
        for (size_t lane = 0; lane < kLanes; ++lane) {
            const float clamped = std::clamp(cutoffHz_[lane], 10.0f, nyquist * 0.95f);
            const float t = std::tan(static_cast<float>(kPi) * clamped / static_cast<float>(sampleRate_));
            g[lane] = t;
            gG[lane] = t / (1.0f + t);
            oneMinus[lane] = 1.0f - gG[lane];
        }
        G_ = SimdFloat4::load(gG.data());
        oneMinusG_ = SimdFloat4::load(oneMinus.data());
    }

    static constexpr float kInvDriveNorm = 1.0f / 0.7615941559557649f; // 1/tanh(1)

    double sampleRate_ = 48000.0;
    int poleCount_ = 4;
    std::array<float, kLanes> cutoffHz_ { 1000.0f, 1000.0f, 1000.0f, 1000.0f };
    std::array<float, kLanes> resonance_ { 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<float, kLanes> drive_ { 1.0f, 1.0f, 1.0f, 1.0f };

    SimdFloat4 G_ { 0.0f }, oneMinusG_ { 1.0f };
    std::array<SimdFloat4, kMaxPoles> states_ {};
};

} // namespace vsm::audio::dsp
