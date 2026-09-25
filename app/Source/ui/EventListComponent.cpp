#include "EventListComponent.h"
#include "Langue.h"
#include "vsm/sequencer/NoteEdit.h"
#include <algorithm>

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

    filtre_.addItem(vsm::app::ui::tr(u8"Tous"), 1);   // D83
    for (int k = 0; k < 6; ++k)
        filtre_.addItem(vsm::app::ui::tr(juce::String::fromUTF8(vsm::sequencer::eventKindLabel(static_cast<EventKind>(k)).c_str())),
                        k + 2);
    filtre_.setSelectedId(1, juce::dontSendNotification);
    filtre_.onChange = [this] { rebuild(); rafraichirAjouter(); };
    addAndMakeVisible(filtre_);

    // D352 : AJOUTER UN ÉVÉNEMENT. À côté du filtre, et c'est LUI qui dit la
    // nature : « Tous » ne peut rien créer, et le bouton se grise alors plutôt
    // que de choisir à la place de l'utilisateur. L'événement naît à la TÊTE DE
    // LECTURE, avec les valeurs d'usine de sa famille — on le règle ensuite dans
    // la liste, qui sait modifier depuis D348.
    ajouter_.setTooltip(tr(u8"Ajouter un événement de la nature choisie, à la tête de lecture"));
    ajouter_.onClick = [this] {
        const auto nature = natureDuFiltre();
        if (!nature) return;
        if (ajouterEvenement(*nature, tete_ > 0 ? tete_ : 0)) {
            if (onEventsCreated) onEventsCreated();   // D352 : couvrir la note d'un clip
            rebuild();
            if (onEventsChanged) onEventsChanged();
        }
    };
    addAndMakeVisible(ajouter_);

    compte_.setColour(juce::Label::textColourId, Palette::textSecondary);
    addAndMakeVisible(compte_);

    // LES COLONNES DISENT CE QU'ELLES PORTENT, et « n° » / « valeur » plutôt
    // que des noms de famille : une même colonne montre la hauteur d'une note,
    // le numéro d'un contrôleur et celui d'un programme.
    for (const auto& c : kColonnes)
        table_.getHeader().addColumn(tr(c.nom), c.id, c.largeur);
    table_.setHeaderHeight(22);
    table_.setRowHeight(20);
    table_.setColour(juce::ListBox::backgroundColourId, Palette::panel);
    // D348 : l'éditeur en place, caché jusqu'au double-clic. Entrée valide,
    // Échap annule, perdre le clavier annule aussi — comme une case de tableur.
    saisie_.setMultiLine(false);
    saisie_.setReturnKeyStartsNewLine(false);
    saisie_.setSelectAllWhenFocused(true);
    saisie_.setColour(juce::TextEditor::backgroundColourId, Palette::panelRaised);
    saisie_.setColour(juce::TextEditor::textColourId, Palette::textPrimary);
    saisie_.setColour(juce::TextEditor::outlineColourId, Palette::accentAmber);
    saisie_.setFont(juce::Font(juce::FontOptions(13.0f)));
    saisie_.setVisible(false);
    saisie_.onReturnKey = [this] {
        const bool fait = validerSaisie();
        fermerSaisie();
        if (fait) { rebuild(); if (onEventsChanged) onEventsChanged(); }
    };
    saisie_.onEscapeKey = [this] { fermerSaisie(); };
    // LA CASE NE SE REFERME PAS SUR UNE PERTE DE CLAVIER, ET C'EST UNE DÉCISION.
    //
    // Elle le faisait — « comme une case de tableur » — et c'était faux deux
    // fois. D'abord parce que le clavier part sans que l'utilisateur ait rien
    // fait : ouverte par un banc, la saisie se refermait dans la foulée (le
    // journal disait « saisie ouverte, bornes 428,100 90x20 » PUIS « saisie
    // refermée », et la photo ne montrait rien, à trois délais différents ; une
    // photo verte obtenue une fois n'était que la chance du moment où le focus
    // arrivait). Ensuite parce que jeter ce qu'on est en train de taper parce
    // qu'un panneau de fond a pris le clavier est une perte de travail, et ce
    // dépôt traite une perte silencieuse comme un défaut.
    //
    // Elle se ferme donc sur ENTRÉE (valider), ÉCHAP (abandonner), sur
    // l'ouverture d'une AUTRE case, et quand la liste change sous elle
    // (`rebuild`, `resized`) — là, sa cellule ne désigne plus la même chose.
    // D347 : L'EN-TÊTE PORTE LES COULEURS DU LOGICIEL. `TableHeaderComponent`
    // garde sinon le gris clair de `LookAndFeel_V4` : 2 095 x 22 px de clair au
    // milieu d'une application sombre — mesuré 164 de luminance moyenne sur la
    // photo, la SEULE surface claire du logiciel, et personne ne l'avait vue
    // parce qu'un fond qu'on oublie ne casse rien et ne fait échouer aucun test.
    // `tools/surfaces-claires.py` la cherche désormais sur n'importe quelle photo.
    auto& entete = table_.getHeader();
    // `gridLineStrong` ET NON `panelRaised` : les rangées alternent `panel`
    // (0x1f1f24) et le même éclairci d'un seizième (0x2a2a2e), et `panelRaised`
    // tombe ENTRE LES DEUX — l'en-tête se confondait avec une rangée sur deux
    // (1,05 de contraste). Mesuré : 1,46 contre la rangée paire et 1,27 contre
    // l'impaire, 9,02 avec son texte.
    entete.setColour(juce::TableHeaderComponent::backgroundColourId, Palette::gridLineStrong);
    entete.setColour(juce::TableHeaderComponent::outlineColourId, Palette::border);
    entete.setColour(juce::TableHeaderComponent::textColourId, Palette::textPrimary);
    entete.setColour(juce::TableHeaderComponent::highlightColourId,
                      Palette::accentAmber.withAlpha(0.25f));
    addAndMakeVisible(table_);
    // APRÈS LA TABLE, ET C'EST STRUCTUREL. L'ordre des enfants EST l'ordre de
    // peinture : déclarée avant `table_`, la saisie était ajoutée avant elle et
    // peinte dessous. Un `toFront()` au moment de l'ouverture le corrigeait...
    // jusqu'à ce qu'un autre geste remette la table devant — mesuré : le relevé
    // disait « saisie ouverte, bornes 428,100 90x20 » et la photo ne montrait
    // rien, à trois délais différents. Un ordre juste ne se rattrape pas à
    // l'exécution.
    addChildComponent(saisie_);
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
    // Le bouton « + » suit l'état : une reconstruction vient d'un changement de
    // projet, de piste ou de filtre, et c'est justement ce dont il dépend.
    rafraichirAjouter();
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
    // D284 : la ligne courante se recalcule sur la liste refaite (une autre
    // piste, un autre filtre), sans attendre le prochain tick du transport.
    courante_ = -1;
    if (tete_ >= 0) setPlayheadTick(tete_, false);
    table_.repaint();
}

