#pragma once
#include "vsm/sequencer/Track.h"
#include <cstddef>
#include <cstdint>
#include <vector>

// Les gestes d'une courbe d'automation -- poser un point, le déplacer, le
// retirer --, en fonctions PURES. Aucune dépendance à JUCE.
//
// POURQUOI ICI : la même règle que `NoteEdit.h` et `ClipEdit.h`. Ce qui décide
// de la valeur d'un paramètre à un instant donné est de la logique musicale,
// pas du dessin. Dans le composant, elle serait intestable -- il faudrait un
// serveur graphique pour vérifier qu'un fondu passe bien par zéro à mi-course.

namespace vsm::sequencer {

/// La valeur d'une courbe à `tick`, interpolée.
///
/// EN DEHORS DE LA PLAGE DÉFINIE, la valeur est MAINTENUE (celle du premier ou
/// du dernier point) plutôt que ramenée à zéro : une courbe qui ne couvre que
/// le refrain ne doit pas faire tomber le paramètre à rien pendant les
/// couplets.
///
/// LA MÊME RÈGLE QUE LE MOTEUR, et un test le vérifie sur les mêmes points
/// (`audio/tests/test_automation_lane.cpp`). Deux interpolations qui
/// divergeraient feraient dessiner une courbe et en entendre une autre -- le
/// genre d'écart qu'on met des heures à ne pas croire.
float automationValueAt(const AutomationCurve& curve, Tick tick);

/// Pose un point, ou déplace celui qui occupe déjà ce tick. Rend son index.
///
/// « Poser ou déplacer » et non « poser » seul : dessiner une courbe, c'est
/// cliquer plusieurs fois au même endroit en corrigeant, et deux points au même
/// tick rendraient le segment entre eux indéfini.
size_t setAutomationPoint(AutomationCurve& curve, Tick tick, float value, bool step = false);

/// DÉPLACER UNE PLAGE (D17.2) : ce qu'un clip emporte quand il bouge.
///
/// Les points de `[fromTick, toTick)` glissent de `deltaTicks` ; les autres ne
/// bougent pas. Un point déplacé qui retombe sur un tick déjà occupé GAGNE :
/// c'est celui qu'on vient de tirer, et deux points au même tick rendraient le
/// segment entre eux indéfini.
///
/// POURQUOI CETTE FONCTION EXISTE. `ClipEdit` ne touchait à `Track::automation`
/// nulle part : déplacer un clip d'une mesure laissait sa courbe de volume là
/// où elle était, et le projet ne jouait plus ce qu'il montrait. C'est le
/// « l'automation suit les événements » de Cubase, actif par défaut chez lui
/// comme ici.
///
/// AUCUN POINT N'EST CRÉÉ AUX BORDS, et c'est délibéré -- au contraire de
/// `writeAutomationRange`, qui en pose deux. Écrire une plage REMPLACE ce
/// qu'elle contenait, donc il faut raccorder ; la déplacer TRANSPORTE ce
/// qu'elle contenait, et poser des raccords ajouterait à chaque déplacement
/// deux points que personne n'a demandés -- au bout de dix gestes, la courbe
/// serait un peigne.
///
/// Rend le nombre de points déplacés.
size_t shiftAutomationRange(AutomationCurve& curve, Tick fromTick, Tick toTick, Tick deltaTicks);

/// ÉCRIRE UNE PLAGE (D16.8) : ce qu'un passage d'automation en jeu dépose.
///
/// Les points de `[fromTick, toTick]` sont REMPLACÉS par ceux qu'on vient de
/// jouer, et les deux bords sont RACCORDÉS à ce que la courbe disait juste
/// avant et juste après. Sans ce raccord, écrire deux mesures au milieu d'un
/// fondu ferait sauter le paramètre à l'entrée et à la sortie de la plage --
/// on aurait corrigé deux mesures en cassant les deux voisines.
///
/// LE RACCORD EST POSÉ AUX BORDS EXACTS, à un tick de la plage : la valeur
/// d'avant est réinscrite à `fromTick - 1`, celle d'après à `toTick + 1`, et
/// seulement si la courbe disait quelque chose là (elle est maintenue hors de
/// sa plage définie, voir `automationValueAt`). Un raccord posé À L'INTÉRIEUR
/// de la plage écraserait le début de ce qu'on vient de jouer.
///
/// `written` est la suite des points joués, en ordre de tick. Vide, la
/// fonction ne fait rien : un passage où l'on n'a touché à rien ne doit pas
/// effacer une courbe.
void writeAutomationRange(AutomationCurve& curve, Tick fromTick, Tick toTick,
                           const std::vector<AutomationPoint>& written);

/// Retire le point le plus proche de `tick`, s'il est à moins de `tolerance`.
/// Rend vrai si un point a été retiré.
bool removeAutomationPointNear(AutomationCurve& curve, Tick tick, Tick tolerance);

/// RÉDUIRE LES POINTS D'UNE COURBE (D30.5) -- le « Reduce automation points »
/// de Cubase.
///
/// POURQUOI IL EN FALLAIT UN. Une passe d'automation écrite en jouant (D16.8)
/// pose un point par tick touché, et depuis D29.3 un potentiomètre MIDI en
/// pose autant que le port en délivre : la courbe obtenue est juste, et
/// illisible -- des centaines de points là où le geste en valait dix. On ne
/// peut plus la retoucher à la main, ce qui est précisément ce qu'on veut
/// faire d'une passe qu'on vient d'enregistrer.
///
/// L'ALGORITHME : Ramer-Douglas-Peucker, sur l'écart VERTICAL au segment. Le
/// classique mesure une distance perpendiculaire, ce qui n'a pas de sens ici :
/// l'axe horizontal est du temps et l'axe vertical une valeur de paramètre --
/// deux grandeurs sans rapport, dont la « distance » dépendrait du zoom. Ce
/// qu'on veut borner est l'écart entre ce qu'on entendait et ce qu'on
/// entendra, c'est-à-dire l'écart de VALEUR à tick égal.
///
/// CE QUI EST TOUJOURS GARDÉ, et c'est ce qui rend la réduction sûre :
///  - les DEUX EXTRÉMITÉS, sans quoi la courbe changerait de portée ;
///  - tout point marqué `step` ET son voisin de droite : un palier n'est pas
///    une rampe, et le supprimer ne déplacerait pas la courbe d'un peu, il en
///    changerait la nature.
///
/// `tolerance` est en UNITÉS DU PARAMÈTRE, comme les valeurs elles-mêmes (la
/// règle de tout le projet) : l'appelant la calcule sur l'amplitude, ce qu'il
/// est le seul à connaître. Négative ou nulle, rien n'est retiré.
///
/// Rend le nombre de points RETIRÉS -- pour le dire à qui a demandé la
/// réduction, plutôt que de la faire en silence.
size_t thinAutomation(AutomationCurve& curve, float tolerance);

/// Le plus grand écart de VALEUR entre deux courbes, mesuré sur l'union de
/// leurs ticks. Sert à vérifier une réduction : c'est le chiffre qui dit si
/// `thinAutomation` a tenu sa tolérance, et il est ici plutôt que dans un test
/// parce que l'application le publie aussi.
float maxAutomationDeviation(const AutomationCurve& a, const AutomationCurve& b);

// ---------------------------------------------------------------------------
// DESSINER UNE AUTOMATION PAR UNE FORME (D34.5) -- l'outil « ligne » de Cubase.
//
// POURQUOI IL EN FALLAIT UN. `setAutomationPoint` pose UN point et
// `writeAutomationRange` dépose ce qu'on vient de JOUER : un balayage de filtre
// sur seize mesures se posait donc à la main, point après point, et un
// trémolo régulier ne se posait pas du tout. C'est le geste d'automation le
// plus mécanique qui soit, et c'est celui qui coûtait le plus cher.
// ---------------------------------------------------------------------------

/// Les formes qu'on sait tracer. `Line` couvre la rampe et le palier plat
/// (mêmes valeurs aux deux bouts) : ce sont le même geste, et les séparer
/// aurait fait trois entrées de menu pour une seule idée.
enum class AutomationShape : uint8_t { Line = 0, Sine = 1, Triangle = 2, Square = 3 };

/// Ce qu'un tracé a fait, pour le DIRE plutôt que de le faire en silence.
struct AutomationDraw {
    size_t removed = 0;   ///< points de la plage qui ont été remplacés
    size_t added = 0;     ///< points posés
};

/// TRACE UNE FORME SUR `[fromTick, toTick]`, en remplaçant ce qui s'y trouvait.
///
/// `from` et `to` sont les valeurs aux deux bouts. Pour `Line`, la forme les
/// joint ; pour les trois autres, elles bornent l'oscillation -- `from` est le
/// creux et `to` la crête, et les inverser retourne la forme.
///
/// `periods` est le nombre d'oscillations sur la plage. Ignoré par `Line`.
/// Zéro ou négatif vaut une période : une forme sans oscillation n'a pas de
/// sens, et refuser silencieusement serait pire que corriger.
///
/// LES BORDS SONT RACCORDÉS comme dans `writeAutomationRange`, et pour la même
/// raison : tracer quatre mesures au milieu d'un fondu ferait autrement sauter
/// le paramètre à l'entrée et à la sortie -- on aurait dessiné quatre mesures
/// en cassant les deux voisines.
///
/// COMBIEN DE POINTS. Le critère est double et les deux moitiés tirent en sens
/// contraires : la forme rendue doit s'écarter de la forme idéale de moins de
/// `tolerance` (en unités du paramètre), ET le nombre de points doit rester
/// PARCIMONIEUX -- une forme qui poserait un point par tick serait juste,
/// illisible et impossible à retoucher, c'est-à-dire inutile. Ne mesurer que la
/// première moitié laisserait passer une forme qui triche en posant mille
/// points.
///
/// D'où la méthode : on échantillonne la forme finement, puis on RÉDUIT par
/// `thinAutomation` -- la fonction de D30.5, écrite pour exactement ce
/// problème sur les passes jouées. Un carré, lui, ne s'échantillonne pas : ses
/// paliers se posent en deux points par période, marqués `step`, parce qu'un
/// carré approché par une rampe très raide n'est pas un carré.
AutomationDraw drawAutomationShape(AutomationCurve& curve, Tick fromTick, Tick toTick,
                                    AutomationShape shape, float from, float to,
                                    int periods, float tolerance);

/// L'index du point le plus proche de `tick` à moins de `tolerance`, ou la
/// taille de la courbe si aucun. Sert à savoir ce qu'on vient de saisir.
size_t automationPointNear(const AutomationCurve& curve, Tick tick, Tick tolerance);

} // namespace vsm::sequencer
