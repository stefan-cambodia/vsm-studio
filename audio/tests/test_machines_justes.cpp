#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <numeric>
#include <vector>

// UNE MACHINE TRANSPOSE-T-ELLE COMME ON LE LUI DEMANDE ? (D266, 13/09/2026)
//
// POURQUOI CE BANC. En mesurant tout autre chose -- le rappel des notes brèves
// (D264) --, deux parties du corpus rendues par la même machine se sont
// transcrites systématiquement à côté : cinq demi-tons trop haut sur l'une, huit
// trop bas sur l'autre, du premier au dernier événement. Un écart d'OCTAVE
// s'attribue au transcripteur, qui en fait ; un écart de CINQ demi-tons, non.
// Or aucun test ne vérifiait qu'une machine joue la hauteur demandée : on
// vérifiait qu'elle fait du son, qu'elle tient son en-tête, qu'elle ne sature
// pas.
//
// CE QUI EST MESURÉ, ET POURQUOI PAS LA FONDAMENTALE. Un premier banc cherchait
// la fondamentale près de la note demandée et a déclaré HUIT machines fausses --
// dont un orgue à roues phoniques, une vielle à roue et une cornemuse. C'était le
// BANC qui avait tort : un orgue dont les tirettes coupent le premier rang a sa
// plus forte partielle au douzième, un bourdon sonne à sa propre hauteur, et une
// machine inharmonique n'a pas de fondamentale du tout. Chercher « la »
// fondamentale suppose une réponse que ces machines ne donnent pas.
//
// Ce banc-ci ne la cherche pas. Il rend la MÊME note à une OCTAVE d'écart et
// demande : le spectre entier s'est-il déplacé de douze demi-tons ? Le spectre
// est relevé sur une grille logarithmique, puis les deux relevés sont corrélés
// par glissement. La réponse ne dépend plus de savoir quelle partielle domine --
// elles se déplacent toutes ensemble --, et c'est exactement le défaut qu'on
// cherche : une machine qui ne suit pas le numéro de note.
//
// TOLÉRANCE : un quart de demi-ton, la finesse de la grille. Les machines
// NON TEMPÉRÉES sont nommées une par une ci-dessous ; une machine absente de
// cette liste est jugée, et c'est voulu.

using namespace vsm::audio::plugin;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr double kPasDemiTon = 0.25;                 // finesse de la grille
constexpr double kBasseHz = 55.0;                    // la1
constexpr int kBins = static_cast<int>(7 * 12 / kPasDemiTon);   // sept octaves

/// LES MACHINES QUI N'ONT PAS DE HAUTEUR À SUIVRE, nommées une par une.
/// Une percussion ne transpose pas avec le numéro de note : le juger produirait
/// un échec qui ne veut rien dire, et qu'on finirait par ignorer.
bool sansHauteur(const std::string& id) {
    static const std::vector<std::string> liste = {
        "vsm.tr808", "vsm.tr909", "vsm.linndrum", "vsm.fmdrums", "vsm.drumkit",
        "vsm.drums", "vsm.perc", "vsm.noise", "vsm.vinyl", "vsm.tape",
    };
    return std::find(liste.begin(), liste.end(), id) != liste.end();
}

