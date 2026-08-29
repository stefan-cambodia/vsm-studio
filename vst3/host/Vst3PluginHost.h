#pragma once
#include "vsm/audio/plugin/ISynthPlugin.h"
#include <string>
#include <vector>

// Hôte VST3 (D7.2) : charger un instrument VST3 externe et le faire jouer dans
// VSM.
//
// MÊME FORME QUE L'HÔTE CLAP, ET CE N'EST PAS UNE COÏNCIDENCE. Un plugin
// chargé est présenté au reste du moteur comme un `ISynthPlugin`, exactement
// comme une machine native ; le registre de machines apprend à résoudre une
// forme d'identifiant de plus. `ProcessGraph`, `AudioEngine`, le format de
// projet et le rendu hors ligne n'ont donc RIEN à changer -- et c'est la
// deuxième fois que cette architecture est mise à l'épreuve par une famille de
// machines qu'on n'a pas écrites, ce qui est la seule façon de savoir qu'elle
// tient.
//
// POURQUOI VST3 ALORS QUE CLAP EST PLUS PROPRE. Parce que le marché est en
// VST3. Un hôte qui n'accepterait que le format qu'il préfère n'hébergerait
// personne.
//
// CE QUE CET EN-TÊTE NE MONTRE PAS : JUCE. `juce_audio_processors` fait le
// travail d'hôte, et JUCE 8 embarque le SDK VST3 -- il n'y a donc aucun
// téléchargement de plus que celui de l'application. Mais rien de tout cela ne
// remonte ici : le reste du projet parle à des `ISynthPlugin`, et `audio/`
// continue d'ignorer jusqu'à l'existence de JUCE.

namespace vsm::vst3 {

/// Ce qu'un fichier `.vst3` déclare contenir.
struct Vst3PluginInfo {
    std::string id;      ///< identifiant stable du plugin dans le fichier
    std::string name;
    std::string vendor;
    std::string version;
    /// VRAI pour un instrument, faux pour un effet. La distinction compte dès
    /// maintenant : poser un effet là où une piste attend un instrument
    /// donnerait une piste muette sans rien expliquer. Les effets viendront en
    /// D7.3, quand l'hôte saura leur donner une entrée audio.
    bool isInstrument = false;
};

/// Liste les plugins d'un fichier `.vst3`. Renvoie une liste vide et remplit
/// `outError` si le fichier n'est pas chargeable -- un plugin tiers cassé ne
/// doit jamais faire tomber l'application qui le scanne.
std::vector<Vst3PluginInfo> scanVst3File(const std::string& vst3Path, std::string& outError);

/// Instancie un instrument d'un fichier `.vst3` et l'expose comme machine VSM.
/// `pluginId` vide = le premier instrument du fichier.
/// Renvoie nullptr et remplit `outError` en cas d'échec.
vsm::audio::plugin::SynthPluginPtr createVst3Instrument(const std::string& vst3Path,
                                                         const std::string& pluginId,
                                                         std::string& outError);

// --- Les plugins VST3 vus par le reste du projet ----------------------------
//
// Même convention que CLAP, et pour les mêmes raisons (voir
// `clap/host/ClapPluginHost.h`) : une piste désigne son instrument par une
// chaîne, écrite telle quelle dans `project.json`. Un instrument VST3 s'appelle
// `vst3:<chemin>#<identifiant>`.
//
// LE CHEMIN EST ABSOLU, ET C'EST ASSUMÉ : un plugin est un logiciel installé
// sur la machine, pas un média du morceau. Le copier dans le dossier de projet
// (D6.4) serait le redistribuer.

std::string vst3InstrumentId(const std::string& vst3Path, const std::string& pluginId);

/// L'inverse. Faux si l'identifiant n'est pas de cette forme -- il désigne
/// alors une machine du parc, ou un plugin CLAP, et ne regarde pas cette
/// couche.
bool parseVst3InstrumentId(const std::string& instrumentId, std::string& outPath,
                            std::string& outPluginId);

/// Branche la couche VST3 dans le registre de machines : tout identifiant
/// `vst3:...` demandé à `PluginRegistry::create` charge alors le fichier.
///
/// COHABITE AVEC CELUI DE CLAP. Le registre n'accepte qu'un résolveur ; celui
/// que pose cette fonction essaie d'abord le sien, puis passe la main à celui
/// qui était déjà en place. Poser les deux dans n'importe quel ordre marche
/// donc, ce qui évite une règle d'ordre que personne ne se rappellerait.
void installVst3Resolver();

/// Le nom de format écrit dans `SynthPreset::nativeStateFormat` pour les états
/// produits par cette couche.
inline constexpr const char* kVst3StateFormat = "vst3";

} // namespace vsm::vst3