void EventListComponent::retraduire() {
    // D94 : les colonnes et le filtre sont posés une fois ; le titre et le
    // compte sont refaits par `rebuild()`. La sélection du filtre se lit AVANT
    // de renommer (D78) : JUCE rend 0 pour une entrée dont le texte a changé.
    for (const auto& c : kColonnes) table_.getHeader().setColumnName(c.id, tr(c.nom));
    const int choisie = filtre_.getSelectedId();
    filtre_.changeItemText(1, tr(u8"Tous"));
    for (int k = 0; k < 6; ++k)
        filtre_.changeItemText(k + 2, tr(juce::String::fromUTF8(
                                          vsm::sequencer::eventKindLabel(static_cast<EventKind>(k)).c_str())));
    filtre_.setSelectedId(choisie, juce::dontSendNotification);
    rebuild();
}

void EventListComponent::resized() {
    // La cellule bouge : ce qui est ouvert dessus ne la désigne plus.
    if (saisie_.isVisible() && ligneEnSaisie_ >= 0) fermerSaisie();
    auto zone = getLocalBounds().reduced(8);
    auto haut = zone.removeFromTop(24);
    titre_.setBounds(haut.removeFromLeft(260));
    haut.removeFromLeft(8);
    filtre_.setBounds(haut.removeFromLeft(180));
    haut.removeFromLeft(8);
    ajouter_.setBounds(haut.removeFromLeft(34));
    haut.removeFromLeft(8);
    compte_.setBounds(haut);
    zone.removeFromTop(6);
    table_.setBounds(zone);
}

std::optional<EventKind> EventListComponent::natureDuFiltre() const {
    const int choix = filtre_.getSelectedId();
    if (choix <= 1) return std::nullopt;                    // « Tous »
    return static_cast<EventKind>(choix - 2);
}

