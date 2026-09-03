#include "HistoryWindow.h"
#include "LookAndFeel/VsmLookAndFeel.h"

using namespace vsm::ui;

namespace vsm::app::ui {

HistoryWindow::HistoryWindow() {
    liste_.setModel(this);
    liste_.setRowHeight(24);
    liste_.setColour(juce::ListBox::backgroundColourId, Palette::panel);
    addAndMakeVisible(liste_);
    explication_.setText(u8"Un clic sur un pas y revient. Le pas en surbrillance est l'état courant ; "
                         u8"au-dessous, ce que Rétablir rendrait.",
                         juce::dontSendNotification);
    explication_.setColour(juce::Label::textColourId, Palette::textSecondary);
    explication_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(explication_);
}

void HistoryWindow::setEntries(std::vector<std::string> undo, std::vector<std::string> redo) {
    undo_ = std::move(undo);
    redo_ = std::move(redo);
    liste_.updateContent();
    liste_.repaint();
    // L'état courant reste en vue, même quand la pile est longue.
    liste_.scrollToEnsureRowIsOnscreen(static_cast<int>(undo_.size()));
}

int HistoryWindow::getNumRows() { return static_cast<int>(undo_.size() + 1 + redo_.size()); }

void HistoryWindow::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool) {
    const int courant = static_cast<int>(undo_.size());
    juce::String texte;
    if (row < courant) texte = juce::String::fromUTF8(undo_[static_cast<size_t>(row)].c_str());
    else if (row == courant) texte = u8"► état courant";
    else texte = juce::String::fromUTF8(redo_[static_cast<size_t>(row - courant - 1)].c_str());

    if (row == courant) {
        g.setColour(Palette::accentTeal.withAlpha(0.25f));
        g.fillRect(0, 0, width, height);
    }
    g.setColour(row > courant ? Palette::textSecondary : Palette::textPrimary);
    g.setFont(juce::Font(juce::FontOptions(14.0f)));
    // Le numéro du pas, puis son libellé — et pour ce qu'un clic ferait,
    // l'écart à l'état courant.
    juce::String prefixe = row < courant ? juce::String(courant - row) + juce::String::fromUTF8(u8" \u2190  ")
                         : row > courant ? juce::String(row - courant) + juce::String::fromUTF8(u8" \u2192  ") : juce::String();
    g.drawText(prefixe + texte, 8, 0, width - 16, height, juce::Justification::centredLeft);
}

void HistoryWindow::listBoxItemClicked(int row, const juce::MouseEvent&) {
    const int courant = static_cast<int>(undo_.size());
    if (row < courant && onUndoSteps) onUndoSteps(static_cast<size_t>(courant - row));
    else if (row > courant && onRedoSteps) onRedoSteps(static_cast<size_t>(row - courant));
}

void HistoryWindow::paint(juce::Graphics& g) { g.fillAll(Palette::background); }

void HistoryWindow::resized() {
    auto zone = getLocalBounds().reduced(8);
    explication_.setBounds(zone.removeFromBottom(40));
    liste_.setBounds(zone);
}

} // namespace vsm::app::ui
