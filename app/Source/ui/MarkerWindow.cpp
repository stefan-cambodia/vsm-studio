#include "MarkerWindow.h"
#include "Langue.h"
#include "LookAndFeel/VsmLookAndFeel.h"

using namespace vsm::ui;

namespace vsm::app::ui {

MarkerWindow::MarkerWindow() {
    liste_.setModel(this);
    liste_.setRowHeight(26);
    liste_.setColour(juce::ListBox::backgroundColourId, Palette::panel);
    addAndMakeVisible(liste_);
    explication_.setColour(juce::Label::textColourId, Palette::textSecondary);
    explication_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(explication_);
    retraduire();
}

void MarkerWindow::retraduire() {
    explication_.setText(tr(u8"Un clic place la tête sur le repère ; un double-clic le renomme ; Suppr, ou le clic "
                            u8"droit, le retire. Les repères se posent par le clic droit sur la règle."),
                         juce::dontSendNotification);
    liste_.repaint();
}

void MarkerWindow::setLignes(std::vector<Ligne> lignes) {
    lignes_ = std::move(lignes);
    liste_.updateContent();
    liste_.repaint();
}

juce::String MarkerWindow::releverPourCapture() const {
    juce::StringArray morceaux;
    for (const auto& l : lignes_) morceaux.add(l.position + " " + l.nom);
    return morceaux.isEmpty() ? tr(u8"(aucun repère)") : morceaux.joinIntoString(" | ");
}

int MarkerWindow::getNumRows() { return static_cast<int>(lignes_.size()); }

void MarkerWindow::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) {
    if (row < 0 || static_cast<size_t>(row) >= lignes_.size()) return;
    if (selected) {
        g.setColour(Palette::accentTeal.withAlpha(0.25f));
        g.fillRect(0, 0, width, height);
    }
    const auto& l = lignes_[static_cast<size_t>(row)];
    g.setFont(juce::Font(juce::FontOptions(14.0f)));
    g.setColour(Palette::textSecondary);
    g.drawText(l.position, 8, 0, 110, height, juce::Justification::centredLeft);
    g.setColour(Palette::textPrimary);
    g.drawText(l.nom, 124, 0, width - 132, height, juce::Justification::centredLeft);
}

void MarkerWindow::listBoxItemClicked(int row, const juce::MouseEvent& e) {
    if (row < 0 || static_cast<size_t>(row) >= lignes_.size()) return;
    if (e.mods.isPopupMenu()) {
        juce::PopupMenu menu;
        menu.addItem(1, tr(u8"Renommer ce repère…"));
        menu.addItem(2, tr(u8"Retirer ce repère"));
        menu.showMenuAsync(juce::PopupMenu::Options(), [this, row](int choix) {
            if (choix == 1 && onRename) onRename(static_cast<size_t>(row));
            if (choix == 2 && onRemove) onRemove(static_cast<size_t>(row));
        });
        return;
    }
    if (onGoTo) onGoTo(static_cast<size_t>(row));
}

void MarkerWindow::listBoxItemDoubleClicked(int row, const juce::MouseEvent&) {
    if (row >= 0 && static_cast<size_t>(row) < lignes_.size() && onRename) onRename(static_cast<size_t>(row));
}

void MarkerWindow::deleteKeyPressed(int lastRowSelected) {
    if (lastRowSelected >= 0 && static_cast<size_t>(lastRowSelected) < lignes_.size() && onRemove)
        onRemove(static_cast<size_t>(lastRowSelected));
}

void MarkerWindow::paint(juce::Graphics& g) { g.fillAll(Palette::background); }

void MarkerWindow::resized() {
    auto zone = getLocalBounds().reduced(8);
    explication_.setBounds(zone.removeFromBottom(44));
    liste_.setBounds(zone);
}

} // namespace vsm::app::ui
