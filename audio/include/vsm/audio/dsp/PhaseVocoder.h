#pragma once
#include "vsm/audio/dsp/RealFft.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vsm::audio::dsp {

/// LE VOCODEUR DE PHASE — l'autre étirement temporel (D12.8,
/// `docs/CDC-etirement-temporel.md` § 7 bis), même contrat que `TimeStretch`.
///
/// CE QU'IL FAIT. Le signal est découpé en trames de `kSize` échantillons sous
/// une fenêtre de Hann, transformé en spectre ; les trames de SYNTHÈSE sont
/// posées à saut FIXE (`kHop`) sur la ligne de temps de sortie, et c'est la
/// carte qui dit, pour chacune, où lire la trame d'ANALYSE dans la source. Ce
/// qui assure la continuité n'est pas la position (un WSOLA cherche où
/// raccorder ; ici la trame est posée exactement où elle doit l'être), c'est
/// la PHASE : chaque bande avance à sa fréquence instantanée, mesurée entre
/// deux trames d'analyse, sur la durée du saut de synthèse.
///
/// LES DEUX PARADES, sans lesquelles un vocodeur de phase ne vaut rien :
///  1. **le verrouillage d'identité sur les pics** (Laroche et Dolson) : les
///     bandes qui entourent un pic gardent, en synthèse, la relation de phase
///     qu'elles ont en analyse. Sans lui, chaque bande dérive de son côté et
///     un son tenu devient « phaseux » -- c'est la promesse du banc 7 ;
///  2. **la remise à zéro des phases aux transitoires** : la trame qui
///     contient une attaque déclarée reprend ses phases d'analyse telles
///     quelles, posée pour que l'attaque tombe où la carte la met ; celle
///     d'après suit naturellement ; les autres trames sont COUPÉES au
///     transitoire, exactement comme les grains du WSOLA (mêmes raisons,
///     même règle du propriétaire) -- c'est la promesse du banc 4.
///
/// CE QU'IL GARANTIT, comme `TimeStretch` : rapport un = l'original au bit
/// près ; trames posées sur la ligne de temps ABSOLUE et calculées dans
/// l'ordre, donc indépendance de la taille des blocs ; aucune allocation hors
/// `prepare()`/`setMap()`/`setTransients()` ; `seek()` repart à froid.
///
/// APPROXIMATION ASSUMÉE : quatre trames se recouvrent (saut = taille/4),
/// et la somme des fenêtres au carré vaut 1,5 -- constante seulement quand
/// les quatre sont là ; autour d'une trame coupée, un creux, comme au WSOLA.
template <class Source>
class PhaseVocoder {
public:
    static constexpr int kSize = 2048;
    static constexpr int kBins = kSize / 2 + 1;
    static constexpr int kHop = kSize / 4;

    struct MapPoint { int64_t outputFrame = 0; double sourceFrame = 0.0; };

