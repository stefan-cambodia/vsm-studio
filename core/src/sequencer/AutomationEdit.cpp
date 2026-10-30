#include "vsm/sequencer/AutomationEdit.h"
#include <utility>
#include <algorithm>
#include <map>
#include <cmath>

namespace vsm::sequencer {

float automationValueAt(const AutomationCurve& curve, Tick tick) {
    const auto& points = curve.points;
    if (points.empty()) return 0.0f;
    if (tick <= points.front().tick) return points.front().value;
    if (tick >= points.back().tick) return points.back().value;

    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const auto& a = points[i];
        const auto& b = points[i + 1];
        if (tick < a.tick || tick > b.tick) continue;
        // UN PALIER TIENT SA VALEUR JUSQU'AU POINT SUIVANT : c'est ce que veut
        // dire `step`, et c'est ce qu'il faut pour un commutateur, un choix de
        // forme d'onde ou tout ce qui ne s'interpole pas.
        if (a.step || b.tick == a.tick) return a.value;
        const float ratio = static_cast<float>(tick - a.tick)
                            / static_cast<float>(b.tick - a.tick);
        // LA COURBURE (D17.7) : la MÊME fonction que le moteur, appelée depuis
        // le modèle. Deux formules qui divergeraient feraient dessiner une
        // courbe et en entendre une autre.
        const float avance = automationCurveEase(a.curve, ratio);
        return a.value + avance * (b.value - a.value);
    }
    return points.back().value;
}

size_t shiftAutomationRange(AutomationCurve& curve, Tick fromTick, Tick toTick,
                            Tick deltaTicks) {
    if (deltaTicks == 0 || toTick <= fromTick || curve.points.empty()) return 0;

    std::map<Tick, AutomationPoint> parTick;
    std::vector<AutomationPoint> deplaces;
    for (const auto& p : curve.points) {
        if (p.tick >= fromTick && p.tick < toTick) {
            AutomationPoint bouge = p;
            bouge.tick = p.tick + deltaTicks;
            // Aucun point ne passe avant zéro : la ligne de temps commence là,
            // et un point négatif ne serait ni dessiné ni joué.
            if (bouge.tick < 0) continue;
            deplaces.push_back(bouge);
        } else {
            parTick[p.tick] = p;
        }
    }
    // EN DERNIER, donc vainqueurs : ce qu'on vient de tirer.
    for (const auto& p : deplaces) parTick[p.tick] = p;

    curve.points.clear();
    curve.points.reserve(parTick.size());
    for (const auto& [tick, point] : parTick) curve.points.push_back(point);
    return deplaces.size();
}

void writeAutomationRange(AutomationCurve& curve, Tick fromTick, Tick toTick,
                           const std::vector<AutomationPoint>& written, Tick returnTicks) {
    if (written.empty() || toTick < fromTick) return;

    // CE QUE LA COURBE DISAIT AUX BORDS, LU AVANT DE TOUCHER À QUOI QUE CE
    // SOIT : après suppression, il n'y aurait plus rien à lire.
    const bool avait = !curve.points.empty();
    // D600.3 : LE RETOUR prolonge la plage réécrite ; sur une courbe vide, il
    // n'y a rien à rejoindre et la plage reste celle qu'on a jouée.
    const bool retour = avait && returnTicks > 0;
    const Tick bout = retour ? toTick + returnTicks : toTick;
    const float avant = avait ? automationValueAt(curve, fromTick > 0 ? fromTick - 1 : 0) : 0.0f;
    const float apres = avait ? automationValueAt(curve, bout + 1) : 0.0f;
    const float auBout = retour ? automationValueAt(curve, bout) : 0.0f;

    // UNE CARTE PAR TICK, et non un tri suivi d'un dédoublonnage : deux points
    // au même tick rendraient le segment entre eux indéfini, et il faut que ce
    // soit le POINT JOUÉ qui gagne, pas celui que l'ordre de tri a mis devant.
    // Un `std::unique` garde le premier, et le tri n'est pas stable sur les
    // ex æquo : la courbe aurait dépendu de l'implémentation de `std::sort`.
    std::map<Tick, AutomationPoint> parTick;
    for (const auto& p : curve.points)
        if (p.tick < fromTick || p.tick > bout) parTick[p.tick] = p;

    // LES RACCORDS, à UN TICK de la plage et non à ses bords : posés dessus,
    // ils écraseraient le premier et le dernier point de ce qu'on vient de
    // jouer. Seulement si la courbe disait quelque chose, et seulement s'il y
    // a la place (une plage qui commence au tick 0 n'a pas de « juste avant »).
    if (avait && fromTick > 0) parTick[fromTick - 1] = {fromTick - 1, avant, false};
    if (avait) parTick[bout + 1] = {bout + 1, apres, false};
    // D600.3 : LE BOUT DU RETOUR, sur la valeur de la courbe d'AVANT en ce
    // tick : la rampe file du dernier point joué jusqu'à lui. Sans ce point, le
    // raccord seul (un tick plus loin) ferait manquer la mi-rampe d'un
    // deux-cent-quarantième.
    if (retour) parTick[bout] = {bout, auBout, false};

    // EN DERNIER, donc vainqueurs : ce qu'on vient de jouer.
    for (const auto& p : written)
        if (p.tick >= fromTick && p.tick <= toTick) parTick[p.tick] = p;

    curve.points.clear();
    curve.points.reserve(parTick.size());
    for (const auto& [tick, point] : parTick) curve.points.push_back(point);
}

