#include "vsm/audio/io/SilenceDetection.h"
#include <algorithm>
#include <cmath>

namespace vsm::audio::io {

SoundBounds detectSound(const std::function<bool(int64_t, float&, float&)>& frameAt,
                         int64_t frames, double sampleRate, double thresholdDb,
                         double preAttackSeconds, double minSilenceSeconds) {
    SoundBounds bornes;
    if (!frameAt || frames <= 0 || sampleRate <= 0.0) return bornes;

    const float seuil = static_cast<float>(std::pow(10.0, thresholdDb / 20.0));

    int64_t premiere = -1, derniere = -1;
    for (int64_t i = 0; i < frames; ++i) {
        float g = 0.0f, d = 0.0f;
        if (!frameAt(i, g, d)) continue;
        if (std::max(std::abs(g), std::abs(d)) < seuil) continue;
        if (premiere < 0) premiere = i;
        derniere = i;
    }
    // TOUT EST SOUS LE SEUIL : on ne rogne rien. Un clip entièrement
    // silencieux réduit à zéro tick disparaîtrait, et personne n'a demandé de
    // le supprimer.
    if (premiere < 0) return bornes;

    const auto marge = static_cast<int64_t>(std::llround(preAttackSeconds * sampleRate));
    const auto minimum = static_cast<int64_t>(std::llround(minSilenceSeconds * sampleRate));

    int64_t debut = std::max<int64_t>(0, premiere - marge);
    int64_t fin = std::min<int64_t>(frames, derniere + 1 + marge);
    // LE GARDE-FOU : sous le silence minimal, on ne touche pas à ce bord. Sans
    // lui, la commande grignoterait quelques millisecondes à chaque clip et
    // l'on ne saurait jamais si elle a fait quelque chose.
    if (debut < minimum) debut = 0;
    if (frames - fin < minimum) fin = frames;

    bornes.firstFrame = debut;
    bornes.lastFrame = fin;
    bornes.found = true;
    return bornes;
}

std::vector<SoundBounds> detectSoundRegions(const std::function<bool(int64_t, float&, float&)>& frameAt,
                                            int64_t frames, double sampleRate, double thresholdDb,
                                            double preAttackSeconds, double minSilenceSeconds) {
    std::vector<SoundBounds> passages;
    if (!frameAt || frames <= 0 || sampleRate <= 0.0) return passages;
    const float seuil = static_cast<float>(std::pow(10.0, thresholdDb / 20.0));
    const auto marge = static_cast<int64_t>(std::llround(preAttackSeconds * sampleRate));
    const auto minimum = std::max<int64_t>(1, static_cast<int64_t>(std::llround(minSilenceSeconds * sampleRate)));

    // LES PASSAGES BRUTS : des trames au-dessus du seuil, réunies tant que le trou qui les sépare
    // est plus court que le silence minimal.
    int64_t debut = -1, derniere = -1;
    for (int64_t i = 0; i < frames; ++i) {
        float g = 0.0f, d = 0.0f;
        if (!frameAt(i, g, d)) continue;
        if (std::max(std::abs(g), std::abs(d)) < seuil) continue;
        if (debut >= 0 && i - derniere - 1 >= minimum) {
            passages.push_back({debut, derniere + 1, true});
            debut = -1;
        }
        if (debut < 0) debut = i;
        derniere = i;
    }
    if (debut >= 0) passages.push_back({debut, derniere + 1, true});
    if (passages.empty()) return passages;

    // LES MARGES, sans empiéter sur le voisin — et les bords du matériau, à la règle de `detectSound`.
    std::vector<SoundBounds> bruts = passages;
    for (size_t k = 0; k < passages.size(); ++k) {
        const int64_t plancher = k == 0 ? 0 : bruts[k - 1].lastFrame;
        const int64_t plafond = k + 1 == passages.size() ? frames : bruts[k + 1].firstFrame;
        passages[k].firstFrame = std::max(plancher, bruts[k].firstFrame - marge);
        passages[k].lastFrame = std::min(plafond, bruts[k].lastFrame + marge);
    }
    if (passages.front().firstFrame < minimum) passages.front().firstFrame = 0;
    if (frames - passages.back().lastFrame < minimum) passages.back().lastFrame = frames;
    return passages;
}

} // namespace vsm::audio::io
