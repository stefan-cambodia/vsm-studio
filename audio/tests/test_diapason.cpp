#include "TestFramework.h"
#include "vsm/audio/dsp/RealFft.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/Diapason.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// H42 (docs/CDC-reload-indifferenciable.md § 4) : LE DIAPASON DU PROJET, MESURÉ
// MACHINE PAR MACHINE SUR LE RENDU.
//
// CE QUE CE BANC GARDE. Toute machine mélodique du registre, jouée à un la4 de
// 446 Hz, doit sortir 446/440 fois plus haut qu'à 440 — à 2 cents près — et les
// machines qui NE SUIVENT PAS (boîtes à rythmes, sampler, guimbarde) doivent
// rendre AU BIT PRÈS la même chose aux deux diapasons. Une machine ajoutée plus
// tard est MÉLODIQUE PAR DÉFAUT : si elle oublie le diapason, ce test la nomme.
//
// LA MESURE EST UN RAPPORT. La hauteur absolue d'un timbre inharmonique ne se lit
// pas par autocorrélation (l'e-piano se lit −52 cents à toutes les notes, D26) ;
// mais une machine qui suit le diapason décale TOUT son spectre du même facteur,
// et le biais de la mesure s'annule dans le rapport des deux rendus.

namespace {

constexpr double kSampleRate = 48000.0;
constexpr float kEssai = 446.0f;

/// Remet le diapason à 440 quoi qu'il arrive : les autres tests supposent le défaut.
struct DiapasonDEssai {
    explicit DiapasonDEssai(float hz) { setDiapason(hz); }
    ~DiapasonDEssai() { setDiapason(440.0f); }
};

/// Les machines qui ne suivent pas le diapason, et pourquoi (Diapason.h).
const std::set<std::string>& quiNeSuiventPas() {
    static const std::set<std::string> ids = {
        "vsm.drums", "vsm.tr808", "vsm.tr909", "vsm.fmdrums", "vsm.perc",   // pièces de batterie
        "vsm.sampler",                                                      // une note choisit une pièce
        "vsm.jewsharp",                                                     // une lame à fréquence fixe
    };
    return ids;
}

/// Le multi-échantillons ne joue rien sans profil chargé : il a son propre cas,
/// dans `test_multisample_synth.cpp`, avec un profil engendré.
bool aSonPropreCas(const std::string& id) {
    // `test.*` : les doublures des tests du registre (`test.dummy` est muette par
    // construction) — ce ne sont pas des machines du parc.
    return id == "vsm.multisample" || id.rfind("test.", 0) == 0;
}

/// Rend une note tenue (canal gauche) avec une machine NEUVE, au diapason courant.
std::vector<float> rendu(const std::string& id, uint8_t note, std::vector<float>* droite = nullptr) {
    auto plugin = PluginRegistry::instance().create(id);
    if (!plugin) return {};
    plugin->initialize(kSampleRate, 512);
    std::vector<float> g(512), d(512), sortie;
    for (int b = 0; b < 90; ++b) {
        std::fill(g.begin(), g.end(), 0.0f);
        std::fill(d.begin(), d.end(), 0.0f);
        if (b == 0) {
            const MidiNoteEvent on{MidiNoteEvent::Kind::NoteOn, 0, 0, note, 100};
            plugin->process(&on, 1, g.data(), d.data(), 512);
        } else {
            plugin->process(nullptr, 0, g.data(), d.data(), 512);
        }
        sortie.insert(sortie.end(), g.begin(), g.end());
        if (droite != nullptr) droite->insert(droite->end(), d.begin(), d.end());
    }
    return sortie;
}

double efficace(const std::vector<float>& y, size_t depuis) {
    double s = 0.0;
    for (size_t i = depuis; i < y.size(); ++i) s += static_cast<double>(y[i]) * y[i];
    return y.size() > depuis ? std::sqrt(s / static_cast<double>(y.size() - depuis)) : 0.0;
}

/// La période par autocorrélation, cherchée à ± 4 demi-tons de la hauteur attendue
/// (la borne du banc de la molette, D26), interpolée au sommet.
double periodeAutourDe(const std::vector<float>& y, size_t depuis, double hzAttendu) {
    const size_t n = std::min<size_t>(y.size() - depuis, static_cast<size_t>(0.8 * kSampleRate));
    std::vector<double> x(n);
    double moy = 0.0;
    for (size_t i = 0; i < n; ++i) { x[i] = y[depuis + i]; moy += x[i]; }
    moy /= static_cast<double>(n);
    for (auto& v : x) v -= moy;
    const double f = std::pow(2.0, 4.0 / 12.0);
    const size_t lo = std::max<size_t>(2, static_cast<size_t>(kSampleRate / (hzAttendu * f)));
    const size_t hi = std::min(n / 2, static_cast<size_t>(kSampleRate / (hzAttendu / f)));
    std::vector<double> ac(hi + 2, 0.0);
    double record = -1e300;
    size_t meilleur = lo;
    for (size_t k = lo - 1; k <= hi + 1; ++k) {
        double s = 0.0;
        for (size_t i = 0; i + k < n; ++i) s += x[i] * x[i + k];
        ac[k] = s;
        if (k >= lo && k <= hi && s > record) { record = s; meilleur = k; }
    }
    double k = static_cast<double>(meilleur);
    const double a = ac[meilleur - 1], b = ac[meilleur], c = ac[meilleur + 1];
    if (a - 2.0 * b + c != 0.0) k += 0.5 * (a - c) / (a - 2.0 * b + c);
    return k;
}

/// LE DÉCALAGE DU SPECTRE ENTIER, en cents : corrélation des deux spectres sur un
/// axe de fréquence LOGARITHMIQUE (pas de 0,5 cent, de 100 Hz à 6 kHz).
///
/// POURQUOI UNE SECONDE MESURE. L'autocorrélation lit une PÉRIODE ; un carillon,
/// une table vectorielle qui se déforme ou une cornemuse et ses bourdons n'en ont
/// pas une seule, et elle y a lu 233, 192 et 211 Hz pour une note de 220 (mesuré
/// à l'écriture de ce test) — le rapport de deux telles lectures ne dit rien. Le
/// spectre, lui, se décale EN BLOC quand la machine suit le diapason, partiels
/// inharmoniques compris ; et une composante qui ne suivrait pas (un formant
/// fixe, un bruit) tirerait le décalage vers zéro. Une machine n'est fautive que
/// si les DEUX mesures la condamnent.
double decalageSpectralCents(const std::vector<float>& a, const std::vector<float>& b) {
    constexpr size_t kN = 32768;
    const size_t depuis = static_cast<size_t>(0.05 * kSampleRate);
    if (a.size() < depuis + kN || b.size() < depuis + kN) return 0.0;
    auto fft = std::make_unique<vsm::audio::dsp::RealIfft<kN>>();
    const auto spectre = [&](const std::vector<float>& y) {
        std::vector<float> x(kN), re(kN / 2 + 1), im(kN / 2 + 1);
        for (size_t i = 0; i < kN; ++i) {
            const double w = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * static_cast<double>(i) / (kN - 1));
            x[i] = static_cast<float>(y[depuis + i] * w);
        }
        fft->forward(x.data(), re.data(), im.data());
        std::vector<double> m(kN / 2 + 1);
        // racine de l'amplitude : le partiel le plus fort ne décide pas seul
        for (size_t k = 0; k < m.size(); ++k) m[k] = std::pow(std::hypot(re[k], im[k]), 0.5);
        return m;
    };
    const auto ma = spectre(a), mb = spectre(b);
    const double df = kSampleRate / static_cast<double>(kN);
    const double pas = 0.5;   // cents
    const size_t n = static_cast<size_t>(1200.0 * std::log2(6000.0 / 100.0) / pas);
    const auto grille = [&](const std::vector<double>& m) {
        std::vector<double> g(n);
        for (size_t i = 0; i < n; ++i) {
            const double pos = 100.0 * std::pow(2.0, static_cast<double>(i) * pas / 1200.0) / df;
            const size_t k = static_cast<size_t>(pos);
            const double t = pos - static_cast<double>(k);
            g[i] = m[k] * (1.0 - t) + m[k + 1] * t;
        }
        return g;
    };
    const auto ga = grille(ma), gb = grille(mb);
    const int lim = 120;   // ± 60 cents
    std::vector<double> c(2 * lim + 1, 0.0);
    for (int l = -lim; l <= lim; ++l) {
        double s = 0.0;
        for (size_t i = static_cast<size_t>(lim); i + static_cast<size_t>(lim) < n; ++i)
            s += ga[i] * gb[static_cast<size_t>(static_cast<long>(i) + l)];
        c[static_cast<size_t>(l + lim)] = s;
    }
    const size_t m = static_cast<size_t>(std::max_element(c.begin(), c.end()) - c.begin());
    double lag = static_cast<double>(m) - lim;
    if (m > 0 && m + 1 < c.size()) {
        const double x0 = c[m - 1], x1 = c[m], x2 = c[m + 1];
        if (x0 - 2.0 * x1 + x2 != 0.0) lag += 0.5 * (x0 - x2) / (x0 - 2.0 * x1 + x2);
    }
    return lag * pas;
}

