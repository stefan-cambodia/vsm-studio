#pragma once
#include "vsm/sequencer/Project.h"
#include <cstddef>
#include <string>

// D535.2 de docs/ROADMAP-daw.md — LES INSTANTANÉS DE LA CONSOLE (les *MixConsole Snapshots* de
// Cubase) : garder l'état du mixage sous un nom, et y revenir.
//
// L'état est porté PAR LA PISTE (`Track::mixSnapshots`) et le projet garde l'ordre des noms
// (`Project::mixSnapshotNames`). Ces fonctions ne font aucun pas d'historique : c'est
// l'application qui l'ouvre, et seulement si `previewMixRecall` dit que le rappel change quelque
// chose (D511).

namespace vsm::sequencer {

/// L'état de console d'une piste, tel que sa tranche le montre.
TrackMixState captureTrackMix(const Track& track);

bool hasMixSnapshot(const Project& project, const std::string& name);

/// `prefixe` + N, le premier N libre à partir du nombre d'instantanés + 1. Le préfixe vient de
/// l'interface (« Instantané », « Snapshot ») : un nom proposé est un texte qu'on lit.
std::string nextMixSnapshotName(const Project& project, const std::string& prefixe);

/// PRENDRE : chaque piste reçoit son état sous `name`. Un nom déjà pris est REMPLACÉ, à sa place
/// dans la liste — c'est à l'application de l'avoir demandé avant. Rend vrai s'il l'était.
bool takeMixSnapshot(Project& project, const std::string& name);

struct MixRecallReport {
    bool found = false;           ///< le nom existe
    size_t recalled = 0;          ///< pistes qui ont un état sous ce nom
    size_t withoutState = 0;      ///< pistes ajoutées après la prise : laissées
    size_t insertsLeft = 0;       ///< inserts ajoutés, retirés ou remplacés depuis : laissés
    size_t changed = 0;           ///< pistes dont la console CHANGE au rappel
};

/// Ce que ferait le rappel, sans rien toucher : l'application en tire le pas (ou son absence).
MixRecallReport previewMixRecall(const Project& project, const std::string& name);

/// RAPPELER : chaque piste qui a un état sous `name` le reprend ; les autres restent telles
/// quelles. Les inserts s'apparient par rang ET par type.
MixRecallReport recallMixSnapshot(Project& project, const std::string& name);

/// SUPPRIMER : le nom et l'état de chaque piste. Faux si le nom n'existe pas.
bool removeMixSnapshot(Project& project, const std::string& name);

/// RETIRER LE BUS DE DÉPART `bus` DES PISTES ET DES INSTANTANÉS : le rang est effacé partout,
/// pour que le départ gardé pour le bus suivant revienne sur CE bus-là au rappel. L'application
/// retire le bus de `Project::sends` elle-même.
void eraseSendLevelEverywhere(Project& project, size_t bus);

/// D559.1 — LEVER TOUS LES SOLOS, TOUS LES MUETS (*Deactivate All Solo / Mute* de Cubase). « Plus aucun solo » (D21.2)
/// n'est que le solo exclusif relâché ; ceci lève celui de TOUTES les pistes — groupes et VCA compris. Le solo PROTÉGÉ
/// (`soloSafe`, D30.1) n'est pas un solo : il reste. Les deux rendent combien de pistes ont perdu le leur.
size_t countSoloedTracks(const Project& project);
size_t countMutedTracks(const Project& project);
size_t clearAllSolos(Project& project);
size_t clearAllMutes(Project& project);

/// D559.2 — REMETTRE LA CONSOLE À ZÉRO (*Reset Mixer* de Cubase) : chaque tranche de piste revient à l'état d'une piste
/// neuve — volume 0 dB, panoramique au centre, ni muet ni solo, trim 0, polarité normale, départs à zéro (le vecteur
/// garde sa taille). Les INSERTS restent, et leur état : retirer une chaîne d'effets est un autre geste. Les bus de
/// départ et le master ne sont pas des tranches de piste et ne bougent pas. Rend le nombre de tranches CHANGÉES ;
/// `countTracksToReset` le même compte sans rien toucher (le libellé du menu).
size_t countTracksToReset(const Project& project);
size_t resetConsole(Project& project);

} // namespace vsm::sequencer
