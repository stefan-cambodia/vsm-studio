#pragma once
#include "vsm/interchange/ParameterDescriptor.h"
#include <cstdint>
#include <string>
#include <vector>

// Troisième niveau d'identifiants (docs/ROADMAP-interop.md § 3) : le
// `clap_id`, entier stable exposé aux hôtes CLAP.
//
//     semanticId       filter.1.cutoff        <- ce que Python manipule
//     paramètre VSM    "VCF Cutoff" (ParamId) <- interne à la machine
//     clap_id          1743821004             <- ce que voit l'hôte
//
// POURQUOI UN HACHAGE PLUTÔT QU'UN COMPTEUR : un hôte CLAP mémorise les
// `clap_id` dans SES projets (automations, assignations de contrôleurs).
// Numéroter les paramètres dans l'ordre de déclaration signifierait qu'insérer
// un paramètre au milieu d'une machine décale tous les suivants -- et le
// projet d'un utilisateur se retrouverait à automatiser la résonance à la
// place de la coupure, sans erreur ni avertissement, des mois plus tard. Un
// hachage de l'identifiant sémantique ne dépend, lui, que du SENS du
// paramètre : tant que `filter.1.cutoff` désigne la même chose, son id ne
// bouge pas, quel que soit l'ordre de déclaration ou l'ajout de voisins.
//
// Le prix à payer est le risque de collision, qui doit être VÉRIFIÉ et non
// supposé : un test parcourt les 308 paramètres de toutes les machines et
// refuse la moindre collision.

namespace vsm::interchange {

/// Hachage FNV-1a 64 bits replié sur 31 bits. CLAP transporte les `clap_id`
/// dans un `uint32_t` et réserve la valeur 0xFFFFFFFF (CLAP_INVALID_ID) ;
/// rester sous 2^31 évite aussi les surprises avec les hôtes qui manipulent
/// ces identifiants en entier signé.
uint32_t clapParameterId(const std::string& semanticId);

/// Un paramètre tel que le voit un hôte CLAP.
struct ClapParameterMapping {
    uint32_t clapId = 0;
    std::string semanticId;
    std::string displayName;
    std::string module;
    vsm::audio::plugin::ParamId vsmParamId = 0;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float defaultValue = 0.0f;
};

/// Correspondance complète pour une machine, dans l'ordre de sa
/// `ParameterList` (l'ordre d'affichage attendu par l'hôte).
std::vector<ClapParameterMapping> clapParameterMap(const std::string& pluginId);

/// Identifiant CLAP d'un plugin, dérivé de son identifiant VSM :
/// `vsm.minimoog` -> `com.vsmstudio.minimoog`. Les hôtes s'en servent comme
/// clé de rappel : il doit être stable et unique, jamais recyclé.
std::string clapPluginId(const std::string& vsmPluginId);

} // namespace vsm::interchange
