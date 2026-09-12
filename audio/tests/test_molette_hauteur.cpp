#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <cstdio>
#include <vector>

using namespace vsm::audio::plugin;

// LA MOLETTE DE HAUTEUR, MESURÉE PLUTÔT QUE DÉCLARÉE (A3 de docs/INDEX.md,
// phase D26 de ROADMAP-daw.md).
//
// CE QUE CE BANC GARDE, ET POURQUOI IL EXISTE. Les tests de machine se
// contentaient de vérifier que `handleControlEvent` REND VRAI — par exemple
// `banjo_honours_pitch_bend`, qui envoie un pli et n'écoute pas le son. Une
// machine peut accepter l'événement, le ranger dans un membre et ne jamais s'en
// servir : le test passerait, et la note ne se plierait pas. Le critère de D26
// est écrit autrement, et c'est lui qui est mesuré ici :
//
//     « une note pliée d'un demi-ton sort à la fréquence de la note du dessus,
//       à 1 % près, mesurée sur le rendu ».
//
// LA MESURE EST UNE AUTOCORRÉLATION et non une transformée : au quart de ton
// près, une FFT de trame courte ne tranche pas, et c'est de justesse qu'il
// s'agit. Le même estimateur sert au banc de la flûte.
//
// LE TÉMOIN FAIT PARTIE DE LA MESURE : chaque machine est d'abord rendue SANS
// pli, et sa hauteur doit tomber sur la note demandée. Sans lui, une machine qui
// joue faux des deux côtés passerait pour une machine qui plie.

namespace {

constexpr double kSampleRate = 48000.0;

SynthPluginPtr machine(const std::string& identifiant, double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create(identifiant);
    if (plugin) plugin->initialize(sr, 512);
    return plugin;
}

MidiNoteEvent noteOn(int offset, uint8_t note, uint8_t velocity = 100) {
    return {MidiNoteEvent::Kind::NoteOn, offset, 0, note, velocity};
}

double hzDeLaNote(int note) {
    return 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0);
}

/// Fréquence fondamentale par AUTOCORRÉLATION, reprise du banc de la flûte —
/// mais CHERCHÉE AUTOUR DE LA HAUTEUR ATTENDUE, à quatre demi-tons près.
///
/// POURQUOI CETTE BORNE, ET POURQUOI ELLE NE CACHE RIEN. Cherchée sur tout le
/// registre, l'autocorrélation d'un timbre INHARMONIQUE se fait prendre : sur
/// l'e-piano, dont les partiels tombent à 4,75 et 10,6 fois le fondamental, elle
/// lisait -52 cents constants aux notes graves et **deux octaves** trop bas aux
/// aiguës (note 64 : 82 Hz pour 330 attendus). Ce n'est pas la machine qui joue
/// faux, c'est la mesure qui s'accroche à un sous-multiple.
///
/// Quatre demi-tons, c'est assez large pour que la faute qu'on traque — une
/// machine qui IGNORE un pli d'un demi-ton, donc qui sort un demi-ton trop bas —
/// tombe dans la fenêtre et fasse échouer l'assertion. La borne écarte les
/// erreurs d'octave, pas les erreurs de justesse.
double frequenceFondamentale(const std::vector<float>& y, size_t depuis, double hzAttendu) {
    const size_t n = std::min<size_t>(y.size() - depuis, static_cast<size_t>(0.8 * kSampleRate));
    if (n < 4096) return 0.0;
    std::vector<double> x(n);
    double moy = 0.0;
    for (size_t i = 0; i < n; ++i) { x[i] = y[depuis + i]; moy += x[i]; }
    moy /= static_cast<double>(n);
    for (auto& v : x) v -= moy;

    const double facteur = std::pow(2.0, 4.0 / 12.0);
    const size_t lo = std::max<size_t>(2, static_cast<size_t>(kSampleRate / (hzAttendu * facteur)));
    const size_t hi = std::min(n / 2, static_cast<size_t>(kSampleRate / (hzAttendu / facteur)));
    double record = -1e30; size_t meilleur = lo;
    std::vector<double> ac(hi + 2, 0.0);
    for (size_t k = lo; k <= hi + 1; ++k) {
        double s = 0.0;
        for (size_t i = 0; i + k < n; ++i) s += x[i] * x[i + k];
        ac[k] = s;
        if (k <= hi && s > record) { record = s; meilleur = k; }
    }
    double k = static_cast<double>(meilleur);
    if (meilleur > lo && meilleur < hi) {
        const double a = ac[meilleur - 1], b = ac[meilleur], c = ac[meilleur + 1];
        k += 0.5 * (a - c) / (a - 2.0 * b + c + 1e-12);
    }
    return kSampleRate / k;
}

