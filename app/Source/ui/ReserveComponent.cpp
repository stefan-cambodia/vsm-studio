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
    // D608.1 : RELIER UN FICHIER MANQUANT — actif sur une ligne MANQUANT choisie, et seulement là.
    relier_.onClick = [this] {
        const auto fichier = fichierChoisi();
        if (fichier.isNotEmpty() && onRelier) onRelier(fichier);
    };
    relier_.setEnabled(false);
    addAndMakeVisible(relier_);
    // D618.1 : POSER UN FICHIER DE LA RÉSERVE À LA TÊTE — actif sur une ligne dont le fichier est sur le disque.
    poser_.onClick = [this] {
        const auto fichiers = fichiersChoisis();   // D630.1 : toutes les lignes choisies
        if (!fichiers.isEmpty() && onPoser) onPoser(fichiers);
    };
    poser_.setEnabled(false);
    addAndMakeVisible(poser_);
    // D628.1 : POSER À L'ORIGINE — actif sur une ligne dont le fichier est sur le disque ET porte une origine (BWF).
    poserOrigine_.onClick = [this] {
        const auto fichiers = fichiersChoisis();   // D630.1
        if (!fichiers.isEmpty() && onPoserOrigine) onPoserOrigine(fichiers);
    };
    poserOrigine_.setEnabled(false);
    addAndMakeVisible(poserOrigine_);
    // D617.1 : CHERCHER LES MANQUANTS DANS UN DOSSIER — actif dès qu'une ligne manque ; la remarque dit ce qui reste.
    chercher_.onClick = [this] { if (onChercher) onChercher(); };
    chercher_.setEnabled(false);
    addAndMakeVisible(chercher_);
    remarque_.setFont(juce::Font(juce::FontOptions(13.0f)));
    remarque_.setColour(juce::Label::textColourId, vsm::ui::Palette::accentAmber);   // un fait, la convention de D152
    remarque_.setJustificationType(juce::Justification::topLeft);
    // REPLIÉE, JAMAIS COMPRIMÉE (regardé sur la photo de D617 : la remarque d'une recherche, longue, tenait sur une
    // ligne écrasée à 70 % de sa largeur — illisible, quand la lisibilité est le besoin de l'utilisateur).
    remarque_.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(remarque_);
    liste_.setRowHeight(26);
    // D630.1 : PLUSIEURS LIGNES — Ctrl+clic ajoute, Maj+clic une plage, un clic seul n'en garde qu'une (la règle de JUCE).
    liste_.setMultipleSelectionEnabled(true);
    liste_.setColour(juce::ListBox::backgroundColourId, vsm::ui::Palette::panel);
    addAndMakeVisible(liste_);
    retraduire();
}

void ReserveComponent::retraduire() {
    titre_.setText(tr(u8"Les fichiers du projet, relus à l'ouverture"), juce::dontSendNotification);
    nettoyer_.setButtonText(tr(u8"Nettoyer le dossier du projet..."));
    nettoyer_.setTooltip(tr(u8"Les fichiers que rien ne désigne vont à la corbeille, après confirmation."));
    rafraichir_.setButtonText(tr(u8"Rafraîchir"));
    relier_.setButtonText(tr(u8"Relier..."));
    relier_.setTooltip(tr(u8"Désigner le fichier qui remplace le fichier manquant choisi : tout ce qui le désignait le "
                          u8"désigne. Pris hors du dossier du projet, il y est copié."));
    poser_.setButtonText(tr(u8"Poser à la tête"));
    poser_.setTooltip(tr(u8"Une piste audio neuve, sous la piste choisie, joue le fichier choisi à partir de la tête. "
                         u8"Le fichier reste où il est : rien n'est copié."));
    poserOrigine_.setButtonText(tr(u8"Poser à l'origine"));
    poserOrigine_.setTooltip(tr(u8"Comme « Poser à la tête », mais à l'instant que le fichier porte en lui (sa référence de "
                                u8"temps BWF). Grisé sur un fichier qui n'en porte pas."));
    chercher_.setButtonText(tr(u8"Chercher dans un dossier..."));
    chercher_.setTooltip(tr(u8"Chaque fichier manquant est cherché par son nom dans le dossier désigné et ses "
                            u8"sous-dossiers. Un seul candidat : il est relié ; plusieurs : rien n'est relié, et c'est dit."));
    total_.setText(total(), juce::dontSendNotification);
    repaint();
}

