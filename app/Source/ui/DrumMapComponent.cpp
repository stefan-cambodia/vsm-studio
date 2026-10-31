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
        contenu_.addAndMakeVisible(*r.nom);
        contenu_.addAndMakeVisible(*r.sortie);
        contenu_.addAndMakeVisible(*r.grille);
        rangees_.push_back(std::move(r));
    }
    resized();
    repaint();
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
        r.nom->setBounds(ligne.removeFromLeft(largeur * 42 / 100).reduced(2));
        r.grille->setBounds(ligne.removeFromRight(std::max(130, largeur * 22 / 100)).reduced(2));   // D622.1
        r.sortie->setBounds(ligne.reduced(2));
        y += kHauteur;
    }
}

} // namespace vsm::app::ui