/// Rend une note tenue, le pli appliqué AVANT le premier bloc (une molette
/// poussée puis une note jouée : le cas le plus simple, et celui qui isole la
/// hauteur du reste).
std::vector<float> rendu(ISynthPlugin& plugin, uint8_t note, float pliDemiTons) {
    if (pliDemiTons != 0.0f) {
        MidiControlEvent pli;
        pli.kind = MidiControlEvent::Kind::PitchBend;
        pli.value = pliDemiTons;
        plugin.handleControlEvent(pli);
    }
    const int blocs = 90;                       // ~0,96 s à 48 kHz par blocs de 512
    std::vector<float> sortie;
    std::vector<float> gauche(512), droite(512);
    for (int b = 0; b < blocs; ++b) {
        std::fill(gauche.begin(), gauche.end(), 0.0f);
        std::fill(droite.begin(), droite.end(), 0.0f);
        if (b == 0) {
            const MidiNoteEvent on = noteOn(0, note);
            plugin.process(&on, 1, gauche.data(), droite.data(), 512);
        } else {
            plugin.process(nullptr, 0, gauche.data(), droite.data(), 512);
        }
        // LE CANAL GAUCHE SEUL : sommer les deux replierait un instrument
        // panoramisé ou déphasé, et l'autocorrélation lirait alors une
        // interférence au lieu d'une hauteur.
        sortie.insert(sortie.end(), gauche.begin(), gauche.end());
    }
    return sortie;
}

/// Le critère de D26, pour une machine : témoin sans pli, puis pli d'un
/// demi-ton, à 1 % près sur le rendu.
void verifieLaMolette(const std::string& identifiant, uint8_t note,
                      const std::string& reglageHarmonique = "") {
    auto sansPli = machine(identifiant);
    // CE RÉGLAGE N'EST PAS UN ARRANGEMENT AVEC LA MESURE, C'EST SA CONDITION.
    // Un timbre INHARMONIQUE n'a pas de période : l'e-piano, dont les partiels
    // tombent à 4,75 et 10,6 fois le fondamental, se lit -52 cents à TOUTES les
    // notes par autocorrélation, et exactement 0 cent dès que ses partiels sont
    // harmoniques (`Character` = 1). La machine joue juste ; c'est la mesure qui
    // ne sait pas lire une lame. On mesure donc la JUSTESSE là où elle a un sens.
    if (!reglageHarmonique.empty())
        for (const auto& info : sansPli->parameterList())
            if (info.name == reglageHarmonique) sansPli->setParameter(info.id, 1.0f);
    VSM_ASSERT(sansPli != nullptr);
    const auto témoin = rendu(*sansPli, note, 0.0f);
    const double attenduTemoin = hzDeLaNote(note);
    const double hzTemoin = frequenceFondamentale(témoin, static_cast<size_t>(0.05 * kSampleRate),
                                                   attenduTemoin);
    VSM_ASSERT_NEAR(hzTemoin / attenduTemoin, 1.0, 0.01);

    auto plie = machine(identifiant);
    VSM_ASSERT(plie != nullptr);
    if (!reglageHarmonique.empty())
        for (const auto& info : plie->parameterList())
            if (info.name == reglageHarmonique) plie->setParameter(info.id, 1.0f);
    const auto son = rendu(*plie, note, 1.0f);
    const double attenduPlie = hzDeLaNote(note + 1);
    const double hzPlie = frequenceFondamentale(son, static_cast<size_t>(0.05 * kSampleRate),
                                                 attenduPlie);
    VSM_ASSERT_NEAR(hzPlie / attenduPlie, 1.0, 0.01);
}

} // namespace

// LES SIX MACHINES D'A3 — celles où un musicien plie la hauteur et où la
// machine ne le faisait pas. La note choisie est dans le registre confortable
// de chacune (autour du la 3), là où l'autocorrélation est fiable et où
// l'instrument réel se joue.