void ReserveComponent::setLignes(std::vector<Ligne> lignes, bool sansDossier) {
    lignes_ = std::move(lignes);
    sansDossier_ = sansDossier;
    nettoyer_.setEnabled(!sansDossier_);
    total_.setText(total(), juce::dontSendNotification);
    liste_.deselectAllRows();
    relier_.setEnabled(false);
    poser_.setEnabled(false);
    poserOrigine_.setEnabled(false);
    chercher_.setEnabled(std::any_of(lignes_.begin(), lignes_.end(), [](const Ligne& l) { return l.etat == Etat::Manquant; }));
    liste_.updateContent();
    liste_.repaint();
    repaint();
}

void ReserveComponent::setRemarque(const juce::String& texte) {
    remarque_.setText(texte, juce::dontSendNotification);
    resized();
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

juce::String ReserveComponent::texteOrigine(const Ligne& l) {   // D629.1
    return l.octets >= 0 && l.origine >= 0.0 ? juce::String(l.origine, 3) + " s" : juce::String::fromUTF8("\xe2\x80\x94");
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
    // Fichier et « Désigné par » se partagent ce que les colonnes fixes laissent (D629.1 : « Origine », 90 px, entre
    // « Désigné par » et « État »).
    const int fixes = 80 + 120 + 80 + 90 + 170;
    const int reste = std::max(160, largeur - fixes - 16);
    const int fichier = reste * 45 / 100, designe = reste - fichier;
    std::vector<juce::Range<int>> c;
    int x = 8;
    for (int w : {fichier, 80, 120, 80, designe, 90, 170}) {
        c.push_back({x, x + w - 6});
        x += w;
    }
    return c;
}

int ReserveComponent::getNumRows() { return sansDossier_ ? 0 : static_cast<int>(lignes_.size()); }

void ReserveComponent::selectedRowsChanged(int) {
    // D630.1 : « Relier... » sur UNE ligne manquante ; « Poser » si TOUTES les choisies sont sur le disque (D618.1) ;
    // « à l'origine » si toutes portent aussi une origine (D628.1) — tout ou rien, comme le geste.
    int choisies = 0;
    bool surLeDisque = true, origines = true, manquante = false;
    for (int i = 0; i < liste_.getNumSelectedRows(); ++i) {
        const int ligne = liste_.getSelectedRow(i);
        if (ligne < 0 || ligne >= static_cast<int>(lignes_.size())) continue;
        const auto& l = lignes_[static_cast<size_t>(ligne)];
        ++choisies;
        surLeDisque = surLeDisque && l.octets >= 0;
        origines = origines && l.origine >= 0.0;
        manquante = l.etat == Etat::Manquant;
    }
    relier_.setEnabled(choisies == 1 && manquante);
    poser_.setEnabled(choisies > 0 && surLeDisque);
    poserOrigine_.setEnabled(choisies > 0 && surLeDisque && origines);
}

double ReserveComponent::origineChoisie() const {
    if (liste_.getNumSelectedRows() != 1) return -1.0;   // D630.1 : l'origine d'UNE ligne
    const int ligne = liste_.getSelectedRow();
    return ligne >= 0 && ligne < static_cast<int>(lignes_.size()) ? lignes_[static_cast<size_t>(ligne)].origine : -1.0;
}

double ReserveComponent::origineDuFichier(const juce::AudioFormatReader& lecteur) {   // D628.1
    // JUCE pose la clé dès qu'un bloc `bext` existe, même à 0 : sa PRÉSENCE dit « une origine », sa valeur dit où.
    const auto& meta = lecteur.metadataValues;
    if (lecteur.sampleRate <= 0.0 || !meta.containsKey(juce::WavAudioFormat::bwavTimeReference)) return -1.0;
    const juce::int64 reference = meta.getValue(juce::WavAudioFormat::bwavTimeReference, "-1").getLargeIntValue();
    return reference >= 0 ? static_cast<double>(reference) / lecteur.sampleRate : -1.0;
}

bool ReserveComponent::choisir(const juce::String& fichier, bool aussi) {
    for (size_t i = 0; i < lignes_.size(); ++i)
        if (lignes_[i].fichier == fichier) {
            liste_.selectRow(static_cast<int>(i), false, !aussi);   // D630.1 : `aussi` n'efface pas le choix (Ctrl+clic)
            selectedRowsChanged(static_cast<int>(i));
            return true;
        }
    return false;
}

juce::StringArray ReserveComponent::fichiersChoisis() const {   // D630.1 : l'ordre de la liste
    juce::StringArray fichiers;
    for (size_t i = 0; i < lignes_.size(); ++i)
        if (liste_.isRowSelected(static_cast<int>(i))) fichiers.add(lignes_[i].fichier);
    return fichiers;
}

juce::String ReserveComponent::fichierChoisi() const {
    const int ligne = liste_.getSelectedRow();
    return ligne >= 0 && ligne < static_cast<int>(lignes_.size()) ? lignes_[static_cast<size_t>(ligne)].fichier
                                                                  : juce::String();
}

void ReserveComponent::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) {
    if (row < 0 || row >= static_cast<int>(lignes_.size())) return;
    const auto& l = lignes_[static_cast<size_t>(row)];
    g.setColour(selected ? vsm::ui::Palette::panelRaised : vsm::ui::Palette::panel);
    g.fillRect(0, 0, width, height);
    // D630.1 : LA LIGNE CHOISIE SE VOIT — regardé sur la photo de D630 : avec deux lignes choisies, le fond à peine plus
    // clair ne disait pas lesquelles ; un liseré sarcelle à gauche, la couleur de ce qui est en jeu.
    if (selected) {
        g.setColour(vsm::ui::Palette::accentTeal);
        g.fillRect(0, 0, 4, height);
    }
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
    ecrire(5, texteOrigine(l), vsm::ui::Palette::textSecondary, juce::Justification::centredRight);   // D629.1
    // L'ÉTAT EN COULEUR : ce qui joue en sarcelle, ce qui manque en rouge (« regarde MAINTENANT »), l'inutilisé en
    // ambre (un fait, la convention du volet d'ouverture, D152) ; ce qui attend, en gris.
    const juce::Colour couleur = l.etat == Etat::Joue       ? vsm::ui::Palette::accentTeal
                               : l.etat == Etat::Manquant   ? vsm::ui::Palette::accentRed
                               : l.etat == Etat::Inutilise  ? vsm::ui::Palette::accentAmber
                                                            : vsm::ui::Palette::textSecondary;
    g.setFont(juce::Font(juce::FontOptions(13.0f, l.etat == Etat::Manquant ? juce::Font::bold : juce::Font::plain)));
    ecrire(6, nomDeLEtat(l.etat), couleur);
}

