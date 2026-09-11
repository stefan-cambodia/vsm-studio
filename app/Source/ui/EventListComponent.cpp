#include "EventListComponent.h"
#include "Langue.h"
#include "vsm/sequencer/NoteEdit.h"

using namespace vsm::ui;
using vsm::sequencer::EventKind;
using vsm::sequencer::EventRow;

namespace vsm::app::ui {

namespace {
constexpr int kColPosition = 1, kColNature = 2, kColCanal = 3,
              kColPremier = 4, kColSecond = 5, kColDuree = 6;
// D94 : UNE SEULE LISTE, lue à la construction et par `retraduire()`. « N° »
// est plus large depuis que la hauteur s'y écrit en toutes lettres (« C#2 (37) »).
struct Colonne { int id; const char8_t* nom; int largeur; };
constexpr Colonne kColonnes[] = {
    { kColPosition, u8"Position", 130 }, { kColNature, u8"Nature", 120 },
    { kColCanal, u8"Canal", 60 },        { kColPremier, u8"N°", 110 },
    { kColSecond, u8"Valeur", 90 },      { kColDuree, u8"Durée (ticks)", 110 },
};
}

EventListComponent::EventListComponent() {
    titre_.setText(tr(u8"Liste — %1").replace("%1", tr(u8"aucune piste")), juce::dontSendNotification);
    titre_.setFont(juce::Font(juce::FontOptions(14.0f).withStyle("Bold")));
    titre_.setColour(juce::Label::textColourId, Palette::textPrimary);
    addAndMakeVisible(titre_);

    filtre_.addItem(vsm::app::ui::tr(u8"Tout"), 1);   // D83
    for (int k = 0; k < 6; ++k)
        filtre_.addItem(vsm::app::ui::tr(juce::String::fromUTF8(vsm::sequencer::eventKindLabel(static_cast<EventKind>(k)).c_str())),
                        k + 2);
    filtre_.setSelectedId(1, juce::dontSendNotification);
    filtre_.onChange = [this] { rebuild(); };
    addAndMakeVisible(filtre_);

    compte_.setColour(juce::Label::textColourId, Palette::textSecondary);
    compte_.setFont(juce::Font(juce::FontOptions(12.0f)));
    addAndMakeVisible(compte_);

    // LES COLONNES DISENT CE QU'ELLES PORTENT, et « n° » / « valeur » plutôt
    // que des noms de famille : une même colonne montre la hauteur d'une note,
    // le numéro d'un contrôleur et celui d'un programme.
    for (const auto& c : kColonnes)
        table_.getHeader().addColumn(tr(c.nom), c.id, c.largeur);
    table_.setHeaderHeight(22);
    table_.setRowHeight(20);
    table_.setColour(juce::ListBox::backgroundColourId, Palette::panel);
    addAndMakeVisible(table_);
}

void EventListComponent::setProject(vsm::sequencer::Project* project) {
    project_ = project;
    rebuild();
}

void EventListComponent::setActiveTrack(int trackIndex) {
    activeTrack_ = trackIndex;
    rebuild();
}

void EventListComponent::refresh() { rebuild(); }

void EventListComponent::rebuild() {
    lignes_.clear();
    juce::String nom = tr(u8"aucune piste");
    if (project_ != nullptr && activeTrack_ >= 0
        && static_cast<size_t>(activeTrack_) < project_->tracks.size()) {
        const auto& piste = project_->tracks[static_cast<size_t>(activeTrack_)];
        nom = juce::String::fromUTF8(piste.name.c_str());
        auto toutes = vsm::sequencer::listTrackEvents(piste);
        const int choix = filtre_.getSelectedId();
        if (choix <= 1) {
            lignes_ = std::move(toutes);
        } else {
            const auto voulue = static_cast<EventKind>(choix - 2);
            for (auto& l : toutes) if (l.kind == voulue) lignes_.push_back(l);
        }
    }
    titre_.setText(tr(u8"Liste — %1").replace("%1", nom), juce::dontSendNotification);
    // LE COMPTE EST ÉCRIT, toujours : c'est le premier chiffre qu'on vient
    // chercher devant une reconstruction, et « 0 » dit qu'on a bien regardé.
    compte_.setText(tr(u8"%1 événement(s)").replace("%1", juce::String(static_cast<int>(lignes_.size()))),
                    juce::dontSendNotification);
    table_.updateContent();
    table_.repaint();
}

void EventListComponent::retraduire() {
    // D94 : les colonnes et le filtre sont posés une fois ; le titre et le
    // compte sont refaits par `rebuild()`. La sélection du filtre se lit AVANT
    // de renommer (D78) : JUCE rend 0 pour une entrée dont le texte a changé.
    for (const auto& c : kColonnes) table_.getHeader().setColumnName(c.id, tr(c.nom));
    const int choisie = filtre_.getSelectedId();
    filtre_.changeItemText(1, tr(u8"Tout"));
    for (int k = 0; k < 6; ++k)
        filtre_.changeItemText(k + 2, tr(juce::String::fromUTF8(
                                          vsm::sequencer::eventKindLabel(static_cast<EventKind>(k)).c_str())));
    filtre_.setSelectedId(choisie, juce::dontSendNotification);
    rebuild();
}

void EventListComponent::resized() {
    auto zone = getLocalBounds().reduced(8);
    auto haut = zone.removeFromTop(24);
    titre_.setBounds(haut.removeFromLeft(260));
    haut.removeFromLeft(8);
    filtre_.setBounds(haut.removeFromLeft(180));
    haut.removeFromLeft(8);
    compte_.setBounds(haut);
    zone.removeFromTop(6);
    table_.setBounds(zone);
}

void EventListComponent::paint(juce::Graphics& g) { g.fillAll(Palette::background); }

void EventListComponent::paintRowBackground(juce::Graphics& g, int row, int w, int h,
                                             bool selected) {
    juce::ignoreUnused(w, h);
    if (selected) g.fillAll(Palette::accentTeal.withAlpha(0.35f));
    else if (row % 2) g.fillAll(juce::Colour(0x10ffffff));
}

juce::String EventListComponent::texteDe(const EventRow& ligne, int columnId) const {
    switch (columnId) {
        case kColPosition: {
            if (project_ == nullptr) return juce::String(static_cast<int>(ligne.tick));
            // MESURE·TEMPS ET LE TICK BRUT : la mesure pour se repérer dans le
            // morceau, le tick parce que c'est ce que le fichier porte et ce
            // que la chaîne d'analyse écrit. Donner l'un sans l'autre
            // obligerait à convertir de tête.
            const int parNoire = project_->ticksPerQuarterNote;
            const auto parMesure = project_->timeSignatureMap.ticksPerBar(0, parNoire);
            const auto mesure = parMesure > 0 ? ligne.tick / parMesure : 0;
            const auto reste = parMesure > 0 ? ligne.tick % parMesure : ligne.tick;
            const auto temps = parNoire > 0 ? reste / parNoire : 0;
            const auto ticks = parNoire > 0 ? reste % parNoire : 0;
            return juce::String(static_cast<int>(mesure + 1)) + "."
                   + juce::String(static_cast<int>(temps + 1))
                   + (ticks != 0 ? "+" + juce::String(static_cast<int>(ticks)) : juce::String())
                   + "  (" + juce::String(static_cast<int>(ligne.tick)) + ")";
        }
        case kColNature: return vsm::app::ui::tr(juce::String::fromUTF8(vsm::sequencer::eventKindLabel(ligne.kind).c_str()));
        case kColCanal:  return juce::String(ligne.channel + 1);
        case kColPremier:
            // Les familles sans « numéro » montrent un tiret plutôt qu'un zéro
            // : un zéro se lit comme une valeur, et l'on chercherait ce qu'il
            // veut dire.
            if (ligne.kind == EventKind::PitchBend || ligne.kind == EventKind::ChannelPressure)
                return juce::String::fromUTF8(u8"—");
            // LA HAUTEUR D'UNE NOTE SE LIT, elle ne se compte pas. « 37 » ne
            // dit rien à personne ; « C#2 (37) », si -- et c'est exactement
            // l'argument que la colonne Position applique déjà en écrivant
            // « 1.3+168 (1128) » plutôt que le seul tick. Le nombre reste, à
            // côté : c'est lui qu'on tape dans un autre logiciel.
            //
            // TROUVÉ EN OUVRANT UNE VRAIE RECONSTRUCTION, où la colonne
            // alignait 29, 37, 48 sur une piste de basse pendant que le piano
            // roll, à trois centimètres de là, écrivait C#2 sur son clavier.
            // `noteNumberToName` existait déjà et servait au piano roll : la
            // fonction était là, elle n'était pas montrée ici.
            if (ligne.kind == EventKind::Note || ligne.kind == EventKind::PolyPressure)
                return juce::String(vsm::sequencer::noteNumberToName(
                           static_cast<uint8_t>(ligne.first)))
                       + " (" + juce::String(ligne.first) + ")";
            return juce::String(ligne.first);
        case kColSecond:
            return ligne.kind == EventKind::ProgramChange ? juce::String::fromUTF8(u8"—")
                                                           : juce::String(ligne.second);
        case kColDuree:
            return ligne.kind == EventKind::Note ? juce::String(static_cast<int>(ligne.length))
                                                  : juce::String::fromUTF8(u8"—");
        default: return {};
    }
}

void EventListComponent::paintCell(juce::Graphics& g, int row, int columnId, int w, int h,
                                    bool selected) {
    juce::ignoreUnused(selected);
    if (row < 0 || row >= static_cast<int>(lignes_.size())) return;
    const auto& ligne = lignes_[static_cast<size_t>(row)];
    g.setColour(columnId == kColNature ? Palette::accentAmber : Palette::textPrimary);
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawText(texteDe(ligne, columnId), juce::Rectangle<int>(0, 0, w, h).reduced(6, 0),
                juce::Justification::centredLeft, true);
}

void EventListComponent::cellDoubleClicked(int row, int, const juce::MouseEvent&) {
    if (row < 0 || row >= static_cast<int>(lignes_.size())) return;
    if (onSeekRequested) onSeekRequested(lignes_[static_cast<size_t>(row)].tick);
}

void EventListComponent::deleteKeyPressed(int lastRowSelected) {
    if (project_ == nullptr || activeTrack_ < 0
        || static_cast<size_t>(activeTrack_) >= project_->tracks.size())
        return;
    if (lastRowSelected < 0 || lastRowSelected >= static_cast<int>(lignes_.size())) return;
    const EventRow ligne = lignes_[static_cast<size_t>(lastRowSelected)];
    if (onEditStarted) onEditStarted(juce::String::fromUTF8(u8"Retirer un événement"));
    auto& piste = project_->tracks[static_cast<size_t>(activeTrack_)];
    if (!vsm::sequencer::removeTrackEvent(piste, ligne)) {
        // PANNE MUETTE INTERDITE : une suppression qui ne trouve plus sa cible
        // le DIT. `removeTrackEvent` refuse plutôt que de retirer le voisin,
        // et taire ce refus ferait croire l'événement parti.
        std::fputs(juce::String::fromUTF8(
                        u8"Liste : cet événement n'existe plus — rien n'a été retiré.\n")
                        .toRawUTF8(), stderr);
        rebuild();
        return;
    }
    rebuild();
    if (onEventsChanged) onEventsChanged();
}

} // namespace vsm::app::ui