namespace {
/// Le décalage joué à `tick` : interpolé entre deux points de la passe, tenu
/// au-delà du premier et du dernier — la règle d'une courbe (`automationValueAt`),
/// sans palier ni courbure : une passe n'en a pas.
float decalageA(const std::vector<AutomationPoint>& decalages, Tick tick) {
    if (tick <= decalages.front().tick) return decalages.front().value;
    if (tick >= decalages.back().tick) return decalages.back().value;
    for (size_t i = 0; i + 1 < decalages.size(); ++i) {
        const auto& a = decalages[i];
        const auto& b = decalages[i + 1];
        if (tick < a.tick || tick > b.tick) continue;
        if (b.tick == a.tick) return b.value;
        const float ratio = static_cast<float>(tick - a.tick) / static_cast<float>(b.tick - a.tick);
        return a.value + ratio * (b.value - a.value);
    }
    return decalages.back().value;
}
} // namespace

bool trimAutomationRange(AutomationCurve& curve, Tick fromTick, Tick toTick,
                         const std::vector<AutomationPoint>& offsets, TrimLaw law,
                         float minimum, float maximum, Tick returnTicks) {
    if (curve.points.empty() || offsets.empty() || toTick < fromTick) return false;

    std::vector<AutomationPoint> decalages = offsets;
    std::stable_sort(decalages.begin(), decalages.end(),
                     [](const AutomationPoint& a, const AutomationPoint& b) { return a.tick < b.tick; });
    // LUE AVANT D'ÉCRIRE : chaque valeur décalée est celle de la courbe d'AVANT.
    const AutomationCurve ancienne = curve;
    // LA BASE D'UN POINT ANCIEN EST SA PROPRE VALEUR, pas celle de la courbe à son tick : au tick d'un point qui suit
    // un palier, `automationValueAt` rend encore la valeur TENUE (le segment qui finit là) — décaler celle-ci aurait
    // effacé la marche (vu au premier essai du test : 0,998 au lieu de 0,5 juste après elle).
    const auto decaler = [&](Tick tick, float v) {
        const float d = decalageA(decalages, tick);
        const float r = law == TrimLaw::GainDb ? v * std::pow(10.0f, d / 20.0f) : v + d;
        return std::clamp(r, minimum, maximum);
    };

    // LES COUPES : les points de la passe dans la plage, et les deux bords — ce
    // qui, tombant DANS un segment ancien, le coupe.
    std::map<Tick, bool> coupes;
    coupes[fromTick] = true;
    coupes[toTick] = true;
    for (const auto& d : decalages)
        if (d.tick >= fromTick && d.tick <= toTick) coupes[d.tick] = true;

    std::map<Tick, AutomationPoint> resultat;
    const auto& anciens = ancienne.points;
    for (size_t i = 0; i < anciens.size(); ++i) {
        const auto& p = anciens[i];
        if (p.tick < fromTick || p.tick > toTick) continue;
        AutomationPoint q = p;
        q.value = decaler(p.tick, p.value);
        // Le segment de `p` court jusqu'au point suivant ; s'il n'y en a pas, la
        // valeur est tenue et sa courbure ne sert à rien.
        if (i + 1 < anciens.size()) {
            const auto coupe = coupes.upper_bound(p.tick);
            if (coupe != coupes.end() && coupe->first < anciens[i + 1].tick) q.curve = 0.0f;
        }
        resultat[p.tick] = q;
    }
    for (const auto& [tick, _] : coupes) {
        if (resultat.count(tick)) continue;   // un point ancien au même tick garde ses attributs
        AutomationPoint q;
        q.tick = tick;
        q.value = decaler(tick, automationValueAt(ancienne, tick));
        // Le palier du segment où il tombe : le dernier point ancien à ou avant lui.
        for (const auto& p : anciens) {
            if (p.tick > tick) break;
            q.step = p.step;
        }
        resultat[tick] = q;
    }

    std::vector<AutomationPoint> ecrits;
    ecrits.reserve(resultat.size());
    for (const auto& [tick, point] : resultat) ecrits.push_back(point);
    writeAutomationRange(curve, fromTick, toTick, ecrits, returnTicks);
    return true;
}

