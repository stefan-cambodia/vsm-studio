#pragma once
#include <cstddef>
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

/// D309 : LE PROFIL CANONIQUE d'un programme, quand l'installateur de banques
/// (`tools/installer-banques-midi.py`) en fabrique un -- « Grand-Piano » pour
/// 0, « Synth-Bass-1 » pour 38, « Warm-Pad » pour 89 ; `nullptr` pour les 83
/// programmes que la liste canonique ne couvre pas. Le fichier installé
/// s'appelle `<PRÉFIXE>-<profil>.synth.json` dans le dossier des profils.
const char* profilCanoniqueGM(uint8_t programme);
/// Les banques, dans l'ordre où on les préfère : préfixe de fichier et nom.
struct BanqueGM { const char* prefixe; const char* nom; };
const BanqueGM* banquesGM(std::size_t& compte);

/// D312 : LA TABLE À L'ENVERS -- le programme General MIDI qui désigne une
/// machine du parc, pour qu'un fichier EXPORTÉ dise ses instruments. C'est le
/// plus petit programme dont la machine est celle-là (piano → 0, TB-303 → 38,
/// Jupiter-8 → 63) ; -1 pour une machine qu'aucun programme ne désigne
/// (vielle, clavicorde, générique…). Une boîte à rythmes a son kit :
/// `kitGMPourMachine` -- drums → 0, TR-808 → 25, TR-909 → 24 ; -1 sinon.
int programmeGMPourMachine(const char* machine);
int kitGMPourMachine(const char* machine);

} // namespace vsm::sequencer