void ReserveComponent::paint(juce::Graphics& g) {
    g.fillAll(vsm::ui::Palette::background);
    if (enTete_.isEmpty()) return;
    g.setColour(vsm::ui::Palette::panelRaised);
    g.fillRect(enTete_);
    g.setColour(vsm::ui::Palette::textSecondary);
    g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    const auto c = colonnes(enTete_.getWidth());
    const juce::String noms[7] = {tr(u8"Fichier"), tr(u8"Durée"), tr(u8"Format"), tr(u8"Taille"), tr(u8"Désigné par"),
                                  tr(u8"Origine"), tr(u8"État")};   // D629.1
    for (size_t i = 0; i < c.size(); ++i)
        g.drawText(noms[i], enTete_.getX() + c[i].getStart(), enTete_.getY(), c[i].getLength(), enTete_.getHeight(),
                   (i == 1 || i == 3 || i == 5) ? juce::Justification::centredRight : juce::Justification::centredLeft);
}

void ReserveComponent::resized() {
    auto zone = getLocalBounds().reduced(12);
    titre_.setBounds(zone.removeFromTop(24));
    auto bas = zone.removeFromBottom(34);
    // D618.1 : CINQ BOUTONS — à gauche ce qui agit sur la ligne choisie, à droite ce qui agit sur tout ; serrés ensemble
    // si la fenêtre est plus étroite que leur somme, plutôt que de se chevaucher. D628.1 : six, « Poser à l'origine » à
    // côté de « Poser à la tête ».
    const float echelle = std::min(1.0f, static_cast<float>(bas.getWidth())
                                             / (120.0f + 190.0f + 190.0f + 250.0f + 260.0f + 130.0f));
    const auto largeur = [echelle](int l) { return juce::roundToInt(static_cast<float>(l) * echelle); };
    relier_.setBounds(bas.removeFromLeft(largeur(120)).reduced(2));
    poser_.setBounds(bas.removeFromLeft(largeur(190)).reduced(2));
    poserOrigine_.setBounds(bas.removeFromLeft(largeur(190)).reduced(2));   // D628.1
    rafraichir_.setBounds(bas.removeFromRight(largeur(130)).reduced(2));
    nettoyer_.setBounds(bas.removeFromRight(largeur(260)).reduced(2));
    chercher_.setBounds(bas.removeFromRight(largeur(250)).reduced(2));   // D617.1
    // D617.1 : LE TOTAL SUR SA LIGNE — quatre boutons ne lui laissent plus la place à leur gauche ; la remarque au-dessus,
    // seulement quand elle dit quelque chose.
    total_.setBounds(zone.removeFromBottom(22));
    remarque_.setBounds(remarque_.getText().isEmpty() ? juce::Rectangle<int>() : zone.removeFromBottom(40));
    zone.removeFromTop(4);
    enTete_ = zone.removeFromTop(24);
    liste_.setBounds(zone.reduced(0, 2));
}

} // namespace vsm::app::ui
