#include "vsm/audio/io/TempoDetection.h"
#include "vsm/audio/dsp/Biquad.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace vsm::audio::io {

namespace {

/// Le biquad du moteur, comme `detectOnsets` : deux filtres écrits à deux endroits finiraient par ne plus
/// couper à la même fréquence.
struct Bande {
    vsm::audio::dsp::Biquad filtre;
    Bande(vsm::audio::dsp::Biquad::Type type, float fc, double sr) {
        filtre.setSampleRate(sr);
        filtre.set(type, fc, 0.70710678f, 0.0f);
    }
    double operator()(double x) { return static_cast<double>(filtre.process(static_cast<float>(x))); }
};

/// Le sommet interpolé (parabole sur trois points) de `r` autour de `i`.
double sommet(const std::vector<double>& r, size_t i) {
    if (i == 0 || i + 1 >= r.size()) return static_cast<double>(i);
    const double a = r[i - 1], b = r[i], c = r[i + 1];
    const double d = a - 2.0 * b + c;
    return d < 0.0 ? static_cast<double>(i) + 0.5 * (a - c) / d : static_cast<double>(i);
}

} // namespace

TempoEstimate estimateTempo(const std::function<bool(int64_t, float&, float&)>& frameAt, int64_t frames,
                            double sampleRate, double minBpm, double maxBpm) {
    TempoEstimate rendu;
    if (sampleRate <= 0.0 || frames < static_cast<int64_t>(sampleRate * 2.0)) {
        rendu.reason = "trop court (moins de deux secondes)";
        return rendu;
    }
    // L'ENVELOPPE DE FORCE D'ATTAQUE : par pas de 256 trames (~6 ms), le niveau efficace de quatre bandes —
    // le tout, le grave sous 200 Hz, le médium, l'aigu au-dessus de 2 kHz — ; la force d'un pas est la somme
    // des HAUSSES d'une bande à l'autre pas (le flux de `detectOnsets`). EN AMPLITUDE, PAS EN LOGARITHME :
    // le logarithme égalisait la force des attaques — un charleston à −12 dB y pesait autant qu'une grosse
    // caisse —, la grille des croches devenait uniforme, et l'a priori rendait 184 pour une boucle à 92
    // (mesuré à la première écriture).
    constexpr int64_t kPas = 256;
    const double pasSecondes = static_cast<double>(kPas) / sampleRate;
    using Type = vsm::audio::dsp::Biquad::Type;
    Bande grave(Type::LowPass, 200.0f, sampleRate);
    Bande coupeGrave(Type::HighPass, 200.0f, sampleRate);
    Bande aigu(Type::HighPass, 2000.0f, sampleRate);
    Bande coupeAigu(Type::LowPass, 2000.0f, sampleRate);
    const auto pas = static_cast<size_t>(frames / kPas);
    std::vector<std::array<double, 4>> energie(pas, std::array<double, 4>{});
    for (int64_t i = 0; i < static_cast<int64_t>(pas) * kPas; ++i) {
        float g = 0.0f, d = 0.0f;
        if (!frameAt(i, g, d)) break;
        const double v = 0.5 * (static_cast<double>(g) + static_cast<double>(d));
        const double bas = grave(v), haut = aigu(v), milieu = coupeAigu(coupeGrave(v));
        auto& e = energie[static_cast<size_t>(i / kPas)];
        e[0] += v * v; e[1] += bas * bas; e[2] += milieu * milieu; e[3] += haut * haut;
    }
    std::vector<double> force(pas, 0.0);
    double niveau = 0.0;
    for (size_t t = 1; t < pas; ++t) {
        niveau += std::sqrt(energie[t][0] / kPas);
        for (size_t b = 0; b < 4; ++b)
            force[t] += std::max(0.0, std::sqrt(energie[t][b] / kPas) - std::sqrt(energie[t - 1][b] / kPas));
    }
    double moyenne = 0.0;
    for (const double f : force) moyenne += f;
    moyenne /= static_cast<double>(pas);
    niveau /= static_cast<double>(pas);
    // UN SON QUI NE BOUGE PAS N'A PAS D'ATTAQUES, si petite et si régulière que soit sa ride : un sinus tenu,
    // découpé en pas de 256 trames qui ne tombent pas sur sa période, a une enveloppe PÉRIODIQUE de quelques
    // pour cent — l'autocorrélation normalisée ne voit pas l'échelle, et lui trouvait un tempo. Le flux moyen
    // doit valoir au moins 15 % du niveau. MESURÉ avant de le fixer (le rapport flux / niveau) : boucles de
    // batterie 0,393 à 0,459, charleston seul 1,00 ; sinus tenu 0,093, bruit blanc 0,071 (ce dernier écarté
    // aussi par la confiance, 0,06). 1 %, écrit d'abord, laissait passer le sinus.
    if (niveau <= 1e-9 || moyenne < 0.15 * niveau) {
        rendu.reason = "aucune attaque (le son ne varie pas)";
        return rendu;
    }
    for (double& f : force) f -= moyenne;

    // L'AUTOCORRÉLATION, normalisée par son retard nul, jusqu'à la moitié de l'enveloppe.
    const auto retardMax = pas / 2;
    std::vector<double> r(retardMax + 1, 0.0);
    for (size_t l = 0; l <= retardMax; ++l) {
        double s = 0.0;
        for (size_t t = l; t < pas; ++t) s += force[t] * force[t - l];
        r[l] = s;
    }
    if (r[0] <= 1e-9) {
        rendu.reason = "aucune attaque (le son ne varie pas)";
        return rendu;
    }
    // LE RETARD NUL D'ABORD : diviser `r` en place par `r[0]` le ramène à 1 au premier tour, et tous les
    // autres retards n'étaient plus divisés que par 1 (une confiance de 5 490, à la première course).
    const double r0 = r[0];
    for (double& x : r) x /= r0;

    // LE GROSSIER : le meilleur retard pondéré par l'a priori log-normal autour de 120 BPM.
    const auto versRetard = [&](double bpm) { return 60.0 / (bpm * pasSecondes); };
    const auto lMin = static_cast<size_t>(std::max(2.0, std::floor(versRetard(maxBpm))));
    const auto lMax = std::min(retardMax - 1, static_cast<size_t>(std::ceil(versRetard(minBpm))));
    if (lMax <= lMin + 2) {
        rendu.reason = "trop court pour la plage de tempo";
        return rendu;
    }
    size_t meilleur = lMin;
    double score = -1e9;
    for (size_t l = lMin; l <= lMax; ++l) {
        if (!(r[l] >= r[l - 1] && r[l] >= r[l + 1])) continue;   // un sommet local seulement
        const double bpm = 60.0 / (static_cast<double>(l) * pasSecondes);
        const double octaves = std::log2(bpm / 120.0);
        const double s = r[l] * std::exp(-0.5 * octaves * octaves);
        if (s > score) { score = s; meilleur = l; }
    }
    rendu.confidence = r[meilleur];
    if (score <= -1e9 || rendu.confidence < kTempoConfidenceFloor) {
        rendu.reason = "pas de périodicité nette (confiance sous le seuil)";
        return rendu;
    }

    // LE FIN : le sommet interpolé, puis ses multiples, et la période ajustée sur eux (moindres carrés par
    // l'origine : L = Σ k·Lk / Σ k²).
    const double l1 = sommet(r, meilleur);
    double numerateur = l1, denominateur = 1.0;
    for (int k = 2; static_cast<double>(k) * l1 + 3.0 < static_cast<double>(retardMax); ++k) {
        const auto centre = static_cast<size_t>(std::llround(static_cast<double>(k) * l1));
        size_t pic = centre;
        for (size_t j = centre - 2; j <= centre + 2; ++j) if (r[j] > r[pic]) pic = j;
        if (r[pic] < 0.5 * rendu.confidence) break;   // la périodicité s'efface : on s'arrête là
        numerateur += static_cast<double>(k) * sommet(r, pic);
        denominateur += static_cast<double>(k) * static_cast<double>(k);
    }
    rendu.bpm = 60.0 / ((numerateur / denominateur) * pasSecondes);
    rendu.found = true;
    return rendu;
}

} // namespace vsm::audio::io