namespace {
/// D602.1 : les cinq remplissages et leurs mots de préférences, dans l'ordre de l'énumération.
constexpr std::pair<AutomationFill, const char*> kRemplissages[] = {
    {AutomationFill::None, "aucun"},  {AutomationFill::Punch, "passe"}, {AutomationFill::ToStart, "debut"},
    {AutomationFill::ToEnd, "fin"},   {AutomationFill::Loop, "boucle"}};
} // namespace

namespace {
/// D602.1 : « JUSQU'À LA FIN », ET AU-DELÀ — le raccord de `writeAutomationRange` à `fin + 1` y rendait l'ancienne valeur
/// (vu au premier essai du test : 0,25 au tick 9 000). Ce qui suit la fin remplie est retiré : la valeur y est tenue.
void couperApres(AutomationCurve& curve, Tick fin) {
    curve.points.erase(std::remove_if(curve.points.begin(), curve.points.end(),
                                      [fin](const AutomationPoint& p) { return p.tick > fin; }),
                       curve.points.end());
}
} // namespace

std::string automationFillName(AutomationFill fill) {
    for (const auto& [mode, nom] : kRemplissages)
        if (mode == fill) return nom;
    return "aucun";
}

AutomationFill automationFillFromName(const std::string& name) {
    for (const auto& [mode, nom] : kRemplissages)
        if (name == nom) return mode;
    return AutomationFill::None;
}

FillResult fillPass(AutomationCurve& curve, Tick from, Tick to, const std::vector<AutomationPoint>& written,
                    AutomationFill fill, const FillBounds& bounds, Tick returnTicks) {
    if (written.empty() || to < from) return {};
    const float v = written.back().value;   // la DERNIÈRE valeur jouée : celle que la main tenait au lâcher
    switch (fill) {
        case AutomationFill::None:
            writeAutomationRange(curve, from, to, written, returnTicks);
            return {};
        case AutomationFill::Punch:
            writeAutomationRange(curve, from, to, {{from, v, false}, {to, v, false}}, returnTicks);
            return {true, from, to, v};
        case AutomationFill::ToStart:
            writeAutomationRange(curve, 0, to, {{0, v, false}, {to, v, false}}, returnTicks);
            return {true, 0, to, v};
        case AutomationFill::ToEnd: {
            // LA PASSE GARDÉE, PUIS SA DERNIÈRE VALEUR JUSQU'À LA FIN : une seule plage réécrite, sans retour — il n'y
            // a pas d'« après » à rejoindre.
            const Tick fin = std::max(bounds.songEnd, to);
            std::vector<AutomationPoint> points = written;
            if (fin > to) points.push_back({fin, v, false});
            writeAutomationRange(curve, from, fin, points, 0);
            couperApres(curve, fin);
            return {fin > to, to, fin, v};
        }
        case AutomationFill::Loop: {
            writeAutomationRange(curve, from, to, written, returnTicks);
            if (bounds.loopEnd <= bounds.loopStart) return {};   // pas de boucle : rien de rempli, et l'appelant le dit
            writeAutomationRange(curve, bounds.loopStart, bounds.loopEnd,
                                 {{bounds.loopStart, v, false}, {bounds.loopEnd, v, false}}, 0);
            return {true, bounds.loopStart, bounds.loopEnd, v};
        }
    }
    return {};
}

