#pragma once
#include "vsm/sequencer/Project.h"

namespace vsm::sequencer {

/// IMPORTER LES PISTES D'UN AUTRE PROJET DANS CELUI-CI (D14.3) — ce que fait
/// « Fichier ▸ Importer un MIDI dans le projet » : le fichier est lu comme un
/// projet à part (`Project::fromParsedFile`), et ses pistes sont AJOUTÉES à la
/// suite des nôtres, posées à `atTick`.
///
/// CE QUI EST CONVERTI, ET CE QUI EST IGNORÉ, dit ici :
///  - les ticks de la source sont ramenés à la résolution du projet
///    (`ticksPerQuarterNote`), sans quoi un fichier à 960 ppq jouerait deux
///    fois trop lentement dans un projet à 480 ;
///  - tout est décalé pour que le premier événement de la source tombe à
///    `atTick` — un import se pose à la tête de lecture, pas au tick 0 ;
///  - les identifiants de notes et de clips sont NEUFS (deux notes du même
///    identifiant sur deux pistes feraient agir un geste sur l'autre) ;
///  - le TEMPO et les MESURES de la source sont IGNORÉS : le projet a les
///    siens, et un import qui les remplacerait changerait tout ce qui existe
///    déjà. Le nombre de changements ignorés est rendu, pour être dit.
struct ImportOutcome {
    size_t tracksAdded = 0;
    size_t notesAdded = 0;
    size_t tempoChangesIgnored = 0;
    size_t timeSignaturesIgnored = 0;
};

ImportOutcome appendTracksFrom(Project& destination, const Project& source, midi::Tick atTick);

} // namespace vsm::sequencer
