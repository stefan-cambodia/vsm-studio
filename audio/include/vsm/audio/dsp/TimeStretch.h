#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vsm::audio::dsp {

/// L'ÉTIREMENT TEMPOREL — un WSOLA (Waveform Similarity Overlap-Add), écrit
/// ici pour D12 (`docs/CDC-etirement-temporel.md`, § 3) et non emprunté.
///
/// CE QU'IL FAIT. Il fabrique la sortie grain par grain : tous les `kHop`
/// échantillons de sortie, un grain de `kWindow` échantillons est pris dans
/// la source, pondéré par une fenêtre de Hann, et additionné aux grains
/// voisins (deux grains se recouvrent toujours, la somme des fenêtres vaut
/// un). La durée change parce que la position NOMINALE dans la source avance
/// au rythme dicté par la carte sortie → source (le rapport d'étirement) ; la
/// hauteur ne change pas parce que chaque grain est copié tel quel. Ce qui
/// distingue un WSOLA d'un simple recouvrement : avant de prendre un grain, il
/// CHERCHE, à ± `kSearch` échantillons de la position nominale, l'endroit qui
/// ressemble le plus à la suite naturelle du grain précédent (corrélation
/// croisée normalisée sur la zone de recouvrement) — si bien que les formes
/// d'onde se raccordent au lieu de battre.
///
/// CE QU'IL GARANTIT :
///  - **rapport 1 = l'original au bit près** : une carte dont tous les
///    segments ont le rapport un et un décalage entier se copie sans passer
///    par les grains (§ 0 du CDC) ;
///  - **indépendance de la taille des blocs** : les grains sont posés sur la
///    ligne de temps de sortie ABSOLUE et calculés dans l'ordre ; rendre par
///    blocs de 256 ou de 4 096 donne les mêmes échantillons, ce qui est la
///    condition « hors ligne = temps réel » de D2.6 ;
///  - **aucune allocation hors `prepare()`, `setMap()`, `setTransients()`**,
///    aucun aléatoire, aucun état caché : un `seek()` recale la chaîne sur la
///    position nominale (un saut de transport ne rejoue pas l'histoire, il
///    repart — et c'est dit ici).
///
/// LA SOURCE est n'importe quel objet qui a `frameAt(int64_t, float&, float&)`
/// et `requestRange(int64_t, int64_t)` — c'est-à-dire un `SampleStore`, résident
/// ou diffusé depuis le disque : la brique lit autour de la position courante
/// (± une fenêtre et la zone de recherche), jamais plus loin.
///
/// LES TRANSITOIRES (D12.3) : une liste de positions dans la source, triée.
/// Une attaque doit sonner UNE fois, à l'instant où la carte la met. Deux
/// choses s'y opposent, et la première n'a pas été devinée, elle a été
/// mesurée (un clic sur deux joué 17 à 20 ms trop tôt à ×1,5) : à
/// l'étirement, la fenêtre d'un grain lit PLUS de source que son avance
/// nominale (2 048 trames contre 683 par saut à ×1,5), si bien qu'un grain
/// cherché avant le grain propriétaire attrape déjà l'attaque dans sa queue ;
/// et à la compression, un grain cherché après peut reculer et la rejouer.
/// Règle, vérifiée au banc : le grain PROPRIÉTAIRE (celui dont la première
/// moitié, en sortie, contient l'attaque) est posé de sorte qu'elle tombe
/// exactement à sa position de sortie ; le grain d'avant est posé une
/// demi-fenêtre plus tôt dans la source (sa suite naturelle EST le grain
/// propriétaire) et le grain d'après suit naturellement — trois grains
/// alignés, dont la somme des fenêtres vaut un, jouent l'attaque entière,
/// une fois. Et TOUT AUTRE GRAIN est coupé au transitoire : ceux d'avant
/// gardent ce qui précède l'attaque, ceux d'après ce qui la suit. Le prix est
/// un creux de quelques millisecondes dans la queue du grain coupé, juste
/// avant l'attaque — ce qu'une attaque réelle a de toute façon.
template <class Source>
class TimeStretch {
public:
    static constexpr int kWindow = 2048;   ///< 43 ms à 48 kHz
    static constexpr int kHop = kWindow / 2;
    static constexpr int kSearch = 768;    ///< 16 ms : une période de 62 Hz
    static constexpr int kCoarse = 8;      ///< pas de la recherche grossière

    /// Un point de la carte : à `outputFrame` (ligne de temps de sortie, en
    /// trames), la source est à `sourceFrame` (fractionnaire autorisé).
    struct MapPoint { int64_t outputFrame = 0; double sourceFrame = 0.0; };