/// LA PUCE SONORE QUANTIFIE SA HAUTEUR PAR SON HORLOGE (`vsm.psg`, fidèle au
/// SN76489) : `horloge / (16 · round(horloge / (16 · f)))`. Son écart attendu est
/// donc celui de la puce, pas le rapport exact — mesuré à 220,19 et 222,90 Hz pour
/// 220,22 et 222,83 prédits.
double centsAttendusPsg(double f440, double f446) {
    const double horloge = 1789773.0;
    const auto q = [&](double f) { return horloge / (16.0 * std::round(horloge / (16.0 * f))); };
    return 1200.0 * std::log2(q(f446) / q(f440));
}

} // namespace

VSM_TEST(diapason_par_defaut_est_440_et_se_borne) {
    VSM_ASSERT(diapason() == 440.0f);
    {
        DiapasonDEssai essai(std::nanf(""));
        VSM_ASSERT(diapason() == 440.0f);   // une valeur non finie rend le défaut
    }
    {
        DiapasonDEssai essai(-3.0f);
        VSM_ASSERT(diapason() == 440.0f);
    }
    {
        DiapasonDEssai essai(442.5f);
        VSM_ASSERT(diapason() == 442.5f);
    }
    VSM_ASSERT(diapason() == 440.0f);   // le garde a remis le défaut
}

