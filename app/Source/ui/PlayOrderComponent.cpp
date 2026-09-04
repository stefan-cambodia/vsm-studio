#include "PlayOrderComponent.h"

namespace vsm::app::ui {

PlayOrderComponent::PlayOrderComponent() {
    auto titre = [](juce::Label& l, const juce::String& t) {
        l.setText(t, juce::dontSendNotification);
        l.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        l.setColour(juce::Label::textColourId, vsm::ui::Palette::accentTeal);
    };
    titre(titreSections_, juce::String::fromUTF8(u8"Sections (déduites des repères)"));
    titre(titreOrdre_, juce::String::fromUTF8(u8"Ordre de jeu"));
    addAndMakeVisible(titreSections_);
    addAndMakeVisible(titreOrdre_);

    avertissement_.setFont(juce::Font(juce::FontOptions(12.0f)));
    avertissement_.setColour(juce::Label::textColourId, vsm::ui::Palette::accentAmber);
    avertissement_.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(avertissement_);

    addAndMakeVisible(sectionAAjouter_);
    ajouter_.onClick = [this] {
        const int index = sectionAAjouter_.getSelectedId() - 1;
        if (index >= 0 && index < static_cast<int>(sections_.size())) {
            ordre_.push_back(index);
            rafraichir();
        }
    };
    retirer_.onClick = [this] {
        const int ligne = liste_.getSelectedRow();
        if (ligne >= 0 && ligne < static_cast<int>(ordre_.size())) {
            ordre_.erase(ordre_.begin() + ligne);
            rafraichir();
            liste_.selectRow(juce::jmin(ligne, static_cast<int>(ordre_.size()) - 1));
        }
    };
    auto deplacer = [this](int sens) {
        const int ligne = liste_.getSelectedRow();
        const int cible = ligne + sens;
        if (ligne < 0 || cible < 0 || cible >= static_cast<int>(ordre_.size())) return;
        std::swap(ordre_[static_cast<size_t>(ligne)], ordre_[static_cast<size_t>(cible)]);
        rafraichir();
        liste_.selectRow(cible);
    };
    monter_.onClick = [deplacer] { deplacer(-1); };
    descendre_.onClick = [deplacer] { deplacer(+1); };
    aplatir_.onClick = [this] { if (onFlatten) onFlatten(ordre_); };
    aplatir_.setColour(juce::TextButton::buttonColourId, vsm::ui::Palette::accentAmber);
    for (auto* b : {&ajouter_, &retirer_, &monter_, &descendre_, &aplatir_})
        addAndMakeVisible(*b);

    liste_.setRowHeight(24);
    liste_.setColour(juce::ListBox::backgroundColourId, vsm::ui::Palette::panel);
    addAndMakeVisible(liste_);
    rafraichir();
}

void PlayOrderComponent::setSections(std::vector<vsm::sequencer::Section> sections,
                                      bool tempoSeraLaisse) {
    sections_ = std::move(sections);
    tempoSeraLaisse_ = tempoSeraLaisse;
    // UN ORDRE QUI DÉSIGNE DES SECTIONS DISPARUES NE DÉSIGNE RIEN : on le vide
    // plutôt que de le laisser pointer à côté. Les repères ont pu bouger.
    for (int index : ordre_)
        if (index < 0 || index >= static_cast<int>(sections_.size())) { ordre_.clear(); break; }
    rafraichir();
}

void PlayOrderComponent::rafraichir() {
    sectionAAjouter_.clear(juce::dontSendNotification);
    for (size_t i = 0; i < sections_.size(); ++i)
        sectionAAjouter_.addItem(juce::String(sections_[i].name), static_cast<int>(i) + 1);
    if (sectionAAjouter_.getSelectedId() == 0 && !sections_.empty())
        sectionAAjouter_.setSelectedId(1, juce::dontSendNotification);

    // CE QUI EST DIT AVANT D'APLATIR, jamais après : aplatir réécrit le
    // matériau, et la carte de tempo, elle, ne bouge pas.
    juce::String message;
    if (sections_.empty())
        message = juce::String::fromUTF8(
            u8"Aucune section : posez des repères sur la règle. Une section va d'un "
            u8"repère au suivant.");
    else if (tempoSeraLaisse_)
        message = juce::String::fromUTF8(
            u8"Ce morceau a plusieurs tempos ou signatures. Aplatir déplace le matériau "
            u8"et LAISSE la carte de tempo en place : un ralenti joué deux fois ne suivra "
            u8"pas sa section.");
    avertissement_.setText(message, juce::dontSendNotification);

    ajouter_.setEnabled(!sections_.empty());
    aplatir_.setEnabled(!ordre_.empty());
    liste_.updateContent();
    liste_.repaint();
}

int PlayOrderComponent::getNumRows() { return static_cast<int>(ordre_.size()); }

void PlayOrderComponent::paintListBoxItem(int row, juce::Graphics& g, int width, int height,
                                           bool selected) {
    if (row < 0 || row >= static_cast<int>(ordre_.size())) return;
    const int index = ordre_[static_cast<size_t>(row)];
    if (index < 0 || index >= static_cast<int>(sections_.size())) return;
    const auto& section = sections_[static_cast<size_t>(index)];

    g.setColour(selected ? vsm::ui::Palette::panelRaised : vsm::ui::Palette::panel);
    g.fillRect(0, 0, width, height);
    g.setColour(vsm::ui::Palette::textPrimary);
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawText(juce::String(row + 1) + ".  " + juce::String(section.name),
                8, 0, width - 90, height, juce::Justification::centredLeft);
    // LA LONGUEUR EST DITE : deux sections du même nom se distinguent, et l'on
    // voit tout de suite qu'un ordre fait huit mesures et non trente.
    g.setColour(vsm::ui::Palette::textSecondary);
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText(juce::String(static_cast<int>(section.length())) + " ticks",
                width - 84, 0, 76, height, juce::Justification::centredRight);
}

void PlayOrderComponent::paint(juce::Graphics& g) { g.fillAll(vsm::ui::Palette::background); }

void PlayOrderComponent::resized() {
    auto zone = getLocalBounds().reduced(12);
    titreSections_.setBounds(zone.removeFromTop(24));
    auto rangee = zone.removeFromTop(28);
    sectionAAjouter_.setBounds(rangee.removeFromLeft(rangee.getWidth() - 110).reduced(0, 2));
    rangee.removeFromLeft(8);
    ajouter_.setBounds(rangee.reduced(0, 2));
    zone.removeFromTop(6);
    avertissement_.setBounds(zone.removeFromTop(46));
    zone.removeFromTop(6);

    titreOrdre_.setBounds(zone.removeFromTop(24));
    auto bas = zone.removeFromBottom(34);
    aplatir_.setBounds(bas.removeFromRight(200).reduced(2));
    retirer_.setBounds(bas.removeFromLeft(90).reduced(2));
    monter_.setBounds(bas.removeFromLeft(90).reduced(2));
    descendre_.setBounds(bas.removeFromLeft(100).reduced(2));
    liste_.setBounds(zone.reduced(0, 4));
}

} // namespace vsm::app::ui