    /// À appeler hors du thread audio. `maxBlock` borne la taille des blocs
    /// rendus d'un coup.
    void prepare(int maxBlock) {
        maxBlock_ = std::max(1, maxBlock);
        ringSize_ = kWindow + kHop + maxBlock_;
        ringL_.assign(static_cast<size_t>(ringSize_), 0.0f);
        ringR_.assign(static_cast<size_t>(ringSize_), 0.0f);
        window_.resize(static_cast<size_t>(kWindow));
        for (int n = 0; n < kWindow; ++n)
            window_[static_cast<size_t>(n)] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * M_PI * n / kWindow));
        candM_.assign(static_cast<size_t>(kWindow + 2 * kSearch), 0.0f);
        refM_.assign(static_cast<size_t>(kHop), 0.0f);
        map_.reserve(64);
        transients_.reserve(4096);
        if (map_.empty()) setRatio(0, 0.0, 1.0);
        prepared_ = true;
        seek(0);
    }
    bool isPrepared() const { return prepared_; }

    /// La carte, par morceaux linéaires, dans l'ordre des sorties croissantes.
    /// Avant le premier point et après le dernier, le rapport du segment
    /// voisin se prolonge. Hors thread audio (elle peut allouer).
    void setMap(const MapPoint* points, int count) {
        map_.assign(points, points + std::max(0, count));
        if (map_.empty()) map_.push_back({0, 0.0});
        if (map_.size() == 1) map_.push_back({map_[0].outputFrame + 1, map_[0].sourceFrame + 1.0});
        // LE COURT-CIRCUIT se décide ici : tous les segments au rapport un
        // exactement, et un décalage entier. Les égalités sont voulues BIT À
        // BIT (c'est la condition qui permet de copier au lieu de calculer) et
        // s'écrivent donc par deux comparaisons d'ordre, l'idiome du dépôt
        // sous `-Wfloat-equal` (`dsp/Constants.h`).
        const auto memeValeur = [](double a, double b) { return !(a < b) && !(b < a); };
        bypass_ = memeValeur(std::floor(map_[0].sourceFrame), map_[0].sourceFrame);
        for (size_t i = 1; i < map_.size() && bypass_; ++i) {
            const double ds = map_[i].sourceFrame - map_[i - 1].sourceFrame;
            const auto dout = static_cast<double>(map_[i].outputFrame - map_[i - 1].outputFrame);
            if (!memeValeur(ds, dout)) bypass_ = false;
        }
    }
    /// Le cas simple : un rapport constant `ratio` = durée de sortie / durée
    /// de source, la source à `sourceStart` quand la sortie est à `outputStart`.
    void setRatio(int64_t outputStart, double sourceStart, double ratio) {
        const double r = std::max(1e-6, ratio);
        MapPoint p[2] = {{outputStart, sourceStart}, {outputStart + 1'000'000, sourceStart + 1'000'000.0 / r}};
        setMap(p, 2);
    }
    bool bypassed() const { return bypass_; }
    /// Les transitoires de la source, en trames, triés. Hors thread audio.
    void setTransients(const int64_t* frames, int count) {
        transients_.assign(frames, frames + std::max(0, count));
    }

    /// Position dans la source pour une trame de sortie, d'après la carte.
    double sourceFor(double outputFrame) const {
        size_t i = 1;
        while (i + 1 < map_.size() && static_cast<double>(map_[i].outputFrame) <= outputFrame) ++i;
        const MapPoint& a = map_[i - 1];
        const MapPoint& b = map_[i];
        return a.sourceFrame + pente(a, b) * (outputFrame - static_cast<double>(a.outputFrame));
    }
    /// L'inverse : la trame de sortie où une position de la source tombe.
    double outputFor(double sourceFrame) const {
        size_t i = 1;
        while (i + 1 < map_.size() && map_[i].sourceFrame <= sourceFrame) ++i;
        const MapPoint& a = map_[i - 1];
        const MapPoint& b = map_[i];
        const double p = pente(a, b);
        return static_cast<double>(a.outputFrame) + (p > 0.0 ? (sourceFrame - a.sourceFrame) / p : 0.0);
    }

    /// Recale la chaîne à `outputFrame` : le prochain grain est pris à sa
    /// position nominale. Sans allocation.
    void seek(int64_t outputFrame) {
        std::fill(ringL_.begin(), ringL_.end(), 0.0f);
        std::fill(ringR_.begin(), ringR_.end(), 0.0f);
        // Le premier grain à calculer est le premier dont le support couvre
        // `outputFrame` : celui d'avant (k-1) le couvre aussi par sa seconde
        // moitié, on le calcule donc AUSSI pour que la somme des fenêtres
        // vaille un dès la première trame.
        nextGrain_ = floorDiv(outputFrame, kHop) - 1;
        prevSource_ = sourceFor(static_cast<double>(nextGrain_ * kHop)) - kHop;
        hasPrev_ = false;
        naturel_ = 0;
        consumed_ = outputFrame;
    }

    /// Rend `numFrames` trames de sortie à partir de `outputFrame`, qui doit
    /// suivre le rendu précédent (sinon la chaîne est recalée par `seek`).
    /// AJOUTE dans les tampons (comme `mixInto`), multiplié par `gain`.
    void render(const Source& source, int64_t outputFrame, int numFrames,
                float* outL, float* outR, float gain = 1.0f) {
        if (!prepared_ || numFrames <= 0) return;
        if (outputFrame != consumed_) seek(outputFrame);
        if (bypass_) {
            const int64_t s = static_cast<int64_t>(sourceFor(static_cast<double>(outputFrame)));
            source.requestRange(s, numFrames);
            for (int i = 0; i < numFrames; ++i) {
                float g = 0.0f, d = 0.0f;
                if (source.frameAt(s + i, g, d)) { outL[i] += g * gain; outR[i] += d * gain; }
            }
            consumed_ = outputFrame + numFrames;
            return;
        }
        int fait = 0;
        while (fait < numFrames) {
            const int n = std::min(maxBlock_, numFrames - fait);
            const int64_t debut = outputFrame + fait;
            // Tous les grains dont le support touche [debut, debut+n) :
            // jusqu'à k_max = floor((debut+n-1)/hop).
            const int64_t kMax = floorDiv(debut + n - 1, kHop);
            while (nextGrain_ <= kMax) calculerGrain(source, nextGrain_++);
            for (int i = 0; i < n; ++i) {
                const size_t idx = ringIndex(debut + i);
                outL[fait + i] += ringL_[idx] * gain;
                outR[fait + i] += ringR_[idx] * gain;
                ringL_[idx] = 0.0f; ringR_[idx] = 0.0f;
            }
            fait += n;
            consumed_ = debut + n;
        }
    }

