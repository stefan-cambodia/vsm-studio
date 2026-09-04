#include "TakeCompComponent.h"

namespace vsm::app::ui {

TakeCompComponent::TakeCompComponent() {
    titre_.setText(juce::String::fromUTF8(u8"Tronçons — « de telle mesure à telle mesure, telle prise »"),
                    juce::dontSendNotification);
    titre_.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    titre_.setColour(juce::Label::textColourId, vsm::ui::Palette::accentTeal);
    addAndMakeVisible(titre_);

    aide_.setFont(juce::Font(juce::FontOptions(12.0f)));
    aide_.setColour(juce::Label::textColourId, vsm::ui::Palette::textSecondary);
    aide_.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(aide_);

    deLabel_.setText(juce::String::fromUTF8(u8"de la mesure"), juce::dontSendNotification);
    aLabel_.setText(juce::String::fromUTF8(u8"à"), juce::dontSendNotification);
    for (auto* l : {&deLabel_, &aLabel_}) {
        l->setFont(juce::Font(juce::FontOptions(13.0f)));
        l->setColour(juce::Label::textColourId, vsm::ui::Palette::textPrimary);
        addAndMakeVisible(*l);
    }
    de_.setText("1");
    a_.setText("2");
    for (auto* e : {&de_, &a_}) {
        e->setInputRestrictions(4, "0123456789");
        e->setFont(juce::Font(juce::FontOptions(13.0f)));
        addAndMakeVisible(*e);
    }
    addAndMakeVisible(prise_);

    ajouter_.onClick = [this] {
        const int index = prise_.getSelectedId() - 1;
        const int mesureDe = de_.getText().getIntValue();
        const int mesureA = a_.getText().getIntValue();
        // LES BORNES SONT EN MESURES ET COMMENCENT À 1, comme la règle : « de
        // la mesure 1 à 2 » est le premier tronçon, pas le second.
        if (index < 0 || mesureA <= mesureDe) return;
        vsm::sequencer::CompSegment troncon;
        troncon.takeIndex = index;
        troncon.fromTick = static_cast<vsm::midi::Tick>(mesureDe - 1) * parMesure_;
        troncon.toTick = static_cast<vsm::midi::Tick>(mesureA - 1) * parMesure_;
        troncons_.push_back(troncon);
        // Le tronçon suivant commence là où celui-ci finit : c'est ce qu'on
        // veut neuf fois sur dix, et c'est corrigeable.
        de_.setText(juce::String(mesureA), juce::dontSendNotification);
        a_.setText(juce::String(mesureA + 1), juce::dontSendNotification);
        rafraichir();
    };
    retirer_.onClick = [this] {
        const int ligne = liste_.getSelectedRow();
        if (ligne >= 0 && ligne < static_cast<int>(troncons_.size())) {
            troncons_.erase(troncons_.begin() + ligne);
            rafraichir();
        }
    };
    composer_.onClick = [this] { if (onCompose) onCompose(troncons_); };
    composer_.setColour(juce::TextButton::buttonColourId, vsm::ui::Palette::accentAmber);
    for (auto* b : {&ajouter_, &retirer_, &composer_}) addAndMakeVisible(*b);

    liste_.setRowHeight(24);
    liste_.setColour(juce::ListBox::backgroundColourId, vsm::ui::Palette::panel);
    addAndMakeVisible(liste_);
    rafraichir();
}

void TakeCompComponent::setTake(std::vector<juce::String> takeNames, int activeTake,
                                 vsm::midi::Tick ticksPerBar, vsm::midi::Tick lastTick) {
    prises_ = std::move(takeNames);
    active_ = activeTake;
    parMesure_ = ticksPerBar > 0 ? ticksPerBar : 1920;
    fin_ = lastTick;
    // DES TRONÇONS QUI DÉSIGNENT DES PRISES DISPARUES NE DÉSIGNENT RIEN : on
    // les vide plutôt que de les laisser pointer à côté. On change de piste.
    troncons_.clear();
    rafraichir();
}

void TakeCompComponent::rafraichir() {
    prise_.clear(juce::dontSendNotification);
    for (size_t i = 0; i < prises_.size(); ++i) {
        // LA PRISE ACTIVE EST DITE : c'est celle qu'on entend, donc celle
        // qu'on est en train de juger.
        const juce::String nom = prises_[i]
            + (static_cast<int>(i) == active_ ? juce::String::fromUTF8(u8"  (celle qu'on entend)")
                                               : juce::String());
        prise_.addItem(nom, static_cast<int>(i) + 1);
    }
    if (prise_.getSelectedId() == 0 && !prises_.empty())
        prise_.setSelectedId(1, juce::dontSendNotification);

    const int mesures = static_cast<int>(fin_ / juce::jmax<vsm::midi::Tick>(1, parMesure_)) + 1;
    aide_.setText(prises_.empty()
                      ? juce::String::fromUTF8(
                            u8"Cette piste n'a aucune prise conservée. Enregistrez en mode "
                            u8"« empiler » pour en garder plusieurs.")
                      : juce::String::fromUTF8(u8"Le morceau fait ")
                            + juce::String(mesures)
                            + juce::String::fromUTF8(
                                  u8" mesures. Ce qu'aucun tronçon ne couvre ne sonnera pas."),
                  juce::dontSendNotification);

    ajouter_.setEnabled(!prises_.empty());
    composer_.setEnabled(!troncons_.empty());
    liste_.updateContent();
    liste_.repaint();
}

int TakeCompComponent::getNumRows() { return static_cast<int>(troncons_.size()); }

void TakeCompComponent::paintListBoxItem(int row, juce::Graphics& g, int width, int height,
                                          bool selected) {
    if (row < 0 || row >= static_cast<int>(troncons_.size())) return;
    const auto& t = troncons_[static_cast<size_t>(row)];
    g.setColour(selected ? vsm::ui::Palette::panelRaised : vsm::ui::Palette::panel);
    g.fillRect(0, 0, width, height);
    g.setColour(vsm::ui::Palette::textPrimary);
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    const juce::String nom = (t.takeIndex >= 0 && t.takeIndex < static_cast<int>(prises_.size()))
                                 ? prises_[static_cast<size_t>(t.takeIndex)]
                                 : juce::String("?");
    g.drawText(juce::String::fromUTF8(u8"mesures ")
                   + juce::String(static_cast<int>(t.fromTick / parMesure_) + 1)
                   + juce::String::fromUTF8(u8" à ")
                   + juce::String(static_cast<int>(t.toTick / parMesure_) + 1)
                   + juce::String::fromUTF8(u8"   →   ") + nom,
                8, 0, width - 16, height, juce::Justification::centredLeft);
}

void TakeCompComponent::paint(juce::Graphics& g) { g.fillAll(vsm::ui::Palette::background); }

void TakeCompComponent::resized() {
    auto zone = getLocalBounds().reduced(12);
    titre_.setBounds(zone.removeFromTop(24));
    aide_.setBounds(zone.removeFromTop(40));
    zone.removeFromTop(4);

    auto rangee = zone.removeFromTop(28);
    deLabel_.setBounds(rangee.removeFromLeft(92));
    de_.setBounds(rangee.removeFromLeft(48).reduced(0, 2));
    rangee.removeFromLeft(6);
    aLabel_.setBounds(rangee.removeFromLeft(16));
    a_.setBounds(rangee.removeFromLeft(48).reduced(0, 2));
    rangee.removeFromLeft(10);
    prise_.setBounds(rangee.reduced(0, 2));

    zone.removeFromTop(6);
    auto boutons = zone.removeFromTop(30);
    ajouter_.setBounds(boutons.removeFromLeft(170).reduced(2));
    retirer_.setBounds(boutons.removeFromLeft(90).reduced(2));
    zone.removeFromTop(6);

    auto bas = zone.removeFromBottom(34);
    composer_.setBounds(bas.removeFromRight(240).reduced(2));
    liste_.setBounds(zone.reduced(0, 4));
}

} // namespace vsm::app::ui
