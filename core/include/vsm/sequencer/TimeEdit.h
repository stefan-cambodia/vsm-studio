#pragma once
#include "vsm/sequencer/Project.h"
#include <functional>

namespace vsm::sequencer {

/// INSÉRER OU SUPPRIMER UNE PLAGE DE TEMPS SUR TOUT LE MORCEAU (D13.3 de
/// `ROADMAP-daw.md`) — l'outil « Plage » de Cubase, appliqué aux locateurs.
///
/// CE QUI GLISSE ENSEMBLE, parce qu'un morceau est UNE ligne de temps : les
/// notes et les clips de toutes les pistes (et de leurs prises), les points
/// de contrôle MIDI (CC, pitch bend, pressions, programmes, méta-événements),
/// l'automation, les repères, les changements de tempo et de mesure, et les
/// régions de boucle et de punch. Déplacer piste par piste ce que ces
/// fonctions déplacent d'un coup était possible ; le faire sans en oublier
/// une, non.
///
/// CE QUI EST À CHEVAL EST COUPÉ, et c'est la règle de la feuille de route :
///  - à l'INSERTION en T, une note ou un clip qui enjambe T est coupé en
///    deux, et la seconde moitié glisse avec le reste -- le silence inséré
///    est vraiment du silence, pas une note tenue par-dessus ;
///  - à la SUPPRESSION de [de, à), ce qui est dedans disparaît, ce qui
///    enjambe est raccourci de ce qu'il avait dedans, et ce qui suit glisse
///    vers l'avant -- les deux bords se rejoignent.
///
/// Les clips passent par `splitClips`, qui sait couper une fenêtre en
/// secondes et une carte de tempo ; c'est pourquoi `ticksToSeconds` est là.
/// Le changement de tempo ou de mesure AU tick 0 ne bouge jamais.

/// Insère `deltaTicks` de silence à `atTick`. Rend le nombre d'objets
/// déplacés ou coupés.
size_t insertTime(Project& project, midi::Tick atTick, midi::Tick deltaTicks,
                  const std::function<double(midi::Tick)>& ticksToSeconds);

/// Supprime [fromTick, toTick). Rend le nombre d'objets déplacés, coupés ou
/// retirés.
size_t deleteTime(Project& project, midi::Tick fromTick, midi::Tick toTick,
                  const std::function<double(midi::Tick)>& ticksToSeconds);

} // namespace vsm::sequencer