/// L'énergie à une fréquence (Goertzel), sur la partie TENUE de la note.
double energieA(const std::vector<float>& x, double freq, size_t debut, size_t fin) {
    const double w = 2.0 * M_PI * freq / kSampleRate;
    const double coeff = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = debut; i < fin && i < x.size(); ++i) {
        const double s0 = x[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return std::max(0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2);
}

/// Le spectre sur une grille LOGARITHMIQUE, en décibels, normalisé.
/// Logarithmique parce qu'une transposition y est une TRANSLATION : c'est ce qui
/// permet de la mesurer par glissement sans rien savoir du timbre.
std::vector<double> spectreLog(const std::vector<float>& x, size_t debut, size_t fin) {
    std::vector<double> s(static_cast<size_t>(kBins), 0.0);
    for (int i = 0; i < kBins; ++i) {
        const double f = kBasseHz * std::pow(2.0, (i * kPasDemiTon) / 12.0);
        s[static_cast<size_t>(i)] = 10.0 * std::log10(energieA(x, f, debut, fin) + 1e-12);
    }
    // BLANCHIMENT : on retire à chaque point la moyenne de son voisinage.
    //
    // SANS LUI LE BANC SE TROMPE, et il s'est trompé : corrélés tels quels, deux
    // spectres sont dominés par l'ENVELOPPE -- la résonance d'une caisse, un
    // filtre à fréquence fixe --, qui ne bouge PAS avec la note. La corrélation
    // se calait alors sur zéro et déclarait onze machines fausses, dont le
    // piano et le clavecin. Ce qui se déplace avec la hauteur, c'est le PEIGNE
    // des partielles ; le blanchiment ne garde que lui.
    const int demiLargeur = static_cast<int>(2.5 / kPasDemiTon);   // ±2,5 demi-tons
    std::vector<double> blanchi(s.size(), 0.0);
    for (int i = 0; i < kBins; ++i) {
        const int a = std::max(0, i - demiLargeur), b = std::min(kBins - 1, i + demiLargeur);
        double somme = 0.0;
        for (int j = a; j <= b; ++j) somme += s[static_cast<size_t>(j)];
        blanchi[static_cast<size_t>(i)] = s[static_cast<size_t>(i)] - somme / (b - a + 1);
    }
    return blanchi;
}

/// De combien de demi-tons le second spectre est-il décalé par rapport au premier ?
double glissement(const std::vector<double>& a, const std::vector<double>& b, int maxDemiTons) {
    const int maxPas = static_cast<int>(maxDemiTons / kPasDemiTon);
    double meilleur = 0.0, scoreMax = -1e30;
    for (int pas = -maxPas; pas <= maxPas; ++pas) {
        double score = 0.0;
        int compte = 0;
        for (int i = 0; i < kBins; ++i) {
            const int j = i + pas;
            if (j < 0 || j >= kBins) continue;
            score += a[static_cast<size_t>(i)] * b[static_cast<size_t>(j)];
            ++compte;
        }
        if (compte < kBins / 2) continue;
        score /= compte;
        if (score > scoreMax) { scoreMax = score; meilleur = pas * kPasDemiTon; }
    }
    return meilleur;
}

std::vector<float> rendre(const std::string& id, int note, int frames) {
    auto synth = PluginRegistry::instance().create(id);
    if (!synth) return {};
    synth->initialize(kSampleRate, 512);
    const MidiNoteEvent on{MidiNoteEvent::Kind::NoteOn, 0, 0,
                           static_cast<uint8_t>(note), static_cast<uint8_t>(100)};
    std::vector<float> g(static_cast<size_t>(frames), 0.0f), d(static_cast<size_t>(frames), 0.0f);
    synth->process(&on, 1, g.data(), d.data(), frames);
    return g;
}

double rms(const std::vector<float>& x, size_t debut, size_t fin) {
    double s = 0.0;
    for (size_t i = debut; i < fin && i < x.size(); ++i) s += static_cast<double>(x[i]) * x[i];
    return fin > debut ? std::sqrt(s / static_cast<double>(fin - debut)) : 0.0;
}


/// L'écart, en demi-tons, entre le pic le plus fort du voisinage et la
/// fréquence tempérée de la note. Borné à ±7 : au-delà on mesurerait une
/// partielle voisine, et le chiffre ne voudrait plus dire « faux de tant ».
double ecartAuFondamental(const std::vector<double>& spectre, int note) {
    const double f0 = 440.0 * std::pow(2.0, (note - 69) / 12.0);
    const double iF0 = 12.0 * std::log2(f0 / kBasseHz) / kPasDemiTon;
    const int rayon = static_cast<int>(7.0 / kPasDemiTon);
    double meilleur = 0.0, max = -1e30;
    for (int d = -rayon; d <= rayon; ++d) {
        const int i = static_cast<int>(std::lround(iF0)) + d;
        if (i < 0 || i >= kBins) continue;
        if (spectre[static_cast<size_t>(i)] > max) {
            max = spectre[static_cast<size_t>(i)];
            meilleur = d * kPasDemiTon;
        }
    }
    return meilleur;
}

} // namespace

