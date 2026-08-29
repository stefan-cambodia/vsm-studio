#pragma once
#include "vsm/audio/effect/IAudioEffect.h"
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
    /// VRAI pour un instrument, faux pour un effet (D7.3). Lu dans les
    /// « features » que le plugin déclare, jamais deviné de son nom : poser un
    /// effet là où une piste attend un instrument -- ou l'inverse -- donnerait
    /// une piste muette qu'il faudrait expliquer à l'oreille.
    bool isInstrument = false;
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

/// Instancie un EFFET d'un fichier `.clap` et l'expose comme insert VSM (D7.3).
/// `pluginId` vide = le premier effet du fichier.
///
/// LA DIFFÉRENCE AVEC UN INSTRUMENT TIENT EN UN MOT : L'ENTRÉE. L'hôte des
/// instruments passe délibérément `audio_inputs = nullptr` ; celui-ci donne au
/// plugin le signal de la piste, et relit ce qu'il en a fait. C'est tout ce que
/// « entrées audio dans l'hôte » veut dire.
///
/// Renvoie nullptr et remplit `outError` si le plugin demandé est un
/// instrument : il ignorerait le signal, et la piste deviendrait muette.
vsm::audio::effect::AudioEffectPtr createClapEffect(const std::string& clapFilePath,
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

// --- D7.1 : les plugins CLAP vus par le reste du projet ---------------------
//
// UN PLUGIN TIERS EST UN IDENTIFIANT COMME UN AUTRE. Une piste désigne son
// instrument par une chaîne (`instrumentId`), écrite telle quelle dans
// `project.json` ; les machines du parc s'appellent `vsm.tb303`. Un plugin
// externe s'appelle donc `clap:<chemin>#<identifiant>`, et rien d'autre dans
// le projet n'a besoin de savoir ce que cette forme veut dire.
//
// LE CHEMIN EST ABSOLU, ET C'EST ASSUMÉ. Ce n'est pas un média du morceau :
// c'est un logiciel installé sur la machine. Le copier dans le dossier de
// projet serait le redistribuer, ce qu'aucune licence ne permet en général. Un
// projet emporté ailleurs signale donc le plugin manquant -- et ne le remplace
// pas, ce que le critère de la phase D7 demande explicitement.

/// Compose l'identifiant d'instrument d'un plugin CLAP.
std::string clapInstrumentId(const std::string& clapFilePath, const std::string& pluginId);

/// L'inverse. Rend faux si l'identifiant n'est pas de cette forme -- auquel cas
/// il désigne une machine du parc, et ne regarde pas cette couche.
bool parseClapInstrumentId(const std::string& instrumentId, std::string& outFilePath,
                            std::string& outPluginId);

/// Branche la couche CLAP dans le registre de machines ET dans la fabrique
/// d'effets : tout identifiant `clap:...` demandé à `PluginRegistry::create` ou
/// à `EffectFactory::create` charge alors le fichier.
///
/// LE MÊME IDENTIFIANT SERT AUX DEUX, et ce n'est pas ambigu : un `.clap` dit
/// lui-même de quoi il est fait, et on ne demande jamais un effet au registre
/// des machines ni l'inverse.
///
/// À APPELER UNE FOIS, au démarrage d'un programme qui veut des plugins tiers
/// (l'application, `vsm-render`). Sans cet appel, rien ne change : les
/// identifiants `clap:` restent introuvables, et l'absence est signalée comme
/// celle de n'importe quelle machine.
void installClapResolver();

} // namespace vsm::clap
