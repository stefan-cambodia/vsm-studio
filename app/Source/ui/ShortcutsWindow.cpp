#include "ShortcutsWindow.h"

namespace vsm::app::ui {

/// Le contenu défilant : une ligne par commande, groupée par famille. Des
/// composants réels et non un tableau dessiné : chaque ligne porte deux
/// boutons, et un bouton qui se dessine sans exister se clique mal.
class ShortcutsWindow::Contenu : public juce::Component {
public:
    void reconstruire(const vsm::interchange::ShortcutTable* table,
                       const std::function<void(vsm::interchange::ShortcutId)>& rebind,
                       const std::function<void(vsm::interchange::ShortcutId)>& reset) {
        lignes_.clear();
        removeAllChildren();
        if (table == nullptr) { setSize(getWidth(), 0); return; }

        juce::String famille;
        for (const auto& commande : vsm::interchange::shortcutCommands()) {
            if (juce::String(commande.category) != famille) {
                famille = commande.category;
                auto titre = std::make_unique<juce::Label>();
                titre->setText(famille, juce::dontSendNotification);
                titre->setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
                titre->setColour(juce::Label::textColourId, juce::Colours::skyblue);
                addAndMakeVisible(*titre);
                lignes_.push_back({std::move(titre), nullptr, nullptr, nullptr, true});
            }

            auto libelle = std::make_unique<juce::Label>();
            libelle->setText(juce::String::fromUTF8(commande.label), juce::dontSendNotification);
            libelle->setFont(juce::Font(juce::FontOptions(15.0f)));
            addAndMakeVisible(*libelle);

            const std::string touche = table->keyFor(commande.id);
            auto bouton = std::make_unique<juce::TextButton>(
                touche.empty() ? juce::String::fromUTF8(u8"(désactivé)")
                               : juce::String::fromUTF8(touche.c_str()));
            const auto id = commande.id;
            bouton->onClick = [rebind, id] { if (rebind) rebind(id); };
            addAndMakeVisible(*bouton);

            std::unique_ptr<juce::TextButton> defaut;
            if (table->isCustom(commande.id)) {
                // LE RETOUR AU DÉFAUT N'EXISTE QUE QUAND IL Y A QUELQUE CHOSE À
                // DÉFAIRE : un bouton toujours présent et inerte quinze fois
                // sur seize n'apprend rien.
                defaut = std::make_unique<juce::TextButton>(juce::String::fromUTF8(u8"↺"));
                defaut->setTooltip(juce::String::fromUTF8(u8"Rétablir ") + commande.defaultKey);
                defaut->onClick = [reset, id] { if (reset) reset(id); };
                addAndMakeVisible(*defaut);
            }
            lignes_.push_back({std::move(libelle), std::move(bouton), std::move(defaut), nullptr, false});
        }

        // CE QUI NE BOUGE PAS EST LISTÉ AUSSI. Voir `fixedShortcuts()`.
        auto titre = std::make_unique<juce::Label>();
        titre->setText(juce::String::fromUTF8(u8"Navigation (non modifiable)"),
                        juce::dontSendNotification);
        titre->setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        titre->setColour(juce::Label::textColourId, juce::Colours::skyblue);
        addAndMakeVisible(*titre);
        lignes_.push_back({std::move(titre), nullptr, nullptr, nullptr, true});

        for (const auto& fixe : vsm::interchange::fixedShortcuts()) {
            auto libelle = std::make_unique<juce::Label>();
            libelle->setText(juce::String::fromUTF8(fixe.label), juce::dontSendNotification);
            libelle->setFont(juce::Font(juce::FontOptions(15.0f)));
            addAndMakeVisible(*libelle);
            auto touche = std::make_unique<juce::Label>();
            touche->setText(juce::String::fromUTF8(fixe.keys), juce::dontSendNotification);
            touche->setFont(juce::Font(juce::FontOptions(15.0f)));
            touche->setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(*touche);
            lignes_.push_back({std::move(libelle), nullptr, nullptr, std::move(touche), false});
        }

        setSize(getWidth(), static_cast<int>(lignes_.size()) * kHauteur + 6);
        resized();
    }

