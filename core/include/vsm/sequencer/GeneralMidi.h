#pragma once
#include <cstdint>

namespace vsm::sequencer {

/// D307 : LA CONVENTION GENERAL MIDI, LUE AU LIEU D'ÊTRE IGNORÉE.
///
/// Un fichier `.mid` porte des changements de programme (0-127) qui nomment
/// l'instrument voulu par son auteur, et le canal 10 est une batterie. Le
/// parc n'a pas cent vingt-huit machines : chaque programme est rendu par la
/// machine du parc qui lui ressemble le plus (un piano acoustique par
/// `vsm.piano`, une basse synthé par `vsm.tb303`, un pad chaud par
/// `vsm.jupiter8`…). Ce n'est pas une identité, c'est un premier choix
/// AUDIBLE : le musicien qui ouvre un fichier entend son morceau au lieu de
/// seize lignes « (Aucun) », et change ce qu'il veut ensuite.
///
/// Le programme 0 est le défaut de la convention (piano) : une piste sans
/// changement de programme se lit comme si elle en portait un à 0.
struct ProgrammeGM {
    uint8_t numero;        ///< 0-127
    const char* nom;       ///< le nom General MIDI, en anglais (celui des fichiers)
    const char* machine;   ///< l'identifiant de la machine du parc qui le rend
};

/// L'entrée de la table pour un programme (0-127 ; au-delà, ramené à 127).
const ProgrammeGM& programmeGM(uint8_t numero);

/// La machine pour une piste de BATTERIE (canal 10). Le programme y désigne un
/// kit (GM2) : 24-31 sont les kits électroniques -- 25 est le TR-808, les
/// autres le TR-909 --, tout le reste est la batterie acoustique.
const char* machinePourKitGM(uint8_t programme);
/// Le nom du kit (GM2) pour le journal.
const char* nomDuKitGM(uint8_t programme);

} // namespace vsm::sequencer
