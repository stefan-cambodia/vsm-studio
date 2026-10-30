#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace vsm::audio::dsp {

/// D605.1 — CE QU'UN ACCORDEUR MESURE : la fondamentale d'un son monophonique.
///
/// `found` faux quand rien ne se mesure — le silence (sous −50 dBFS d'efficace), un bruit ou un accord (aucun creux de
/// YIN sous le seuil) : une mesure qui ne peut pas voir le DIT, elle ne rend pas une note au hasard (la leçon de D265).
struct PitchReading {
    bool found = false;
    float hz = 0.0f;      ///< la fréquence mesurée
    int note = 0;         ///< la note MIDI la plus proche, rapportée au diapason donné
    float cents = 0.0f;   ///< l'écart à cette note, en cents (−50..+50)
    float clarity = 0.0f; ///< 1 − le creux de YIN : 1 pour un sinus pur, vers 0 pour un bruit
};

/// La note et l'écart d'une fréquence, au diapason `referenceA4` (le la 4, la note 69).
inline void noteDeFrequence(float hz, float referenceA4, int& note, float& cents) {
    const double midi = 69.0 + 12.0 * std::log2(static_cast<double>(hz) / static_cast<double>(referenceA4));
    note = static_cast<int>(std::lround(midi));
    cents = static_cast<float>((midi - note) * 100.0);
}

/// YIN (de Cheveigné et Kawahara, 2002) sur `n` échantillons à `sampleRate` : la différence de la fenêtre avec elle-même
/// décalée, cumulée et normalisée ; le PREMIER creux sous `seuil` (pas le plus profond : c'est ce qui écarte l'octave du
/// dessous), affiné par une parabole. Il faut au moins une période de `minHz` de décalage PLUS une longueur de
/// comparaison : avec `n` échantillons, la période la plus longue mesurable est `n / 2`. `travail` est réutilisé d'un
/// appel à l'autre — rien n'est alloué quand sa capacité suffit (le fil audio de l'accordeur le dimensionne au départ).
inline PitchReading detectPitch(const float* x, int n, double sampleRate, float referenceA4, std::vector<float>& travail,
                                float minHz = 40.0f, float maxHz = 2000.0f, float seuil = 0.15f) {
    PitchReading lecture;
    if (x == nullptr || n < 8 || !(sampleRate > 0.0) || !(referenceA4 > 0.0f)) return lecture;
    double energie = 0.0;
    for (int i = 0; i < n; ++i) energie += static_cast<double>(x[i]) * x[i];
    if (std::sqrt(energie / n) < 0.0031623) return lecture;   // −50 dBFS : le silence ne se mesure pas
    const int tauMin = std::max(2, static_cast<int>(std::floor(sampleRate / maxHz)));
    const int tauMax = std::min(n / 2, static_cast<int>(std::ceil(sampleRate / minHz)));
    if (tauMax <= tauMin + 2) return lecture;
    const int largeur = n - tauMax;   // la longueur de comparaison
    // DEUX COURBES : la différence BRUTE (à partir de `brute`), et sa normalisation cumulée (au début) — le creux se
    // CHOISIT sur la seconde, il s'INTERPOLE sur la première (voir plus bas).
    const size_t brute = static_cast<size_t>(tauMax + 2);
    travail.assign(2 * brute, 0.0f);
    // LA DIFFÉRENCE, puis sa normalisation cumulée (d'[0] = 1).
    for (int tau = 1; tau <= tauMax + 1 && tau < n; ++tau) {
        double somme = 0.0;
        const int fin = std::min(largeur, n - tau);
        for (int j = 0; j < fin; ++j) {
            const double d = static_cast<double>(x[j]) - x[j + tau];
            somme += d * d;
        }
        travail[static_cast<size_t>(tau)] = static_cast<float>(somme);
        travail[brute + static_cast<size_t>(tau)] = static_cast<float>(somme);
    }
    travail[0] = 1.0f;
    double cumul = 0.0;
    for (int tau = 1; tau <= tauMax + 1; ++tau) {
        cumul += travail[static_cast<size_t>(tau)];
        travail[static_cast<size_t>(tau)] =
            cumul > 0.0 ? static_cast<float>(travail[static_cast<size_t>(tau)] * tau / cumul) : 1.0f;
    }
    // LE PREMIER CREUX SOUS LE SEUIL, descendu jusqu'à son fond.
    int choisi = -1;
    for (int tau = tauMin; tau <= tauMax; ++tau) {
        if (travail[static_cast<size_t>(tau)] < seuil) {
            while (tau + 1 <= tauMax && travail[static_cast<size_t>(tau + 1)] < travail[static_cast<size_t>(tau)]) ++tau;
            choisi = tau;
            break;
        }
    }
    if (choisi < 0) return lecture;   // un bruit, un accord : aucun creux franc — rien ne se mesure
    // L'INTERPOLATION PARABOLIQUE du creux : la période entre deux échantillons — sur la différence BRUTE. CORRIGÉ APRÈS
    // LA MESURE (D605) : interpolée sur la courbe normalisée, elle tirait vers l'aigu à mesure que la période raccourcit —
    // +1,95 c au do 6 à 44,1 kHz (le banc de l'application lisait +2,03 c), +7 c à 2 kHz ; sur la différence brute,
    // +0,04 c et +0,6 c (mesuré sur une réplique de l'algorithme avant de le changer).
    double periode = choisi;
    if (choisi > 1 && choisi < tauMax + 1) {
        const double a = travail[brute + static_cast<size_t>(choisi - 1)];
        const double b = travail[brute + static_cast<size_t>(choisi)];
        const double c = travail[brute + static_cast<size_t>(choisi + 1)];
        const double courbure = a - 2.0 * b + c;
        if (courbure > 0.0) periode += 0.5 * (a - c) / courbure;
    }
    lecture.found = true;
    lecture.hz = static_cast<float>(sampleRate / periode);
    lecture.clarity = std::clamp(1.0f - travail[static_cast<size_t>(choisi)], 0.0f, 1.0f);
    noteDeFrequence(lecture.hz, referenceA4, lecture.note, lecture.cents);
    return lecture;
}

} // namespace vsm::audio::dsp
