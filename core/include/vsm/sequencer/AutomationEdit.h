#pragma once
#include "vsm/sequencer/Track.h"
#include <cstddef>

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

/// Retire le point le plus proche de `tick`, s'il est à moins de `tolerance`.
/// Rend vrai si un point a été retiré.
bool removeAutomationPointNear(AutomationCurve& curve, Tick tick, Tick tolerance);

/// L'index du point le plus proche de `tick` à moins de `tolerance`, ou la
/// taille de la courbe si aucun. Sert à savoir ce qu'on vient de saisir.
size_t automationPointNear(const AutomationCurve& curve, Tick tick, Tick tolerance);

} // namespace vsm::sequencer
