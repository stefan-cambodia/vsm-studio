#include "ReserveComponent.h"
#include "Langue.h"
#include <algorithm>
#include <cmath>

namespace vsm::app::ui {

ReserveComponent::ReserveComponent() {
    titre_.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    titre_.setColour(juce::Label::textColourId, vsm::ui::Palette::accentTeal);
    addAndMakeVisible(titre_);
    total_.setFont(juce::Font(juce::FontOptions(13.0f)));
    total_.setColour(juce::Label::textColourId, vsm::ui::Palette::textPrimary);
    total_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(total_);
    nettoyer_.onClick = [this] { if (onNettoyer) onNettoyer(); };
    rafraichir_.onClick = [this] { if (onRafraichir) onRafraichir(); };
    addAndMakeVisible(nettoyer_);
    addAndMakeVisible(rafraichir_);
    liste_.setRowHeight(26);
    liste_.setColour(juce::ListBox::backgroundColourId, vsm::ui::Palette::panel);
    addAndMakeVisible(liste_);
    retraduire();
}

void ReserveComponent::retraduire() {
    titre_.setText(tr(u8"Les fichiers du projet, relus à l'ouverture"), juce::dontSendNotification);
    nettoyer_.setButtonText(tr(u8"Nettoyer le dossier du projet..."));
    nettoyer_.setTooltip(tr(u8"Les fichiers que rien ne désigne vont à la corbeille, après confirmation."));
    rafraichir_.setButtonText(tr(u8"Rafraîchir"));
    total_.setText(total(), juce::dontSendNotification);
    repaint();
}

void ReserveComponent::setLignes(std::vector<Ligne> lignes, bool sansDossier) {
    lignes_ = std::move(lignes);
    sansDossier_ = sansDossier;
    nettoyer_.setEnabled(!sansDossier_);
    total_.setText(total(), juce::dontSendNotification);
    liste_.updateContent();
    liste_.repaint();
    repaint();
}

juce::String ReserveComponent::total() const {
    if (sansDossier_) return tr(u8"Projet jamais enregistré : la réserve se lit dans le dossier du projet.");
    int manquants = 0, inutilises = 0;
    int64_t octets = 0;
    for (const auto& l : lignes_) {
        if (l.etat == Etat::Manquant) ++manquants;
        if (l.etat == Etat::Inutilise || l.etat == Etat::GardePourLHistoire) ++inutilises;
        if (l.octets > 0) octets += l.octets;
    }
    return tr(u8"%1 fichier(s) · %2 Mo · %3 manquant(s) · %4 inutilisé(s)")
        .replace("%1", juce::String(static_cast<int>(lignes_.size())))
        .replace("%2", juce::String(static_cast<double>(octets) / 1048576.0, 1))
        .replace("%3", juce::String(manquants))
        .replace("%4", juce::String(inutilises));
}

juce::String ReserveComponent::texteDuree(const Ligne& l) {
    return l.duree > 0.0 ? juce::String(l.duree, 2) + " s" : juce::String::fromUTF8("\xe2\x80\x94");
}

juce::String ReserveComponent::texteFormat(const Ligne& l) {
    if (l.frequence <= 0.0) return juce::String::fromUTF8("\xe2\x80\x94");
    const double k = l.frequence / 1000.0;
    const juce::String frequence = (std::abs(k - std::round(k)) < 1e-9 ? juce::String(static_cast<int>(std::round(k)))
                                                                         : juce::String(k, 1)) + " kHz";
    const juce::String canaux = l.canaux == 1 ? tr(u8"mono")
                              : l.canaux == 2 ? tr(u8"stéréo")
                                              : tr(u8"%1 canaux").replace("%1", juce::String(l.canaux));
    return frequence + " " + canaux;
}

juce::String ReserveComponent::texteTaille(const Ligne& l) {
    if (l.octets < 0) return juce::String::fromUTF8("\xe2\x80\x94");
    if (l.octets < 1048576) return tr(u8"%1 ko").replace("%1", juce::String((l.octets + 1023) / 1024));
    return tr(u8"%1 Mo").replace("%1", juce::String(static_cast<double>(l.octets) / 1048576.0, 1));
}

juce::String ReserveComponent::nomDeLEtat(Etat etat) {
    switch (etat) {
        case Etat::Joue:               return tr(u8"joué");
        case Etat::EnReserve:          return tr(u8"en réserve");
        case Etat::Manquant:           return tr(u8"MANQUANT");
        case Etat::Inutilise:          return tr(u8"inutilisé");
        case Etat::GardePourLHistoire: return tr(u8"gardé pour l'historique");
    }
    return {};
}

std::vector<juce::Range<int>> ReserveComponent::colonnes(int largeur) {
    // Fichier et « Désigné par » se partagent ce que les quatre colonnes fixes laissent.
    const int fixes = 80 + 120 + 80 + 170;
    const int reste = std::max(160, largeur - fixes - 16);
    const int fichier = reste * 45 / 100, designe = reste - fichier;
    std::vector<juce::Range<int>> c;
    int x = 8;
    for (int w : {fichier, 80, 120, 80, designe, 170}) {
        c.push_back({x, x + w - 6});
        x += w;
    }
    return c;
}

int ReserveComponent::getNumRows() { return sansDossier_ ? 0 : static_cast<int>(lignes_.size()); }

void ReserveComponent::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) {
    if (row < 0 || row >= static_cast<int>(lignes_.size())) return;
    const auto& l = lignes_[static_cast<size_t>(row)];
    g.setColour(selected ? vsm::ui::Palette::panelRaised : vsm::ui::Palette::panel);
    g.fillRect(0, 0, width, height);
    const auto c = colonnes(width);
    const auto ecrire = [&](size_t i, const juce::String& texte, juce::Colour couleur,
                            juce::Justification j = juce::Justification::centredLeft) {
        g.setColour(couleur);
        g.drawFittedText(texte, c[i].getStart(), 0, c[i].getLength(), height, j, 1, 0.85f);
    };
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    ecrire(0, l.fichier, vsm::ui::Palette::textPrimary);
    ecrire(1, texteDuree(l), vsm::ui::Palette::textSecondary, juce::Justification::centredRight);
    ecrire(2, texteFormat(l), vsm::ui::Palette::textSecondary);
    ecrire(3, texteTaille(l), vsm::ui::Palette::textSecondary, juce::Justification::centredRight);
    ecrire(4, l.designePar, vsm::ui::Palette::textPrimary);
    // L'ÉTAT EN COULEUR : ce qui joue en sarcelle, ce qui manque en rouge (« regarde MAINTENANT »), l'inutilisé en
    // ambre (un fait, la convention du volet d'ouverture, D152) ; ce qui attend, en gris.
    const juce::Colour couleur = l.etat == Etat::Joue       ? vsm::ui::Palette::accentTeal
                               : l.etat == Etat::Manquant   ? vsm::ui::Palette::accentRed
                               : l.etat == Etat::Inutilise  ? vsm::ui::Palette::accentAmber
                                                            : vsm::ui::Palette::textSecondary;
    g.setFont(juce::Font(juce::FontOptions(13.0f, l.etat == Etat::Manquant ? juce::Font::bold : juce::Font::plain)));
    ecrire(5, nomDeLEtat(l.etat), couleur);
}

