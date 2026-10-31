#include "PanelWindow.h"
#include "LookAndFeel/VsmLookAndFeel.h"
#include "UiScale.h"
#include "Langue.h"
#include <cstdio>
#include <cstdlib>
#include <string>

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
    // D613.1 : sans contraignant, JUCE ne déclarait aucune taille minimale, et le compositeur a fait naître des fenêtres
    // en 128 × 128 (D607, D608, D612) — leurs boutons sans surface.
    // L'A/B de la série de D613 : `VSM_FENETRE_SANS_LIMITE=1` retire la limite — la seule variable de la mesure.
    if (const char* sans = std::getenv("VSM_FENETRE_SANS_LIMITE"); sans == nullptr || *sans == '\0' || *sans == '0')
        setResizeLimits(kLargeurMinimale, kHauteurMinimale, 1 << 14, 1 << 14);
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
        // D613.1 : SOUS LA TAILLE MINIMALE, une taille retenue est un accident (une fenêtre née dégénérée, puis retenue) :
        // elle est ignorée, la taille d'origine du panneau la remplace. Le seuil d'avant (120 × 80) reprenait 128 × 128.
        if (limites.getWidth() >= kLargeurMinimale && limites.getHeight() >= kHauteurMinimale) {
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
    ramenerSiDegeneree();
    memoriser();
}

void PanelWindow::ramenerSiDegeneree() {
    const auto ici = getBounds();
    if (ici.getWidth() >= kLargeurMinimale && ici.getHeight() >= kHauteurMinimale) {
        derniereBonne_ = ici;
        corrections_ = 0;
        return;
    }
    // D613.1 : LE SYSTÈME L'A RÉDUITE SOUS LE PLANCHER (des fenêtres nées en 128 × 128 : D607, D608, D612) — JUCE
    // applique la taille que le compositeur lui donne sans passer par le contraignant. Ramenée à sa dernière taille
    // correcte, de façon différée (pas pendant le redimensionnement), et DIT ; au plus trois fois d'affilée.
    if (!isVisible() || derniereBonne_.isEmpty() || ici.isEmpty()) return;
    if (++corrections_ > 3) {
        if (corrections_ == 4)
            std::fputs(("VSM_FENETRE : \xc2\xab " + cle_.toStdString() + " \xc2\xbb laiss\xc3\xa9" "e en "
                        + std::to_string(ici.getWidth()) + "x" + std::to_string(ici.getHeight())
                        + " (trois retours sans effet)\n").c_str(), stderr);
        return;
    }
    std::fputs(("VSM_FENETRE : \xc2\xab " + cle_.toStdString() + " \xc2\xbb ramen\xc3\xa9" "e de "
                + std::to_string(ici.getWidth()) + "x" + std::to_string(ici.getHeight()) + " \xc3\xa0 "
                + std::to_string(derniereBonne_.getWidth()) + "x" + std::to_string(derniereBonne_.getHeight())
                + " (taille impos\xc3\xa9" "e sous le plancher)\n").c_str(), stderr);
    const auto cible = derniereBonne_;
    juce::Component::SafePointer<PanelWindow> moi(this);
    juce::MessageManager::callAsync([moi, cible] { if (moi != nullptr) moi->setBounds(cible); });
}

void PanelWindow::retraduire() {
    setName(vsm::app::ui::tr(cle_));
}

void PanelWindow::memoriser() {
    // Seulement une fenêtre visible : les limites posées avant l'affichage
    // sont la taille par défaut, pas un réglage.
    if (!isVisible() || getWidth() <= 0 || getHeight() <= 0) return;
    // D613.1 : une taille sous le plancher ne se retient jamais — elle se reprendrait à chaque ouverture.
    if (getWidth() < kLargeurMinimale || getHeight() < kHauteurMinimale) return;
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
