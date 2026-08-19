#include "PanelWindow.h"
#include "LookAndFeel/VsmLookAndFeel.h"

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