private:
    static double pente(const MapPoint& a, const MapPoint& b) {
        return (b.sourceFrame - a.sourceFrame)
               / static_cast<double>(std::max<int64_t>(1, b.outputFrame - a.outputFrame));
    }
    static int64_t floorDiv(int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
    size_t ringIndex(int64_t frame) const {
        const int64_t m = frame % ringSize_;
        return static_cast<size_t>(m < 0 ? m + ringSize_ : m);
    }

    /// Le transitoire que le grain k+1 doit recevoir : celui dont la position
    /// de sortie tombe dans la PREMIÈRE moitié du grain k+1. Rend vrai et la
    /// position source où poser le grain k+1 pour que l'attaque tombe juste.
    bool verrouPour(int64_t kSuivant, int64_t& poseSuivant) const {
        if (transients_.empty()) return false;
        const int64_t gs = kSuivant * kHop;
        const double sDebut = sourceFor(static_cast<double>(gs));
        const double sFin = sourceFor(static_cast<double>(gs + kHop));
        // Le premier transitoire de la source entre les deux (la carte est
        // croissante) ; on cherche dans la source, puis on vérifie en sortie.
        auto it = std::lower_bound(transients_.begin(), transients_.end(),
                                   static_cast<int64_t>(std::floor(sDebut)));
        for (; it != transients_.end() && static_cast<double>(*it) < sFin + 1.0; ++it) {
            const int64_t outT = static_cast<int64_t>(std::llround(outputFor(static_cast<double>(*it))));
            if (outT >= gs && outT < gs + kHop) {
                poseSuivant = *it - (outT - gs);
                return true;
            }
        }
        return false;
    }

    /// Le grain propriétaire d'un transitoire : celui dont la première moitié,
    /// en sortie, le contient.
    int64_t proprietaire(int64_t transitoire) const {
        return floorDiv(static_cast<int64_t>(std::llround(outputFor(static_cast<double>(transitoire)))), kHop);
    }

    void calculerGrain(const Source& source, int64_t k) {
        const int64_t gs = k * kHop;                       // début du grain, en sortie
        const int64_t base = static_cast<int64_t>(std::llround(sourceFor(static_cast<double>(gs))));
        int64_t q = base;
        int64_t poseSuivant = 0;
        bool aligne = true;
        const bool prepareVerrou = verrouPour(k + 1, poseSuivant);
        if (prepareVerrou) {
            // Ce grain est la moitié d'avant du grain propriétaire : sa suite
            // naturelle est exactement le grain propriétaire.
            q = poseSuivant - kHop;
            naturel_ = 2;   // le propriétaire, puis celui d'après, suivent
        } else if (naturel_ > 0 && hasPrev_) {
            q = static_cast<int64_t>(std::llround(prevSource_)) + kHop;
            --naturel_;
        } else if (hasPrev_) {
            q = chercher(source, base);
            aligne = false;
        } else {
            aligne = false;
        }
        // UN GRAIN CHERCHÉ NE CONTIENT JAMAIS DE TRANSITOIRE : coupé au
        // premier qu'il rencontre, du côté qui le concerne.
        int nDebut = 0, nFin = kWindow;
        if (!aligne && !transients_.empty()) {
            auto it = std::lower_bound(transients_.begin(), transients_.end(), q + 1);
            if (it != transients_.end() && *it < q + kWindow) {
                const int64_t o = proprietaire(*it);
                if (k < o) nFin = static_cast<int>(*it - q);           // avant : on garde ce qui précède
                else if (k > o) nDebut = static_cast<int>(*it - q);    // après : ce qui suit
            }
        }
        source.requestRange(q + nDebut, nFin - nDebut);
        for (int n = nDebut; n < nFin; ++n) {
            float g = 0.0f, d = 0.0f;
            if (!source.frameAt(q + n, g, d)) continue;
            const float w = window_[static_cast<size_t>(n)];
            const size_t idx = ringIndex(gs + n);
            ringL_[idx] += g * w;
            ringR_[idx] += d * w;
        }
        prevSource_ = static_cast<double>(q);
        hasPrev_ = true;
    }

    /// LA RECHERCHE : le candidat à ± kSearch du nominal qui ressemble le plus
    /// à la suite naturelle du grain précédent, sur la zone de recouvrement.
    int64_t chercher(const Source& source, int64_t base) {
        const int64_t ref = static_cast<int64_t>(std::llround(prevSource_)) + kHop;
        const int64_t zoneDebut = base - kSearch;
        const int64_t zoneTaille = kWindow + 2 * kSearch;
        source.requestRange(zoneDebut, zoneTaille);
        for (int64_t i = 0; i < zoneTaille; ++i) {
            float g = 0.0f, d = 0.0f;
            source.frameAt(zoneDebut + i, g, d);
            candM_[static_cast<size_t>(i)] = g + d;
        }
        source.requestRange(ref, kHop);
        double refE = 0.0;
        for (int i = 0; i < kHop; ++i) {
            float g = 0.0f, d = 0.0f;
            source.frameAt(ref + i, g, d);
            refM_[static_cast<size_t>(i)] = g + d;
            refE += static_cast<double>(g + d) * (g + d);
        }
        if (refE <= 1e-12) return base;   // silence : rien à raccorder
        // Grossière : tous les `kCoarse` échantillons ; fine : ± kCoarse
        // autour du meilleur. Le décalage 0 est toujours candidat.
        int meilleur = 0;
        double meilleurScore = score(0, refE);
        for (int delta = -kSearch; delta <= kSearch; delta += kCoarse) {
            const double s = score(delta, refE);
            if (s > meilleurScore) { meilleurScore = s; meilleur = delta; }
        }
        const int centre = meilleur;
        for (int delta = std::max(-kSearch, centre - kCoarse + 1);
             delta <= std::min(kSearch, centre + kCoarse - 1); ++delta) {
            const double s = score(delta, refE);
            if (s > meilleurScore) { meilleurScore = s; meilleur = delta; }
        }
        return base + meilleur;
    }

    /// Corrélation croisée normalisée entre la référence et le candidat à
    /// `delta` du nominal, sur la zone de recouvrement (kHop échantillons).
    double score(int delta, double refE) const {
        const size_t o = static_cast<size_t>(delta + kSearch);
        double num = 0.0, ene = 0.0;
        for (int i = 0; i < kHop; ++i) {
            const double c = candM_[o + static_cast<size_t>(i)];
            num += c * refM_[static_cast<size_t>(i)];
            ene += c * c;
        }
        if (ene < 1e-12) return -1.0;
        return num / std::sqrt(ene * refE);
    }

    int maxBlock_ = 512;
    int64_t ringSize_ = 1;
    std::vector<float> ringL_, ringR_, window_, candM_, refM_;
    std::vector<MapPoint> map_;
    std::vector<int64_t> transients_;
    int64_t nextGrain_ = 0, consumed_ = 0;
    double prevSource_ = 0.0;
    int naturel_ = 0;
    bool hasPrev_ = false, prepared_ = false, bypass_ = true;
};

} // namespace vsm::audio::dsp
