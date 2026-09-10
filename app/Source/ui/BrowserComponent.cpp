#include "BrowserComponent.h"
#include "Langue.h"

namespace vsm::app::ui {

using vsm::interchange::BrowserItem;
using vsm::interchange::BrowserItemKind;

namespace {
constexpr const char* kPrefixe = "vsm-browser:";
constexpr int kHauteurLigne = 26;
/// Assez large pour « Échantillon » en entier : c'est le mot le plus long, et
/// le tronquer est précisément ce qui rendait la colonne inutile.
constexpr int kLargeurFamille = 100;

juce::Colour couleurDe(BrowserItemKind kind) {
    switch (kind) {
        case BrowserItemKind::Machine: return juce::Colours::skyblue;
        case BrowserItemKind::Preset:  return juce::Colours::lightgreen;
        case BrowserItemKind::Profile: return juce::Colours::orange;
        case BrowserItemKind::Sample:  return juce::Colours::violet;
        case BrowserItemKind::EffectPreset: return juce::Colours::lightseagreen;
    }
    return juce::Colours::white;
}
} // namespace

juce::String BrowserComponent::dragDescriptionFor(const BrowserItem& item) {
    return juce::String(kPrefixe) + juce::String(static_cast<int>(item.kind)) + ":"
           + juce::String::fromUTF8(item.reference.c_str());
}

bool BrowserComponent::parseDragDescription(const juce::String& description,
                                             BrowserItemKind& kind, juce::String& reference) {
    if (!description.startsWith(kPrefixe)) return false;
    const juce::String reste = description.substring(static_cast<int>(std::strlen(kPrefixe)));
    const int deuxPoints = reste.indexOfChar(':');
    if (deuxPoints <= 0) return false;
    const int numero = reste.substring(0, deuxPoints).getIntValue();
    if (numero < 0 || numero > static_cast<int>(BrowserItemKind::EffectPreset)) return false;
    kind = static_cast<BrowserItemKind>(numero);
    reference = reste.substring(deuxPoints + 1);
    return true;
}

/// La liste : dessinée plutôt que composée. Ici, contrairement à la fenêtre des
/// raccourcis, les lignes ne portent aucun bouton et peuvent se compter par
/// milliers -- un dossier d'échantillons en contient facilement autant, et
/// autant de composants JUCE mettraient une seconde à s'ouvrir.
class BrowserComponent::Liste : public juce::Component {
public:
    explicit Liste(BrowserComponent& parent) : parent_(parent) {}

    void setItems(const std::vector<BrowserItem>* items) {
        items_ = items;
        setSize(getWidth(), items_ ? static_cast<int>(items_->size()) * kHauteurLigne + 4 : 0);
        repaint();
    }

    void paint(juce::Graphics& g) override {
        if (items_ == nullptr) return;
        const auto zone = g.getClipBounds();
        const int premier = std::max(0, zone.getY() / kHauteurLigne);
        const int dernier = std::min(static_cast<int>(items_->size()),
                                      (zone.getBottom() / kHauteurLigne) + 1);
        for (int i = premier; i < dernier; ++i) {
            const auto& entree = (*items_)[static_cast<size_t>(i)];
            auto ligne = juce::Rectangle<int>(0, i * kHauteurLigne, getWidth(), kHauteurLigne);
            // D88 : LA LIGNE SURVOLÉE, ET ELLE SEULE. `fillAll` remplit toute la
            // zone à repeindre : le pointeur au-dessus d'une ligne éclaircissait
            // la liste entière -- 301 562 pixels sur une capture anglaise où la
            // souris passait par là, pris d'abord pour un effet de la largeur.
            if (i == survol_) {
                g.setColour(juce::Colour(0x18ffffff));
                g.fillRect(ligne);
            }
            g.setColour(couleurDe(entree.kind));
            g.setFont(juce::Font(juce::FontOptions(14.0f)));
            g.drawText(juce::String::fromUTF8(vsm::interchange::browserKindShortLabel(entree.kind)),
                        ligne.removeFromLeft(kLargeurFamille).reduced(4, 0),
                        juce::Justification::centredLeft);
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(juce::FontOptions(15.0f)));
            g.drawText(juce::String::fromUTF8(entree.name.c_str()),
                        ligne.removeFromLeft(ligne.getWidth() * 3 / 5), juce::Justification::centredLeft);
            g.setColour(juce::Colours::grey);
            g.setFont(juce::Font(juce::FontOptions(13.0f)));
            g.drawText(juce::String::fromUTF8(entree.origin.c_str()), ligne,
                        juce::Justification::centredRight);
            // D32.1 : LE « ▶ » SUR LA LIGNE SURVOLÉE, et seulement sur un
            // ÉCHANTILLON. Le clic simple la joue déjà ; le triangle est là
            // pour le faire savoir, parce qu'un geste qu'on ne devine pas est
            // un geste que personne n'utilise. Sur un preset ou une machine il
            // n'apparaît pas : il promettrait un son qu'on ne sait pas rendre
            // sans instrument.
            if (i == survol_ && entree.kind == vsm::interchange::BrowserItemKind::Sample) {
                g.setColour(juce::Colours::violet.brighter(0.4f));
                g.setFont(juce::Font(juce::FontOptions(13.0f)));
                g.drawText(juce::String::fromUTF8(u8"▶"),
                            juce::Rectangle<int>(0, i * kHauteurLigne, kLargeurFamille,
                                                  kHauteurLigne).reduced(3, 0),
                            juce::Justification::centredRight);
            }
        }
    }

