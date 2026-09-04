#include "PanelWindow.h"
#include "LookAndFeel/VsmLookAndFeel.h"
#include "UiScale.h"

PanelWindow::PanelWindow(const juce::String& title, juce::Component& content)
    : DocumentWindow(title, vsm::ui::Palette::panel,
                      DocumentWindow::closeButton | DocumentWindow::minimiseButton, true) {
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
    const juce::String etat = vsm::app::ui::UiScale::properties().getValue("fenetre." + getName());
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

void PanelWindow::memoriser() {
    // Seulement une fenêtre visible : les limites posées avant l'affichage
    // sont la taille par défaut, pas un réglage.
    if (!isVisible() || getWidth() <= 0 || getHeight() <= 0) return;
    vsm::app::ui::UiScale::properties().setValue("fenetre." + getName(), getBounds().toString());
}
