#include "MidiLearnWindow.h"

namespace vsm::app::ui {

/// Le contenu défilant : une ligne par association. Des composants réels et
/// non un `ListBox` dessiné : chaque ligne porte un bouton, et un bouton qui
/// se dessine sans exister se clique mal.
class MidiLearnWindow::Contenu : public juce::Component {
public:
    void setRows(std::vector<Row> lignes, std::function<void(int)> retirer) {
        lignes_ = std::move(lignes);
        etiquettes_.clear();
        boutons_.clear();
        for (const auto& ligne : lignes_) {
            auto etiquette = std::make_unique<juce::Label>();
            etiquette->setText("CC " + juce::String(ligne.controller) + juce::String::fromUTF8(u8"  →  ")
                                   + ligne.description,
                                juce::dontSendNotification);
            etiquette->setFont(juce::Font(juce::FontOptions(15.0f)));
            addAndMakeVisible(*etiquette);
            etiquettes_.push_back(std::move(etiquette));

            auto bouton = std::make_unique<juce::TextButton>("Retirer");
            const int cc = ligne.controller;
            bouton->onClick = [retirer, cc] { if (retirer) retirer(cc); };
            addAndMakeVisible(*bouton);
            boutons_.push_back(std::move(bouton));
        }
        setSize(getWidth(), static_cast<int>(lignes_.size()) * kHauteurLigne + 4);
        resized();
    }

    void resized() override {
        for (size_t i = 0; i < etiquettes_.size(); ++i) {
            auto zone = juce::Rectangle<int>(0, static_cast<int>(i) * kHauteurLigne,
                                              getWidth(), kHauteurLigne).reduced(4, 3);
            boutons_[i]->setBounds(zone.removeFromRight(90));
            zone.removeFromRight(8);
            etiquettes_[i]->setBounds(zone);
        }
    }

private:
    static constexpr int kHauteurLigne = 34;
    std::vector<Row> lignes_;
    std::vector<std::unique_ptr<juce::Label>> etiquettes_;
    std::vector<std::unique_ptr<juce::TextButton>> boutons_;
};

MidiLearnWindow::MidiLearnWindow() : contenu_(std::make_unique<Contenu>()) {
    defilement_.setViewedComponent(contenu_.get(), false);
    defilement_.setScrollBarsShown(true, false);
    addAndMakeVisible(defilement_);

    // QUAND IL N'Y A RIEN, ON LE DIT. Une liste vide et une fenêtre qui n'a
    // pas fini de charger se ressemblent trop.
    vide_.setText(juce::String::fromUTF8(
                       u8"Aucune association.\n\nDans le Synth Rack, clic droit sur un réglage ▸ "
                       u8"« Apprendre un contrôleur MIDI », puis tournez le potentiomètre."),
                   juce::dontSendNotification);
    vide_.setJustificationType(juce::Justification::centred);
    vide_.setFont(juce::Font(juce::FontOptions(15.0f)));
    addAndMakeVisible(vide_);

    toutEffacer_.onClick = [this] { if (onRemoveAll) onRemoveAll(); };
    addAndMakeVisible(toutEffacer_);

    // APPRENDRE DEPUIS ICI, et pas seulement en touchant un réglage du Synth
    // Rack : le transport et le mixeur n'ont pas de potentiomètre à toucher, et
    // c'était exactement la raison pour laquelle on ne pouvait pas les
    // apprendre.
    apprendre_.onClick = [this] { if (onLearn) onLearn(&apprendre_); };
    addAndMakeVisible(apprendre_);

    attente_.setJustificationType(juce::Justification::centredLeft);
    attente_.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
    attente_.setColour(juce::Label::textColourId, juce::Colours::gold);
    addAndMakeVisible(attente_);
}

MidiLearnWindow::~MidiLearnWindow() = default;

void MidiLearnWindow::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff23262b)); }

void MidiLearnWindow::resized() {
    auto zone = getLocalBounds().reduced(10);
    auto bas = zone.removeFromBottom(34);
    toutEffacer_.setBounds(bas.removeFromRight(140).reduced(0, 2));
    bas.removeFromRight(8);
    apprendre_.setBounds(bas.removeFromRight(140).reduced(0, 2));
    attente_.setBounds(bas);
    zone.removeFromBottom(8);
    defilement_.setBounds(zone);
    vide_.setBounds(zone);
    contenu_->setSize(zone.getWidth(), contenu_->getHeight());
}

void MidiLearnWindow::setWaiting(const juce::String& quoi) {
    attente_.setText(quoi.isEmpty() ? juce::String()
                                     : juce::String::fromUTF8(u8"Tournez un potentiomètre pour ")
                                           + quoi + juce::String::fromUTF8(u8"..."),
                      juce::dontSendNotification);
}

void MidiLearnWindow::setRows(std::vector<Row> rows) {
    const bool aucune = rows.empty();
    contenu_->setRows(std::move(rows), onRemove);
    contenu_->setSize(defilement_.getWidth(), contenu_->getHeight());
    vide_.setVisible(aucune);
    defilement_.setVisible(!aucune);
    toutEffacer_.setEnabled(!aucune);
}

} // namespace vsm::app::ui
