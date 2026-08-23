#pragma once
#include "vsm/audio/plugin/IMultisampleBank.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/interchange/Json.h"
#include <string>
#include <vector>

// Format de PROFIL multi-échantillons `*.profile.json` (docs/CDC-multisample.md).
//
// POURQUOI IL VIT ICI ET PAS DANS `audio/`. L'invariant du projet est net : le
// moteur ne connaît aucun format d'échange, ni JSON, ni rien qui vienne de la
// couche d'interopérabilité (feuille de route § 8, ROADMAP-interop § 0). La
// machine `vsm.multisample` reçoit donc des ZONES, structures nues décrites par
// `IMultisampleBank`, et n'a jamais entendu parler de ce fichier. Ce n'est pas
// une élégance gratuite : c'est ce qui permet de changer le format -- d'y
// ajouter les round-robin, les échantillons de relâchement, un jour le SFZ --
// sans recompiler ni reregresser une seule machine.
//
// CE QUE LE FORMAT GARANTIT, et qui est vérifié à la lecture :
//
//   - `format` et `version` sont contrôlés. Un fichier inconnu est REFUSÉ, pas
//     lu au mieux : une zone mal interprétée ferait jouer le mauvais
//     échantillon sur la moitié du clavier, sans le moindre message.
//   - les chemins d'échantillons sont RELATIFS, obligatoirement, et résolus
//     par rapport au dossier du profil. Un chemin absolu rendrait le profil
//     non transportable : il s'ouvrirait sur la machine qui l'a produit et
//     nulle part ailleurs.
//   - l'attribution est OBLIGATOIRE (§ 28 d'ARCHITECTURE.md). Une banque dont
//     on ne sait pas sous quelle licence elle circule ne se charge pas « en
//     attendant de vérifier ».

namespace vsm::interchange {

inline constexpr const char* kMultisampleProfileFormat = "vsm-multisample-profile";
inline constexpr int kMultisampleProfileVersion = 1;

struct MultisampleProfileLoadResult {
    bool success = false;
    vsm::audio::plugin::MultisampleProfileSpec spec;
    std::string error;
    /// Champs présents dans le fichier mais NON pris en charge par cette
    /// version, nommés un par un. Jamais un à-peu-près silencieux : le lecteur
    /// dit ce qu'il a ignoré, comme le fera l'import SoundFont.
    std::vector<std::string> ignored;
};

/// Lit un profil depuis du texte JSON. `baseFolder` est le dossier auquel les
/// chemins relatifs se rapportent -- normalement celui du fichier de profil.
MultisampleProfileLoadResult parseMultisampleProfile(const std::string& jsonText,
                                                      const std::string& baseFolder,
                                                      const std::string& sourcePath = {});

/// Lit un profil depuis un fichier. FAIT DES ENTRÉES/SORTIES : à n'appeler ni
/// depuis le thread audio, ni pendant la lecture.
MultisampleProfileLoadResult loadMultisampleProfileFile(const std::string& path);

/// Écrit un profil au format. Sert à l'outil d'installation de banque, qui
/// produit le fichier plutôt que de le faire écrire à la main.
JsonValue multisampleProfileToJson(const vsm::audio::plugin::MultisampleProfileSpec& spec,
                                    const std::string& baseFolder);

/// Dossier où les profils s'installent.
///
/// AUCUNE BANQUE DANS LE DÉPÔT (§ 9 du cahier des charges) : les profils vivent
/// donc chez l'utilisateur, et le dépôt reste léger et compilable hors ligne.
/// La variable d'environnement `VSM_PROFILS` prime -- c'est elle qui permet aux
/// tests et à la chaîne d'analyse de travailler sur un dossier à eux sans
/// toucher à l'installation réelle.
std::string multisampleProfileFolder();

/// Un profil installé, tel que la découverte le voit.
struct InstalledProfile {
    std::string path;        ///< chemin complet du `*.profile.json`
    std::string name;        ///< nom déclaré dans le fichier
    std::string attribution;
    int zoneCount = 0;
    std::string error;       ///< non vide = fichier présent mais illisible
};

/// Profils présents dans le dossier d'installation, triés par nom de fichier.
///
/// Les fichiers ILLISIBLES sont rendus eux aussi, avec leur erreur : un profil
/// cassé doit se voir dans la liste plutôt que disparaître, sans quoi
/// l'utilisateur cherche pourquoi sa banque « n'est pas installée » alors
/// qu'elle l'est et qu'elle est fautive.
std::vector<InstalledProfile> installedMultisampleProfiles();

/// Ce qui s'est passé à l'installation d'un profil dans une machine.
struct MultisampleProfileApplyReport {
    bool applied = false;
    std::string profileName;
    int zoneCount = 0;
    size_t memoryBytes = 0;
    std::vector<std::string> ignored;
    /// Vide si tout s'est bien passé. JAMAIS silencieux : une machine muette
    /// sans message est le genre de panne qu'on cherche pendant une heure.
    std::string error;

    std::string summary() const;
};

/// Charge le profil désigné dans la machine, si elle en accepte un. Ne fait
/// rien -- sans erreur -- si la machine n'est pas un lecteur de profils.
/// `cache` peut être nul. Un appelant qui installe le même profil des
/// centaines de fois -- le service de rendu, qui crée une instance de machine
/// par requête -- doit en fournir un : sans lui, chaque installation redécode
/// la banque entière (mesuré à 124 ms contre 4 ms pour un soustractif).
MultisampleProfileApplyReport applyMultisampleProfile(vsm::audio::plugin::ISynthPlugin& plugin,
                                                       const std::string& profilePath,
                                                       vsm::audio::plugin::MultisampleSampleCache* cache = nullptr);

} // namespace vsm::interchange
