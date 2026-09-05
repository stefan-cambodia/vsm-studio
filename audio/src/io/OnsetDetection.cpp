#include "vsm/audio/io/OnsetDetection.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace vsm::audio::io {

namespace {

/// Un biquad RBJ, passe-bas ou passe-haut, Q de Butterworth. Assez pour
/// séparer trois bandes dont on ne compare que l'ÉNERGIE.
struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    static Biquad passeBas(double fc, double sr) {
        Biquad f;
        const double w = 2.0 * 3.14159265358979323846 * fc / sr;
        const double alpha = std::sin(w) / (2.0 * 0.70710678);
        const double c = std::cos(w);
        const double a0 = 1.0 + alpha;
        f.b0 = (1.0 - c) / 2.0 / a0; f.b1 = (1.0 - c) / a0; f.b2 = f.b0;
        f.a1 = -2.0 * c / a0; f.a2 = (1.0 - alpha) / a0;
        return f;
    }
    static Biquad passeHaut(double fc, double sr) {
        Biquad f;
        const double w = 2.0 * 3.14159265358979323846 * fc / sr;
        const double alpha = std::sin(w) / (2.0 * 0.70710678);
        const double c = std::cos(w);
        const double a0 = 1.0 + alpha;
        f.b0 = (1.0 + c) / 2.0 / a0; f.b1 = -(1.0 + c) / a0; f.b2 = f.b0;
        f.a1 = -2.0 * c / a0; f.a2 = (1.0 - alpha) / a0;
        return f;
    }
    double operator()(double x) {
        // Forme directe II transposée.
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

} // namespace

std::vector<int64_t> detectOnsets(const std::function<bool(int64_t, float&, float&)>& frameAt,
                                  int64_t frames, double sampleRate,
                                  double sensitivityDb, double minGapSeconds,
                                  double floorDb, double preAttackSeconds) {
    std::vector<int64_t> attaques;
    if (frames <= 0 || sampleRate <= 0.0) return attaques;

    // LES TRAMES : 20 ms de fenêtre, un pas de 5 ms. La fenêtre est PLUS LONGUE
    // QU'UNE PÉRIODE DE BASSE, et c'est mesuré : à 5 ms, une dent de scie de
    // TB-303 à 92 Hz -- un clic toutes les 10,8 ms dans la bande haute --
    // faisait bondir la trame qui contient le clic de dix décibels au-dessus
    // de celle qui ne le contient pas, et le détecteur trouvait vingt attaques
    // sur quatre notes. Vingt millisecondes en contiennent deux périodes, et
    // la précision de l'instant ne vient pas de la trame mais de l'affinage
    // ci-dessous.
    const auto fenetre = std::max<int64_t>(32, static_cast<int64_t>(std::llround(sampleRate * 0.020)));
    const auto pas = std::max<int64_t>(8, fenetre / 4);
    const auto contexte = std::max<int64_t>(4, static_cast<int64_t>(std::llround(sampleRate * 0.050 / static_cast<double>(pas))));
    // L'ÉCART MINIMAL NE DESCEND PAS SOUS DEUX PAS : deux bandes qui
    // bondissent à une trame d'écart sur LA MÊME frappe en feraient deux.
    const auto ecartMinimal = std::max<int64_t>(2 * pas,
                                                static_cast<int64_t>(std::llround(minGapSeconds * sampleRate)));
    const auto marge = static_cast<int64_t>(std::llround(preAttackSeconds * sampleRate));
    const double plancher = std::pow(10.0, floorDb / 20.0);
    // Une bande peut être bien plus faible que le tout et porter l'attaque
    // quand même (le charleston sur la queue d'une grosse caisse) : son
    // plancher est dix décibels plus bas que celui du tout.
    const double plancherBande = std::pow(10.0, (floorDb - 10.0) / 20.0);
    const double saut = std::pow(10.0, sensitivityDb / 20.0);

    // TROIS BANDES, ET LE TOUT. Mesuré sur un stem de TR-909 du banc : les
    // frappes de charleston ne font monter l'énergie TOTALE que de 0,8 à
    // 2,4 dB, parce qu'une grosse caisse et un charleston ouvert traînent à
    // -20 dB pendant toute la mesure ; dans la bande haute, la même frappe
    // bondit de vingt décibels. Le flux se calcule donc PAR BANDE, chacune
    // avec sa propre moyenne de ce qui précède, et une attaque est un bond
    // dans l'une d'elles.
    constexpr int kBandes = 4;   // 0 = tout, 1 = grave (< 200 Hz), 2 = médium, 3 = aigu (> 2 kHz)
    Biquad grave = Biquad::passeBas(200.0, sampleRate);
    Biquad coupeGrave = Biquad::passeHaut(200.0, sampleRate);
    Biquad aigu = Biquad::passeHaut(2000.0, sampleRate);
    Biquad coupeAigu = Biquad::passeBas(2000.0, sampleRate);

    // L'ÉNERGIE DE CHAQUE TRAME, PAR BANDE, en une seule passe sur les
    // échantillons : chaque échantillon compte dans la trame qui le contient
    // et dans la précédente, que la fenêtre chevauche.
    const auto nombreTrames = static_cast<size_t>((frames + pas - 1) / pas);
    std::vector<std::array<double, kBandes>> energie(nombreTrames, std::array<double, kBandes>{});
    for (int64_t i = 0; i < frames; ++i) {
        float g = 0.0f, d = 0.0f;
        if (!frameAt(i, g, d)) break;
        const double v = 0.5 * (static_cast<double>(g) + static_cast<double>(d));
        const double bas = grave(v);
        const double haut = aigu(v);
        const double milieu = coupeAigu(coupeGrave(v));
        const std::array<double, kBandes> carre{v * v, bas * bas, milieu * milieu, haut * haut};
        const auto t = static_cast<size_t>(i / pas);
        for (int b = 0; b < kBandes; ++b) energie[t][static_cast<size_t>(b)] += carre[static_cast<size_t>(b)];
        if (t > 0 && i - static_cast<int64_t>(t - 1) * pas < fenetre)
            for (int b = 0; b < kBandes; ++b) energie[t - 1][static_cast<size_t>(b)] += carre[static_cast<size_t>(b)];
    }
    std::vector<std::array<double, kBandes>> rms(nombreTrames);
    for (size_t t = 0; t < nombreTrames; ++t) {
        const auto debut = static_cast<int64_t>(t) * pas;
        const double n = static_cast<double>(std::max<int64_t>(1, std::min(frames, debut + fenetre) - debut));
        for (int b = 0; b < kBandes; ++b)
            rms[t][static_cast<size_t>(b)] = std::sqrt(energie[t][static_cast<size_t>(b)] / n);
    }

    // L'INSTANT PRÉCIS D'UNE ATTAQUE : dans la trame qui a bondi (et le pas
    // qui la précède), l'échantillon où l'énergie de la milliseconde qui SUIT
    // dépasse le plus celle des CINQ millisecondes qui précèdent -- la montée
    // la plus raide. Cinq millisecondes avant, et non une : sur la queue d'une
    // grosse caisse à 50 Hz, une milliseconde tombe tantôt sur un ventre,
    // tantôt sur un nœud, et le rapport culminait aux passages par zéro de la
    // queue plutôt qu'à l'attaque (mesuré : 5 à 11 ms d'avance sur les huit
    // frappes vraies).
    const auto ms = std::max<int64_t>(4, static_cast<int64_t>(std::llround(sampleRate * 0.001)));
    // ET DANS LA BANDE QUI A BONDI, pas dans le tout. Mesuré sur le même stem :
    // à 0,492 s, une caisse claire et un charleston tombent sur la queue de la
    // grosse caisse, et l'énergie TOTALE par milliseconde reste plate à -18 dB
    // -- rien à voir, sinon les nœuds de la queue toutes les 8 ms, sur lesquels
    // la montée « la plus raide » se posait, huit millisecondes trop tard. La
    // bande haute, elle, voit la frappe. Les échantillons de la bande sont
    // refiltrés localement, avec cinquante millisecondes d'amorce pour que le
    // filtre soit établi : on ne garde pas la bande de tout le clip en mémoire.
    auto monteeLaPlusRaide = [&](int64_t trame, int bande) {
        // UNE FENÊTRE EN ARRIÈRE AUSSI : le bond peut n'apparaître qu'à la
        // trame suivante quand la bande qui le porte est encore couverte par
        // ce qui précède, et l'attaque est alors AVANT la trame qui a bondi.
        const int64_t de = std::max<int64_t>(5 * ms, trame - fenetre);
        const int64_t a = std::min(frames - ms, trame + fenetre);
        if (a <= de) return trame;
        const int64_t amorce = static_cast<int64_t>(std::llround(sampleRate * 0.050));
        const int64_t origine = std::max<int64_t>(0, de - 5 * ms - amorce);
        Biquad f1 = bande == 1 ? Biquad::passeBas(200.0, sampleRate)
                  : bande == 2 ? Biquad::passeHaut(200.0, sampleRate)
                  : bande == 3 ? Biquad::passeHaut(2000.0, sampleRate) : Biquad{};
        Biquad f2 = bande == 2 ? Biquad::passeBas(2000.0, sampleRate) : Biquad{};
        std::vector<double> carre;
        carre.reserve(static_cast<size_t>(a + ms - origine + 1));
        for (int64_t k = origine; k < a + ms; ++k) {
            float g = 0.0f, d = 0.0f;
            if (!frameAt(k, g, d)) break;
            double v = 0.5 * (static_cast<double>(g) + static_cast<double>(d));
            if (bande != 0) v = f1(v);
            if (bande == 2) v = f2(v);
            carre.push_back(v * v);
        }
        auto energieDe = [&](int64_t debut, int64_t longueur) {
            double somme = 0.0;
            int64_t n = 0;
            for (int64_t k = debut; k < debut + longueur; ++k) {
                const auto index = static_cast<size_t>(k - origine);
                if (k < origine || index >= carre.size()) break;
                somme += carre[index];
                ++n;
            }
            return n > 0 ? somme / static_cast<double>(n) : 0.0;
        };
        int64_t meilleur = trame;
        double meilleurRapport = -1.0;
        for (int64_t i = de; i < a; ++i) {
            const double avant = energieDe(i - 5 * ms, 5 * ms);
            const double apres = energieDe(i, ms);
            const double rapport = apres / (avant + 1e-12);
            if (rapport > meilleurRapport) { meilleurRapport = rapport; meilleur = i; }
        }
        return meilleur;
    };

    int64_t derniere = -ecartMinimal - 1;
    std::array<double, kBandes> sommeContexte{};
    int64_t dansContexte = 0;
    for (size_t t = 0; t < nombreTrames; ++t) {
        const int64_t trame = static_cast<int64_t>(t) * pas;
        // LA BANDE QUI BONDIT LE PLUS désigne l'attaque, et c'est dans elle
        // que l'instant se cherche ensuite.
        int bande = -1;
        double plusGrandBond = 0.0;
        if (rms[t][0] >= plancher) {
            for (int b = 0; b < kBandes; ++b) {
                const double valeur = rms[t][static_cast<size_t>(b)];
                const double moyenne = dansContexte > 0 ? sommeContexte[static_cast<size_t>(b)] / static_cast<double>(dansContexte) : 0.0;
                const double plancherIci = b == 0 ? plancher : plancherBande;
                if (valeur < plancherIci || valeur < moyenne * saut) continue;
                const double rapport = valeur / (moyenne + 1e-12);
                if (rapport > plusGrandBond) { plusGrandBond = rapport; bande = b; }
            }
        }
        if (bande >= 0 && trame - derniere >= ecartMinimal) {
            const int64_t instant = std::max<int64_t>(0, monteeLaPlusRaide(trame, bande) - marge);
            // Une attaque au tout début est le début du clip : elle ne coupe
            // rien, on ne la rend pas -- ni à zéro, ni à dix millisecondes, ce
            // qui ferait un clip de dix millisecondes. La première coupe est
            // au moins à l'écart minimal du début.
            if (instant >= ecartMinimal && (attaques.empty() || instant - attaques.back() >= ecartMinimal))
                attaques.push_back(instant);
            derniere = trame;
        }
        for (int b = 0; b < kBandes; ++b) sommeContexte[static_cast<size_t>(b)] += rms[t][static_cast<size_t>(b)];
        ++dansContexte;
        if (dansContexte > contexte) {
            for (int b = 0; b < kBandes; ++b)
                sommeContexte[static_cast<size_t>(b)] -= rms[t - static_cast<size_t>(contexte)][static_cast<size_t>(b)];
            --dansContexte;
        }
    }
    return attaques;
}

} // namespace vsm::audio::io
