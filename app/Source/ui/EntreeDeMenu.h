#pragma once
#include <JuceHeader.h>
#include <cstdio>

namespace vsm::app::ui {

/// D91 : l'identifiant de l'entrée dont le libellé est `libelle` -- exact, sinon
/// son début --, sous-menus compris ; 0 si aucune, ou si elle est grisée : une
/// entrée grisée ne se clique pas, et le banc ne doit pas pouvoir plus que la souris.
///
/// D115 : SORTIE D'`ArrangementComponent.cpp` pour servir aussi le piano roll.
/// Deux copies de cette recherche finiraient par ne plus lire la même chose --
/// et un banc qui trouve une entrée dans un menu et pas dans l'autre mesurerait
/// la copie, pas le menu.
/// D354 : UNE ENTRÉE GRISÉE N'EST PAS UNE ENTRÉE ABSENTE, et le journal disait
/// la seconde pour la première. « Quantifier (100 %) » demandée sans sélection
/// rendait « aucune entrée « Quantifier (100 %) » dans le menu pianoroll » :
/// exact au mot près, faux au sens. L'entrée est là, la souris la voit, elle
/// est simplement grisée faute de sélection — et une course qui lit « aucune
/// entrée » cherche une faute d'orthographe pendant que la cause est ailleurs.
/// C'est la panne muette de D147, sous une autre forme. Le refus est donc dit
/// ICI, au seul endroit qui connaît les deux états, pour les quatre menus qui
/// s'en servent — écrit dans chaque appelant, il finirait par diverger.
inline int entreeParLibelle(const juce::PopupMenu& menu, const juce::String& libelle) {
    int parDebut = 0;
    juce::String griseeVue;
    for (juce::PopupMenu::MenuItemIterator it(menu, true); it.next();) {
        const auto& item = it.getItem();
        if (item.itemID == 0) continue;
        if (!item.isEnabled) {
            if (griseeVue.isEmpty() && (item.text == libelle || item.text.startsWith(libelle)))
                griseeVue = item.text;
            continue;
        }
        if (item.text == libelle) return item.itemID;
        if (parDebut == 0 && item.text.startsWith(libelle)) parDebut = item.itemID;
    }
    if (parDebut == 0 && griseeVue.isNotEmpty())
        std::fputs((juce::String::fromUTF8("VSM_MENU_CONTEXTE : \xc2\xab ") + griseeVue
                    + juce::String::fromUTF8(" \xc2\xbb est GRIS\xc3\x89" "E \xe2\x80\x94 pr\xc3\xa9sente dans le menu, "
                                             "mais rien n'a \xc3\xa9t\xc3\xa9 fait\n")).toRawUTF8(),
                   stderr);
    return parDebut;
}

} // namespace vsm::app::ui
