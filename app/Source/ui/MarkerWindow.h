#pragma once
#include <JuceHeader.h>
#include <functional>
#include <vector>

namespace vsm::app::ui {

/// D543.2 : LA FENÊTRE DES REPÈRES — une ligne par repère, dans l'ordre du morceau : sa position en
/// mesure · temps et son nom. Une porte de plus vers les gestes des règles : un clic y place la tête,
/// un double-clic renomme, Suppr (ou le clic droit) retire. La fenêtre ne touche jamais au projet
/// elle-même : elle appelle les fonctions de l'application, celles des règles.
class MarkerWindow : public juce::Component, private juce::ListBoxModel {
public:
    struct Ligne {
        juce::String position;   ///< « mes. 9 · 1 », le format de la barre de transport
        juce::String nom;
    };

    MarkerWindow();

    void retraduire();
    void setLignes(std::vector<Ligne> lignes);

    std::function<void(size_t)> onGoTo;
    std::function<void(size_t)> onRename;
    std::function<void(size_t)> onRemove;

    /// Les lignes telles qu'elles sont PEINTES (« mes. 1 · 1 Intro | … ») : un panneau qui peint ses
    /// lignes est invisible au relevé des composants (D149).
    juce::String releverPourCapture() const;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& e) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent& e) override;
    void deleteKeyPressed(int lastRowSelected) override;

    std::vector<Ligne> lignes_;
    juce::ListBox liste_;
    juce::Label explication_;
};

} // namespace vsm::app::ui