FillResult fillTrimPass(AutomationCurve& curve, Tick from, Tick to, const std::vector<AutomationPoint>& offsets,
                        TrimLaw law, float minimum, float maximum, AutomationFill fill, const FillBounds& bounds,
                        Tick returnTicks, bool& applied) {
    applied = false;
    if (curve.points.empty() || offsets.empty() || to < from) return {};
    // LE DERNIER DÉCALAGE, celui que la main tenait au lâcher ; tenu au-delà du dernier point (`decalageA`).
    Tick dernier = offsets.front().tick;
    float d = offsets.front().value;
    for (const auto& o : offsets)
        if (o.tick >= dernier) { dernier = o.tick; d = o.value; }
    const std::vector<AutomationPoint> constant = {{from, d, false}};
    switch (fill) {
        case AutomationFill::None:
            applied = trimAutomationRange(curve, from, to, offsets, law, minimum, maximum, returnTicks);
            return {};
        case AutomationFill::Punch:
            applied = trimAutomationRange(curve, from, to, constant, law, minimum, maximum, returnTicks);
            return {applied, from, to, d};
        case AutomationFill::ToStart:
            applied = trimAutomationRange(curve, 0, to, {{0, d, false}}, law, minimum, maximum, returnTicks);
            return {applied, 0, to, d};
        case AutomationFill::ToEnd: {
            const Tick fin = std::max(bounds.songEnd, to);
            applied = trimAutomationRange(curve, from, fin, offsets, law, minimum, maximum, 0);
            if (applied) couperApres(curve, fin);
            return {applied && fin > to, to, fin, d};
        }
        case AutomationFill::Loop: {
            applied = trimAutomationRange(curve, from, to, offsets, law, minimum, maximum, returnTicks);
            if (!applied || bounds.loopEnd <= bounds.loopStart) return {};
            trimAutomationRange(curve, bounds.loopStart, bounds.loopEnd, {{bounds.loopStart, d, false}}, law, minimum,
                                maximum, 0);
            return {true, bounds.loopStart, bounds.loopEnd, d};
        }
    }
    return {};
}

namespace {
/// D601.2 : les six familles et leurs mots de fichier, dans l'ordre des bits.
constexpr std::pair<uint8_t, const char*> kFamilles[] = {
    {kFamilyVolume, "volume"}, {kFamilyPan, "pan"},          {kFamilySends, "sends"},
    {kFamilyTrim, "trim"},     {kFamilyInserts, "inserts"},  {kFamilyMachines, "machines"}};
} // namespace

uint8_t automationFamilyOf(const std::string& parameter) {
    // LES PRÉFIXES DE `resolveAutomation` (interchange), dans le même ordre : sans préfixe connu, c'est un réglage de
    // la machine de la piste.
    if (parameter == "mix.volume") return kFamilyVolume;
    if (parameter == "mix.pan") return kFamilyPan;
    if (parameter == "mix.trim") return kFamilyTrim;
    if (parameter.rfind("mix.send.", 0) == 0) return kFamilySends;
    if (parameter.rfind("master.", 0) == 0) return 0;
    if (parameter.rfind("insert.", 0) == 0) return kFamilyInserts;
    return kFamilyMachines;
}

std::string automationFamilyName(uint8_t family) {
    for (const auto& [bit, nom] : kFamilles)
        if (bit == family) return nom;
    return {};
}

uint8_t automationFamilyFromName(const std::string& name) {
    for (const auto& [bit, nom] : kFamilles)
        if (name == nom) return bit;
    return 0;
}

size_t setAutomationPoint(AutomationCurve& curve, Tick tick, float value, bool step) {
    for (size_t i = 0; i < curve.points.size(); ++i) {
        if (curve.points[i].tick != tick) continue;
        curve.points[i].value = value;
        curve.points[i].step = step;
        return i;
    }
    curve.points.push_back({tick, value, step});
    std::stable_sort(curve.points.begin(), curve.points.end(),
                      [](const AutomationPoint& a, const AutomationPoint& b) {
                          return a.tick < b.tick;
                      });
    for (size_t i = 0; i < curve.points.size(); ++i)
        if (curve.points[i].tick == tick) return i;
    return curve.points.size() - 1;
}

