#pragma once
#include <JuceHeader.h>
#include <functional>
#include <string>
#include <vector>

namespace vsm::app::ui {

/// L'HISTORIQUE DES MODIFICATIONS, VISIBLE (D11, 03/09/2026).
///
/// Ctrl+Z et Ctrl+Y existaient, et l'historique était une pile sans
/// fenêtre : pour savoir ce que le prochain Ctrl+Z allait défaire, il
/// fallait l'essayer. Cette liste montre chaque pas — les plus anciens en
/// haut, l'état courant marqué, puis ce que Rétablir rendrait — et un clic
/// sur un pas y REVIENT, en une fois, par autant d'annulations ou de
/// rétablissements qu'il faut. Elle ne connaît pas le projet : on lui donne
/// des libellés, elle rend un nombre de pas.
class HistoryWindow : public juce::Component, private juce::ListBoxModel {
public:
    HistoryWindow();

    /// Les libellés des pas annulables (du plus ancien au plus récent) et
    /// des pas rétablissables (du prochain au plus lointain).
    /// D82 : repose l'explication dans la langue courante.
    void retraduire();
    void setEntries(std::vector<std::string> undo, std::vector<std::string> redo);
    /// D588.3 : COMBIEN DE PAS PLUS ANCIENS SONT SORTIS de l'historique, et sa profondeur — écrit en tête de la liste,
    /// et rien quand aucun n'est sorti. Un `juce::Label` et non une ligne peinte : le relevé de textes le voit (D149).
    void setOublies(size_t oublies, size_t profondeur);
    /// D588.3 : le texte de cet en-tête (vide pour zéro), dans la langue courante — le relevé du banc lit le même.
    static juce::String texteDesOublies(size_t oublies, size_t profondeur);

    /// « Reviens N pas en arrière » / « avance de N pas ».
    std::function<void(size_t)> onUndoSteps;
    std::function<void(size_t)> onRedoSteps;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& e) override;

    std::vector<std::string> undo_, redo_;
    juce::ListBox liste_;
    juce::Label explication_;
    juce::Label oublies_;   ///< D588.3
    size_t nombreOublies_ = 0, profondeur_ = 0;
};

} // namespace vsm::app::ui