    void resized() override {
        int y = 0;
        for (auto& ligne : lignes_) {
            auto zone = juce::Rectangle<int>(0, y, getWidth(), kHauteur).reduced(4, 2);
            y += kHauteur;
            if (ligne.titre) { ligne.libelle->setBounds(zone); continue; }
            if (ligne.bouton) {
                if (ligne.defaut) ligne.defaut->setBounds(zone.removeFromRight(34));
                zone.removeFromRight(4);
                ligne.bouton->setBounds(zone.removeFromRight(180));
                zone.removeFromRight(8);
            } else if (ligne.toucheFixe) {
                ligne.toucheFixe->setBounds(zone.removeFromRight(180));
                zone.removeFromRight(8);
            }
            ligne.libelle->setBounds(zone);
        }
    }

private:
    static constexpr int kHauteur = 30;
    struct Ligne {
        std::unique_ptr<juce::Label> libelle;
        std::unique_ptr<juce::TextButton> bouton;
        std::unique_ptr<juce::TextButton> defaut;
        std::unique_ptr<juce::Label> toucheFixe;
        bool titre = false;
    };
    std::vector<Ligne> lignes_;
};

ShortcutsWindow::ShortcutsWindow() : contenu_(std::make_unique<Contenu>()) {
    defilement_.setViewedComponent(contenu_.get(), false);
    defilement_.setScrollBarsShown(true, false);
    addAndMakeVisible(defilement_);

    attente_.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
    attente_.setColour(juce::Label::textColourId, juce::Colours::gold);
    addAndMakeVisible(attente_);

    exporter_.onClick = [this] { if (onExport) onExport(); };
    addAndMakeVisible(exporter_);
    toutRetablir_.onClick = [this] { if (onResetAll) onResetAll(); };
    addAndMakeVisible(toutRetablir_);
    setWantsKeyboardFocus(true);
}

ShortcutsWindow::~ShortcutsWindow() = default;

void ShortcutsWindow::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff23262b)); }

void ShortcutsWindow::resized() {
    auto zone = getLocalBounds().reduced(10);
    auto bas = zone.removeFromBottom(34);
    toutRetablir_.setBounds(bas.removeFromRight(150).reduced(0, 2));
    bas.removeFromRight(8);
    exporter_.setBounds(bas.removeFromRight(200).reduced(0, 2));
    attente_.setBounds(bas);
    zone.removeFromBottom(8);
    defilement_.setBounds(zone);
    contenu_->setSize(zone.getWidth(), contenu_->getHeight());
}

void ShortcutsWindow::setTable(const vsm::interchange::ShortcutTable* table) {
    contenu_->setSize(defilement_.getWidth() > 0 ? defilement_.getWidth() : 600,
                       contenu_->getHeight());
    contenu_->reconstruire(table, onRebind, onReset);
}

bool ShortcutsWindow::keyPressed(const juce::KeyPress& key) {
    // PENDANT UNE CAPTURE, LA TOUCHE EST UNE DONNÉE, PAS UNE COMMANDE. Sans ce
    // détournement, appuyer sur « Espace » pour le réassigner lancerait la
    // lecture -- et on ne pourrait jamais réassigner une touche déjà prise,
    // c'est-à-dire aucune de celles qu'on veut changer.
    if (onKeyCaptured && onKeyCaptured(key)) return true;
    return juce::Component::keyPressed(key);
}

void ShortcutsWindow::setCapturing(const juce::String& commande) {
    attente_.setText(commande.isEmpty()
                          ? juce::String()
                          : juce::String::fromUTF8(u8"Appuyez sur la nouvelle touche pour « ")
                                + commande + juce::String::fromUTF8(u8" » (Échap : annuler)"),
                      juce::dontSendNotification);
    if (!commande.isEmpty()) grabKeyboardFocus();
}

} // namespace vsm::app::ui