size_t automationPointNear(const AutomationCurve& curve, Tick tick, Tick tolerance) {
    size_t meilleur = curve.points.size();
    Tick distance = tolerance + 1;
    for (size_t i = 0; i < curve.points.size(); ++i) {
        const Tick d = std::abs(curve.points[i].tick - tick);
        if (d <= tolerance && d < distance) { distance = d; meilleur = i; }
    }
    return meilleur;
}

bool removeAutomationPointNear(AutomationCurve& curve, Tick tick, Tick tolerance) {
    const size_t index = automationPointNear(curve, tick, tolerance);
    if (index >= curve.points.size()) return false;
    curve.points.erase(curve.points.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

// ---------------------------------------------------------------------------
// D30.5 — RÉDUIRE LES POINTS D'UNE COURBE.
// ---------------------------------------------------------------------------

namespace {

/// Un tronçon [debut, fin] gardé tel quel ; la récursion l'ouvre en deux au
/// point qui s'écarte le plus.
void simplifier(const std::vector<AutomationPoint>& points, size_t debut, size_t fin,
                 float tolerance, std::vector<bool>& garde) {
    if (fin <= debut + 1) return;
    const AutomationPoint& a = points[debut];
    const AutomationPoint& b = points[fin];
    // LA DROITE DE RÉFÉRENCE EST CELLE QUI RESTERAIT si l'on retirait tout
    // l'intérieur -- donc la même interpolation que `automationValueAt`, qui
    // est linéaire entre deux points. Un palier est traité par l'appelant :
    // ici la droite serait fausse, et c'est pourquoi `step` et `curve` coupent.
    const double span = static_cast<double>(b.tick - a.tick);
    size_t pire = debut;
    double pireEcart = 0.0;
    for (size_t i = debut + 1; i < fin; ++i) {
        const double t = span > 0.0 ? static_cast<double>(points[i].tick - a.tick) / span : 0.0;
        const double surLaDroite = a.value + (b.value - a.value) * t;
        const double ecart = std::fabs(points[i].value - surLaDroite);
        if (ecart > pireEcart) { pireEcart = ecart; pire = i; }
    }
    if (pireEcart <= static_cast<double>(tolerance)) return;   // tout l'intérieur tombe
    garde[pire] = true;
    simplifier(points, debut, pire, tolerance, garde);
    simplifier(points, pire, fin, tolerance, garde);
}

} // namespace

AutomationRemoval removeAllAutomation(Track& track) {
    AutomationRemoval r;
    r.curves = track.automation.size();
    for (const auto& c : track.automation) r.points += c.points.size();
    track.automation.clear();
    return r;
}

size_t thinAutomation(AutomationCurve& curve, float tolerance) {
    if (tolerance <= 0.0f || curve.points.size() <= 2) return 0;
    const auto& points = curve.points;
    const size_t n = points.size();
    std::vector<bool> garde(n, false);
    garde[0] = true;
    garde[n - 1] = true;

    // LES SEGMENTS QUI NE SONT PAS DES DROITES COUPENT LA COURBE EN TRONÇONS,
    // et l'on simplifie chacun séparément. Il y en a DEUX SORTES, et oublier
    // la seconde aurait fait mentir la tolérance :
    //  - le PALIER (`step`) : le segment qui en part est plat ;
    //  - la COURBURE (`curve`, D17.7) : le segment qui en part est fléchi.
    // Dans les deux cas, la droite que la simplification suppose entre deux
    // points n'existe pas, et l'écart qu'elle mesurerait serait celui d'une
    // courbe qu'on ne joue pas. On garde donc ce point ET celui qui le suit --
    // retirer le second déplacerait la marche ou la flèche.
    size_t debut = 0;
    for (size_t i = 0; i < n; ++i) {
        if (!points[i].step && points[i].curve == 0.0f) continue;
        garde[i] = true;
        if (i + 1 < n) garde[i + 1] = true;
        simplifier(points, debut, i, tolerance, garde);
        debut = i + 1 < n ? i + 1 : i;
    }
    simplifier(points, debut, n - 1, tolerance, garde);

    std::vector<AutomationPoint> restants;
    restants.reserve(n);
    for (size_t i = 0; i < n; ++i)
        if (garde[i]) restants.push_back(points[i]);
    const size_t retires = n - restants.size();
    curve.points = std::move(restants);
    return retires;
}

float maxAutomationDeviation(const AutomationCurve& a, const AutomationCurve& b) {
    // SUR L'UNION DES TICKS, et non sur ceux de l'une des deux : le pire écart
    // d'une réduction se trouve précisément AUX POINTS RETIRÉS, qui ne sont
    // plus dans la courbe réduite. Ne mesurer que sur celle-ci rendrait zéro à
    // tous les coups -- un chiffre qui se contente de confirmer ce qu'on veut
    // croire.
    std::vector<Tick> ticks;
    ticks.reserve(a.points.size() + b.points.size());
    for (const auto& p : a.points) ticks.push_back(p.tick);
    for (const auto& p : b.points) ticks.push_back(p.tick);
    std::sort(ticks.begin(), ticks.end());
    ticks.erase(std::unique(ticks.begin(), ticks.end()), ticks.end());
    float pire = 0.0f;
    for (Tick t : ticks)
        pire = std::max(pire, std::fabs(automationValueAt(a, t) - automationValueAt(b, t)));
    return pire;
}

// ---------------------------------------------------------------------------
// DESSINER UNE FORME (D34.5)
// ---------------------------------------------------------------------------

namespace {

/// La forme, sur un avancement `x` de 0 à 1, ramenée entre 0 et 1.
/// `Line` monte tout droit ; les autres oscillent `periods` fois en partant du
/// creux, comme le fait un LFO qu'on met en marche.
float formeA(AutomationShape shape, double x, int periods) {
    if (shape == AutomationShape::Line) return static_cast<float>(x);
    if (shape == AutomationShape::Parabola) return static_cast<float>(x * x);   // D581.2 : une fois, sans période
    const double phase = x * periods;
    const double dansLaPeriode = phase - std::floor(phase);
    switch (shape) {
        case AutomationShape::Sine:
            // DÉPART AU CREUX ET RETOUR AU CREUX : `(1 - cos)/2` plutôt qu'un
            // sinus brut, qui commencerait à mi-course. Un LFO qu'on met en
            // marche part du bas, et c'est ce qu'on dessine à la main.
            return static_cast<float>((1.0 - std::cos(dansLaPeriode * 2.0 * 3.14159265358979323846)) * 0.5);
        case AutomationShape::Triangle:
            return static_cast<float>(dansLaPeriode < 0.5 ? dansLaPeriode * 2.0
                                                          : (1.0 - dansLaPeriode) * 2.0);
        case AutomationShape::Square:
            return dansLaPeriode < 0.5 ? 0.0f : 1.0f;
        case AutomationShape::Line:
        case AutomationShape::Parabola:
        default:
            return static_cast<float>(x);
    }
}

} // namespace

AutomationDraw drawAutomationShape(AutomationCurve& curve, Tick fromTick, Tick toTick,
                                    AutomationShape shape, float from, float to,
                                    int periods, float tolerance) {
    AutomationDraw fait;
    if (toTick <= fromTick) return fait;
    if (periods <= 0) periods = 1;   // une forme sans oscillation n'a pas de sens

    // CE QUE LA COURBE DISAIT AUX BORDS, relevé AVANT de toucher à quoi que ce
    // soit : les raccords s'appuient dessus, et les lire après le retrait
    // rendrait la valeur maintenue de la plage voisine.
    const bool avaitQuelqueChose = !curve.points.empty();
    const float avant = avaitQuelqueChose ? automationValueAt(curve, fromTick - 1) : 0.0f;
    const float apres = avaitQuelqueChose ? automationValueAt(curve, toTick + 1) : 0.0f;

    const size_t nAvant = curve.points.size();
    curve.points.erase(std::remove_if(curve.points.begin(), curve.points.end(),
                                       [&](const AutomationPoint& p) {
                                           return p.tick >= fromTick && p.tick <= toTick;
                                       }),
                        curve.points.end());
    fait.removed = nAvant - curve.points.size();

    const Tick etendue = toTick - fromTick;
    std::vector<AutomationPoint> traces;

    if (shape == AutomationShape::Square) {
        // UN CARRÉ NE S'ÉCHANTILLONNE PAS. Approché par des rampes très
        // raides, il n'est plus un carré : il devient une suite de fondus
        // courts, ce qui s'entend. Deux points par période, marqués `step`, et
        // la courbe dit exactement ce qu'elle joue.
        for (int k = 0; k < periods; ++k) {
            const Tick base = fromTick + static_cast<Tick>(etendue * k / periods);
            const Tick mi = fromTick + static_cast<Tick>(etendue * (2 * k + 1) / (2 * periods));
            traces.push_back({base, from, true, 0.0f});
            if (mi > base) traces.push_back({mi, to, true, 0.0f});
        }
        traces.push_back({toTick, from, true, 0.0f});
    } else {
        // ON ÉCHANTILLONNE FIN, PUIS ON RÉDUIT. Le nombre de points de départ
        // n'a pas à être malin : `thinAutomation` (D30.5) est écrite pour
        // exactement ce problème, et lui confier la parcimonie vaut mieux que
        // de la deviner ici forme par forme.
        // 256 ÉCHANTILLONS PAR PÉRIODE, ET NON 64. La tolérance promise porte
        // sur l'écart à la forme IDÉALE, or DEUX erreurs s'y ajoutent : celle
        // de la réduction, que `thinAutomation` borne, et celle de la
        // POLYLIGNE échantillonnée elle-même, dont les cordes coupent les
        // sommets. À 64 points par période, cette seconde erreur vaut à elle
        // seule un huitième du budget, et la mesure rendait 1,01 % pour 1 %
        // promis -- un dépassement de rien du tout, qui restait un dépassement.
        // À 256 elle tombe d'un facteur seize et devient négligeable.
        // D581.2 : LA PARABOLE, 256 échantillons UNE fois — elle n'a pas de période, et `periods` (une par mesure, au
        // menu) ne ferait que multiplier les points que la réduction retire.
        const int parPeriode = shape == AutomationShape::Line ? 1 : 256;
        const int echantillons = shape == AutomationShape::Parabola ? 256
                                                                     : std::max(2, parPeriode * std::max(1, periods));
        for (int i = 0; i <= echantillons; ++i) {
            const double x = static_cast<double>(i) / echantillons;
            const Tick tick = fromTick + static_cast<Tick>(std::llround(etendue * x));
            traces.push_back({tick, from + (to - from) * formeA(shape, x, periods), false, 0.0f});
        }
    }

    for (const auto& point : traces) setAutomationPoint(curve, point.tick, point.value, point.step);

    // LES RACCORDS, à un tick de la plage et jamais dedans : un raccord posé à
    // l'intérieur écraserait le début de la forme qu'on vient de tracer.
    if (avaitQuelqueChose) {
        if (fromTick > 0) setAutomationPoint(curve, fromTick - 1, avant);
        setAutomationPoint(curve, toTick + 1, apres);
    }

    // LA RÉDUCTION NE TOUCHE QUE CE QU'ON VIENT DE POSER : `thinAutomation`
    // travaille sur toute la courbe, ce qui amincirait aussi des passages que
    // personne n'a demandé de retoucher. On réduit donc une courbe qui ne
    // contient que la plage, puis on la recolle.
    if (shape != AutomationShape::Square && tolerance > 0.0f) {
        // ET L'ON RÉSERVE UN VINGTIÈME DU BUDGET à ce qui reste d'erreur
        // d'échantillonnage : promettre `tolerance` puis en dépenser la
        // totalité à la réduction seule, c'est promettre ce qu'on ne tient
        // qu'au bord. Le témoin est la mesure, qui doit passer SOUS le chiffre
        // annoncé, pas dessus.
        const float budget = tolerance * 0.95f;
        AutomationCurve plage;
        AutomationCurve reste;
        for (const auto& p : curve.points)
            (p.tick >= fromTick && p.tick <= toTick ? plage : reste).points.push_back(p);
        thinAutomation(plage, budget);
        curve.points = std::move(reste.points);
        for (const auto& p : plage.points) curve.points.push_back(p);
        std::sort(curve.points.begin(), curve.points.end(),
                  [](const AutomationPoint& a, const AutomationPoint& b) { return a.tick < b.tick; });
    }

    for (const auto& p : curve.points)
        if (p.tick >= fromTick && p.tick <= toTick) ++fait.added;
    return fait;
}

} // namespace vsm::sequencer
