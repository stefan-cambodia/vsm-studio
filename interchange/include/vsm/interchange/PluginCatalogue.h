#pragma once
#include "vsm/interchange/Json.h"
#include <string>
#include <vector>

// CATALOGUE DES PLUGINS INSTALLÉS (D7.5).
//
// LE PROBLÈME QUE CE FICHIER RÉSOUT N'EST PAS « LISTER DES FICHIERS ». C'est
// que **balayer les plugins d'une machine est dangereux**. Un plugin se balaie
// en le CHARGEANT : on ouvre sa bibliothèque, on l'interroge, on l'exécute donc.
// Un seul plugin mal écrit -- et il y en a -- fait tomber le processus qui l'a
// ouvert. Un balayage naïf transforme donc « l'utilisateur a installé un plugin
// douteux » en « le DAW ne démarre plus », sans le moindre message.
//
// D'OÙ DEUX PROCESSUS. Le balayage se fait dans un processus ENFANT, un fichier
// à la fois. S'il tombe, il tombe seul : le parent le constate, note le fichier
// comme fautif, et passe au suivant. C'est le sens exact de « plugin fautif
// isolé et signalé, jamais fatal » -- et c'est irréalisable dans un seul
// processus, où le premier plugin fautif emporte tout.
//
// CE FICHIER-CI NE CONNAÎT NI PROCESSUS NI FIL D'EXÉCUTION : il porte le
// CATALOGUE (ce qu'on a trouvé, ce qui est fautif) et le PROTOCOLE (comment
// l'enfant le dit au parent). Les deux se vérifient sans lancer quoi que ce
// soit, ce qui est précisément ce qu'on ne peut pas faire du reste.

namespace vsm::interchange {

inline constexpr const char* kPluginCatalogueFormat = "vsm-plugin-catalogue";
inline constexpr int kPluginCatalogueVersion = 1;

struct CataloguedPlugin {
    /// « clap » ou « vst3 ».
    std::string format;
    /// Chemin du fichier sur CETTE machine. Un catalogue ne se transporte pas :
    /// il décrit ce qui est installé ici, et se refait ailleurs.
    std::string path;
    /// Identifiant du plugin DANS son fichier (un fichier peut en contenir
    /// plusieurs).
    std::string id;
    std::string name;
    std::string vendor;
    /// Instrument ou effet. Lu dans ce que le plugin déclare, jamais deviné :
    /// c'est ce qui décide s'il apparaît dans le menu des pistes ou dans celui
    /// des inserts (D7.3).
    bool isInstrument = false;

    /// L'identifiant que `PluginRegistry` et `EffectFactory` savent lire.
    std::string instrumentId() const;
};

/// Un fichier que le balayage n'a pas pu lire, avec la raison. GARDÉ dans le
/// catalogue plutôt qu'oublié : sans cela, chaque balayage retenterait le même
/// plugin fautif, et l'utilisateur n'apprendrait jamais lequel des deux cents
/// fichiers de son disque pose problème.
struct FaultyPlugin {
    std::string path;
    std::string reason;
};

struct PluginCatalogue {
    std::vector<CataloguedPlugin> plugins;
    std::vector<FaultyPlugin> faulty;

    /// Vrai si ce chemin a déjà été balayé -- trouvé OU fautif. C'est ce qui
    /// permet à un second balayage de ne rouvrir que les nouveautés : rouvrir
    /// un plugin qui a déjà fait tomber un processus n'apprendrait rien de plus.
    bool alreadyKnown(const std::string& path) const;

    /// Les instruments, puis les effets, chacun trié par nom -- l'ordre dans
    /// lequel un menu se lit, décidé ici pour que deux vues ne le rangent pas
    /// différemment.
    std::vector<CataloguedPlugin> instruments() const;
    std::vector<CataloguedPlugin> effects() const;
};

JsonValue pluginCatalogueToJson(const PluginCatalogue& catalogue);

struct PluginCatalogueLoadResult {
    bool success = false;
    PluginCatalogue catalogue;
    std::string error;
};

PluginCatalogueLoadResult parsePluginCatalogue(const std::string& jsonText);

// --- Le protocole entre le processus enfant et son parent -------------------
//
// UNE LIGNE PAR PLUGIN TROUVÉ, sur la sortie standard. Du texte et non du
// binaire, pour une raison pratique : quand un balayage se passe mal, on relit
// ce que l'enfant a écrit, et des octets ne se relisent pas. Les champs sont
// séparés par des tabulations, qu'aucun nom de plugin ne contient -- et ceux
// qui en contiendraient les verraient remplacées par des espaces plutôt que de
// couper la ligne en deux.

inline constexpr const char* kScanLinePrefix = "PLUGIN\t";

std::string encodeScanLine(const CataloguedPlugin& plugin);

/// Décode une ligne produite par `encodeScanLine`. Rend faux pour toute autre
/// ligne -- un plugin qui écrirait sur la sortie standard pendant son
/// chargement (cela arrive) ne doit pas se retrouver dans le catalogue.
bool decodeScanLine(const std::string& line, CataloguedPlugin& out);

} // namespace vsm::interchange
