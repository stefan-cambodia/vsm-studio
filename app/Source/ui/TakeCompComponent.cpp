#include "TakeCompComponent.h"
#include <cmath>
#include "Langue.h"

namespace vsm::app::ui {

TakeCompComponent::TakeCompComponent() {
    titre_.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    titre_.setColour(juce::Label::textColourId, vsm::ui::Palette::accentTeal);
    addAndMakeVisible(titre_);

    aide_.setFont(juce::Font(juce::FontOptions(12.0f)));
    aide_.setColour(juce::Label::textColourId, vsm::ui::Palette::textSecondary);
    aide_.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(aide_);

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
    retraduire();   // D86 : les textes fixes, puis le rafraîchissement
}

void TakeCompComponent::retraduire() {
    // D86 : rien n'y passait par la table, et les boutons étaient initialisés
    // en français dans l'en-tête.
    titre_.setText(tr(u8"Tronçons — « de telle mesure à telle mesure, telle prise »"), juce::dontSendNotification);
    deLabel_.setText(tr(u8"de la mesure"), juce::dontSendNotification);
    aLabel_.setText(tr(u8"à"), juce::dontSendNotification);
    ajouter_.setButtonText(tr(u8"Ajouter le tronçon"));
    retirer_.setButtonText(tr(u8"Retirer"));
    composer_.setButtonText(tr(u8"Composer (écrit le matériau)"));
    rafraichir();
    resized();   // la case de « à / to » suit la longueur du mot
}

void TakeCompComponent::setTake(std::vector<juce::String> takeNames, int activeTake,
                                 vsm::midi::Tick ticksPerBar, vsm::midi::Tick lastTick,
                                 std::vector<vsm::sequencer::CompSegment> segments) {
    prises_ = std::move(takeNames);
    active_ = activeTake;
    parMesure_ = ticksPerBar > 0 ? ticksPerBar : 1920;
    fin_ = lastTick;
    // D55.2 : LA RECETTE VIENT DE LA PISTE, elle n'est plus jetée. Le panneau
    // vidait sa liste ici, à chaque ouverture -- le commentaire disait « on
    // change de piste », mais le code ne le vérifiait pas et vidait aussi
    // quand on rouvrait la même. Les tronçons qui désignaient une prise
    // disparue sont écartés en amont, à la lecture du projet, et nommés au
    // rapport : ce que la piste porte ici a déjà été vérifié.
    troncons_ = std::move(segments);
    rafraichir();
}

bool TakeCompComponent::addSegmentForCapture(int takeIndex, int fromBar, int toBar) {
    if (takeIndex < 0 || takeIndex >= static_cast<int>(prises_.size())) return false;
    prise_.setSelectedId(takeIndex + 1, juce::dontSendNotification);
    de_.setText(juce::String(fromBar), juce::dontSendNotification);
    a_.setText(juce::String(toBar), juce::dontSendNotification);
    const size_t avant = troncons_.size();
    ajouter_.onClick();
    return troncons_.size() > avant;
}

bool TakeCompComponent::composeForCapture() {
    if (troncons_.empty()) return false;
    composer_.onClick();
    return true;
}

void TakeCompComponent::rafraichir() {
    prise_.clear(juce::dontSendNotification);
    for (size_t i = 0; i < prises_.size(); ++i) {
        // LA PRISE ACTIVE EST DITE : c'est celle qu'on entend, donc celle
        // qu'on est en train de juger.
        const juce::String nom = static_cast<int>(i) == active_
            ? tr(u8"%1  (celle qu'on entend)").replace("%1", prises_[i])
            : prises_[i];
        prise_.addItem(nom, static_cast<int>(i) + 1);
    }
    if (prise_.getSelectedId() == 0 && !prises_.empty())
        prise_.setSelectedId(1, juce::dontSendNotification);

    const int mesures = static_cast<int>(fin_ / juce::jmax<vsm::midi::Tick>(1, parMesure_)) + 1;
    aide_.setText(prises_.empty()
                      ? tr(u8"Cette piste n'a aucune prise conservée. Enregistrez en mode "
                           u8"« empiler » pour en garder plusieurs.")
                      // D86 : « 1 mesures » était faux en français aussi -- le
                      // singulier a son propre modèle.
                      : tr(mesures > 1 ? u8"Le morceau fait %1 mesures. Ce qu'aucun tronçon ne couvre ne sonnera pas."
                                       : u8"Le morceau fait %1 mesure. Ce qu'aucun tronçon ne couvre ne sonnera pas.")
                            .replace("%1", juce::String(mesures)),
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
    // D86 : un modèle entier -- traduit par morceaux, « bars 1 à 3 » serait sorti.
    g.drawText(tr(u8"mesures %1 à %2   →   %3")
                   .replace("%1", juce::String(static_cast<int>(t.fromTick / parMesure_) + 1))
                   .replace("%2", juce::String(static_cast<int>(t.toTick / parMesure_) + 1))
                   .replace("%3", nom),
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
    // D86 : LA CASE SE TAILLE SUR SON TEXTE. 16 px tenaient « à » et pas
    // « to », que la capture anglaise montrait « … ». La lisibilité prime : on
    // agrandit la case, on ne rétrécit pas le mot (règle de D74, « Straight »).
    // Le cadre d'un juce::Label prend 5 px de chaque côté.
    aLabel_.setBounds(rangee.removeFromLeft(juce::jmax(16, static_cast<int>(std::ceil(
        juce::GlyphArrangement::getStringWidth(aLabel_.getFont(), aLabel_.getText()))) + 10)));
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
