#pragma once
#include "IAudioEffect.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace vsm::audio::effect {

/// PITCH SHIFT (D13.8) — transposer en temps réel, la durée ne bougeant pas.
///
/// CE N'EST PAS LE VOCODEUR DE PHASE de D12.8, et c'est dit : un insert reçoit
/// un flux, pas un fichier qu'on peut relire à n'importe quelle position ;
/// l'étireur de D12 vit sur les clips. Ici, la recette des premiers
/// harmonizers (Eventide H910, 1975) : une ligne de retard lue par DEUX têtes
/// dont le retard glisse à la vitesse (1 − rapport) -- une tête qui recule
/// dans le passé lit plus vite, donc plus haut --, chacune sous une fenêtre en
/// demi-sinus, décalées d'une demi-période, si bien que l'une est à plein
/// quand l'autre saute. Le prix, dit : un léger battement à la période du
/// grain (`Grain`), audible sur un son tenu et transposé loin ; c'est la
/// signature de la famille, pas un défaut de mise en œuvre.
///
/// LA TÊTE QUI REDÉMARRE S'ALIGNE EN PHASE SUR L'AUTRE. Sans cela, mesuré :
/// sur un si♭3 transposé d'une octave, la seconde tête reprend la source
/// 1 200 échantillons plus tôt, soit 5,83 périodes -- un saut de phase de
/// 0,17 tour toutes les 25 ms, que la transformée lit comme +25 cents
/// (473,1 Hz au lieu de 466,2). Au redémarrage d'une tête (sa fenêtre à
/// zéro), on cherche donc, à ± `kAlign` échantillons, le décalage qui rend sa
/// lecture la plus semblable à celle de l'autre tête, alors à plein -- la
/// recherche du WSOLA (D12.2), appliquée une fois par grain et non à chaque
/// échantillon. Le décalage reste jusqu'au redémarrage suivant.
///
/// LA LATENCE est la moitié du grain (la tête est, en moyenne, à mi-course) ;
/// elle est déclarée pour la compensation (D4.5) et ne dépend que de `Grain`.
class PitchShiftEffect : public IAudioEffect {
public:
    enum ParamIds : vsm::audio::plugin::ParamId { kSemitones = 0, kCents, kGrain, kMix, kNumParams };

