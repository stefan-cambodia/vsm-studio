#pragma once
#include "vsm/audio/plugin/ISynthPlugin.h"
#include <memory>
#include <string>
#include <vector>

// Hôte CLAP (Phase 7, P6) : charger un plugin CLAP externe et le faire jouer
// dans VSM.
//
// L'IDÉE DIRECTRICE : un plugin CLAP chargé est présenté au reste du moteur
// comme un `ISynthPlugin`, exactement comme une machine native. `ProcessGraph`,
// `AudioEngine`, le Synth Rack et le piano roll n'ont donc RIEN à changer pour
// accepter des instruments tiers -- c'est la garantie « ajouter une machine ne
// touche ni le moteur ni l'UI » (section 22), tenue jusqu'au bout : elle vaut
// aussi pour les machines qu'on n'a pas écrites.
//
// Cet en-tête n'inclut délibérément AUCUN en-tête CLAP : le SDK reste confiné
// au .cpp. Le reste du projet peut donc parler à des plugins CLAP sans que
// quoi que ce soit d'autre ne dépende du SDK.

namespace vsm::clap {

/// Ce qu'un fichier `.clap` déclare contenir.
struct ClapPluginInfo {
    std::string id;      ///< identifiant stable, ex. "com.vsmstudio.minimoog"
    std::string name;
    std::string vendor;
    std::string version;
};

/// Liste les plugins contenus dans un fichier `.clap` (un fichier peut en
/// contenir plusieurs). Renvoie une liste vide et remplit `outError` si le
/// fichier n'est pas chargeable -- un plugin tiers cassé ne doit jamais faire
/// tomber l'application qui le scanne.
std::vector<ClapPluginInfo> scanClapFile(const std::string& clapFilePath, std::string& outError);

/// Instancie un plugin d'un fichier `.clap` et l'expose comme instrument VSM.
/// `pluginId` vide = le premier plugin du fichier.
/// Renvoie nullptr et remplit `outError` en cas d'échec.
///
/// L'objet retourné garde le module chargé en vie aussi longtemps qu'il
/// existe : le libérer décharge la bibliothèque.
vsm::audio::plugin::SynthPluginPtr createClapInstrument(const std::string& clapFilePath,
                                                         const std::string& pluginId,
                                                         std::string& outError);

/// Lit l'ÉTAT NATIF d'un plugin CLAP chargé par `createClapInstrument`.
///
/// À SAVOIR, et c'est une décision de l'adaptateur, pas un hasard : cet état
/// est un preset SÉMANTIQUE (`*.synth.json`), et non une table d'identifiants
/// internes. Un projet d'hôte enregistré aujourd'hui reste donc lisible même
/// si les identifiants internes d'une machine changent, et reste inspectable à
/// la main.
///
/// La conséquence pratique compte : le fichier écrit par la chaîne d'analyse
/// dans `instruments/track_NN.synth.json` EST déjà un état CLAP valide. Il n'y
/// a pas de second format à produire, et un dossier `states/` qui reprendrait
/// les mêmes octets sous un autre nom ne serait que de la duplication.
///
/// Renvoie false et remplit `outError` si l'objet n'est pas un plugin CLAP ou
/// s'il n'expose pas l'extension d'état.
bool saveClapState(vsm::audio::plugin::ISynthPlugin& instrument,
                    std::string& outText, std::string& outError);

/// Restaure un état natif (un `*.synth.json`) dans un plugin CLAP chargé.
bool loadClapState(vsm::audio::plugin::ISynthPlugin& instrument,
                    const std::string& text, std::string& outError);

} // namespace vsm::clap
