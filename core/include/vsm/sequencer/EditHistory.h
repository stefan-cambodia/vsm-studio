#pragma once
#include "vsm/sequencer/SnapshotHistory.h"
#include "vsm/sequencer/Track.h"
#include <vector>

namespace vsm::sequencer {

/// Historique portant sur le seul vecteur de notes d'une piste.
///
/// CONSERVÉ, MAIS PLUS UTILISÉ PAR LE DAW : l'application est passée à
/// `ProjectHistory`, qui couvre tout ce que l'utilisateur peut modifier et non
/// les seules notes de la piste affichée. Celui-ci reste comme brique -- il est
/// la même implémentation, à un paramètre de type près -- et parce qu'un
/// historique portant sur une seule liste de notes est exactement ce qu'il faut
/// à un éditeur isolé (un test, un outil hors écran).
using EditHistory = SnapshotHistory<std::vector<Note>>;

} // namespace vsm::sequencer