// LE CRITÈRE : chaque machine mélodique suit le diapason à 2 cents près ; chaque
// machine qui ne le suit pas rend au bit près. Les fautives sont NOMMÉES — toutes,
// pas seulement la première.
VSM_TEST(diapason_chaque_machine_melodique_le_suit_les_autres_rendent_au_bit) {
    registerBuiltInPlugins();
    const uint8_t note = 57;   // la3, 220 Hz : le registre confortable de tout le parc
    const double attendu = 1200.0 * std::log2(static_cast<double>(kEssai) / 440.0);   // +23,45 cents
    std::vector<std::string> fautives;
    int melodiques = 0, fixes = 0;
    for (const auto& [id, nom] : PluginRegistry::instance().listAvailable()) {
        if (aSonPropreCas(id)) continue;
        const bool fixe = quiNeSuiventPas().count(id) > 0;
        std::vector<float> d440, d446;
        std::vector<float> a440, a446;
        {
            DiapasonDEssai la(440.0f);
            a440 = rendu(id, fixe ? 38 : note, &d440);
        }
        {
            DiapasonDEssai la(kEssai);
            a446 = rendu(id, fixe ? 38 : note, &d446);
        }
        if (fixe) {
            ++fixes;
            if (a440 != a446 || d440 != d446) fautives.push_back(id + " (ne doit pas suivre, et son rendu a changé)");
            continue;
        }
        ++melodiques;
        const size_t depuis = static_cast<size_t>(0.05 * kSampleRate);
        if (efficace(a440, depuis) < 1e-5) {
            // UNE MESURE QUI NE VOIT RIEN LE DIT : une machine muette n'a pas « suivi ».
            fautives.push_back(id + " (MUETTE à la note 57 — non mesurée)");
            continue;
        }
        const double p440 = periodeAutourDe(a440, depuis, 220.0);
        const double p446 = periodeAutourDe(a446, depuis, 220.0 * kEssai / 440.0);
        const double cents = 1200.0 * std::log2(p440 / p446);
        const double viser = id == "vsm.psg" ? centsAttendusPsg(220.0, 220.0 * kEssai / 440.0) : attendu;
        if (std::abs(cents - viser) <= 2.0) continue;
        const double spectral = decalageSpectralCents(a440, a446);
        if (std::abs(spectral - viser) <= 2.0) {
            std::fprintf(stderr, "    DIAPASON : %s suit — mesure spectrale %+.2f cents (l'autocorrélation lisait %+.2f)\n",
                         id.c_str(), spectral, cents);
            continue;
        }
        {
            char ligne[220];
            // La hauteur ABSOLUE des deux rendus, pour trancher entre la machine et la
            // mesure : une machine qui suit sort 220 et 223 Hz, à son biais près.
            std::snprintf(ligne, sizeof ligne, "%s (%+.2f cents par période, %+.2f par spectre, au lieu de %+.2f ; lu %.3f Hz à 440, %.3f Hz à 446)",
                          id.c_str(), cents, spectral, viser, kSampleRate / p440, kSampleRate / p446);
            fautives.push_back(ligne);
        }
    }
    for (const auto& f : fautives) std::fprintf(stderr, "    DIAPASON : %s\n", f.c_str());
    std::fprintf(stderr, "    DIAPASON : %d machines mélodiques, %d qui ne suivent pas, %zu fautive(s)\n",
                 melodiques, fixes, fautives.size());
    VSM_ASSERT(melodiques > 50);   // le registre a bien été parcouru
    VSM_ASSERT(fautives.empty());
}

// LA MESURE SPECTRALE SE VÉRIFIE AVANT DE SERVIR D'ARBITRE : sur une machine dont
// la hauteur se lit sans ambiguïté (le Minimoog), elle doit lire le décalage exact,
// et zéro contre elle-même. Une mesure qui innocenterait tout rendrait le test vert
// pour rien.
VSM_TEST(diapason_la_mesure_spectrale_lit_juste_sur_une_machine_franche) {
    registerBuiltInPlugins();
    std::vector<float> a, b;
    {
        DiapasonDEssai la(440.0f);
        a = rendu("vsm.minimoog", 57);
    }
    {
        DiapasonDEssai la(kEssai);
        b = rendu("vsm.minimoog", 57);
    }
    VSM_ASSERT_NEAR(decalageSpectralCents(a, a), 0.0, 0.01);
    VSM_ASSERT_NEAR(decalageSpectralCents(a, b), 1200.0 * std::log2(static_cast<double>(kEssai) / 440.0), 1.0);
}
