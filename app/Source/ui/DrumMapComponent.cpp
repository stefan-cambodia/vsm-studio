#include "DrumMapComponent.h"
#include "DrumVoiceNames.h"
#include "Langue.h"
#include <set>

namespace vsm::app::ui {

namespace {
constexpr int kElleMeme = 1;               ///< l'identifiant de « elle-même » dans la liste
int idDe(int note) { return note + 2; }    ///< l'identifiant d'une note de sortie (0 est réservé par JUCE)
} // namespace

DrumMapComponent::DrumMapComponent() {
    titre_.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    titre_.setColour(juce::Label::textColourId, vsm::ui::Palette::accentTeal);
    addAndMakeVisible(titre_);
    aide_.setFont(juce::Font(juce::FontOptions(13.0f)));
    aide_.setColour(juce::Label::textColourId, vsm::ui::Palette::textSecondary);
    addAndMakeVisible(aide_);
    identite_.onClick = [this] { if (onIdentite) onIdentite(); };
    addAndMakeVisible(identite_);
    defilement_.setViewedComponent(&contenu_, false);
    defilement_.setScrollBarsShown(true, false);
    addAndMakeVisible(defilement_);
    retraduire();
}

void DrumMapComponent::retraduire() {
    aide_.setText(tr(u8"À gauche, la note écrite ; à droite, la note que la machine joue. Le matériau ne change pas."),
                  juce::dontSendNotification);
    identite_.setButtonText(tr(u8"Remettre à l'identité"));
}

juce::String DrumMapComponent::nomEcrit(int note) const {
    // LA NOTE ÉCRITE SE NOMME EN GENERAL MIDI : c'est la convention des parties écrites et des projets reconstruits —
    // la machine, elle, peut ne rien avoir à cet endroit (la 909 en 41), et c'est justement ce que la carte répare.
    const std::string gm = drumVoiceName(std::string(), static_cast<uint8_t>(note));
    return juce::String(note) + "  " + nomDeNote(note)
           + (gm.empty() ? juce::String() : "  " + tr(juce::String::fromUTF8(gm.c_str())));
}

juce::String DrumMapComponent::nomJoue(int note) const {
    const std::string piece = drumVoiceName(instrument_, static_cast<uint8_t>(note));
    return (piece.empty() ? tr(u8"note %1").replace("%1", juce::String(note))
                          : tr(juce::String::fromUTF8(piece.c_str())) + " (" + juce::String(note) + ")");
}

void DrumMapComponent::setPiste(size_t index, const vsm::sequencer::Track& piste) {
    piste_ = index;
    instrument_ = piste.instrumentId;
    titre_.setText(tr(u8"Piste « %1 » — %2").replace("%1", juce::String::fromUTF8(piste.name.c_str()))
                       .replace("%2", juce::String::fromUTF8(piste.instrumentId.c_str())),
                   juce::dontSendNotification);
    std::set<int> ecrites;
    for (const auto& n : piste.notes) ecrites.insert(n.number);
    for (const auto& [ecrite, jouee] : piste.drumMap) ecrites.insert(ecrite);
    rangees_.clear();
    contenu_.removeAllChildren();
    for (int ecrite : ecrites) {
        Rangee r;
        r.ecrite = ecrite;
        r.nom = std::make_unique<juce::Label>();
        r.nom->setText(nomEcrit(ecrite), juce::dontSendNotification);
        r.nom->setFont(juce::Font(juce::FontOptions(13.0f)));
        r.nom->setColour(juce::Label::textColourId, vsm::ui::Palette::textPrimary);
        r.sortie = std::make_unique<juce::ComboBox>();
        r.sortie->addItem(tr(u8"elle-même"), kElleMeme);
        // LES PIÈCES QUE LA MACHINE NOMME, dans l'ordre des hauteurs ; et la note déjà choisie si la machine ne la
        // nomme pas (une carte relue d'un fichier) — jamais une sortie montrée autrement qu'elle n'est.
        const int choisie = piste.drumOutput(static_cast<uint8_t>(ecrite));
        for (int n = 0; n < 128; ++n)
            if (!drumVoiceName(instrument_, static_cast<uint8_t>(n)).empty() || (n == choisie && n != ecrite))
                r.sortie->addItem(nomJoue(n), idDe(n));
        r.sortie->setSelectedId(choisie == ecrite ? kElleMeme : idDe(choisie), juce::dontSendNotification);
        r.sortie->onChange = [this, ecrite, combo = r.sortie.get()] {
            const int id = combo->getSelectedId();
            if (onChoix) onChoix(ecrite, id == kElleMeme ? -1 : id - 2);
        };
        contenu_.addAndMakeVisible(*r.nom);
        contenu_.addAndMakeVisible(*r.sortie);
        rangees_.push_back(std::move(r));
    }
    resized();
    repaint();
}

bool DrumMapComponent::choisir(int ecrite, int jouee) {
    for (auto& r : rangees_)
        if (r.ecrite == ecrite) {
            const int id = jouee < 0 ? kElleMeme : idDe(jouee);
            if (r.sortie->indexOfItemId(id) < 0) return false;
            r.sortie->setSelectedId(id, juce::sendNotificationSync);   // LE MÊME CHEMIN que la souris : `onChange`
            return true;
        }
    return false;
}

juce::StringArray DrumMapComponent::lignesPourReleve() const {
    juce::StringArray lignes;
    for (const auto& r : rangees_)
        lignes.add(r.nom->getText() + juce::String::fromUTF8(" \xe2\x86\x92 ") + r.sortie->getText());
    return lignes;
}

void DrumMapComponent::paint(juce::Graphics& g) { g.fillAll(vsm::ui::Palette::background); }

void DrumMapComponent::resized() {
    auto zone = getLocalBounds().reduced(12);
    titre_.setBounds(zone.removeFromTop(24));
    aide_.setBounds(zone.removeFromTop(22));
    auto bas = zone.removeFromBottom(34);
    identite_.setBounds(bas.removeFromRight(220).reduced(2));
    zone.removeFromTop(6);
    defilement_.setBounds(zone);
    const int largeur = std::max(200, zone.getWidth() - defilement_.getScrollBarThickness());
    constexpr int kHauteur = 30;
    contenu_.setSize(largeur, static_cast<int>(rangees_.size()) * kHauteur);
    int y = 0;
    for (auto& r : rangees_) {
        juce::Rectangle<int> ligne(0, y, largeur, kHauteur);
        r.nom->setBounds(ligne.removeFromLeft(largeur * 45 / 100).reduced(2));
        r.sortie->setBounds(ligne.reduced(2));
        y += kHauteur;
    }
}

} // namespace vsm::app::ui