void EventListComponent::rafraichirAjouter() {
    const bool possible = natureDuFiltre().has_value() && project_ != nullptr && activeTrack_ >= 0;
    ajouter_.setEnabled(possible);
}

bool EventListComponent::ajouterEvenement(EventKind nature, vsm::midi::Tick tick) {
    if (project_ == nullptr || activeTrack_ < 0
        || static_cast<size_t>(activeTrack_) >= project_->tracks.size()) {
        std::fputs("VSM_LISTE : aucune piste choisie \xe2\x80\x94 rien n'est ajout\xc3\xa9\n", stderr);
        return false;
    }
    auto& piste = project_->tracks[static_cast<size_t>(activeTrack_)];
    // LES VALEURS D'USINE DE CHAQUE FAMILLE, et elles sont un choix : ce qui
    // naît doit s'entendre et se voir. Une note à la noire sur do central, un
    // contrôleur 7 à mi-course, un pli au centre, une pression à mi-course, un
    // programme 1. Rien ici n'est à zéro : un événement invisible qu'on vient
    // de créer se cherche, et l'on croit que le bouton n'a rien fait.
    const vsm::midi::Tick parNoire = project_->ticksPerQuarterNote > 0
                                       ? project_->ticksPerQuarterNote : 480;
    int premier = 60, second = 100;
    vsm::midi::Tick duree = parNoire;
    switch (nature) {
        case EventKind::Note:            break;                       // do central, vélocité 100
        case EventKind::ControlChange:   premier = 7;  second = 64; break;
        case EventKind::PitchBend:       premier = 0;  second = 0;  break;
        case EventKind::PolyPressure:    premier = 60; second = 64; break;
        case EventKind::ChannelPressure: premier = 0;  second = 64; break;
        case EventKind::ProgramChange:   premier = 0;  second = 0;  break;
    }
    if (onEditStarted) onEditStarted(juce::String::fromUTF8(u8"Ajouter un événement"));
    uint64_t compteur = project_->peekNextNoteId();
    const bool fait = vsm::sequencer::addTrackEvent(piste, nature, tick,
                                                     static_cast<int>(piste.channel), premier,
                                                     second, duree, compteur);
    if (!fait) {
        // PANNE MUETTE INTERDITE : un refus se dit, comme pour la modification.
        std::fputs((juce::String::fromUTF8("VSM_LISTE : ajout refus\xc3\xa9 (nature ")
                    + juce::String(static_cast<int>(nature)) + ", tick "
                    + juce::String(static_cast<int>(tick)) + ")\n").toRawUTF8(), stderr);
        return false;
    }
    if (nature == EventKind::Note) project_->ensureNoteIdAbove(compteur - 1);
    std::fputs((juce::String::fromUTF8("VSM_LISTE : ajout\xc3\xa9 \xc2\xab ")
                + juce::String::fromUTF8(vsm::sequencer::eventKindLabel(nature).c_str())
                + " \xc2\xbb au tick " + juce::String(static_cast<int>(tick))
                + ", canal " + juce::String(static_cast<int>(piste.channel) + 1) + "\n").toRawUTF8(), stderr);
    return true;
}

bool EventListComponent::ajouterPourCapture(const juce::String& consigne) {
    const int rang = consigne.upToFirstOccurrenceOf(":", false, false).getIntValue();
    const juce::String reste = consigne.fromFirstOccurrenceOf(":", false, false).trim();
    if (rang < 0 || rang > 5) {
        std::fputs(("VSM_LISTE : nature " + juce::String(rang)
                    + juce::String::fromUTF8(" inconnue (0 note, 1 contr\xc3\xb4leur, 2 pli, "
                                              "3 pression poly, 4 pression de canal, 5 programme)")
                    + "\n").toRawUTF8(), stderr);
        return false;
    }
    const vsm::midi::Tick tick = reste.isNotEmpty() ? static_cast<vsm::midi::Tick>(reste.getLargeIntValue())
                                                     : (tete_ > 0 ? tete_ : 0);
    if (!ajouterEvenement(static_cast<EventKind>(rang), tick)) return false;
    if (onEventsCreated) onEventsCreated();   // D352 : le même chemin que le bouton
    rebuild();
    if (onEventsChanged) onEventsChanged();
    return true;
}

void EventListComponent::paint(juce::Graphics& g) { g.fillAll(Palette::background); }

