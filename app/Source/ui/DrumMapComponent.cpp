#include "DrumMapComponent.h"
#include "DrumVoiceNames.h"
#include "Langue.h"
#include <iterator>
#include <set>

namespace vsm::app::ui {

namespace {
constexpr int kElleMeme = 1;               ///< l'identifiant de « elle-même » dans la liste
int idDe(int note) { return note + 2; }    ///< l'identifiant d'une note de sortie (0 est réservé par JUCE)
/// D622.1 : LES GRILLES QU'UN RANG PEUT PRENDRE — le libellé écrit au projet (`gridResolutionFromLabel`), et le mot qui
/// le montre. L'identifiant JUCE d'une grille est son rang ici + 2 ; 1 est « globale ».
constexpr int kGlobale = 1;
constexpr const char* kGrilles[] = {"1/2", "1/4", "1/8", "1/16", "1/32", "1/64",
                                    "1/4T", "1/8T", "1/16T", "1/32T", "1/4.", "1/8.", "1/16."};
juce::String texteDeGrille(const char* libelle) {
    // Le modificateur se lit au dernier caractère du libellé (« T » triolet, « . » pointée) ; le mot, traduit.
    const juce::String l(libelle);
    if (l.endsWithChar('T')) return l.dropLastCharacters(1) + " " + tr(u8"triolet");
    if (l.endsWithChar('.')) return l.dropLastCharacters(1) + " " + tr(u8"pointée");
    return l;
}
int idDeGrille(const std::string& libelle) {
    if (libelle.empty()) return kGlobale;
    for (size_t i = 0; i < std::size(kGrilles); ++i)
        if (libelle == kGrilles[i]) return static_cast<int>(i) + 2;
    return -1;
}
} // namespace

DrumMapComponent::DrumMapComponent() {
    titre_.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    titre_.setColour(juce::Label::textColourId, vsm::ui::Palette::accentTeal);
    addAndMakeVisible(titre_);
    aide_.setFont(juce::Font(juce::FontOptions(13.0f)));
    aide_.setColour(juce::Label::textColourId, vsm::ui::Palette::textSecondary);
    aide_.setJustificationType(juce::Justification::topLeft);
    aide_.setMinimumHorizontalScale(1.0f);   // D622.1 : repliée, jamais comprimée (regardé sur la photo — la leçon de D617)
    addAndMakeVisible(aide_);
    identite_.onClick = [this] { if (onIdentite) onIdentite(); };
    addAndMakeVisible(identite_);
    adapter_.onClick = [this] { if (onAdapter) onAdapter(); };   // D610.1
    addAndMakeVisible(adapter_);
    remarque_.setFont(juce::Font(juce::FontOptions(13.0f)));
    remarque_.setColour(juce::Label::textColourId, vsm::ui::Palette::accentAmber);   // un fait, la convention de D152
    remarque_.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(remarque_);
    defilement_.setViewedComponent(&contenu_, false);
    defilement_.setScrollBarsShown(true, false);
    addAndMakeVisible(defilement_);
    retraduire();
}

void DrumMapComponent::retraduire() {
    aide_.setText(tr(u8"La note écrite, la note que la machine joue, et la grille du rang (« globale » : celle de la "
                     u8"barre). Le matériau ne change pas."),
                  juce::dontSendNotification);
    identite_.setButtonText(tr(u8"Remettre à l'identité"));
    adapter_.setButtonText(tr(u8"Adapter General MIDI à la machine"));
    adapter_.setTooltip(tr(u8"Chaque pièce General MIDI que la piste écrit va à la pièce de la même catégorie la plus "
                           u8"proche sur la machine ; ce qui n'a pas d'équivalent reste lui-même, et c'est dit."));
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
    // D636.1 : L'ORDRE DE LA CARTE, s'il est posé — les notes qu'il nomme dans son ordre, les autres à la suite par numéro
    // croissant ; sans ordre, le numéro croissant, comme avant.
    std::vector<int> ordre;
    for (const uint8_t h : piste.drumOrder)
        if (ecrites.erase(static_cast<int>(h)) > 0) ordre.push_back(static_cast<int>(h));
    ordre.insert(ordre.end(), ecrites.begin(), ecrites.end());
    rangees_.clear();
    contenu_.removeAllChildren();
    for (size_t rang = 0; rang < ordre.size(); ++rang) {
        const int ecrite = ordre[rang];
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
        // D622.1 : LA GRILLE DU RANG — « globale », ou une valeur ; un libellé que la liste ne porte pas (un projet écrit
        // ailleurs) y est ajouté tel quel, jamais montré autrement qu'il n'est.
        r.grille = std::make_unique<juce::ComboBox>();
        r.grille->addItem(tr(u8"globale"), kGlobale);
        for (size_t i = 0; i < std::size(kGrilles); ++i) r.grille->addItem(texteDeGrille(kGrilles[i]), static_cast<int>(i) + 2);
        const auto ici = piste.drumGrid.find(static_cast<uint8_t>(ecrite));
        const std::string libelle = ici == piste.drumGrid.end() ? std::string() : ici->second;
        int idGrille = idDeGrille(libelle);
        if (idGrille < 0) {
            idGrille = static_cast<int>(std::size(kGrilles)) + 2;
            r.grille->addItem(juce::String::fromUTF8(libelle.c_str()), idGrille);
        }
        r.grille->setSelectedId(idGrille, juce::dontSendNotification);
        r.grille->setTooltip(tr(u8"La grille de ce rang : une frappe posée ici s'y aimante et dure une case ; « Quantifier » "
                                u8"l'y aligne. « globale » : la grille de la barre."));
        r.grille->onChange = [this, ecrite, combo = r.grille.get()] {
            const int id = combo->getSelectedId();
            const bool connu = id >= 2 && id < static_cast<int>(std::size(kGrilles)) + 2;
            if (onGrille && (id == kGlobale || connu))
                onGrille(ecrite, id == kGlobale ? std::string() : std::string(kGrilles[static_cast<size_t>(id - 2)]));
        };
        // D636.1 : ▲ ET ▼ — grisés en haut et en bas de la liste ; l'appui remonte TOUT l'ordre affiché, la ligne échangée.
        // GRISÉS, ET CELA SE VOIT : `juce::ArrowButton` dessine un bouton désactivé comme les autres (vu sur la photo) — la
        // flèche qui ne peut rien faire prend une encre éteinte.
        const auto encre = [](bool actif) { return actif ? vsm::ui::Palette::accentTeal : vsm::ui::Palette::textSecondary.withAlpha(0.3f); };
        r.monter = std::make_unique<juce::ArrowButton>("monter " + juce::String(ecrite), 0.75f, encre(rang > 0));
        r.descendre = std::make_unique<juce::ArrowButton>("descendre " + juce::String(ecrite), 0.25f, encre(rang + 1 < ordre.size()));
        r.monter->setTooltip(tr(u8"Monter ce rang : le piano roll replié suit l'ordre de la carte."));
        r.descendre->setTooltip(tr(u8"Descendre ce rang : le piano roll replié suit l'ordre de la carte."));
        r.monter->setEnabled(rang > 0);
        r.descendre->setEnabled(rang + 1 < ordre.size());
        r.monter->onClick = [this, rang] { deplacerLigne(rang, true); };
        r.descendre->onClick = [this, rang] { deplacerLigne(rang, false); };
        contenu_.addAndMakeVisible(*r.nom);
        contenu_.addAndMakeVisible(*r.sortie);
        contenu_.addAndMakeVisible(*r.grille);
        // D637.1 : LE MUET DU RANG — une case ; la lecture se tait, le fichier garde les frappes.
        r.muet = std::make_unique<juce::ToggleButton>("M");
        r.muet->setToggleState(piste.drumRowMuted(static_cast<uint8_t>(ecrite)), juce::dontSendNotification);
        r.muet->setTooltip(tr(u8"Rang muet : ses frappes ne se jouent plus, ni à la lecture ni à l'export audio. Le "
                              u8"fichier les garde, et le clavier les joue encore."));
        r.muet->onClick = [this, ecrite, bouton = r.muet.get()] {
            if (onMuet) onMuet(ecrite, bouton->getToggleState());
        };
        // D640.1 : LE SOLO DU RANG — seuls les rangs en solo de la piste jouent ; le muet l'emporte.
        r.solo = std::make_unique<juce::ToggleButton>("S");
        r.solo->setToggleState(piste.drumSolo.count(static_cast<uint8_t>(ecrite)) > 0, juce::dontSendNotification);
        r.solo->setTooltip(tr(u8"Rang en solo : seuls les rangs en solo de cette piste jouent. Un rang muet reste muet, et "
                              u8"les autres pistes ne sont pas touchées."));
        r.solo->onClick = [this, ecrite, bouton = r.solo.get()] {
            if (onSolo) onSolo(ecrite, bouton->getToggleState());
        };
        contenu_.addAndMakeVisible(*r.monter);
        contenu_.addAndMakeVisible(*r.descendre);
        contenu_.addAndMakeVisible(*r.muet);
        contenu_.addAndMakeVisible(*r.solo);
        rangees_.push_back(std::move(r));
    }
    resized();
    repaint();
}

void DrumMapComponent::deplacerLigne(size_t rang, bool monter) {
    if (rang >= rangees_.size() || (monter && rang == 0) || (!monter && rang + 1 >= rangees_.size())) return;
    std::vector<uint8_t> ordre;
    for (const auto& r : rangees_) ordre.push_back(static_cast<uint8_t>(r.ecrite));
    std::swap(ordre[rang], ordre[monter ? rang - 1 : rang + 1]);
    // DIFFÉRÉ : l'application écrit le projet puis rappelle `setPiste`, qui REFAIT la liste — et détruirait le bouton
    // pressé pendant son propre `onClick`. Le message suivant, le bouton a fini de répondre.
    juce::MessageManager::callAsync([moi = juce::Component::SafePointer<DrumMapComponent>(this), ordre] {
        if (moi != nullptr && moi->onOrdre) moi->onOrdre(ordre);
    });
}

bool DrumMapComponent::deplacer(int ecrite, bool monter) {
    for (auto& r : rangees_)
        if (r.ecrite == ecrite) {
            auto& bouton = monter ? r.monter : r.descendre;
            if (!bouton->isEnabled()) return false;
            bouton->onClick();   // LE MÊME RAPPEL que le clic de la souris
            return true;
        }
    return false;
}

bool DrumMapComponent::basculerMuet(int ecrite) {
    for (auto& r : rangees_)
        if (r.ecrite == ecrite) {
            r.muet->setToggleState(!r.muet->getToggleState(), juce::dontSendNotification);   // ce que le clic fait à la case
            r.muet->onClick();                                                                 // puis LE MÊME RAPPEL
            return true;
        }
    return false;
}

bool DrumMapComponent::basculerSolo(int ecrite) {
    for (auto& r : rangees_)
        if (r.ecrite == ecrite) {
            r.solo->setToggleState(!r.solo->getToggleState(), juce::dontSendNotification);   // ce que le clic fait à la case
            r.solo->onClick();                                                                 // puis LE MÊME RAPPEL
            return true;
        }
    return false;
}

juce::StringArray DrumMapComponent::solosPourReleve() const {
    juce::StringArray solos;
    for (const auto& r : rangees_)
        if (r.solo->getToggleState()) solos.add(juce::String(r.ecrite));
    return solos;
}

juce::StringArray DrumMapComponent::muetsPourReleve() const {
    juce::StringArray muets;
    for (const auto& r : rangees_)
        if (r.muet->getToggleState()) muets.add(juce::String(r.ecrite));
    return muets;
}

void DrumMapComponent::setRemarque(const juce::String& texte) {
    remarque_.setText(texte, juce::dontSendNotification);
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

bool DrumMapComponent::choisirGrille(int ecrite, const std::string& libelle) {
    for (auto& r : rangees_)
        if (r.ecrite == ecrite) {
            const int id = idDeGrille(libelle);
            if (id < 0 || r.grille->indexOfItemId(id) < 0) return false;
            r.grille->setSelectedId(id, juce::sendNotificationSync);   // LE MÊME CHEMIN que la souris : `onChange`
            return true;
        }
    return false;
}

juce::StringArray DrumMapComponent::grillesPourReleve() const {
    juce::StringArray lignes;
    for (const auto& r : rangees_)
        lignes.add(juce::String(r.ecrite) + juce::String::fromUTF8(" \xe2\x86\x92 ") + r.grille->getText());
    return lignes;
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
    aide_.setBounds(zone.removeFromTop(36));   // D622.1 : deux lignes
    auto bas = zone.removeFromBottom(34);
    identite_.setBounds(bas.removeFromRight(220).reduced(2));
    adapter_.setBounds(bas.removeFromRight(300).reduced(2));   // D610.1
    remarque_.setBounds(zone.removeFromBottom(40));
    zone.removeFromTop(6);
    defilement_.setBounds(zone);
    const int largeur = std::max(200, zone.getWidth() - defilement_.getScrollBarThickness());
    constexpr int kHauteur = 30;
    contenu_.setSize(largeur, static_cast<int>(rangees_.size()) * kHauteur);
    int y = 0;
    for (auto& r : rangees_) {
        juce::Rectangle<int> ligne(0, y, largeur, kHauteur);
        r.monter->setBounds(ligne.removeFromLeft(24).reduced(3, 6));       // D636.1
        r.descendre->setBounds(ligne.removeFromLeft(24).reduced(3, 6));
        r.nom->setBounds(ligne.removeFromLeft(largeur * 42 / 100).reduced(2));
        r.grille->setBounds(ligne.removeFromRight(std::max(130, largeur * 22 / 100)).reduced(2));   // D622.1
        r.solo->setBounds(ligne.removeFromRight(52).reduced(2));   // D640.1
        r.muet->setBounds(ligne.removeFromRight(52).reduced(2));   // D637.1
        r.sortie->setBounds(ligne.reduced(2));
        y += kHauteur;
    }
}

} // namespace vsm::app::ui