void ReserveComponent::paint(juce::Graphics& g) {
    g.fillAll(vsm::ui::Palette::background);
    if (enTete_.isEmpty()) return;
    g.setColour(vsm::ui::Palette::panelRaised);
    g.fillRect(enTete_);
    g.setColour(vsm::ui::Palette::textSecondary);
    g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    const auto c = colonnes(enTete_.getWidth());
    const juce::String noms[6] = {tr(u8"Fichier"), tr(u8"Durée"), tr(u8"Format"), tr(u8"Taille"), tr(u8"Désigné par"),
                                  tr(u8"État")};
    for (size_t i = 0; i < c.size(); ++i)
        g.drawText(noms[i], enTete_.getX() + c[i].getStart(), enTete_.getY(), c[i].getLength(), enTete_.getHeight(),
                   (i == 1 || i == 3) ? juce::Justification::centredRight : juce::Justification::centredLeft);
}

void ReserveComponent::resized() {
    auto zone = getLocalBounds().reduced(12);
    titre_.setBounds(zone.removeFromTop(24));
    auto bas = zone.removeFromBottom(34);
    rafraichir_.setBounds(bas.removeFromRight(130).reduced(2));
    nettoyer_.setBounds(bas.removeFromRight(260).reduced(2));
    total_.setBounds(bas);
    zone.removeFromTop(4);
    enTete_ = zone.removeFromTop(24);
    liste_.setBounds(zone.reduced(0, 2));
}

} // namespace vsm::app::ui
