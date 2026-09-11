#pragma once
#include <JuceHeader.h>

namespace vsm::app::ui {

/// D91 : l'identifiant de l'entrée dont le libellé est `libelle` -- exact, sinon
/// son début --, sous-menus compris ; 0 si aucune, ou si elle est grisée : une
/// entrée grisée ne se clique pas, et le banc ne doit pas pouvoir plus que la souris.
///
/// D115 : SORTIE D'`ArrangementComponent.cpp` pour servir aussi le piano roll.
/// Deux copies de cette recherche finiraient par ne plus lire la même chose --
/// et un banc qui trouve une entrée dans un menu et pas dans l'autre mesurerait
/// la copie, pas le menu.
inline int entreeParLibelle(const juce::PopupMenu& menu, const juce::String& libelle) {
    int parDebut = 0;
    for (juce::PopupMenu::MenuItemIterator it(menu, true); it.next();) {
        const auto& item = it.getItem();
        if (item.itemID == 0 || !item.isEnabled) continue;
        if (item.text == libelle) return item.itemID;
        if (parDebut == 0 && item.text.startsWith(libelle)) parDebut = item.itemID;
    }
    return parDebut;
}

} // namespace vsm::app::ui
