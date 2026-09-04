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

} // namespace vsm::audio::io