    void prepare(int maxBlock) {
        maxBlock_ = std::max(1, maxBlock);
        ringSize_ = kSize + kHop + maxBlock_;
        ringL_.assign(static_cast<size_t>(ringSize_), 0.0f);
        ringR_.assign(static_cast<size_t>(ringSize_), 0.0f);
        window_.resize(static_cast<size_t>(kSize));
        for (int n = 0; n < kSize; ++n)
            window_[static_cast<size_t>(n)] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * M_PI * n / kSize));
        trameL_.assign(static_cast<size_t>(kSize), 0.0f);
        trameR_.assign(static_cast<size_t>(kSize), 0.0f);
        for (auto* v : {&reL_, &imL_, &reR_, &imR_, &magL_, &phaL_, &phaR_, &phaPrevL_, &phaPrevR_,
                        &synL_, &synR_})
            v->assign(static_cast<size_t>(kBins), 0.0f);
        pic_.assign(static_cast<size_t>(kBins), 0);
        map_.reserve(64);
        transients_.reserve(4096);
        if (map_.empty()) setRatio(0, 0.0, 1.0);
        prepared_ = true;
        seek(0);
    }
    bool isPrepared() const { return prepared_; }

    void setMap(const MapPoint* points, int count) {
        map_.assign(points, points + std::max(0, count));
        if (map_.empty()) map_.push_back({0, 0.0});
        if (map_.size() == 1) map_.push_back({map_[0].outputFrame + 1, map_[0].sourceFrame + 1.0});
        const auto memeValeur = [](double a, double b) { return !(a < b) && !(b < a); };
        bypass_ = memeValeur(std::floor(map_[0].sourceFrame), map_[0].sourceFrame);
        for (size_t i = 1; i < map_.size() && bypass_; ++i) {
            const double ds = map_[i].sourceFrame - map_[i - 1].sourceFrame;
            const auto dout = static_cast<double>(map_[i].outputFrame - map_[i - 1].outputFrame);
            if (!memeValeur(ds, dout)) bypass_ = false;
        }
    }
    void setRatio(int64_t outputStart, double sourceStart, double ratio) {
        const double r = std::max(1e-6, ratio);
        MapPoint p[2] = {{outputStart, sourceStart}, {outputStart + 1'000'000, sourceStart + 1'000'000.0 / r}};
        setMap(p, 2);
    }
    bool bypassed() const { return bypass_; }
    void setTransients(const int64_t* frames, int count) {
        transients_.assign(frames, frames + std::max(0, count));
    }

    double sourceFor(double outputFrame) const {
        size_t i = 1;
        while (i + 1 < map_.size() && static_cast<double>(map_[i].outputFrame) <= outputFrame) ++i;
        const MapPoint& a = map_[i - 1];
        const MapPoint& b = map_[i];
        return a.sourceFrame + pente(a, b) * (outputFrame - static_cast<double>(a.outputFrame));
    }
    double outputFor(double sourceFrame) const {
        size_t i = 1;
        while (i + 1 < map_.size() && map_[i].sourceFrame <= sourceFrame) ++i;
        const MapPoint& a = map_[i - 1];
        const MapPoint& b = map_[i];
        const double p = pente(a, b);
        return static_cast<double>(a.outputFrame) + (p > 0.0 ? (sourceFrame - a.sourceFrame) / p : 0.0);
    }

    void seek(int64_t outputFrame) {
        std::fill(ringL_.begin(), ringL_.end(), 0.0f);
        std::fill(ringR_.begin(), ringR_.end(), 0.0f);
        // Les quatre trames dont le support couvre `outputFrame` : on repart
        // trois sauts plus tôt pour que la somme des fenêtres soit entière
        // dès la première trame lue.
        nextFrame_ = floorDiv(outputFrame, kHop) - 3;
        hasPrev_ = false;
        naturel_ = 0;
        consumed_ = outputFrame;
    }

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
            const int64_t kMax = floorDiv(debut + n - 1, kHop);
            while (nextFrame_ <= kMax) calculerTrame(source, nextFrame_++);
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
    static float princarg(float phase) {
        const float deuxPi = static_cast<float>(2.0 * M_PI);
        return phase - deuxPi * std::floor((phase + static_cast<float>(M_PI)) / deuxPi);
    }
    int64_t proprietaire(int64_t transitoire) const {
        return floorDiv(static_cast<int64_t>(std::llround(outputFor(static_cast<double>(transitoire)))), kHop);
    }
    bool verrouPour(int64_t kSuivant, int64_t& poseSuivant) const {
        if (transients_.empty()) return false;
        const int64_t gs = kSuivant * kHop;
        const double sDebut = sourceFor(static_cast<double>(gs));
        const double sFin = sourceFor(static_cast<double>(gs + kHop));
        auto it = std::lower_bound(transients_.begin(), transients_.end(),
                                   static_cast<int64_t>(std::floor(sDebut)));
        for (; it != transients_.end() && static_cast<double>(*it) < sFin + 1.0; ++it) {
            const int64_t outT = static_cast<int64_t>(std::llround(outputFor(static_cast<double>(*it))));
            if (outT >= gs && outT < gs + kHop) { poseSuivant = *it - (outT - gs); return true; }
        }
        return false;
    }

    void calculerTrame(const Source& source, int64_t k) {
        const int64_t gs = k * kHop;
        const int64_t base = static_cast<int64_t>(std::llround(sourceFor(static_cast<double>(gs))));
        int64_t a = base;
        bool aligne = false, reset = false;
        int64_t poseSuivant = 0;
        if (verrouPour(k + 1, poseSuivant)) {
            // La trame d'avant le propriétaire : sa suite naturelle EST le
            // propriétaire, et elle est déjà coupée avant l'attaque (ci-dessous).
            a = poseSuivant - kHop;
            naturel_ = 2;
            aligne = true;
            // La trame propriétaire (la suivante) et celle d'après reprennent
            // leurs phases d'analyse : c'est la remise à zéro.
            resetSuivant_ = 2;
        } else if (naturel_ > 0 && hasPrev_) {
            a = prevSource_ + kHop;
            --naturel_;
            aligne = true;
        }
        if (resetSuivant_ > 0) { reset = true; --resetSuivant_; }

        // LA TRAME D'ANALYSE, coupée au transitoire si elle n'est pas alignée.
        int nDebut = 0, nFin = kSize;
        if (!aligne && !transients_.empty()) {
            auto it = std::lower_bound(transients_.begin(), transients_.end(), a + 1);
            if (it != transients_.end() && *it < a + kSize) {
                const int64_t o = proprietaire(*it);
                if (k < o) nFin = static_cast<int>(*it - a);
                else if (k > o) nDebut = static_cast<int>(*it - a);
            }
        }
        source.requestRange(a, kSize);
        for (int n = 0; n < kSize; ++n) {
            float g = 0.0f, d = 0.0f;
            if (n < nDebut || n >= nFin || !source.frameAt(a + n, g, d)) { g = 0.0f; d = 0.0f; }
            const float w = window_[static_cast<size_t>(n)];
            trameL_[static_cast<size_t>(n)] = g * w;
            trameR_[static_cast<size_t>(n)] = d * w;
        }
        fft_.forward(trameL_.data(), reL_.data(), imL_.data());
        fft_.forward(trameR_.data(), reR_.data(), imR_.data());
        for (int b = 0; b < kBins; ++b) {
            const auto i = static_cast<size_t>(b);
            magL_[i] = std::sqrt(reL_[i] * reL_[i] + imL_[i] * imL_[i]);
            phaL_[i] = std::atan2(imL_[i], reL_[i]);
            phaR_[i] = std::atan2(imR_[i], reR_[i]);
        }

        // LA PROPAGATION DE PHASE, sur le canal gauche (la magnitude du
        // mélange décide des pics), appliquée aux deux canaux : la relation
        // de phase entre gauche et droite est conservée telle quelle, sinon
        // l'image stéréo se déferait.
        const bool premiere = !hasPrev_ || reset;
        if (premiere) {
            for (int b = 0; b < kBins; ++b) {
                synL_[static_cast<size_t>(b)] = phaL_[static_cast<size_t>(b)];
                synR_[static_cast<size_t>(b)] = phaR_[static_cast<size_t>(b)];
            }
        } else {
            const double deltaA = static_cast<double>(a - prevSource_);
            // Les pics : une bande plus forte que ses deux voisines de chaque côté.
            int dernierPic = 0;
            for (int b = 0; b < kBins; ++b) {
                const auto i = static_cast<size_t>(b);
                const bool pic = b >= 2 && b + 2 < kBins
                                 && magL_[i] > magL_[i - 1] && magL_[i] >= magL_[i + 1]
                                 && magL_[i] > magL_[i - 2] && magL_[i] >= magL_[i + 2];
                if (pic) dernierPic = b;
                pic_[i] = dernierPic;
            }
            // Chaque bande appartient au pic le plus proche : à gauche du
            // milieu entre deux pics, le pic d'avant ; à droite, celui d'après.
            for (int b = 0; b < kBins; ++b) {
                const auto i = static_cast<size_t>(b);
                int p = pic_[i];
                // Le pic suivant, s'il est plus proche.
                int q = b;
                while (q + 1 < kBins && pic_[static_cast<size_t>(q + 1)] == p) ++q;
                if (q + 1 < kBins) {
                    const int suivant = pic_[static_cast<size_t>(q + 1)];
                    if (suivant - b < b - p) p = suivant;
                }
                pic_[i] = p;
            }
            // Le pic avance à sa fréquence instantanée ; ses bandes le suivent.
            for (int b = 0; b < kBins; ++b) {
                const auto i = static_cast<size_t>(b);
                const int p = pic_[i];
                if (b == p || b == 0) {
                    const double omega = 2.0 * M_PI * static_cast<double>(b) / kSize;
                    const double attendu = omega * deltaA;
                    const double dev = princarg(static_cast<float>(phaL_[i] - phaPrevL_[i] - attendu));
                    const double omegaHat = deltaA > 0.0 ? omega + dev / deltaA : omega;
                    synL_[i] = princarg(static_cast<float>(synL_[i] + omegaHat * kHop));
                    synR_[i] = princarg(static_cast<float>(synR_[i] + omegaHat * kHop));
                }
            }
            for (int b = 0; b < kBins; ++b) {
                const auto i = static_cast<size_t>(b);
                const auto ip = static_cast<size_t>(pic_[i]);
                if (b == pic_[i] || b == 0) continue;
                synL_[i] = princarg(synL_[ip] + (phaL_[i] - phaL_[ip]));
                synR_[i] = princarg(synR_[ip] + (phaR_[i] - phaR_[ip]));
            }
        }
        for (int b = 0; b < kBins; ++b) {
            const auto i = static_cast<size_t>(b);
            const float mR = std::sqrt(reR_[i] * reR_[i] + imR_[i] * imR_[i]);
            reL_[i] = magL_[i] * std::cos(synL_[i]); imL_[i] = magL_[i] * std::sin(synL_[i]);
            reR_[i] = mR * std::cos(synR_[i]);        imR_[i] = mR * std::sin(synR_[i]);
            phaPrevL_[i] = phaL_[i];
            phaPrevR_[i] = phaR_[i];
        }
        fft_.inverse(reL_.data(), imL_.data(), trameL_.data());
        fft_.inverse(reR_.data(), imR_.data(), trameR_.data());
        // La synthèse, fenêtrée à son tour et normalisée par la somme des
        // fenêtres au carré à ce recouvrement (1,5).
        constexpr float kNorme = 1.0f / 1.5f;
        for (int n = 0; n < kSize; ++n) {
            const auto i = static_cast<size_t>(n);
            const float w = window_[i] * kNorme;
            const size_t idx = ringIndex(gs + n);
            ringL_[idx] += trameL_[i] * w;
            ringR_[idx] += trameR_[i] * w;
        }
        prevSource_ = a;
        hasPrev_ = true;
    }

    int maxBlock_ = 512;
    int64_t ringSize_ = 1;
    std::vector<float> ringL_, ringR_, window_, trameL_, trameR_;
    std::vector<float> reL_, imL_, reR_, imR_, magL_, phaL_, phaR_, phaPrevL_, phaPrevR_, synL_, synR_;
    std::vector<int> pic_;
    std::vector<MapPoint> map_;
    std::vector<int64_t> transients_;
    RealIfft<static_cast<size_t>(kSize)> fft_;
    int64_t nextFrame_ = 0, consumed_ = 0, prevSource_ = 0;
    int naturel_ = 0, resetSuivant_ = 0;
    bool hasPrev_ = false, prepared_ = false, bypass_ = true;
};

} // namespace vsm::audio::dsp