    void mouseMove(const juce::MouseEvent& e) override {
        const int rang = e.y / kHauteurLigne;
        if (rang == survol_) return;
        survol_ = rang;
        repaint();
    }
    void mouseExit(const juce::MouseEvent&) override { survol_ = -1; repaint(); }

    // D32.1 : LE CLIC SIMPLE PRÉ-ÉCOUTE UN ÉCHANTILLON, comme chez Live.
    // Pas de bouton séparé : la ligne entière est la cible, ce qui est
    // exactement ce dont on a besoin quand on parcourt deux cents fichiers à
    // la volée. Le double-clic continue d'APPLIQUER -- les deux gestes ne se
    // gênent pas, le premier clic d'un double-clic pré-écoute puis le second
    // applique, ce qui est l'ordre qu'on veut.
    void mouseDown(const juce::MouseEvent& e) override {
        const auto* entree = itemAt(e.y);
        if (entree == nullptr) return;
        if (entree->kind != vsm::interchange::BrowserItemKind::Sample) return;
        if (parent_.onAudition) parent_.onAudition(*entree);
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override {
        const auto* entree = itemAt(e.y);
        if (entree != nullptr && parent_.onApply) parent_.onApply(*entree);
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (parent_.isDragAndDropActive()) return;
        const auto* entree = itemAt(e.y);
        if (entree == nullptr) return;
        parent_.startDragging(BrowserComponent::dragDescriptionFor(*entree), this);
    }

private:
    const BrowserItem* itemAt(int y) const {
        if (items_ == nullptr) return nullptr;
        const int rang = y / kHauteurLigne;
        if (rang < 0 || rang >= static_cast<int>(items_->size())) return nullptr;
        return &(*items_)[static_cast<size_t>(rang)];
    }

    BrowserComponent& parent_;
    const std::vector<BrowserItem>* items_ = nullptr;
    int survol_ = -1;
};

BrowserComponent::BrowserComponent() : liste_(std::make_unique<Liste>(*this)) {
    recherche_.onTextChange = [this] { refilter(); };
    addAndMakeVisible(recherche_);

    compte_.setFont(juce::Font(juce::FontOptions(13.0f)));
    compte_.setColour(juce::Label::textColourId, juce::Colours::grey);
    compte_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(compte_);

    defilement_.setViewedComponent(liste_.get(), false);
    defilement_.setScrollBarsShown(true, false);
    addAndMakeVisible(defilement_);
    retraduire();
}

void BrowserComponent::retraduire() {
    recherche_.setTextToShowWhenEmpty(tr(u8"Chercher une machine, un preset, un échantillon..."),
                                      juce::Colours::grey);   // D87
    repaint();
}

BrowserComponent::~BrowserComponent() = default;

void BrowserComponent::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff23262b)); }

void BrowserComponent::resized() {
    auto zone = getLocalBounds().reduced(8);
    auto haut = zone.removeFromTop(30);
    compte_.setBounds(haut.removeFromRight(120));
    haut.removeFromRight(8);
    recherche_.setBounds(haut);
    zone.removeFromTop(6);
    defilement_.setBounds(zone);
    // D88 : LA LARGEUR VISIBLE, PAS CELLE DE LA ZONE. La barre de défilement
    // occupe le bord droit ; une liste aussi large que la zone passait dessous,
    // et la colonne de la source, alignée à droite, sortait « Parc VSI… » -- dans
    // les deux langues.
    liste_->setSize(defilement_.getMaximumVisibleWidth(), liste_->getHeight());
}

void BrowserComponent::setItems(std::vector<BrowserItem> items) {
    tous_ = std::move(items);
    refilter();
}

void BrowserComponent::refilter() {
    filtres_ = vsm::interchange::filterBrowserItems(tous_, recherche_.getText().toStdString());
    liste_->setItems(&filtres_);
    liste_->setSize(defilement_.getMaximumVisibleWidth(), liste_->getHeight());   // D88
    compte_.setText(juce::String(static_cast<int>(filtres_.size())) + " / "
                        + juce::String(static_cast<int>(tous_.size())),
                    juce::dontSendNotification);
}

} // namespace vsm::app::ui