    PitchShiftEffect() {
        parameterList_ = {
            {kSemitones, "Semitones", -12.0f, 12.0f, 0.0f, "st"},
            {kCents, "Cents", -100.0f, 100.0f, 0.0f, "ct"},
            {kGrain, "Grain", 20.0f, 100.0f, 50.0f, "ms"},
            {kMix, "Mix", 0.0f, 1.0f, 1.0f, ""},
        };
        for (const auto& p : parameterList_) params_[p.id].store(p.defaultValue, std::memory_order_relaxed);
    }
    void prepare(double sampleRate, int) override {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        taille_ = 1;
        while (taille_ < static_cast<int>(0.1 * sampleRate_) + 8) taille_ <<= 1;
        ligneL_.assign(static_cast<size_t>(taille_), 0.0f);
        ligneR_.assign(static_cast<size_t>(taille_), 0.0f);
        reset();
    }
    void reset() override {
        std::fill(ligneL_.begin(), ligneL_.end(), 0.0f);
        std::fill(ligneR_.begin(), ligneR_.end(), 0.0f);
        ecriture_ = 0;
        phase_ = 0.0;
        decalage_[0] = decalage_[1] = 0.0;
        dernierP_[0] = 0.0; dernierP_[1] = 0.5;
    }
    static constexpr int kAlign = 384;     ///< ± 8 ms : une période de 125 Hz
    static constexpr int kAlignWindow = 256;
    int latencySamples() const override {
        return static_cast<int>(0.5 * params_[kGrain].load(std::memory_order_relaxed) * 0.001 * sampleRate_);
    }
    void process(float* left, float* right, int numSamples) override {
        if (ligneL_.empty()) return;
        const double st = params_[kSemitones].load(std::memory_order_relaxed) + params_[kCents].load(std::memory_order_relaxed) / 100.0;
        const double rapport = std::exp2(st / 12.0);
        const double grain = std::max(8.0, params_[kGrain].load(std::memory_order_relaxed) * 0.001 * sampleRate_);
        const float mix = std::clamp(params_[kMix].load(std::memory_order_relaxed), 0.0f, 1.0f);
        // La tête glisse de (1 - rapport) échantillon par échantillon : à +12 st,
        // elle remonte vers le présent d'un échantillon par échantillon, ce qui
        // lit deux fois plus vite.
        const double pas = (1.0 - rapport) / grain;
        const int masque = taille_ - 1;
        for (int i = 0; i < numSamples; ++i) {
            ligneL_[static_cast<size_t>(ecriture_)] = left[i];
            ligneR_[static_cast<size_t>(ecriture_)] = right[i];
            float wetL = 0.0f, wetR = 0.0f;
            for (int tete = 0; tete < 2; ++tete) {
                double p = phase_ + (tete == 0 ? 0.0 : 0.5);
                p -= std::floor(p);
                // LE REDÉMARRAGE : la phase a sauté (elle avance dans un sens
                // et vient de repasser par le bord). On aligne cette tête sur
                // l'autre, qui est alors à plein.
                if (std::abs(p - dernierP_[tete]) > 0.5) aligner(tete, p, grain, masque);
                dernierP_[tete] = p;
                const double retard = std::clamp(p * grain + decalage_[tete], 0.0, static_cast<double>(taille_ - 4));
                const float fenetre = static_cast<float>(std::sin(M_PI * p));
                const double position = static_cast<double>(ecriture_) - retard;
                const auto base = static_cast<long long>(std::floor(position));
                const float frac = static_cast<float>(position - static_cast<double>(base));
                const int i0 = static_cast<int>(base & masque), i1 = static_cast<int>((base + 1) & masque);
                wetL += fenetre * (ligneL_[static_cast<size_t>(i0)] * (1.0f - frac) + ligneL_[static_cast<size_t>(i1)] * frac);
                wetR += fenetre * (ligneR_[static_cast<size_t>(i0)] * (1.0f - frac) + ligneR_[static_cast<size_t>(i1)] * frac);
            }
            left[i] = left[i] * (1.0f - mix) + wetL * mix;
            right[i] = right[i] * (1.0f - mix) + wetR * mix;
            ecriture_ = (ecriture_ + 1) & masque;
            phase_ += pas;
            phase_ -= std::floor(phase_);
        }
    }
    void setParameter(vsm::audio::plugin::ParamId id, float v) override { if (id < kNumParams) params_[id].store(v, std::memory_order_relaxed); }
    float getParameter(vsm::audio::plugin::ParamId id) const override { return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f; }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    const char* effectName() const override { return "Pitch Shift"; }

private:
    /// Cherche, pour la tête `tete` qui redémarre à la phase `p`, le décalage
    /// (± kAlign) qui rend sa lecture la plus semblable à celle de l'autre
    /// tête. Corrélation normalisée sur `kAlignWindow` échantillons, pas de
    /// grossier : une fois par grain, le coût est sans importance.
    void aligner(int tete, double p, double grain, int masque) {
        const int autre = 1 - tete;
        double pAutre = p + 0.5;
        pAutre -= std::floor(pAutre);
        const double retardAutre = pAutre * grain + decalage_[autre];
        const double retardMoi = p * grain;
        const auto lire = [&](double retard, int k) {
            const double position = static_cast<double>(ecriture_) - retard - static_cast<double>(k);
            const auto base = static_cast<long long>(std::floor(position));
            return ligneL_[static_cast<size_t>(base & masque)] + ligneR_[static_cast<size_t>(base & masque)];
        };
        double refE = 0.0;
        for (int k = 0; k < kAlignWindow; ++k) { const double v = lire(retardAutre, k); refE += v * v; }
        if (refE < 1e-9) { decalage_[tete] = 0.0; return; }
        double meilleur = -2.0; int meilleurD = 0;
        for (int d = -kAlign; d <= kAlign; d += 2) {
            const double retard = retardMoi + d;
            if (retard < 0.0 || retard > static_cast<double>(taille_ - kAlignWindow - 8)) continue;
            double num = 0.0, ene = 0.0;
            for (int k = 0; k < kAlignWindow; ++k) {
                const double a = lire(retardAutre, k), b = lire(retard, k);
                num += a * b; ene += b * b;
            }
            const double score = ene > 1e-9 ? num / std::sqrt(ene * refE) : -1.0;
            if (score > meilleur) { meilleur = score; meilleurD = d; }
        }
        decalage_[tete] = meilleurD;
    }

    double sampleRate_ = 48000.0, phase_ = 0.0;
    double decalage_[2] = {0.0, 0.0}, dernierP_[2] = {0.0, 0.5};
    int taille_ = 1, ecriture_ = 0;
    std::vector<float> ligneL_, ligneR_;
    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
};

} // namespace vsm::audio::effect
