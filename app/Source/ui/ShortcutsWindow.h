#pragma once
#include <JuceHeader.h>
#include "vsm/interchange/ShortcutTable.h"
#include <functional>
#include <memory>

namespace vsm::app::ui {

/// LA PAGE QUI LES LISTE TOUS (D10.3).
///
/// C'est le critère de l'étape, pris au mot. Une commande par ligne, sa
/// famille, sa touche ; un clic sur la touche demande la nouvelle, et
/// l'échappement annule. Ce qui ne se reconfigure pas -- les flèches, dont le
/// sens EST la direction -- figure en bas, marqué comme fixe : une page qui
/// prétend tout lister et tait quatre touches ment davantage qu'une page qui
/// dit « celles-ci ne bougent pas ».
///
/// **ET ELLE S'IMPRIME.** Le bouton écrit un fichier texte : on l'imprime, on
/// le colle au mur du studio, on le cherche avec Ctrl+F. Une capture d'écran
/// ne ferait aucune de ces trois choses.
class ShortcutsWindow : public juce::Component {
public:
    ShortcutsWindow();
    ~ShortcutsWindow() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /// Republie la liste depuis la table.
    void setTable(const vsm::interchange::ShortcutTable* table);

    /// L'utilisateur veut changer la touche de cette commande.
    std::function<void(vsm::interchange::ShortcutId)> onRebind;
    std::function<void(vsm::interchange::ShortcutId)> onReset;
    std::function<void()> onResetAll;
    std::function<void()> onExport;

    /// Affiche « appuyez sur la nouvelle touche... », ou rien. Prend le focus
    /// clavier tant qu'on attend : la touche suivante doit arriver ICI et non
    /// déclencher la commande à laquelle elle est encore associée.
    void setCapturing(const juce::String& commande);
    /// La touche saisie pendant une capture. Renvoyer faux la laisse suivre son
    /// chemin normal.
    std::function<bool(const juce::KeyPress&)> onKeyCaptured;

    bool keyPressed(const juce::KeyPress& key) override;

private:
    class Contenu;
    std::unique_ptr<Contenu> contenu_;
    juce::Viewport defilement_;
    juce::Label attente_;
    juce::TextButton exporter_ { "Enregistrer la table..." };
    juce::TextButton toutRetablir_ { u8"Tout rétablir" };
};

} // namespace vsm::app::ui
