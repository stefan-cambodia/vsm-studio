#pragma once
#include "vsm/audio/plugin/ParameterTypes.h"
#include <optional>
#include <string>
#include <vector>

// Identité SÉMANTIQUE des paramètres (Phase 7, P2 -- docs/ROADMAP-interop.md § 3).
//
// LE PROBLÈME QU'ELLE RÉSOUT : un projet Python qui veut « ouvrir le filtre »
// ne peut pas connaître les identifiants internes de chaque machine, ni leurs
// libellés d'affichage -- le Minimoog dit « Filter Cutoff », le Juno « VCF
// Cutoff », le MS-20 « LPF Cutoff », et un renommage d'interface casserait
// tout. Trois niveaux d'identifiants cohabitent donc :
//
//     semanticId          filter.1.cutoff        <- ce que Python manipule
//          v
//     paramètre VSM       "VCF Cutoff" (ParamId) <- interne à la machine
//          v
//     futur clap_id       2001                   <- stable pour l'interop CLAP
//
// Python ne travaille QUE sur le semanticId ; la correspondance vit ici, dans
// la couche interop, et JAMAIS dans le DSP : `audio/` n'a pas à connaître le
// vocabulaire d'interopérabilité pour produire du son, et ce vocabulaire doit
// pouvoir évoluer sans recompiler ni reregresser douze machines.

namespace vsm::interchange {

/// D'où vient la valeur d'un paramètre par rapport au matériel d'origine.
/// Une estimation n'est JAMAIS présentée comme une mesure (roadmap § 5).
enum class Fidelity { Measured, Derived, Estimated, Approximated, Unknown };

/// Ce que la machine sait faire du paramètre sémantique demandé.
/// Une approximation n'est jamais silencieuse : elle se lit dans le rapport
/// d'import.
enum class SupportStatus { Supported, Approximated, Unsupported };

const char* fidelityName(Fidelity fidelity);
const char* supportStatusName(SupportStatus status);

/// Description complète d'un paramètre, prête à être sérialisée.
struct ParameterDescriptor {
    std::string semanticId;   ///< ex. "filter.1.cutoff"
    std::string displayName;  ///< ex. "VCF Cutoff" (tel que l'affiche la machine)
    std::string module;       ///< ex. "filter" (premier segment du semanticId)
    vsm::audio::plugin::ParamId paramId = 0;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float defaultValue = 0.0f;
    std::string unit;         ///< "Hz", "s", "st", "" ...
    Fidelity fidelity = Fidelity::Unknown;
};

/// Table sémantique d'UNE machine (ou d'un effet) : la liste de ses
/// paramètres, décrits à partir de sa `ParameterList` réelle.
class SemanticProfile {
public:
    SemanticProfile() = default;
    explicit SemanticProfile(std::string pluginId, std::vector<ParameterDescriptor> parameters)
        : pluginId_(std::move(pluginId)), parameters_(std::move(parameters)) {}

    const std::string& pluginId() const { return pluginId_; }
    const std::vector<ParameterDescriptor>& parameters() const { return parameters_; }

    const ParameterDescriptor* findBySemanticId(const std::string& semanticId) const;
    const ParameterDescriptor* findByParamId(vsm::audio::plugin::ParamId id) const;
    bool empty() const { return parameters_.empty(); }

private:
    std::string pluginId_;
    std::vector<ParameterDescriptor> parameters_;
};

/// Construit le profil sémantique d'une machine enregistrée (`vsm.minimoog`,
/// `vsm.dx7`...) ou d'un effet (`fx.reverb`...) en croisant sa
/// `ParameterList` réelle avec la table de correspondance du projet.
/// Renvoie un profil vide si l'identifiant est inconnu.
SemanticProfile buildSemanticProfile(const std::string& pluginId);

/// Tous les identifiants pour lesquels une table existe (machines et effets).
std::vector<std::string> knownSemanticPluginIds();

/// Le semanticId correspondant à un nom de paramètre pour une machine donnée,
/// ou une chaîne vide si la table n'en déclare pas. Exposé pour les tests de
/// complétude : aucun paramètre d'aucune machine ne doit rester sans identité.
std::string lookupSemanticId(const std::string& pluginId, const std::string& parameterName);

} // namespace vsm::interchange