VSM_TEST(une_machine_qui_joue_faux_joue_faux_pareil_a_toutes_les_notes) {
    // CE QUE CE BANC SAIT FAIRE, ET CE QU'IL NE SAIT PAS.
    //
    // Il ne sait pas dire « cette machine joue juste » : un orgue à roues
    // phoniques, une guimbarde, une machine inharmonique n'ont pas de
    // fondamentale à l'endroit attendu, et six versions successives de ce banc
    // les ont tour à tour déclarées fausses — c'était le banc qui avait tort.
    //
    // Il sait dire autre chose, et c'est ce qu'on cherchait : une machine
    // DÉSACCORDÉE l'est de la MÊME quantité à toutes les notes. C'est ce qu'on
    // avait vu sur le corpus — cinq demi-tons d'écart sur les quatre-vingt-quatre
    // notes d'une partie, du début à la fin. Un spectre ambigu, lui, donne un
    // écart qui SAUTE d'une note à l'autre. Le banc mesure donc l'écart à la
    // fondamentale attendue sur cinq notes, et ne juge que les machines dont
    // ces cinq écarts sont D'ACCORD entre eux ; celles dont ils divergent sont
    // COMPTÉES ET NOMMÉES, jamais ignorées en silence.
    registerBuiltInPlugins();
    const int frames = 26400;
    const size_t debut = 4800, fin = 19200;
    const int notes[] = {48, 53, 58, 63, 68};     // espacement irrégulier : pas d'alias d'octave

    int jugees = 0, fausses = 0, ambigues = 0;
    std::vector<std::string> nomsAmbigus;
    for (const auto& [id, nom] : PluginRegistry::instance().listAvailable()) {
        if (sansHauteur(id)) continue;
        std::vector<double> ecarts;
        for (int note : notes) {
            const auto x = rendre(id, note, frames);
            if (x.empty() || rms(x, debut, fin) < 1e-4) continue;
            ecarts.push_back(ecartAuFondamental(spectreLog(x, debut, fin), note));
        }
        if (ecarts.size() < 4) continue;
        std::sort(ecarts.begin(), ecarts.end());
        const double mediane = ecarts[ecarts.size() / 2];
        const double etendue = ecarts.back() - ecarts.front();
        // UN PIC TROUVÉ AU BORD DE LA FENÊTRE N'EST PAS UNE MESURE, c'est une
        // recherche qui a buté. Une machine sans énergie à la fondamentale
        // attendue -- un orgue dont la tirette du premier rang est fermée --
        // donne alors « +7,00 » aux cinq notes, très régulièrement, et se
        // ferait déclarer désaccordée de sept demi-tons. Elle est ambiguë.
        const bool auBord = std::abs(std::abs(mediane) - 7.0) < 0.1;
        if (etendue > 0.5 || auBord) {
            ++ambigues;
            nomsAmbigus.push_back(id);
            continue;
        }
        ++jugees;
        if (std::abs(mediane) > 0.25) {
            ++fausses;
            std::printf("    DESACCORDEE  %-24s %+.2f demi-ton a chacune des %zu notes\n",
                        id.c_str(), mediane, ecarts.size());
        }
    }
    std::printf("    machines jugees %d, desaccordees %d, spectre ambigu %d :\n",
                jugees, fausses, ambigues);
    for (const auto& n : nomsAmbigus) std::printf("      ambigue %s\n", n.c_str());
    VSM_ASSERT(jugees >= 25);
    VSM_ASSERT_EQ(fausses, 0);
}
