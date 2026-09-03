#include "vsm/interchange/ShortcutTable.h"
#include "vsm/interchange/Json.h"
#include <algorithm>

namespace vsm::interchange {

namespace {
constexpr const char* kFormat = "vsm.raccourcis.v1";
}

const std::vector<ShortcutCommand>& shortcutCommands() {
    static const std::vector<ShortcutCommand> catalogue = {
        {ShortcutId::FileSave,          "file.save",        "Fichier",   "Enregistrer",                    "ctrl + S",         ""},
        {ShortcutId::FileSaveAs,        "file.saveAs",      "Fichier",   "Enregistrer sous",               "ctrl + shift + S", ""},
        {ShortcutId::TransportPlayStop, "transport.playStop","Transport","Lecture / arrêt",                "spacebar",         ""},
        {ShortcutId::ReferenceCycle,    "reference.cycle",  "Transport", "Basculer l'écoute A/B",          "R",                ""},

        {ShortcutId::EditUndo,          "edit.undo",        "Édition",   "Annuler",                        "ctrl + Z",         ""},
        {ShortcutId::EditRedo,          "edit.redo",        "Édition",   "Rétablir",                       "ctrl + shift + Z", "ctrl + Y"},
        {ShortcutId::EditSelectAll,     "edit.selectAll",   "Édition",   "Tout sélectionner",              "ctrl + A",         ""},
        {ShortcutId::EditSelectNone,    "edit.selectNone",  "Édition",   "Ne rien sélectionner",           "escape",           ""},
        {ShortcutId::EditInvertSelection,"edit.invertSelection","Édition","Inverser la sélection",         "ctrl + I",         ""},
        {ShortcutId::EditCopy,          "edit.copy",        "Édition",   "Copier",                         "ctrl + C",         ""},
        {ShortcutId::EditCut,           "edit.cut",         "Édition",   "Couper",                         "ctrl + X",         ""},
        {ShortcutId::EditPaste,         "edit.paste",       "Édition",   "Coller",                         "ctrl + V",         ""},
        {ShortcutId::EditDuplicate,     "edit.duplicate",   "Édition",   "Dupliquer",                      "ctrl + D",         ""},
        {ShortcutId::EditDelete,        "edit.delete",      "Édition",   "Supprimer la sélection",         "delete",           "backspace"},
        {ShortcutId::EditLegato,        "edit.legato",      "Édition",   "Legato",                         "ctrl + L",         ""},
        {ShortcutId::EditQuantize,      "edit.quantize",    "Édition",   "Quantifier",                     "ctrl + Q",         ""},
        {ShortcutId::EditToggleMute,    "edit.toggleMute",  "Édition",   "Muet sur la sélection",          "ctrl + M",         ""},
        {ShortcutId::EditJoin,          "edit.join",        "Édition",   "Joindre",                        "ctrl + J",         ""},
        {ShortcutId::EditSplitAtPlayhead,"edit.splitAtPlayhead","Édition","Couper à la tête de lecture",   "ctrl + E",         ""},
        {ShortcutId::EditToggleSnap,    "edit.toggleSnap",  "Édition",   "Aimantation",                    "G",                ""},

        {ShortcutId::ToolSelect,        "tool.select",      "Outils",    "Sélection",                      "1",                ""},
        {ShortcutId::ToolDraw,          "tool.draw",        "Outils",    "Crayon",                         "2",                ""},
        {ShortcutId::ToolErase,         "tool.erase",       "Outils",    "Gomme",                          "3",                ""},
        {ShortcutId::ToolSplit,         "tool.split",       "Outils",    "Ciseaux",                        "4",                ""},
        {ShortcutId::ToolGlue,          "tool.glue",        "Outils",    "Colle",                          "5",                ""},
        {ShortcutId::ToolMute,          "tool.mute",        "Outils",    "Muet",                           "6",                ""},

        {ShortcutId::ViewZoomToFit,     "view.zoomToFit",   "Affichage", "Ajuster à la fenêtre",           "ctrl + 0",         ""},
        {ShortcutId::ViewZoomIn,        "view.zoomIn",      "Affichage", "Zoom avant",                     "=",                "+"},
        {ShortcutId::ViewZoomOut,       "view.zoomOut",     "Affichage", "Zoom arrière",                   "-",                "_"},
        {ShortcutId::NavNextDoubtful,   "nav.nextDoubtful", "Affichage", "Note douteuse suivante (Maj : précédente)", "D",     ""},
        // D11.3 — se repérer en musique : le début, et les marqueurs.
        {ShortcutId::NavGoToStart,      "nav.goToStart",    "Transport", "Retour au début",                "home",             ""},
        {ShortcutId::NavNextMarker,     "nav.nextMarker",   "Transport", "Marqueur suivant",               "shift + N",        ""},
        {ShortcutId::NavPreviousMarker, "nav.previousMarker","Transport","Marqueur précédent",             "shift + B",        ""},
        {ShortcutId::ViewFullScreen,    "view.fullScreen",  "Affichage", "Plein écran",                    "F11",              ""},
    };
    return catalogue;
}

const std::vector<FixedShortcut>& fixedShortcuts() {
    static const std::vector<FixedShortcut> fixes = {
        {"←  →", "Navigation", "Déplacer la sélection (Maj : par quatre pas) ; sans sélection, faire défiler"},
        {"↑  ↓", "Navigation", "Transposer d'un demi-ton (Maj : d'une octave) ; sans sélection, faire défiler"},
    };
    return fixes;
}

const ShortcutCommand* findShortcutCommand(ShortcutId id) {
    for (const auto& commande : shortcutCommands())
        if (commande.id == id) return &commande;
    return nullptr;
}

const ShortcutCommand* findShortcutCommandByKey(const std::string& key) {
    for (const auto& commande : shortcutCommands())
        if (key == commande.key) return &commande;
    return nullptr;
}

std::string ShortcutTable::keyFor(ShortcutId id) const {
    const auto* commande = findShortcutCommand(id);
    if (commande == nullptr) return {};
    const auto it = overrides_.find(commande->key);
    return it != overrides_.end() ? it->second : std::string(commande->defaultKey);
}

bool ShortcutTable::isCustom(ShortcutId id) const {
    const auto* commande = findShortcutCommand(id);
    return commande != nullptr && overrides_.count(commande->key) > 0;
}

void ShortcutTable::setKey(ShortcutId id, const std::string& key) {
    const auto* commande = findShortcutCommand(id);
    if (commande == nullptr) return;
    // Réassigner la touche PAR DÉFAUT n'est pas une personnalisation : c'est un
    // retour à la normale, et l'écrire dans les préférences ferait grossir le
    // fichier de réglages qui n'en sont pas.
    if (key == commande->defaultKey) { overrides_.erase(commande->key); return; }
    overrides_[commande->key] = key;
}

void ShortcutTable::reset(ShortcutId id) {
    const auto* commande = findShortcutCommand(id);
    if (commande != nullptr) overrides_.erase(commande->key);
}

bool ShortcutTable::commandForKey(const std::string& key, ShortcutId& out) const {
    if (key.empty()) return false;
    for (const auto& commande : shortcutCommands()) {
        if (keyFor(commande.id) == key) { out = commande.id; return true; }
        // L'ALIAS NE SUIT PAS LA PERSONNALISATION : il existe pour ne pas
        // surprendre (Retour arrière supprime, Ctrl+Y rétablit), pas pour être
        // un second raccourci à gérer. Il ne vaut que tant que la commande a
        // gardé sa touche d'origine ; sinon, réassigner « Supprimer » à F1
        // laisserait Retour arrière effacer encore, sans que rien le dise.
        if (*commande.alias != '\0' && !isCustom(commande.id) && key == commande.alias) {
            out = commande.id;
            return true;
        }
    }
    return false;
}

std::vector<ShortcutId> ShortcutTable::conflictsFor(const std::string& key,
                                                     ShortcutId except) const {
    std::vector<ShortcutId> trouves;
    if (key.empty()) return trouves;
    for (const auto& commande : shortcutCommands()) {
        if (commande.id == except) continue;
        if (keyFor(commande.id) == key) trouves.push_back(commande.id);
    }
    return trouves;
}

std::string shortcutTableToJson(const ShortcutTable& table) {
    JsonValue racine = JsonValue::makeObject();
    racine.set("format", JsonValue::makeString(kFormat));
    JsonValue objets = JsonValue::makeObject();
    for (const auto& [identifiant, touche] : table.overrides())
        objets.set(identifiant, JsonValue::makeString(touche));
    racine.set("overrides", std::move(objets));
    return racine.toString(2);
}

bool shortcutTableFromJson(const std::string& text, ShortcutTable& out) {
    out.resetAll();
    if (text.empty()) return true;   // rien de personnalisé : ce n'est pas une panne
    const JsonParseResult analyse = parseJson(text);
    if (!analyse.success || !analyse.value.isObject()) return false;
    if (analyse.value["format"].asString() != kFormat) return false;

    for (const auto& [identifiant, valeur] : analyse.value["overrides"].members()) {
        // UNE COMMANDE INCONNUE EST IGNORÉE : le fichier peut venir d'une
        // version qui en avait davantage. La garder ne servirait à rien et la
        // réécrire la ferait survivre indéfiniment.
        const auto* commande = findShortcutCommandByKey(identifiant);
        if (commande == nullptr || !valeur.isString()) continue;
        out.setKey(commande->id, valeur.asString());
    }
    return true;
}

std::string shortcutTableToPrintableText(const ShortcutTable& table) {
    std::string texte = "RACCOURCIS CLAVIER — Vintage Synth MIDI Studio\n";
    texte += std::string(64, '=') + "\n";

    std::string familleCourante;
    auto ligne = [&texte](const std::string& touche, const std::string& libelle,
                           const std::string& note) {
        std::string gauche = touche;
        // Les colonnes sont alignées à la main : c'est un fichier texte, il
        // sera lu dans une police à chasse fixe ou imprimé tel quel.
        while (gauche.size() < 22) gauche += ' ';
        texte += "  " + gauche + libelle;
        if (!note.empty()) texte += "   (" + note + ")";
        texte += "\n";
    };

    for (const auto& commande : shortcutCommands()) {
        if (commande.category != familleCourante) {
            familleCourante = commande.category;
            texte += "\n" + familleCourante + "\n" + std::string(familleCourante.size(), '-') + "\n";
        }
        const std::string touche = table.keyFor(commande.id);
        std::string note;
        if (table.isCustom(commande.id)) note = "modifié, défaut : " + std::string(commande.defaultKey);
        else if (*commande.alias != '\0') note = "ou " + std::string(commande.alias);
        ligne(touche.empty() ? std::string("(désactivé)") : touche, commande.label, note);
    }

    texte += "\nNavigation (non modifiable)\n";
    texte += std::string(26, '-') + "\n";
    for (const auto& fixe : fixedShortcuts()) ligne(fixe.keys, fixe.label, {});
    return texte;
}

} // namespace vsm::interchange