VSM_TEST(molette_epiano_plie_d_un_demi_ton) { verifieLaMolette("vsm.epiano", 57, "Character"); }
VSM_TEST(molette_clavinet_plie_d_un_demi_ton) { verifieLaMolette("vsm.clavinet", 57); }
// LA GUIMBARDE A SON PROPRE CRITÈRE, ET C'EST UNE DÉCISION, PAS UN PASSE-DROIT.
//
// Mesuré avant d'écrire une ligne de code : cette machine rend **82,00 Hz pour
// TOUTES les notes** (45, 52, 57, 64, 69). C'est sa nature, écrite dans son
// en-tête — une lame d'acier a une fréquence fixe, et le joueur change sa
// CAVITÉ, pas sa lame. Le critère de D26 (« la note du dessus à 1 % près »)
// n'a donc rien à mesurer ici : plier le bourdon ferait de cette machine un
// synthétiseur au timbre de guimbarde.
//
// Ce que la molette doit faire, c'est PROLONGER LE GESTE DE LA NOTE — qui
// choisit le formant. D'où le critère, plus exigeant que celui des cinq autres
// parce qu'il est exact : plier de douze demi-tons doit sonner comme la note
// douze demi-tons plus haut. La mesure est le taux de passages par zéro, qui
// suit le formant sans demander de transformée.
VSM_TEST(molette_jewsharp_deplace_le_formant_comme_la_note) {
    auto tauxDePassages = [](const std::vector<float>& y) {
        size_t depuis = static_cast<size_t>(0.10 * kSampleRate), n = 0;
        for (size_t i = depuis + 1; i < y.size(); ++i)
            if ((y[i - 1] < 0.0f) != (y[i] < 0.0f)) ++n;
        return static_cast<double>(n) / static_cast<double>(y.size() - depuis);
    };

    auto basse = machine("vsm.jewsharp");
    const double zcrBasse = tauxDePassages(rendu(*basse, 57, 0.0f));
    auto haute = machine("vsm.jewsharp");
    const double zcrHaute = tauxDePassages(rendu(*haute, 69, 0.0f));
    // LE TÉMOIN : la note DOIT déjà déplacer le formant, sinon la mesure ne
    // verrait rien bouger et le reste ne voudrait rien dire.
    VSM_ASSERT(zcrHaute > zcrBasse * 1.2);

    auto pliee = machine("vsm.jewsharp");
    const double zcrPliee = tauxDePassages(rendu(*pliee, 57, 12.0f));
    // LE CRITÈRE : pliée de douze demi-tons, la note 57 sonne comme la note 69.
    VSM_ASSERT_NEAR(zcrPliee / zcrHaute, 1.0, 0.05);
}
VSM_TEST(molette_hurdygurdy_plie_d_un_demi_ton) { verifieLaMolette("vsm.hurdygurdy", 57); }
VSM_TEST(molette_mandolin_plie_d_un_demi_ton) { verifieLaMolette("vsm.mandolin", 57); }
VSM_TEST(molette_kalimba_plie_d_un_demi_ton) { verifieLaMolette("vsm.kalimba", 57); }

// UN CONTRÔLE PRIS DANS LE LOT QUI PLIE DÉJÀ : si ce test-ci tombait, ce serait
// la MESURE qui serait en cause, pas les six machines.
VSM_TEST(molette_le_controle_minimoog_plie_deja) { verifieLaMolette("vsm.minimoog", 57); }


// CE QUE LE DIAGNOSTIC A TROUVÉ, ET QUI N'EST PLUS MESURÉ ICI. Un test jetable
// a servi une fois, puis a été retiré — ses chiffres vivent dans la phase D26 :
//   · l'e-piano se lit -52 cents à TOUTES les notes par autocorrélation libre,
//     et exactement 0 cent dès que ses partiels sont harmoniques. Il joue juste ;
//     c'est la mesure qui ne sait pas lire un timbre inharmonique.
//   · la guimbarde rend 82,00 Hz aux notes 45, 52, 57, 64 et 69 — le bourdon de
//     sa lame, que la note ne bouge pas. D'où son critère à part, ci-dessus.
