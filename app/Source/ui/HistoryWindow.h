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
    void setEntries(std::vector<std::string> undo, std::vector<std::string> redo);

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
};

} // namespace vsm::app::ui