void EventListComponent::paintRowBackground(juce::Graphics& g, int row, int w, int h,
                                             bool selected) {
    juce::ignoreUnused(w, h);
    if (selected) g.fillAll(Palette::accentTeal.withAlpha(0.35f));
    // D284 : LA LIGNE SOUS LA TÊTE, en ambre -- la couleur de la tête de
    // lecture partout ailleurs --, distincte du teal de la sélection : l'une
    // dit où l'on EST, l'autre ce qu'on a CHOISI, et les deux peuvent différer.
    else if (row == courante_) g.fillAll(Palette::accentAmber.withAlpha(0.25f));
    else if (row % 2) g.fillAll(juce::Colour(0x10ffffff));
}

void EventListComponent::setPlayheadTick(vsm::midi::Tick tick, bool playing) {
    tete_ = tick;
    // LE DERNIER ÉVÉNEMENT PASSÉ SOUS LA TÊTE : les lignes sont triées par
    // tick (`listTrackEvents`), la borne supérieure le trouve en log n --
    // trente fois par seconde sur une liste de plusieurs milliers de lignes,
    // un parcours linéaire se verrait.
    int courante = -1;
    if (tick >= 0 && !lignes_.empty()) {
        const auto apres = std::upper_bound(lignes_.begin(), lignes_.end(), tick,
                                            [](vsm::midi::Tick t, const EventRow& l) { return t < l.tick; });
        courante = static_cast<int>(apres - lignes_.begin()) - 1;
    }
    if (courante == courante_) return;
    const int avant = courante_;
    courante_ = courante;
    if (avant >= 0) table_.repaintRow(avant);
    if (courante_ >= 0) table_.repaintRow(courante_);
    // EN LECTURE SEULEMENT, la ligne est gardée à l'écran : à l'arrêt, faire
    // sauter la liste sous la souris de quelqu'un qui la parcourt serait pire
    // que ne pas suivre. `isVisible()` et non `isShowing()` : sous un écran
    // verrouillé le second rend faux partout (D94), et un banc lirait un
    // suivi absent.
    // D287 : LA LIGNE COURANTE AU MILIEU, pas au bord. `scrollToEnsureRowIsOnscreen`
    // la laissait courir le long du bord bas (D284, « nommé, non fait ») : on
    // voyait ce qui venait de passer, jamais ce qui arrive. Au milieu, la moitié
    // haute est le passé, la moitié basse l'avenir -- c'est ce qu'un musicien
    // lit pendant que ça joue. Le défilement ne se fait que si la ligne sort
    // du tiers central, pour que la liste n'avance pas d'un cran à chaque note.
    if (playing && courante_ >= 0 && isVisible()) {
        if (auto* vue = table_.getViewport()) {
            const int hauteur = table_.getRowHeight();
            const int visible = vue->getViewHeight();
            const int yLigne = courante_ * hauteur - vue->getViewPositionY();
            if (yLigne < visible / 3 || yLigne + hauteur > visible * 2 / 3)
                vue->setViewPosition(vue->getViewPositionX(),
                                     std::max(0, courante_ * hauteur - visible / 2));
        }
    }
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

// ---------------------------------------------------------------------------
// D348 — MODIFIER UNE VALEUR DEPUIS LA LISTE.
//
// La liste savait REGARDER et SUPPRIMER ; elle ne savait pas MODIFIER, alors
// que c'est la raison d'être d'un éditeur en liste — Cubase, Logic et Reaper y
// changent hauteur, vélocité, position et durée au clavier. Corriger une
// vélocité demandait d'aller retrouver la note dans le piano roll.
//
// LE DOUBLE-CLIC GARDE SES DEUX SENS, et c'est un choix : sur une colonne
// MODIFIABLE il ouvre la saisie, ailleurs il place la tête de lecture (D284).
// Les colonnes « Nature » et « Canal » ne se modifient pas ici — changer la
// nature d'un événement, c'est en créer un autre —, et ce sont justement elles
// qu'on double-clique pour naviguer.
// ---------------------------------------------------------------------------

bool EventListComponent::colonneModifiable(int columnId, EventKind nature) {
    switch (columnId) {
        case kColPosition: return true;                       // toutes les familles
        // D353 : LE CANAL AUSSI, dans les six familles. Dans ce logiciel il ne
        // choisit pas QUI JOUE — la machine de la piste joue toutes ses notes —,
        // il décide de ce qui SORT : l'octet de statut du fichier MIDI et du
        // port. C'est le réglage dont on a besoin quand un format 0 a été
        // découpé par canal (D305) ou qu'un expandeur attend un canal précis.
        case kColCanal:    return true;
        case kColSecond:   return nature != EventKind::ProgramChange;   // un programme n'a que son numéro
        case kColPremier:  return nature != EventKind::PitchBend
                                  && nature != EventKind::ChannelPressure;
        case kColDuree:    return nature == EventKind::Note;   // seule une note a une durée
        default:           return false;
    }
}

void EventListComponent::ouvrirSaisie(int row, int columnId) {
    if (row < 0 || row >= static_cast<int>(lignes_.size())) return;
    const auto& ligne = lignes_[static_cast<size_t>(row)];
    if (!colonneModifiable(columnId, ligne.kind)) return;
    ligneEnSaisie_ = row;
    colonneEnSaisie_ = columnId;
    // LA SAISIE MONTRE LE NOMBRE, PAS LE TEXTE DE LA CASE. La colonne Position
    // affiche « 1.3+168 (1128) » et la colonne N° « C#2 (37) » : on ne demande
    // pas à l'utilisateur de retaper cette mise en forme, on lui donne le tick
    // et le numéro, qui sont ce que le champ porte.
    const long long actuel =
        columnId == kColPosition ? static_cast<long long>(ligne.tick)
        // D353 : LE CANAL SE SAISIT COMME IL S'AFFICHE, de 1 à 16. Le modèle le
        // garde de 0 à 15 (c'est l'octet MIDI) ; demander « 0 » à l'écran pour
        // le premier canal serait demander à l'utilisateur de penser en octets.
        : columnId == kColCanal ? ligne.channel + 1
        : columnId == kColPremier ? ligne.first
        : columnId == kColSecond ? ligne.second
        : static_cast<long long>(ligne.length);
    saisie_.setText(juce::String(actuel), juce::dontSendNotification);
    saisie_.setBounds(table_.getCellPosition(columnId, row, true)
                          .translated(table_.getX(), table_.getY()));
    saisie_.setVisible(true);
    // DEVANT LA TABLE, et il a fallu le payer pour le voir : `saisie_` est
    // déclarée AVANT `table_` dans la classe, donc ajoutée avant elle au parent,
    // donc peinte DESSOUS. Le journal disait « saisie ouverte », la photo ne
    // montrait rien — c'est exactement le genre d'écart que la photo existe pour
    // attraper.
    saisie_.toFront(true);
    saisie_.grabKeyboardFocus();
    saisie_.selectAll();
}

bool EventListComponent::validerSaisie() {
    if (ligneEnSaisie_ < 0 || project_ == nullptr || activeTrack_ < 0
        || static_cast<size_t>(activeTrack_) >= project_->tracks.size()
        || ligneEnSaisie_ >= static_cast<int>(lignes_.size()))
        return false;
    const auto ligne = lignes_[static_cast<size_t>(ligneEnSaisie_)];
    const juce::String texte = saisie_.getText().trim();
    // CE QUI N'EST PAS UN NOMBRE EST REFUSÉ, ET DIT. `getIntValue()` rend 0 sur
    // une saisie vide ou du texte : accepter ce 0 poserait une vélocité nulle
    // que personne n'a demandée.
    if (texte.isEmpty() || !texte.containsOnly("-0123456789")) {
        std::fputs(("VSM_LISTE : " + texte + juce::String::fromUTF8(
                        u8" — refusé, ce n'est pas un nombre\n")).toRawUTF8(), stderr);
        return false;
    }
    using vsm::sequencer::EventField;
    const EventField champ = colonneEnSaisie_ == kColPosition ? EventField::Position
                            : colonneEnSaisie_ == kColCanal ? EventField::Channel
                            : colonneEnSaisie_ == kColPremier ? EventField::Number
                            : colonneEnSaisie_ == kColSecond ? EventField::Value
                                                              : EventField::Length;
    // D353 : de l'écran (1-16) au modèle (0-15), au seul endroit où la saisie
    // devient une valeur. Convertir ailleurs ferait diverger l'affichage et la
    // saisie, qui doivent parler la même langue.
    const long long saisi = texte.getLargeIntValue()
                            - (champ == EventField::Channel ? 1 : 0);
    // LE NOM DU PAS SE STOCKE EN FRANÇAIS et se traduit À L'AFFICHAGE
    // (`trGeste`, Langue.cpp) : le traduire ici stockerait l'anglais, et
    // l'historique ne retrouverait plus sa clé. Ce qu'il lui fallait, c'est
    // une CLÉ dans la table — l'inventaire A9 le disait, et c'est la leçon de
    // D150 (« deux n'avaient aucune clé »).
    if (onEditStarted) onEditStarted(juce::String::fromUTF8(u8"Modifier un événement"));
    auto& piste = project_->tracks[static_cast<size_t>(activeTrack_)];
    if (!vsm::sequencer::setTrackEventField(piste, ligne, champ, saisi)) {
        // PANNE MUETTE INTERDITE (la règle de `deleteKeyPressed`, juste en
        // dessous) : un refus se DIT. `setTrackEventField` refuse une valeur
        // hors bornes plutôt que de la borner en silence — l'utilisateur tape
        // 300, et il doit savoir qu'il n'a pas obtenu 127.
        std::fputs(("VSM_LISTE : " + texte + juce::String::fromUTF8(
                        u8" — refusé (hors bornes, champ sans objet, ou ligne périmée)\n"))
                       .toRawUTF8(), stderr);
        return false;
    }
    std::fputs(("VSM_LISTE : colonne " + juce::String(colonneEnSaisie_) + " = " + texte
                + juce::String::fromUTF8(u8" — écrit\n")).toRawUTF8(), stderr);
    return true;
}

void EventListComponent::fermerSaisie() {
    // QUI FERME, ET QUAND : sans cette ligne, une saisie refermée par une perte
    // de clavier se lit comme une saisie jamais peinte.
    if (saisie_.isVisible())
        std::fputs("VSM_LISTE : saisie referm\xc3\xa9" "e\n", stderr);
    saisie_.setVisible(false);
    ligneEnSaisie_ = -1;
    colonneEnSaisie_ = 0;
}

bool EventListComponent::editerPourCapture(const juce::String& consigne) {
    const int row = consigne.upToFirstOccurrenceOf(":", false, false).getIntValue();
    const juce::String reste = consigne.fromFirstOccurrenceOf(":", false, false);
    const int colonne = reste.upToFirstOccurrenceOf(":", false, false).getIntValue();
    const juce::String valeur = reste.fromFirstOccurrenceOf(":", false, false).trim();
    if (row < 0 || row >= static_cast<int>(lignes_.size())) {
        std::fputs(("VSM_LISTE : ligne " + juce::String(row) + juce::String::fromUTF8(u8" hors liste (")
                    + juce::String(static_cast<int>(lignes_.size()))
                    + juce::String::fromUTF8(u8" ligne(s))\n")).toRawUTF8(), stderr);
        return false;
    }
    if (!colonneModifiable(colonne, lignes_[static_cast<size_t>(row)].kind)) {
        std::fputs(("VSM_LISTE : colonne " + juce::String(colonne)
                    + juce::String::fromUTF8(u8" non modifiable pour cette nature\n")).toRawUTF8(), stderr);
        return false;
    }
    ouvrirSaisie(row, colonne);
    // « ? » OUVRE ET LAISSE OUVERT, comme `:?` des menus de banc : c'est le seul
    // moyen de PHOTOGRAPHIER l'éditeur en place, qui se referme sinon dans la
    // même fonction et ne serait jamais sur une image.
    if (valeur == "?") {
        // ET SES BORNES : une saisie posée sur une table pas encore disposée
        // tombe hors de l'écran, visible pour le code et absente de la photo.
        const auto b = saisie_.getBounds();
        std::fputs(("VSM_LISTE : saisie ouverte sur la ligne " + juce::String(row)
                    + ", colonne " + juce::String(colonne) + ", texte \xc2\xab "
                    + saisie_.getText() + " \xc2\xbb, bornes " + juce::String(b.getX()) + ","
                    + juce::String(b.getY()) + " " + juce::String(b.getWidth()) + "x"
                    + juce::String(b.getHeight()) + " dans " + juce::String(getWidth()) + "x"
                    + juce::String(getHeight()) + "\n").toRawUTF8(), stderr);
        return true;
    }
    saisie_.setText(valeur, juce::dontSendNotification);
    const bool fait = validerSaisie();
    fermerSaisie();
    if (fait) { rebuild(); if (onEventsChanged) onEventsChanged(); }
    return fait;
}

void EventListComponent::cellDoubleClicked(int row, int columnId, const juce::MouseEvent&) {
    if (row < 0 || row >= static_cast<int>(lignes_.size())) return;
    if (colonneModifiable(columnId, lignes_[static_cast<size_t>(row)].kind)) {
        ouvrirSaisie(row, columnId);
        return;
    }
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
