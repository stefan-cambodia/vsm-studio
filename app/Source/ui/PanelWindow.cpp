#include "PanelWindow.h"
#include "LookAndFeel/VsmLookAndFeel.h"
#include "UiScale.h"
#include "Langue.h"

PanelWindow::PanelWindow(const juce::String& title, juce::Component& content)
    : DocumentWindow(vsm::app::ui::tr(title), vsm::ui::Palette::panel,
                      // D122 : AGRANDIR AUSSI -- le gestionnaire de fenêtres dessine
                      // agrandir et restaurer, un clic chacun (demande de l'utilisateur).
                      DocumentWindow::closeButton | DocumentWindow::minimiseButton
                          | DocumentWindow::maximiseButton, true),
      cle_(title) {
    setUsingNativeTitleBar(true);
    setContentNonOwned(&content, true);
    setResizable(true, true);
}

void PanelWindow::closeButtonPressed() {
    setVisible(false); // ne quitte JAMAIS l'app -- juste caché, réouvrable depuis le menu Affichage
}

void PanelWindow::visibilityChanged() {
    if (onVisibilityChanged) onVisibilityChanged(isVisible());
}

void PanelWindow::setDefaultSize(int width, int height) {
    const juce::String etat = vsm::app::ui::UiScale::properties().getValue("fenetre." + cle_);
    if (etat.isNotEmpty()) {
        auto limites = juce::Rectangle<int>::fromString(etat);
        if (limites.getWidth() >= 120 && limites.getHeight() >= 80) {
            if (auto* ecran = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                limites = limites.constrainedWithin(ecran->userArea);
            setBounds(limites);
            return;
        }
    }
    setSize(width, height);
}

void PanelWindow::moved() {
    DocumentWindow::moved();
    memoriser();
}

void PanelWindow::resized() {
    DocumentWindow::resized();
    memoriser();
}

void PanelWindow::retraduire() {
    setName(vsm::app::ui::tr(cle_));
}

void PanelWindow::memoriser() {
    // Seulement une fenêtre visible : les limites posées avant l'affichage
    // sont la taille par défaut, pas un réglage.
    if (!isVisible() || getWidth() <= 0 || getHeight() <= 0) return;
    // D122 : UNE FENÊTRE AGRANDIE NE SE RETIENT PAS -- on retient la fenêtre
    // normale, pas son plein écran. `isFullScreen()` ne suit, sous Linux, que les
    // appels de JUCE : un agrandissement demandé par le bouton du gestionnaire de
    // fenêtres ne le met pas à vrai. D'où la géométrie en plus : une fenêtre qui
    // couvre presque toute la zone utile de son écran est tenue pour agrandie.
    if (isFullScreen()) return;
    if (auto* ecran = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getBounds()))
        if (getWidth() >= ecran->userArea.getWidth() * 95 / 100
            && getHeight() >= ecran->userArea.getHeight() * 90 / 100)
            return;
    vsm::app::ui::UiScale::properties().setValue("fenetre." + cle_, getBounds().toString());
}
