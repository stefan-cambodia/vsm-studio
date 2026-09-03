#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace vsm::interchange {

/// LES RACCOURCIS CLAVIER, EN UN SEUL ENDROIT (D10.3).
///
/// **CE QUI EXISTAIT** : deux `switch` sur des codes de touches, l'un dans
/// `MainComponent`, l'autre dans le piano roll, plus une poignée d'alias. Rien
/// ne les listait. La seule façon de savoir ce que faisait une touche était de
/// l'essayer, et la seule façon de savoir quelles touches faisaient quelque
/// chose était de lire deux fichiers de code. C'est ce que « une page les liste
/// tous » corrige.
///
/// **LE CATALOGUE EST LA SOURCE, LES `switch` SONT DES CONSÉQUENCES.** Chaque
/// commande est déclarée ici une fois, avec son libellé, sa famille et sa
/// touche par défaut ; l'application ne fait plus que demander « quelle
/// commande pour cette touche ? ». Un raccourci qu'on ajouterait dans le code
/// sans le déclarer ici n'apparaîtrait pas dans la page — et c'est exactement
/// pour cela que le code ne doit plus les connaître autrement.
enum class ShortcutId : uint16_t {
    FileSave = 0,
    FileSaveAs,
    TransportPlayStop,
    ReferenceCycle,

    EditUndo,
    EditRedo,
    EditSelectAll,
    EditSelectNone,
    EditInvertSelection,
    EditCopy,
    EditCut,
    EditPaste,
    EditDuplicate,
    EditDelete,
    EditLegato,
    EditQuantize,
    EditToggleMute,
    EditJoin,
    EditSplitAtPlayhead,
    EditToggleSnap,

    ToolSelect,
    ToolDraw,
    ToolErase,
    ToolSplit,
    ToolGlue,
    ToolMute,

    ViewZoomToFit,
    ViewZoomIn,
    ViewZoomOut,
    NavNextDoubtful,
    NavGoToStart,
    NavNextMarker,
    NavPreviousMarker,
    ViewFullScreen,

    Count
};

struct ShortcutCommand {
    ShortcutId id = ShortcutId::FileSave;
    /// Identifiant STABLE, écrit dans les préférences. Le numéro de l'enum ne
    /// l'est pas : insérer une commande au milieu ferait glisser tous les
    /// raccourcis déjà personnalisés d'un cran, en silence.
    const char* key = "";
    const char* category = "";
    const char* label = "";
    /// La touche par défaut, dans l'écriture de JUCE (« ctrl + S », « spacebar »).
    /// Cette couche ne l'interprète pas : elle la transporte. C'est
    /// l'application, qui a JUCE, qui sait la lire et la comparer.
    const char* defaultKey = "";
    /// Une touche de plus qui fait la même chose, quand l'usage l'impose
    /// (Retour arrière pour Supprimer, Ctrl+Y pour Rétablir). Elle n'est pas
    /// modifiable et n'est là que pour ne pas surprendre.
    const char* alias = "";
};

/// Le catalogue complet, dans l'ordre d'affichage.
const std::vector<ShortcutCommand>& shortcutCommands();
const ShortcutCommand* findShortcutCommand(ShortcutId id);
const ShortcutCommand* findShortcutCommandByKey(const std::string& key);

/// LES TOUCHES QUI NE SE RECONFIGURENT PAS, ET POURQUOI ELLES SONT ÉCRITES
/// PLUTÔT QU'OMISES. Les flèches déplacent la sélection, `Maj` en quadruple le
/// pas ; leur sens EST leur direction, et les réassigner produirait une flèche
/// gauche qui monte. Elles figurent donc dans la page, marquées comme fixes :
/// une page qui prétend tout lister et tait quatre touches ment davantage
/// qu'une page qui dit « celles-ci ne bougent pas ».
struct FixedShortcut { const char* keys; const char* category; const char* label; };
const std::vector<FixedShortcut>& fixedShortcuts();

/// Les raccourcis effectifs : les défauts, plus ce que l'utilisateur a changé.
class ShortcutTable {
public:
    /// La touche d'une commande : celle de l'utilisateur si elle existe, celle
    /// du catalogue sinon.
    std::string keyFor(ShortcutId id) const;
    /// L'utilisateur a-t-il changé celle-ci ?
    bool isCustom(ShortcutId id) const;
    /// Réassigne. Une chaîne vide DÉSACTIVE la commande -- c'est un choix
    /// possible, et le taire obligerait à inventer une touche pour se
    /// débarrasser d'un raccourci gênant.
    void setKey(ShortcutId id, const std::string& key);
    void reset(ShortcutId id);
    void resetAll() { overrides_.clear(); }

    /// La commande à laquelle une touche est associée, ou nullptr. Les alias
    /// comptent. `Count` n'est jamais rendu.
    bool commandForKey(const std::string& key, ShortcutId& out) const;

    /// Les commandes qui portent DÉJÀ cette touche. Réassigner sans le dire
    /// produirait deux commandes sur la même touche, dont une seule
    /// répondrait -- et rien n'expliquerait laquelle.
    std::vector<ShortcutId> conflictsFor(const std::string& key, ShortcutId except) const;

    const std::map<std::string, std::string>& overrides() const { return overrides_; }

private:
    std::map<std::string, std::string> overrides_;   ///< identifiant stable -> touche
};

std::string shortcutTableToJson(const ShortcutTable& table);
/// Un texte vide rend une table par défaut, et c'est un succès : au premier
/// lancement rien n'a été personnalisé.
bool shortcutTableFromJson(const std::string& text, ShortcutTable& out);

/// LA TABLE IMPRIMABLE. Du texte, et non une capture d'écran : on l'imprime, on
/// la colle au mur du studio, on la cherche avec Ctrl+F. C'est aussi ce qui
/// permet de la produire sans afficher quoi que ce soit -- donc de la tester.
std::string shortcutTableToPrintableText(const ShortcutTable& table);

} // namespace vsm::interchange
