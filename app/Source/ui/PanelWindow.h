#pragma once
#include <JuceHeader.h>
#include <functional>

// Fenêtre flottante réutilisable pour un panneau de l'application (Track
// Editor, Piano Roll, Synth Rack, Mixer...). Contrairement à la fenêtre
// principale, fermer une PanelWindow ne quitte JAMAIS l'application : ça la
// cache simplement (setVisible(false)), récupérable depuis le menu
// Affichage -- le composant qu'elle contient n'est jamais détruit (il
// appartient à MainComponent, pas à cette fenêtre : setContentNonOwned),
// donc son état (scroll, sélection, zoom...) est préservé d'un
// masquage/affichage à l'autre.
class PanelWindow : public juce::DocumentWindow {
public:
    PanelWindow(const juce::String& title, juce::Component& content);

    void closeButtonPressed() override;
    void visibilityChanged() override;
    /// LA TAILLE PAR DÉFAUT, OU CE QU'ON AVAIT RÉGLÉ (D15.3) : si cette
    /// fenêtre a déjà été déplacée ou redimensionnée lors d'une exécution
    /// passée, ses limites sont reprises (ramenées dans l'écran) ; sinon la
    /// taille donnée. Chaque déplacement ou redimensionnement d'une fenêtre
    /// visible est retenu sous son titre dans le fichier de préférences.
    void setDefaultSize(int width, int height);
    void moved() override;
    void resized() override;

    /// Notifie MainComponent qu'il faut resynchroniser la coche du menu
    /// Affichage correspondant à ce panneau.
    std::function<void(bool)> onVisibilityChanged;

private:
    void memoriser();
};
