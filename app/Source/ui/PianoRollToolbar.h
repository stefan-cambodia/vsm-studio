#pragma once
#include <JuceHeader.h>
#include "PianoRollComponent.h"

/// Barre d'outils du piano roll : outils, grille, snap, swing, vélocité par
/// défaut, gamme, affichage, zoom, annuler/rétablir et les opérations
/// d'édition les plus courantes.
///
/// Elle ne fait QUE piloter le PianoRollComponent (aucun état musical propre)
/// et se resynchronise avec lui via refreshFromPianoRoll() : deux sources de
/// vérité pour "quel est l'outil courant" finiraient forcément par diverger
/// -- typiquement quand l'utilisateur change d'outil au clavier (touches 1-6)
/// sans passer par les boutons.
class PianoRollToolbar : public juce::Component, private juce::Timer {
public:
    explicit PianoRollToolbar(PianoRollComponent& pianoRoll);

    void paint(juce::Graphics&) override;
    void resized() override;

    /// D61 : LA HAUTEUR QU'IL FAUT À CETTE LARGEUR. Les bandes se replient ;
    /// le nombre de rangées dépend donc de la largeur, et le panneau ne peut
    /// plus réserver une constante. Il appelle ceci, et fait défiler au-delà
    /// de son plafond -- de sorte qu'aucune commande ne se retrouve à zéro
    /// pixel, ce qui était le cas de onze d'entre elles à toute taille.
    int hauteurUtile(int largeur);

    /// Relit l'état du piano roll (outil, annuler/rétablir, sélection).
    void refreshFromPianoRoll();
    /// D29.4 : la ligne d'information des notes choisies, relue par la minuterie.
    void refreshSelectionInfo();

private:
    /// Pose (ou seulement compte) les rangées pour cette largeur. Un seul
    /// parcours sert aux deux, sans quoi la mesure et la pose divergeraient.
    int disposer(int largeurTotale, bool placer);
    void configureButton(juce::Button& button, const juce::String& tooltip);
    void applyGridFromCombos();
    void applyScaleFromCombos();
    void timerCallback() override { refreshSelectionInfo(); }
    void applyInfoLine(int champ);

    PianoRollComponent& pianoRoll_;

    juce::TextButton selectTool_ { u8"Sél." }, drawTool_ { "Dess." }, eraseTool_ { "Eff." },
                     splitTool_ { "Coup." }, glueTool_ { "Coll." }, muteTool_ { "Muet" };
    juce::TextButton undoButton_ { "Annuler" }, redoButton_ { u8"Rétablir" };
    juce::TextButton quantizeButton_ { "Quantifier" }, legatoButton_ { "Legato" },
                     humanizeButton_ { "Humaniser" }, chordButton_ { "Accord" }, moreButton_ { "Plus..." };
    juce::TextButton zoomInButton_ { "+" }, zoomOutButton_ { "-" }, zoomFitButton_ { "Tout" };
    juce::ToggleButton snapButton_ { "Aimant" }, ghostButton_ { u8"Fantômes" }, foldButton_ { "Replier" },
                       followButton_ { "Suivre" }, scaleHighlightButton_ { "Gamme" },
                       stepButton_ { u8"Pas à pas" };

    juce::ComboBox gridCombo_, gridModifierCombo_, scaleRootCombo_, scaleTypeCombo_;
    juce::Slider swingSlider_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider velocitySlider_ { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label gridLabel_, swingLabel_, velocityLabel_, scaleLabel_;
    /// D29.4 : « Début », « Durée », « Vélocité » des notes choisies, éditables.
    juce::Label infoLabel_, debutEdit_, dureeEdit_, veloEdit_;
    bool infoEnEdition_ = false;
};
