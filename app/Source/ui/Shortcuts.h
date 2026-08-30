#pragma once
#include <JuceHeader.h>
#include "vsm/interchange/ShortcutTable.h"

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

} // namespace vsm::app::ui
