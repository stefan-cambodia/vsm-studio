#pragma once
#include "Constants.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace vsm::audio::dsp {

/// Corde vibrante par GUIDE D'ONDES : une ligne à retard qui porte
/// l'aller-retour de l'onde, et ce qu'elle perd à chaque tour.
///
/// POURQUOI CETTE BRIQUE EXISTE. Elle est née dans `vsm.string` (corde pincée
/// et frottée) ; `vsm.piano` a besoin EXACTEMENT de la même boucle, avec une
/// autre excitation et plusieurs cordes désaccordées par note. Recopier la
/// boucle aurait donné deux exemplaires d'une même physique, qui divergent
/// toujours à la longue -- c'est précisément ce contre quoi le § 8.4 du cahier
/// des charges met en garde. La boucle est donc ouverte ici, une fois, et les
/// deux machines la partagent ; ce qui les distingue (comment on excite la
/// corde, combien il y en a, ce qui rayonne ensuite) leur reste propre.
///
/// LA BOUCLE, ET CE QUE CHAQUE ORGANE RÈGLE
///
/// ```
///   ┌──> ligne à retard (N) ──> retard fractionnaire ──┐
///   │                                                  │
///   └── gain de boucle <── dispersion <── amortissement ┘
/// ```
///
///  1. **Amortissement** — passe-bas d'ordre un `(1-b)x[n] + b·x[n-1]`. Son
///     gain vaut EXACTEMENT 1 en continu, ce qui est la condition pour que le
///     T60 demandé soit vraiment celui du fondamental.
///  2. **Raideur** — trois passe-tout d'ordre un. Une corde raide transmet les
///     aigus plus vite que les graves : ses partiels ne tombent pas sur des
///     multiples entiers, ils montent. Le coefficient DÉPEND DE LA NOTE, et
///     c'est une mesure qui l'a imposé : à coefficient fixe, l'inharmonicité
///     suit la fréquence ABSOLUE et non le rang du partiel, si bien qu'elle
///     disparaît sur les cordes graves -- là où elle s'entend le plus. Le
///     coefficient est résolu pour que l'inharmonicité visée au 16e partiel
///     soit LINÉAIRE dans le réglage, de 0 à 25 cents ; la loi
///     `|a| = exp(-k·ω16)` avec `k = 3,26·cents^-0,368` vient de l'inversion
///     numérique de la réponse du peigne, et le facteur k s'est révélé
///     indépendant de la note à 1 % près sur cinq octaves.
///  3. **Retard fractionnaire** — un passe-tout accordé pour que la boucle
///     fasse exactement SR/f0. Sans lui la hauteur se quantifierait à
///     l'échantillon près : à 4 kHz, un échantillon vaut plus d'un demi-ton.
///     Erreur mesurée : moins de 0,2 cent sur cinq octaves.
///  4. **Gain de boucle** — la décroissance, calculée depuis le T60 demandé.
///
/// APPROXIMATIONS ASSUMÉES (§ 27 d'ARCHITECTURE.md) :
///
///  - **Une seule ligne à retard**, là où la physique en demande deux (onde
///    montante et onde descendante). Ce qui excite la corde agit donc sur
///    l'onde résultante et non sur une jonction entre deux ondes.
///  - **Raideur rognée dans l'aigu** : le retard qu'exigent les passe-tout de
///    dispersion ne peut pas dépasser 40 % de la boucle, faute de quoi une
///    note très aiguë n'aurait plus de ligne à retard. Au-dessus d'environ
///    1 kHz la raideur demandée est donc réduite ; la note reste juste, elle
///    est seulement moins inharmonique que réglée.
class StringWaveguide {
public:
    /// Réserve la ligne pour la note la plus grave qu'on acceptera de tenir.
    /// À appeler dans `initialize()`, JAMAIS depuis le thread audio.
    void prepare(double sampleRate, float lowestHz) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        lowestHz_ = std::max(1.0f, lowestHz);
        const size_t capacity =
            static_cast<size_t>(sampleRate_ / static_cast<double>(lowestHz_)) + 8;
        line_.assign(capacity, 0.0f);
        reset();
    }

    bool isPrepared() const { return !line_.empty(); }

    /// Remet la corde au repos. Un memset, jamais une allocation : appelable
    /// depuis le thread audio.
    void reset() {
        std::fill(line_.begin(), line_.end(), 0.0f);
        writeIndex_ = 0;
        dampingState_ = 0.0f;
        tuning_.reset();
        for (auto& stage : dispersion_) stage.reset();
    }

    /// Règle la géométrie de la boucle. Une fois par bloc : c'est ici que
    /// vivent les `std::pow` et les divisions, pas dans `advance()`.
    void setTuning(float hz, float damping, float stiffness, float t60Seconds) {
        hz = std::clamp(hz, lowestHz_, static_cast<float>(sampleRate_) * 0.25f);
        totalDelay_ = static_cast<float>(sampleRate_) / hz;

        dampingB_ = 0.02f + 0.47f * std::clamp(damping, 0.0f, 1.0f);

        const float amount = std::clamp(stiffness, 0.0f, 1.0f);
        float a = 0.0f;
        if (amount > 1.0e-3f) {
            const float targetCents = 25.0f * amount;
            const float k = 3.26f * std::pow(targetCents, -0.368f);
            const float omega16 = 32.0f * static_cast<float>(kPi) * hz / static_cast<float>(sampleRate_);
            a = -std::min(0.95f, std::exp(-k * omega16));
        }
        float dispersionDelay = static_cast<float>(kDispersionStages) * allpassDelay(a);
        const float budget = 0.40f * totalDelay_;
        if (dispersionDelay > budget) {
            const float perStage = std::max(1.0f, budget / static_cast<float>(kDispersionStages));
            a = (1.0f - perStage) / (1.0f + perStage);
            dispersionDelay = static_cast<float>(kDispersionStages) * perStage;
        }
        for (auto& stage : dispersion_) stage.a = a;

        const float remainder = totalDelay_ - dampingB_ - dispersionDelay;
        float integerPart = std::floor(remainder - 0.5f);
        if (integerPart < 2.0f) integerPart = 2.0f;
        const float maxInteger = static_cast<float>(line_.size() - 2);
        if (integerPart > maxInteger) integerPart = maxInteger;
        const float fraction = std::max(0.05f, remainder - integerPart);
        delaySamples_ = static_cast<size_t>(integerPart);
        tuning_.a = (1.0f - fraction) / (1.0f + fraction);

        const float t60 = std::max(0.02f, t60Seconds);
        loopGain_ = std::min(0.99999f,
            std::pow(10.0f, -3.0f * totalDelay_ / (static_cast<float>(sampleRate_) * t60)));
    }

    /// Longueur totale de la boucle, en échantillons. Sert à placer le point
    /// de contact : c'est une FRACTION de la corde, pas une durée.
    float loopDelay() const { return totalDelay_; }

    /// Point d'injection correspondant à une position sur la corde (0 = au
    /// chevalet, 0,5 = au milieu), en échantillons.
    size_t contactOffset(float positionRatio) const {
        const float position = std::clamp(positionRatio, 0.005f, 0.5f);
        const size_t offset = static_cast<size_t>(position * totalDelay_);
        return std::clamp(offset, size_t{1}, delaySamples_ > 1 ? delaySamples_ - 1 : size_t{1});
    }

    /// Un tour de boucle : lit la corde et la fait revenir amortie, dispersée
    /// et accordée. Le résultat est ce que « voit » l'excitation -- l'archet en
    /// a besoin AVANT d'injecter, puisque sa force dépend de la vitesse
    /// relative entre le crin et la corde.
    float advance() {
        const size_t capacity = line_.size();
        const size_t readIndex = (writeIndex_ + capacity - delaySamples_) % capacity;
        const float delayed = line_[readIndex];
        const float damped = (1.0f - dampingB_) * delayed + dampingB_ * dampingState_;
        dampingState_ = delayed;
        float dispersed = damped;
        for (auto& stage : dispersion_) dispersed = stage.process(dispersed);
        return tuning_.process(dispersed) * loopGain_;
    }

    /// Injecte l'excitation EN UN POINT de la corde et referme la boucle.
    ///
    /// Le signal entre en phase au point de contact et en opposition à sa
    /// symétrie : c'est cela, et non un filtre qui l'imiterait, qui produit le
    /// peigne `1 - z^-pD` -- c'est-à-dire le facteur `sin(n·pi·p)` de la corde
    /// idéale. Frapper ou pincer au milieu ne peut pas exciter les harmoniques
    /// paires, parce que le point de contact y est un noeud.
    ///
    /// Renvoie la valeur écrite, qui est le signal de la corde au chevalet.
    float inject(float looped, float drive, size_t contact) {
        const size_t capacity = line_.size();
        float value = looped + drive;
        if (!isSameValue(drive, 0.0f)) {
            const size_t opposite = (writeIndex_ + capacity - contact) % capacity;
            line_[opposite] -= drive;
        }
        // Garde-fou : une excitation entretenue (l'archet, l'anche) est une
        // boucle de réaction, et une boucle de réaction doit être bornée
        // quelque part. Jamais atteint aux réglages utiles ; il empêche une
        // divergence de sortir en NaN.
        value = std::clamp(value, -4.0f, 4.0f);
        line_[writeIndex_] = value;
        writeIndex_ = (writeIndex_ + 1) % capacity;
        return value;
    }

private:
    /// Passe-tout d'ordre un, H(z) = (a + z^-1) / (1 + a z^-1).
    /// Retard de phase en continu : (1-a)/(1+a).
    struct Allpass {
        float a = 0.0f, x1 = 0.0f, y1 = 0.0f;
        void reset() { x1 = y1 = 0.0f; }
        float process(float x) {
            const float y = a * x + x1 - a * y1;
            x1 = x;
            y1 = y;
            return y;
        }
    };

    static float allpassDelay(float a) { return (1.0f - a) / (1.0f + a); }

    static constexpr int kDispersionStages = 3;

    double sampleRate_ = 48000.0;
    float lowestHz_ = 8.0f;
    std::vector<float> line_;
    size_t writeIndex_ = 0;
    size_t delaySamples_ = 100;
    float totalDelay_ = 100.0f;
    float loopGain_ = 0.999f;
    float dampingB_ = 0.2f;
    float dampingState_ = 0.0f;
    Allpass tuning_{};
    std::array<Allpass, kDispersionStages> dispersion_{};
};

} // namespace vsm::audio::dsp
