#pragma once
#include "vsm/sequencer/Project.h"
#include "vsm/sequencer/SnapshotHistory.h"

namespace vsm::sequencer {

/// L'annulation du DAW : un instantané du projet entier.
///
/// CE QU'ELLE COUVRE, ET QUE LA PRÉCÉDENTE NE COUVRAIT PAS. L'historique
/// d'origine mémorisait le seul `std::vector<Note>` de la piste active. Il
/// devait donc être VIDÉ à chaque changement de piste -- restaurer les notes
/// d'une piste dans une autre n'aurait aucun sens, les identifiants n'y
/// existant pas --, et il ne pouvait rien annuler d'autre que des notes :
/// ajouter ou supprimer une piste, régler un fader, insérer un effet, dessiner
/// une courbe d'automation, poser un repère, déplacer un clip, tout cela était
/// définitif. Le projet entier tient dans un instantané ; plus rien ne l'est.
///
/// LE COÛT, CHIFFRÉ. Une `Note` pèse une trentaine d'octets. Un morceau
/// reconstruit dense -- celui de *Sky and Sand* en compte environ 9 600 sur
/// quatre pistes -- occupe donc quelques centaines de kilo-octets par
/// instantané, soit quelques dizaines de méga-octets à la profondeur maximale.
/// C'est le prix d'une annulation à laquelle on peut se fier, et il est assumé.
using ProjectHistory = SnapshotHistory<Project>;

} // namespace vsm::sequencer
