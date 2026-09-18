#pragma once
#include <JuceHeader.h>
#include "vsm/interchange/ShortcutTable.h"
#include "Langue.h"   // D358 : toucheLisible

namespace vsm::app::ui {

/// TRADUIRE UNE TOUCHE PRESSÉE EN CE QUE LA TABLE ÉCRIT (D10.3).
///
/// `juce::KeyPress::getTextDescription()` produit déjà « ctrl + S »,
/// « spacebar », « delete ». Deux corrections restent nécessaires, et elles ne
/// sont pas cosmétiques :
///
/// **`command` DEVIENT `ctrl`.** Sous macOS, JUCE écrit « command + S » ; le
/// reste de l'application accepte depuis toujours Ctrl et Cmd indifféremment
/// plutôt que de compiler deux jeux de raccourcis. Une table qui les
/// distinguerait obligerait l'utilisateur d'un Mac à tout reconfigurer.
inline juce::String normalizedKeyDescription(const juce::KeyPress& key) {
    return key.getTextDescription().replace("command + ", "ctrl + ");
}

/// La commande associée à une touche, ou faux.
///
/// **DEUX ESSAIS, ET LE SECOND A UNE RAISON PRÉCISE.** Sur la plupart des
/// dispositions, `+` s'obtient par `Maj` `=` : JUCE rend alors « shift + = »,
/// qui ne ressemble à rien de ce qu'on a écrit dans la table. On réessaie donc
/// sans le `Maj` -- mais SEULEMENT quand il est le seul modificateur. Le faire
/// toujours ferait répondre « Annuler » à Ctrl+Maj+Z, qui est « Rétablir ».
inline bool lookupShortcut(const vsm::interchange::ShortcutTable& table,
                            const juce::KeyPress& key,
                            vsm::interchange::ShortcutId& out) {
    const juce::String description = normalizedKeyDescription(key);
    if (table.commandForKey(description.toStdString(), out)) return true;

    const auto mods = key.getModifiers();
    if (mods.isShiftDown() && !mods.isCtrlDown() && !mods.isCommandDown() && !mods.isAltDown()) {
        const juce::String sansMaj = description.replace("shift + ", "");
        return table.commandForKey(sansMaj.toStdString(), out);
    }
    return false;
}

/// D358 : UN TEXTE SUIVI DE SA TOUCHE EFFECTIVE, pour ce qui n'est pas une entrée
/// de menu -- une infobulle de bouton, qu'aucun `shortcutKeyDescription` ne
/// dessine. Sans table, ou sans touche, le texte revient tel quel : une infobulle
/// muette vaut mieux qu'une fausse.
///
/// `avant` sert aux touches COMPOSÉES que la table ne porte pas telles quelles :
/// « Maj » + la touche de « note douteuse suivante » donne la précédente, et
/// cette composition-là doit suivre le remaniement de la touche de base.
inline juce::String libelleAvecTouche(const juce::String& texte,
                                       const vsm::interchange::ShortcutTable* table,
                                       vsm::interchange::ShortcutId commande,
                                       const juce::String& avant = {}) {
    if (table == nullptr) return texte;
    const juce::String touche = juce::String(table->keyFor(commande));
    if (touche.isEmpty() || !juce::KeyPress::createFromDescription(touche).isValid()) return texte;
    return texte + " (" + avant + toucheLisible(touche) + ")";
}

/// D358 : SORTIE DE `MainComponent.cpp`, OÙ ELLE NE SERVAIT QUE LA BARRE DE
/// MENUS. La règle de D155 y était posée et n'en était jamais sortie : le menu
/// du clip écrivait encore « Couper à la tête de lecture (Ctrl+E) » dans son
/// libellé, et cette parenthèse MENT dès que l'utilisateur change la touche
/// (mesuré : la fenêtre des raccourcis disait Ctrl+Maj+E, le menu Ctrl+E).
/// C'est la leçon de D332, sur les libellés cette fois — une règle posée à un
/// seul endroit ne vaut que là.
///
/// D155 : LA TOUCHE D'UNE ENTRÉE DE MENU, DESSINÉE PAR JUCE ET NON ÉCRITE DANS
/// LE LIBELLÉ.
///
/// CE QUI EXISTAIT, ET CE QUE LA MESURE EN A DIT. Quatre commandes seulement
/// vivent des deux côtés — dans un menu ET dans la table des raccourcis, appariées
/// par l'ACTION qu'elles appellent et non par leur libellé, qui trompe. Trois
/// affichaient leur touche EN DUR dans le texte (« Enregistrer (Ctrl+S) »,
/// « Plein écran (F11) », « … (touche R) ») et la quatrième pas du tout. Écrire
/// la touche dans le libellé a trois conséquences, toutes mesurées : elle part
/// dans la clé de TRADUCTION, elle perd l'alignement à droite que JUCE donne, et
/// le banc (`VSM_MENU=libellé`) doit connaître la parenthèse.
///
/// CE QUE CETTE FONCTION FAIT : elle lit la touche EFFECTIVE — celle de la table
/// de l'utilisateur, pas la valeur d'usine —, si bien qu'un raccourci réassigné
/// s'affiche réassigné. Une commande sans touche rend une chaîne vide, et
/// l'entrée s'affiche comme avant.
inline void ajouterAvecRaccourci(juce::PopupMenu& menu, int identifiant, const juce::String& libelle,
                          const vsm::interchange::ShortcutTable& table,
                          vsm::interchange::ShortcutId commande, bool actif = true,
                          bool coche = false) {
    juce::PopupMenu::Item entree(libelle);
    entree.itemID = identifiant;
    entree.isEnabled = actif;
    entree.isTicked = coche;
    const juce::String touche = juce::String(table.keyFor(commande));
    if (touche.isNotEmpty()) {
        // D157 : la description de JUCE (« ctrl + shift + S ») réécrite dans la
        // langue de l'interface. `getTextDescriptionWithIcons()` rendait la
        // syntaxe brute, qu'aucune autre application n'affiche.
        if (juce::KeyPress::createFromDescription(touche).isValid())
            entree.shortcutKeyDescription = toucheLisible(touche);
    }
    menu.addItem(std::move(entree));
}

} // namespace vsm::app::ui
