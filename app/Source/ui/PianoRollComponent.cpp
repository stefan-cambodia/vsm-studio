#include "PianoRollComponent.h"
#include "EntreeDeMenu.h"
#include "Langue.h"
#include "DrumVoiceNames.h"
#include "Shortcuts.h"
#include "LookAndFeel/VsmLookAndFeel.h"
#include <algorithm>
#include <cstdio>
#include <set>
#include <array>
#include <cmath>

using namespace vsm::sequencer;
using namespace vsm::midi;
using namespace vsm::ui;

namespace {

/// D164 (A27) : la largeur sous laquelle une note se dessine en rectangle PLEIN.
/// Égale à deux fois le rayon de coin du chemin ordinaire (2,5 px) : au-dessous,
/// la figure arrondie est entièrement faite de coins et ne rend qu'un pâté.
constexpr float kLargeurMinimaleArrondie = 4.0f;

bool isBlackKey(int noteInOctave) {
    static const bool black[12] = { false, true, false, true, false, false, true, false, true, false, true, false };
    return black[((noteInOctave % 12) + 12) % 12];
}

juce::String noteName(uint8_t note) { return juce::String(noteNumberToName(note)); }

/// Identifiants du menu contextuel. Regroupés par famille (dizaines) pour que
/// l'ajout d'une entrée ne décale jamais les autres.
// LA BASE EST À 100 000, ET C'EST UNE CORRECTION DE PANNE MUETTE. Elle valait
// 100, et MainComponent route « tout identifiant au-delà de la base » vers ce
// menu-ci. Or l'énumération des menus de l'application a grossi (les plages
// d'effets de départ à elles seules font 160 entrées) jusqu'à DÉPASSER 100 :
// chaque clic sur « Affichage » partait ici et mourait en silence dans le
// default. Une base qu'aucune énumération séquentielle n'atteindra jamais.
enum ContextMenuId {
    kCtxUndo = 100000, kCtxRedo,
    kCtxCut = 100010, kCtxCopy, kCtxPaste, kCtxDelete, kCtxDuplicate,
    kCtxSelectAll = 100020, kCtxSelectNone, kCtxSelectInvert, kCtxSelectSamePitch,
    kCtxSelectNextDoubtful, kCtxSelectPrevDoubtful, kCtxSelectDoubtful, kCtxSelectLeastConfident,
    kCtxSelectWeak64 = 100420, kCtxSelectWeak32, kCtxSelectWeak16, kCtxSelectShortGrid, kCtxSelectShortHalfGrid,
    kCtxTransposeUp = 100030, kCtxTransposeDown, kCtxOctaveUp, kCtxOctaveDown,
    kCtxQuantizeFull = 100040, kCtxQuantizeHalf, kCtxQuantizeEnds, kCtxHumanize,
    kCtxLegato = 100050, kCtxRemoveOverlaps, kCtxLengthToGrid, kCtxLengthDouble, kCtxLengthHalve,
    kCtxTimesDouble = 100430, kCtxTimesHalve,   // D22.3
    kCtxSplit = 100060, kCtxJoin, kCtxReverse, kCtxMirror, kCtxMute,
    kCtxVelocityFull = 100070, kCtxVelocityHalf, kCtxVelocityUp, kCtxVelocityDown,
    kCtxVelocityRampUp, kCtxVelocityRampDown, kCtxVelocityRandom,
    kCtxVelocityCompress, kCtxVelocityCompressFull, kCtxVelocityLimit,
    kCtxScaleConstrain = 100080,
    kCtxArpUp = 100090, kCtxArpDown, kCtxArpUpDown, kCtxArpRandom,
    kCtxChordBase = 100100, // + index dans allChordTypes()
    kCtxZoomFit = 100300, kCtxZoomSelection, kCtxZoomIn, kCtxZoomOut,   // D497 : ±
    kCtxToolSelect = 100310, kCtxToolDraw, kCtxToolErase, kCtxToolSplit, kCtxToolGlue, kCtxToolMute,   // D497
    kCtxFold = 100400,
};

} // namespace

PianoRollComponent::PianoRollComponent() {
    setWantsKeyboardFocus(true);
    setOpaque(true);
    addAndMakeVisible(horizontalScrollBar_);
    addAndMakeVisible(verticalScrollBar_);
    horizontalScrollBar_.addListener(this);
    verticalScrollBar_.addListener(this);
    horizontalScrollBar_.setAutoHide(false);
    verticalScrollBar_.setAutoHide(false);
}

void PianoRollComponent::setProject(Project* project) {
    project_ = project;
    selectedNoteIds_.clear();
    // L'HISTORIQUE N'EST PAS VIDÉ ICI, et c'est délibéré. `setProject` est
    // rappelé à chaque republication -- y compris après un annuler --, si bien
    // qu'y vider la pile effacerait l'annulation à l'instant même où on s'en
    // sert. C'est l'application qui la vide, là où un VRAI document change :
    // nouveau projet, ouverture d'un MIDI, ouverture d'un dossier de projet.
    notifyEditState();
    updateScrollBars();
    repaint();
}

void PianoRollComponent::setActiveTrackIndex(size_t trackIndex) {
    const bool autrePiste = trackIndex != activeTrackIndex_;
    if (autrePiste) stopAudition();
    activeTrackIndex_ = trackIndex;
    selectedNoteIds_.clear();
    refreshFoldRows();
    // CADRER LES NOTES DE LA PISTE QU'ON VIENT DE CHOISIR, verticalement
    // seulement. Le piano roll s'ouvrait toujours sur C6 en haut, quelle que
    // soit la piste : une basse reconstruite (octave 1) montrait une fenêtre
    // vide, ou ses seules notes fantômes de transcription dans l'aigu. La
    // hauteur visée est la MÉDIANE des hauteurs pondérée par la durée -- pas
    // le milieu de l'étendue, qu'une note fantôme deux octaves plus haut
    // déplacerait. Le zoom horizontal, lui, ne bouge pas : c'est le choix de
    // l'utilisateur, pas celui de la piste.
    if (autrePiste) {
        cadrerSurLesNotes();
        amenerLesNotesDansLaFenetre();   // D524 : et le temps, comme la hauteur
    }
    // L'HISTORIQUE N'EST PLUS VIDÉ ICI. Il portait sur les notes d'une seule
    // piste, et restaurer celles de l'une dans l'autre n'aurait rien voulu
    // dire ; il porte désormais sur le projet entier, et regarder une autre
    // piste n'efface plus ce qu'on pouvait annuler.
    notifyEditState();
    updateScrollBars();
    repaint();
}

int PianoRollComponent::keyboardWidth() const {
    const Track* track = activeTrack();
    return (track && track->channel == 9) ? kKeyboardWidthDrums : kKeyboardWidthNotes;
}

Track* PianoRollComponent::activeTrack() const {
    if (!project_ || activeTrackIndex_ >= project_->tracks.size()) return nullptr;
    return &project_->tracks[activeTrackIndex_];
}

// ---------------------------------------------------------------------------
// Conversions
// ---------------------------------------------------------------------------

float PianoRollComponent::tickToX(Tick tick) const {
    return static_cast<float>(keyboardWidth()) +
           static_cast<float>(static_cast<double>(tick - scrollTick_) * pixelsPerTick_);
}

Tick PianoRollComponent::xToTick(float x) const {
    if (pixelsPerTick_ <= 0.0) return scrollTick_;
    return scrollTick_ + static_cast<Tick>(static_cast<double>(x - keyboardWidth()) / pixelsPerTick_);
}

// D20.2 : TOUT PASSE PAR LES RANGÉES. Dépliée, la rangée d'une hauteur est
// « 127 moins la hauteur » et rien n'a changé ; repliée, c'est le nombre de
// hauteurs jouées plus aiguës qu'elle. Une hauteur absente (le haut de la vue
// pendant un défilement) tombe sur la rangée de la plus proche en dessous.
int PianoRollComponent::rowOfNote(int note) const {
    if (!folded()) return 127 - juce::jlimit(0, 127, note);
    int rangee = 0;
    for (uint8_t h : rangees_) {
        if (static_cast<int>(h) > note) ++rangee;
        else break;
    }
    return std::min(rangee, static_cast<int>(rangees_.size()) - 1);
}

int PianoRollComponent::noteOfRow(int row) const {
    if (!folded()) return juce::jlimit(0, 127, 127 - row);
    return rangees_[static_cast<size_t>(juce::jlimit(0, static_cast<int>(rangees_.size()) - 1, row))];
}

int PianoRollComponent::noteToY(uint8_t note) const {
    return (rowOfNote(static_cast<int>(note)) - rowOfNote(topNote_)) * noteHeight_;
}

uint8_t PianoRollComponent::yToNote(float y) const {
    const int rangee = rowOfNote(topNote_) + static_cast<int>(std::floor(y / static_cast<float>(noteHeight_)));
    return static_cast<uint8_t>(noteOfRow(rangee));
}

void PianoRollComponent::refreshFoldRows() {
    rangees_.clear();
    if (!fold_) return;
    const Track* track = activeTrack();
    if (!track) return;
    std::set<uint8_t> hauteurs;
    for (const auto& n : track->notes) hauteurs.insert(n.number);
    rangees_.assign(hauteurs.rbegin(), hauteurs.rend());
}

bool PianoRollComponent::setFoldEnabled(bool enabled) {
    fold_ = enabled;
    refreshFoldRows();
    if (fold_ && rangees_.empty()) {
        // RIEN À REPLIER, ET C'EST DIT : un piano roll replié sur zéro rangée
        // serait une grille vide sans explication.
        fold_ = false;
        if (onStatusChanged) onStatusChanged(vsm::app::ui::tr(u8"Rien \u00e0 replier : cette piste n'a aucune note."));
        repaint();
        return false;
    }
    // La rangée du haut est la plus aiguë jouée : replier montre tout.
    if (fold_) topNote_ = rangees_.front();
    if (onStatusChanged)
        onStatusChanged(fold_ ? vsm::app::ui::tr(u8"Repli\u00e9 sur %1 hauteur(s) jou\u00e9e(s)")
                                    .replace("%1", juce::String(static_cast<int>(rangees_.size())))
                              : vsm::app::ui::tr(u8"D\u00e9pli\u00e9 : toutes les hauteurs"));
    updateScrollBars();
    repaint();
    return true;
}

Tick PianoRollComponent::gridTicks() const {
    if (!project_) return 120;
    if (!adaptiveGrid_) return gridResolutionToTicks(gridResolution_, project_->ticksPerQuarterNote);
    // D29.5 : de la plus fine à la plus grosse, la première dont la case fait
    // 24 px ; au-delà de la ronde, la ronde -- une grille qui disparaîtrait au
    // dézoom serait une grille qu'on ne peut plus aimanter.
    static const NoteValue kDeLaPlusFine[] = {
        NoteValue::HundredTwentyEighth, NoteValue::SixtyFourth, NoteValue::ThirtySecond, NoteValue::Sixteenth,
        NoteValue::Eighth, NoteValue::Quarter, NoteValue::Half, NoteValue::Whole };
    Tick choisi = 0;
    for (NoteValue v : kDeLaPlusFine) {
        GridResolution r = gridResolution_;
        r.value = v;
        choisi = gridResolutionToTicks(r, project_->ticksPerQuarterNote);
        if (static_cast<double>(choisi) * pixelsPerTick_ >= 24.0) break;
    }
    return std::max<Tick>(1, choisi);
}

Tick PianoRollComponent::ticksPerBeat() const {
    return project_ ? static_cast<Tick>(project_->ticksPerQuarterNote) : 480;
}

Tick PianoRollComponent::ticksPerBarAt(Tick tick) const {
    if (!project_) return 1920;
    const Tick bar = project_->timeSignatureMap.ticksPerBar(tick, project_->ticksPerQuarterNote);
    return bar > 0 ? bar : static_cast<Tick>(project_->ticksPerQuarterNote) * 4;
}

Tick PianoRollComponent::snapTick(Tick tick) const {
    if (!snapEnabled_ || !project_) return std::max<Tick>(0, tick);
    QuantizeSettings settings;
    settings.grid = gridResolution_;
    settings.strength = 1.0f;
    settings.swing = swing_;
    return std::max<Tick>(0, quantizeTick(tick, settings, project_->ticksPerQuarterNote));
}

Note* PianoRollComponent::findNoteAt(juce::Point<float> pos, bool* nearRightEdge, bool* nearLeftEdge) {
    if (nearRightEdge) *nearRightEdge = false;
    if (nearLeftEdge) *nearLeftEdge = false;
    Track* track = activeTrack();
    if (!track) return nullptr;

    constexpr float kEdgeTolerance = 6.0f;
    // Parcours en sens inverse : la dernière note dessinée est celle du dessus,
    // c'est donc elle qui doit gagner le clic.
    for (int i = static_cast<int>(track->notes.size()) - 1; i >= 0; --i) {
        Note& note = track->notes[static_cast<size_t>(i)];
        const float x1 = tickToX(note.startTick);
        const float x2 = tickToX(note.endTick);
        const float y = static_cast<float>(noteToY(note.number));
        if (pos.x >= x1 && pos.x <= x2 && pos.y >= y && pos.y <= y + static_cast<float>(noteHeight_)) {
            if (nearRightEdge) *nearRightEdge = (x2 - pos.x) <= kEdgeTolerance;
            if (nearLeftEdge) *nearLeftEdge = (pos.x - x1) <= kEdgeTolerance;
            return &note;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Historique et notifications
// ---------------------------------------------------------------------------

bool PianoRollComponent::beginEdit(const juce::String& label) {
    // LE VERROU (D16.5), EN UN SEUL ENDROIT. Les trente-deux gestes d'édition
    // de notes passent tous par ici avant de toucher au matériau : mettre le
    // cadenas dans chacun d'eux garantirait qu'un jour l'un l'oublie.
    if (activeTrackLocked()) {
        if (onLockRefused) onLockRefused();
        return false;
    }
    // D432 : le pas nomme la piste qu'il touche, comme ceux de la console
    // (D425) ; `trGeste` traduit la partie avant « — ».
    juce::String nom = label;
    if (const Track* piste = activeTrack(); piste != nullptr && !piste->name.empty())
        nom += juce::String::fromUTF8(" \xe2\x80\x94 ") + juce::String::fromUTF8(piste->name.c_str());
    if (project_ != nullptr && history_ != nullptr) {
        history_->beginEdit(*project_, nom.toStdString());
        pasOuvert_ = true;   // D511 : jugé à la fin du geste (`notifyEdited`)
        libelleDuPas_ = label;
    }
    return true;
}

bool PianoRollComponent::notesIdentiquesA(const vsm::sequencer::Project& avant) const {
    // D511 : les gestes du piano roll ne modifient QUE les notes de la piste active
    // (relevé des 29 `beginEdit` de ce fichier) : c'est donc là qu'on regarde. Même
    // ordre, mêmes champs — une note seulement déplacée dans le vecteur compte comme
    // un changement, et garde son pas : prudent, jamais l'inverse.
    if (project_ == nullptr || activeTrackIndex_ >= project_->tracks.size()
        || activeTrackIndex_ >= avant.tracks.size() || avant.tracks.size() != project_->tracks.size())
        return false;
    const auto& a = avant.tracks[activeTrackIndex_].notes;
    const auto& b = project_->tracks[activeTrackIndex_].notes;
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].startTick != b[i].startTick || a[i].endTick != b[i].endTick || a[i].channel != b[i].channel
            || a[i].number != b[i].number || a[i].velocity != b[i].velocity
            || a[i].releaseVelocity != b[i].releaseVelocity || a[i].id != b[i].id || a[i].muted != b[i].muted
            || a[i].confidence != b[i].confidence)
            return false;
    return true;
}

bool PianoRollComponent::activeTrackLocked() const {
    const Track* track = activeTrack();
    return track != nullptr && track->locked;
}

void PianoRollComponent::notifyEdited() {
    // D511 : UN GESTE QUI N'A RIEN CHANGÉ NE LAISSE PAS DE PAS. Arpéger des notes
    // seules, fusionner ce qui ne se touche pas, couper là où rien ne commence :
    // chacun laissait un Ctrl+Z qui n'annulait rien — et vidait la branche
    // « rétablir ». Le pas est retiré, la branche rendue, et la ligne d'état le dit.
    if (pasOuvert_ && history_ != nullptr && project_ != nullptr) {
        pasOuvert_ = false;
        const auto* avant = history_->etatDuDernierPas();
        if (avant != nullptr && notesIdentiquesA(*avant) && history_->retirerLeDernierPas() && onStatusChanged)
            onStatusChanged(vsm::app::ui::tr(u8"%1 : rien à changer").replace("%1", vsm::app::ui::trGeste(libelleDuPas_)));
    }
    if (onNotesEdited) onNotesEdited();
    notifyEditState();
    updateScrollBars();
    repaint();
}

void PianoRollComponent::notifyEditState() {
    // D357 : LE COMPTE DE LA SÉLECTION SE DIT LÀ OÙ ELLE CHANGE, et non dans un
    // seul de ses deux dispatchers. D233 avait posé cette ligne dans
    // `performContextMenuAction` : le MENU disait son compte, le RACCOURCI ne
    // disait rien -- « Tout sélectionner » par Ctrl+A ne laissait AUCUNE trace,
    // et les gestes de sélection étaient les seuls qu'aucun banc ne pouvait
    // mesurer (mesuré le 18/09 : 1 ligne par le menu, 0 par la touche). Ici, le
    // compte est celui de l'état, quel que soit le chemin qui l'a changé -- la
    // souris comprise, qu'aucun des deux dispatchers ne voyait.
    //
    // SEULEMENT QUAND IL CHANGE : `notifyEditState` est appelée par dix-huit
    // endroits, dont un glissé de souris, et répéter « 8 note(s) choisie(s) »
    // noierait la ligne qui compte.
    const int compte = static_cast<int>(selectedNoteIds_.size());
    if (compte != dernierCompteDit_) {
        const bool premierCompte = dernierCompteDit_ < 0;
        dernierCompteDit_ = compte;
        std::fputs(("VSM_SELECTION : " + juce::String(compte)
                    + juce::String::fromUTF8(u8" note(s) choisie(s)\n")).toRawUTF8(), stderr);
        // D515 : ET À L'ÉCRAN. La ligne d'état ne se refaisait qu'aux gestes de
        // souris : après Ctrl+A, huit notes choisies, elle disait « Prêt ». Le
        // premier compte (celui du chargement) ne la touche pas : « Prêt » y reste.
        if (!premierCompte) updateStatusText(derniereSouris_, sourisDedans_);
    }
    if (onEditStateChanged) onEditStateChanged();
}

void PianoRollComponent::undo() {
    if (onAvantHistorique) onAvantHistorique();   // D144
    if (project_ == nullptr || history_ == nullptr || !history_->undo(*project_)) return;
    if (onProjectRestored) onProjectRestored();
    Track* track = activeTrack();
    if (!track) { notifyEdited(); return; }
    // Une note annulée peut avoir disparu : la sélection ne doit pas garder de
    // références fantômes (elles rendraient les opérations suivantes muettes).
    NoteSelection stillValid;
    for (const auto& n : track->notes)
        if (selectedNoteIds_.count(n.id) > 0) stillValid.insert(n.id);
    selectedNoteIds_ = std::move(stillValid);
    notifyEdited();
}

void PianoRollComponent::redo() {
    if (onAvantHistorique) onAvantHistorique();   // D144
    if (project_ == nullptr || history_ == nullptr || !history_->redo(*project_)) return;
    if (onProjectRestored) onProjectRestored();
    Track* track = activeTrack();
    if (!track) { notifyEdited(); return; }
    NoteSelection stillValid;
    for (const auto& n : track->notes)
        if (selectedNoteIds_.count(n.id) > 0) stillValid.insert(n.id);
    selectedNoteIds_ = std::move(stillValid);
    notifyEdited();
}

// ---------------------------------------------------------------------------
// Écoute (audition)
// ---------------------------------------------------------------------------

void PianoRollComponent::startAudition(uint8_t note, uint8_t velocity) {
    if (!onAudition) return;
    if (auditionNote_ == static_cast<int>(note)) return; // déjà en train de sonner
    stopAudition();
    onAudition(note, velocity, true);
    auditionNote_ = static_cast<int>(note);
}

void PianoRollComponent::stopAudition() {
    if (auditionNote_ < 0) return;
    if (onAudition) onAudition(static_cast<uint8_t>(auditionNote_), 0, false);
    auditionNote_ = -1;
}

// ---------------------------------------------------------------------------
// Réglages
// ---------------------------------------------------------------------------

void PianoRollComponent::setTool(Tool tool) { tool_ = tool; notifyEditState(); repaint(); }
void PianoRollComponent::setGridResolution(GridResolution grid) { gridResolution_ = grid; repaint(); }
void PianoRollComponent::setSnapEnabled(bool enabled) {
    if (snapEnabled_ == enabled) return;
    snapEnabled_ = enabled;
    if (onBasculesChanged) onBasculesChanged();   // D494
}
void PianoRollComponent::setSwing(float swing) { swing_ = swing; repaint(); }
void PianoRollComponent::setScale(Scale scale) { scale_ = scale; repaint(); }
void PianoRollComponent::setScaleHighlightEnabled(bool enabled) { scaleHighlight_ = enabled; repaint(); }
void PianoRollComponent::setGhostNotesVisible(bool visible) {
    if (ghostNotes_ == visible) return;
    ghostNotes_ = visible;
    if (onBasculesChanged) onBasculesChanged();   // D494
    repaint();
}
void PianoRollComponent::setFollowPlayhead(bool follow) {
    if (followPlayhead_ == follow) return;
    followPlayhead_ = follow;
    if (onBasculesChanged) onBasculesChanged();   // D494
}

void PianoRollComponent::setLoopRegion(Tick start, Tick end, bool active) {
    // D520 : la règle lit CETTE région (`boucleDebut`…) ; sa propre copie, que rien ne
    // tenait à jour, effaçait celle du projet au double-clic.
    loopStartTick_ = start;
    loopEndTick_ = end;
    loopActive_ = active;
    repaint();
}

void PianoRollComponent::setPlayheadTick(Tick tick) {
    // D168 (A28) : RENDRE LA MAIN QUAND RIEN N'A BOUGÉ.
    //
    // Le minuteur de `MainComponent` bat à 30 Hz et pousse la position de
    // lecture dans les trois vues à chaque tour, que le transport joue ou non.
    // L'arrangement se protège depuis toujours (`ArrangementComponent.cpp`,
    // « if (tick == playhead_) return; ») et le panneau de machine aussi, par son
    // numéro de pas ; le piano roll, lui, appelait `repaint()` inconditionnellement
    // -- soit trente redessins par seconde d'une tête de lecture immobile.
    // Mesuré par D167 : 23,30 % d'un cœur au repos, dont 21,45 sur le fil des
    // messages, et le même chiffre sur un projet VIDE -- ce n'était pas le
    // contenu qu'on redessinait, c'était la surface. Sur un portable, c'est de
    // l'autonomie dépensée à refaire une image identique à la précédente.
    if (tick == playheadTick_) return;
    const Tick ancienneTete = playheadTick_;
    const Tick ancienDefilement = scrollTick_;
    playheadTick_ = tick;
    if (followPlayhead_) {
        // Défilement par "pages" plutôt que centré en continu : un curseur qui
        // recentre à chaque image donne un fond qui glisse en permanence,
        // fatigant et rendant la lecture des positions difficile.
        const float x = tickToX(tick);
        const auto area = contentArea();
        if (x > static_cast<float>(area.getRight()) - 40.0f || x < static_cast<float>(keyboardWidth())) {
            const double visibleTicks = static_cast<double>(area.getWidth()) / pixelsPerTick_;
            scrollTick_ = std::max<Tick>(0, tick - static_cast<Tick>(visibleTicks * 0.15));
            updateScrollBars();
        }
    }
    // D171 : DEUX BANDES, PAS UNE FENÊTRE.
    //
    // D170 a mesuré le prix du trait qui avance : en lecture, l'interface coûte
    // 15,40 % d'un cœur, presque autant que le moteur qui fabrique le son, parce
    // que déplacer une ligne verticale de quelques pixels redessinait le piano
    // roll ENTIER trente fois par seconde. On ne demande donc que les deux
    // bandes où la tête était et où elle est : JUCE découpe le dessin sur la
    // zone sale, et seules les notes qui croisent ces quelques pixels sont
    // repeintes.
    //
    // SAUF QUAND LA PAGE TOURNE. Si `followPlayhead_` a déplacé le défilement,
    // tout ce qui est à l'écran a changé de place : la vue entière se redessine,
    // et c'est le seul cas où elle le doit.
    if (scrollTick_ != ancienDefilement) {
        repaint();
        return;
    }
    const auto bande = [this](Tick t) {
        const int x = static_cast<int>(std::floor(tickToX(t)));
        return juce::Rectangle<int>(x - 2, 0, 5, getHeight());
    };
    repaint(bande(ancienneTete));
    repaint(bande(playheadTick_));
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

void PianoRollComponent::zoomHorizontally(float factor) {
    // Le zoom conserve le tick situé au centre de la fenêtre : sans ça, zoomer
    // "part" toujours vers la gauche et on perd ce qu'on regardait.
    const auto area = contentArea();
    const Tick centreTick = xToTick(static_cast<float>(area.getCentreX()));
    pixelsPerTick_ = juce::jlimit(0.001, 8.0, pixelsPerTick_ * static_cast<double>(factor));
    const double halfVisible = static_cast<double>(area.getWidth()) * 0.5 / pixelsPerTick_;
    scrollTick_ = std::max<Tick>(0, centreTick - static_cast<Tick>(halfVisible));
    updateScrollBars();
    repaint();
}

void PianoRollComponent::releverRangPourCapture() const {
    // D338 : ce que le clavier écrit, lu à la source de la peinture -- pas sur la photo.
    // D369 : ET LA FENÊTRE DE HAUTEURS, sans quoi « le piano roll se rouvre où on
    // l'a laissé » n'est mesurable par aucun banc. Le zoom, le défilement et la
    // note du haut vivent dans des champs privés que la peinture consomme ; la
    // photo, elle, ne rend que des pixels. On dit donc les DEUX bornes, celle du
    // haut et celle du bas — la seconde dépend du rang, et c'est précisément ce
    // couplage qui fait qu'une note du haut reprise sans son rang ne montre pas
    // les mêmes touches.
    const int lignes = std::max(1, contentArea().getHeight() / std::max(1, noteHeight_));
    // D524 : CE QUE LA PISTE CHOISIE MONTRE D'ELLE-MÊME — ses notes qui tombent dans
    // la fenêtre, en temps ET en hauteur, par les mêmes `tickToX` / `noteToY` que la
    // peinture (lignes repliées comprises). Les notes fantômes des autres pistes
    // n'en sont pas : une fenêtre qui n'en montre que des fantômes est vide.
    const auto zone = contentArea();
    const Tick fenetreFin = xToTick(static_cast<float>(zone.getRight()));
    int visibles = 0, total = 0;
    if (const Track* piste = activeTrack())
        for (const auto& n : piste->notes) {
            ++total;
            const int y = noteToY(n.number);
            if (n.endTick > scrollTick_ && n.startTick < fenetreFin && y + noteHeight_ > zone.getY()
                && y < zone.getBottom())
                ++visibles;
        }
    // LE ZOOM PASSE PAR `juce::String` ET NON PAR `%f` : la locale du processus
    // est celle de JUCE, pas la nôtre, et `fprintf` y écrivait « zoom=0,080000 »
    // — une VIRGULE. Un banc qui lit ce nombre pour le comparer ne le lirait
    // pas. Mesuré ici même à la première course du relevé ; c'est le piège des
    // nombres qui traversent une frontière, et il vaut pour un relevé comme
    // pour un fichier.
    std::fputs((juce::String("VSM_PIANOROLL_RANG : rang=") + juce::String(noteHeight_)
                + " police=12 touches-nommees="
                + ((noteHeight_ >= kRangNomme || folded()) ? "toutes" : "do-seulement")
                + " do-nommes=1 haut=" + juce::String(topNote_)
                + " bas=" + juce::String(std::max(0, topNote_ - lignes))
                + " lignes=" + juce::String(lignes)
                + " zoom=" + juce::String(pixelsPerTick_, 6)
                + " defilement=" + juce::String(static_cast<juce::int64>(scrollTick_))
                + " fenetre=" + juce::String(static_cast<juce::int64>(scrollTick_)) + ".."
                + juce::String(static_cast<juce::int64>(fenetreFin))
                + " visibles=" + juce::String(visibles) + "/" + juce::String(total)
                // D497 : l'outil courant — la barre le marque en couleur, et un
                // texte peint ne se relève pas (D149).
                + " outil=" + (tool_ == Tool::Select ? "selection" : tool_ == Tool::Draw ? "crayon"
                               : tool_ == Tool::Erase ? "gomme" : tool_ == Tool::Split ? "ciseaux"
                               : tool_ == Tool::Glue ? "colle" : "muet")
                + "\n").toRawUTF8(), stderr);
}

void PianoRollComponent::zoomVertically(float factor) {
    noteHeight_ = juce::jlimit(4, 48, static_cast<int>(std::lround(static_cast<float>(noteHeight_) * factor)));
    updateScrollBars();
    repaint();
}

void PianoRollComponent::cadrerSurLesNotes() {
    const Track* track = activeTrack();
    if (!track || track->notes.empty()) return;
    std::vector<std::pair<int, Tick>> hauteurs;
    hauteurs.reserve(track->notes.size());
    Tick total = 0;
    for (const auto& n : track->notes) {
        const Tick duree = std::max<Tick>(1, n.endTick - n.startTick);
        hauteurs.push_back({static_cast<int>(n.number), duree});
        total += duree;
    }
    std::sort(hauteurs.begin(), hauteurs.end());
    Tick cumul = 0;
    int mediane = hauteurs.front().first;
    for (const auto& [hauteur, duree] : hauteurs) {
        cumul += duree;
        if (cumul * 2 >= total) { mediane = hauteur; break; }
    }
    const int lignesVisibles = std::max(1, contentArea().getHeight() / std::max(1, noteHeight_));
    topNote_ = juce::jlimit(12, 127, mediane + lignesVisibles / 2);
    // D461 : LE CADRAGE SE SOUVIENT DE CE QU'IL CENTRE. À l'ouverture, le panneau
    // n'a souvent qu'une ou deux rangées : la médiane tombait sur celle du haut,
    // et y restait quand le panneau grandissait (`cdl` agrandi : 771 notes sur
    // 2 219 dans la fenêtre, la médiane au bord).
    hauteurCentree_ = mediane;
    topNoteDuCadrage_ = topNote_;
}

void PianoRollComponent::amenerLesNotesDansLaFenetre() {
    // D524 : LE TEMPS, COMME LA HAUTEUR. Le cadrage de la piste choisie était
    // vertical seulement : une piste qui entre à la mesure 10 s'ouvrait sur les
    // mesures 1 à 6, ses seuls fantômes à l'écran (`b4wuzthen`, la kick). Si une
    // note de la piste est déjà dans la fenêtre de temps, RIEN ne bouge — l'endroit
    // où l'on travaille est le choix de l'utilisateur, comme le zoom, qui ne bouge
    // jamais ici. Sinon, la note la plus proche : d'après, elle se pose au bord
    // gauche (la marge de « tout voir ») ; d'avant, elle finit au bord droit.
    const Track* track = activeTrack();
    if (!track || track->notes.empty() || pixelsPerTick_ <= 0.0) return;
    const Tick debut = scrollTick_;
    const Tick fin = xToTick(static_cast<float>(contentArea().getRight()));
    const Tick largeur = std::max<Tick>(1, fin - debut);
    const Tick marge = largeur / 50;
    Tick prochain = -1, precedent = -1;   // le début de la première d'après, la fin de la dernière d'avant
    for (const auto& n : track->notes) {
        if (n.endTick > debut && n.startTick < fin) return;   // déjà à l'écran
        if (n.startTick >= fin && (prochain < 0 || n.startTick < prochain)) prochain = n.startTick;
        if (n.endTick <= debut && n.endTick > precedent) precedent = n.endTick;
    }
    const bool apres = prochain >= 0 && (precedent < 0 || prochain - fin <= debut - precedent);
    scrollTick_ = std::max<Tick>(0, apres ? prochain - marge : precedent + marge - largeur);
}

void PianoRollComponent::zoomToFit() {
    const Track* track = activeTrack();
    if (!track || track->notes.empty()) return;
    Tick first = track->notes.front().startTick, last = track->notes.front().endTick;
    int lowest = 127, highest = 0;
    for (const auto& n : track->notes) {
        first = std::min(first, n.startTick);
        last = std::max(last, n.endTick);
        lowest = std::min(lowest, static_cast<int>(n.number));
        highest = std::max(highest, static_cast<int>(n.number));
    }
    const auto area = contentArea();
    const Tick span = std::max<Tick>(1, last - first);
    pixelsPerTick_ = juce::jlimit(0.001, 8.0, static_cast<double>(area.getWidth()) * 0.95 / static_cast<double>(span));
    scrollTick_ = std::max<Tick>(0, first - static_cast<Tick>(span * 0.02));

    const int noteSpan = std::max(1, highest - lowest + 2);
    // D338 : jamais sous le rang qui porte un nom ; ce qui ne tient pas défile,
    // centré sur la médiane des hauteurs (pondérée par la durée).
    noteHeight_ = juce::jlimit(kRangNomme, 48, area.getHeight() / noteSpan);
    if (noteSpan * noteHeight_ <= area.getHeight()) topNote_ = juce::jlimit(12, 127, highest + 1);
    else cadrerSurLesNotes();
    updateScrollBars();
    repaint();
}

void PianoRollComponent::zoomToSelection() {
    const Track* track = activeTrack();
    if (!track || selectedNoteIds_.empty()) return;
    const SelectionStats stats = computeSelectionStats(track->notes, selectedNoteIds_);
    const auto area = contentArea();
    const Tick span = std::max<Tick>(1, stats.endTick - stats.startTick);
    pixelsPerTick_ = juce::jlimit(0.001, 8.0, static_cast<double>(area.getWidth()) * 0.9 / static_cast<double>(span));
    scrollTick_ = std::max<Tick>(0, stats.startTick - static_cast<Tick>(span * 0.05));
    const int noteSpan = std::max(1, static_cast<int>(stats.highestNote) - static_cast<int>(stats.lowestNote) + 2);
    // D338 : même plancher que « tout voir » ; une sélection trop haute se centre.
    noteHeight_ = juce::jlimit(kRangNomme, 48, area.getHeight() / noteSpan);
    if (noteSpan * noteHeight_ <= area.getHeight())
        topNote_ = juce::jlimit(12, 127, static_cast<int>(stats.highestNote) + 1);
    else {
        const int lignesVisibles = std::max(1, area.getHeight() / noteHeight_);
        const int milieu = (static_cast<int>(stats.highestNote) + static_cast<int>(stats.lowestNote)) / 2;
        topNote_ = juce::jlimit(12, 127, milieu + lignesVisibles / 2);
    }
    updateScrollBars();
    repaint();
}

void PianoRollComponent::scrollToPlayhead() {
    const auto area = contentArea();
    const double visibleTicks = static_cast<double>(area.getWidth()) / pixelsPerTick_;
    scrollTick_ = std::max<Tick>(0, playheadTick_ - static_cast<Tick>(visibleTicks * 0.15));
    updateScrollBars();
    repaint();
}

// ---------------------------------------------------------------------------
// Sélection
// ---------------------------------------------------------------------------

void PianoRollComponent::selectAll() {
    if (Track* track = activeTrack()) selectedNoteIds_ = selectAllNotes(track->notes);
    notifyEditState();
    repaint();
}

void PianoRollComponent::selectNone() {
    selectedNoteIds_.clear();
    notifyEditState();
    repaint();
}

void PianoRollComponent::invertSelection() {
    if (Track* track = activeTrack()) selectedNoteIds_ = invertNoteSelection(track->notes, selectedNoteIds_);
    notifyEditState();
    repaint();
}

void PianoRollComponent::selectBelowVelocity(uint8_t velocity) {
    if (Track* track = activeTrack())
        selectedNoteIds_ = selectNotesBelowVelocity(track->notes, velocity);
    // D515 : LE MESSAGE DU GESTE APRÈS le résumé que `notifyEditState` refait
    // désormais : c'est lui qui dit ce que le geste a trouvé, et il gagne.
    notifyEditState();
    if (onStatusChanged)
        onStatusChanged(vsm::app::ui::tr(u8"%1 note(s) plus faible(s) que %2")
                            .replace("%1", juce::String(static_cast<int>(selectedNoteIds_.size())))
                            .replace("%2", juce::String(static_cast<int>(velocity))));
    repaint();
}

void PianoRollComponent::selectShorterThan(Tick ticks) {
    if (Track* track = activeTrack())
        selectedNoteIds_ = selectNotesShorterThan(track->notes, ticks);
    notifyEditState();   // D515 : le résumé d'abord, le message du geste ensuite
    if (onStatusChanged)
        onStatusChanged(vsm::app::ui::tr(u8"%1 note(s) plus courte(s) que %2 ticks")
                            .replace("%1", juce::String(static_cast<int>(selectedNoteIds_.size())))
                            .replace("%2", juce::String(static_cast<int>(ticks))));
    repaint();
}

void PianoRollComponent::selectSamePitch() {
    if (Track* track = activeTrack())
        selectedNoteIds_ = selectNotesWithSamePitch(track->notes, selectedNoteIds_);
    notifyEditState();
    repaint();
}

size_t PianoRollComponent::doubtfulNoteCount() const {
    const Track* track = activeTrack();
    return track ? countDoubtfulNotes(track->notes) : 0;
}

void PianoRollComponent::selectDoubtfulNotes() {
    if (Track* track = activeTrack()) selectedNoteIds_ = vsm::sequencer::selectDoubtfulNotes(track->notes);
    notifyEditState();
    repaint();
}

void PianoRollComponent::selectLeastConfidentNotes() {
    if (Track* track = activeTrack())
        selectedNoteIds_ = vsm::sequencer::selectLeastConfident(track->notes, 0.10);
    notifyEditState();
    repaint();
}

void PianoRollComponent::selectNextDoubtfulNote(bool forward) {
    Track* track = activeTrack();
    if (!track) return;
    const uint64_t id = nextDoubtfulNote(track->notes, selectedNoteIds_, playheadTick_, forward);
    if (id == 0) return;                     // aucune note douteuse : rien à faire, rien ne bouge
    selectedNoteIds_ = {id};

    // Amener la note dans la vue SANS toucher au zoom : zoomer sur une seule
    // note (zoomToSelection) ferait perdre le contexte -- les notes autour,
    // qui sont précisément ce qu'on compare pour juger si celle-ci est juste.
    // On ne défile que si elle est hors champ, pour que le regard n'ait pas à
    // repartir de zéro à chaque D.
    const auto it = std::find_if(track->notes.begin(), track->notes.end(),
                                 [id](const Note& n) { return n.id == id; });
    if (it != track->notes.end()) {
        const auto area = contentArea();
        const float x = tickToX(it->startTick);
        if (x < static_cast<float>(area.getX()) || x > static_cast<float>(area.getRight()) - 40.0f) {
            const double visibleTicks = static_cast<double>(area.getWidth()) / pixelsPerTick_;
            scrollTick_ = std::max<Tick>(0, it->startTick - static_cast<Tick>(visibleTicks * 0.15));
        }
        const int y = noteToY(it->number);
        if (y < 0 || y + noteHeight_ > area.getHeight()) {
            const int visibleRows = std::max(1, area.getHeight() / std::max(1, noteHeight_));
            topNote_ = juce::jlimit(12, 127, static_cast<int>(it->number) + visibleRows / 2);
        }
        updateScrollBars();
    }
    notifyEditState();
    repaint();
}

// ---------------------------------------------------------------------------
// Édition
//
// Toutes ces méthodes suivent le même squelette : vérifier la piste, mémoriser
// l'état pour l'annulation (beginEdit), appeler l'opération PURE de
// vsm::sequencer, puis notifier. Aucune ne contient de logique musicale.
// ---------------------------------------------------------------------------

void PianoRollComponent::deleteSelection(const juce::String& libelle) {
    Track* track = activeTrack();
    if (!track || selectedNoteIds_.empty()) return;
    if (!beginEdit(libelle)) return;   // D439
    track->notes.erase(std::remove_if(track->notes.begin(), track->notes.end(),
                                       [this](const Note& n) { return selectedNoteIds_.count(n.id) > 0; }),
                        track->notes.end());
    selectedNoteIds_.clear();
    notifyEdited();
}

void PianoRollComponent::duplicateSelection() {
    Track* track = activeTrack();
    if (!track || !project_ || selectedNoteIds_.empty()) return;
    const SelectionStats stats = computeSelectionStats(track->notes, selectedNoteIds_);
    // Décalage = longueur de la sélection, arrondie à la grille : dupliquer un
    // motif d'une mesure doit tomber pile sur la mesure suivante.
    const Tick grid = gridTicks();
    Tick offset = stats.endTick - stats.startTick;
    if (grid > 0) offset = ((offset + grid - 1) / grid) * grid;
    offset = std::max<Tick>(grid, offset);

    if (!beginEdit("Dupliquer")) return;
    uint64_t idCounter = project_->peekNextNoteId() - 1;
    NoteSelection created = duplicateNotes(track->notes, selectedNoteIds_, offset, idCounter);
    project_->ensureNoteIdAbove(idCounter);
    selectedNoteIds_ = std::move(created);
    notifyEdited();
}

void PianoRollComponent::copySelection() {
    Track* track = activeTrack();
    if (!track) return;
    clipboard_.clear();
    for (const auto& note : track->notes)
        if (selectedNoteIds_.count(note.id) > 0) clipboard_.push_back(note);
    notifyEditState();
}

void PianoRollComponent::cutSelection() {
    copySelection();
    deleteSelection(u8"Couper");   // D439 : le pas dit le geste -- couper, pas supprimer
}

void PianoRollComponent::paste() {
    Track* track = activeTrack();
    if (!track || !project_ || clipboard_.empty()) return;

    Tick minTick = clipboard_.front().startTick;
    for (const auto& n : clipboard_) minTick = std::min(minTick, n.startTick);
    const Tick offset = playheadTick_ - minTick;

    if (!beginEdit("Coller")) return;
    selectedNoteIds_.clear();
    for (const auto& n : clipboard_) {
        Note copy = n;
        copy.startTick = std::max<Tick>(0, n.startTick + offset);
        copy.endTick = copy.startTick + n.durationTicks();
        copy.id = project_->nextNoteId();
        track->notes.push_back(copy);
        selectedNoteIds_.insert(copy.id);
    }
    notifyEdited();
}

void PianoRollComponent::transposeSelection(int semitones) {
    Track* track = activeTrack();
    if (!track || selectedNoteIds_.empty()) return;
    // D432 : DE COMBIEN -- « Transposer + » valait pour ↑ comme pour Maj+↑.
    if (!beginEdit("Transposer " + juce::String(semitones > 0 ? "+" : "") + juce::String(semitones))) return;
    transposeNotes(track->notes, selectedNoteIds_, semitones);
    notifyEdited();
}

void PianoRollComponent::nudgeSelection(int64_t deltaTicks) {
    Track* track = activeTrack();
    if (!track || selectedNoteIds_.empty()) return;
    if (!beginEdit(u8"Décaler")) return;
    nudgeNotes(track->notes, selectedNoteIds_, deltaTicks);
    notifyEdited();
}

void PianoRollComponent::setSelectionLength(vsm::midi::Tick ticks) {
    Track* track = activeTrack();
    if (!track || selectedNoteIds_.empty()) return;
    if (!beginEdit(u8"Dur\u00e9e")) return;
    setNoteLengths(track->notes, selectedNoteIds_, std::max<Tick>(1, ticks));
    notifyEdited();
}

void PianoRollComponent::setSelectionLengthToGrid() {
    Track* track = activeTrack();
    if (!track || selectedNoteIds_.empty()) return;
    if (!beginEdit(u8"Durée = grille")) return;
    setNoteLengths(track->notes, selectedNoteIds_, gridTicks());
    notifyEdited();
}

void PianoRollComponent::scaleSelectionLength(float factor) {
    Track* track = activeTrack();
    if (!track || selectedNoteIds_.empty()) return;
    if (!beginEdit(juce::String(u8"Durée x%1").replace("%1", juce::String(factor, 2)))) return;
    scaleNoteLengths(track->notes, selectedNoteIds_, factor);
    notifyEdited();
}

bool PianoRollComponent::selectionStartTick(vsm::midi::Tick& debut) const {
    const Track* track = activeTrack();
    if (!track || selectedNoteIds_.empty()) return false;
    bool trouve = false;
    for (const auto& n : track->notes)
        if (selectedNoteIds_.count(n.id) > 0 && (!trouve || n.startTick < debut)) { debut = n.startTick; trouve = true; }
    return trouve;
}

void PianoRollComponent::scaleSelectionTime(double factor) {
    Track* track = activeTrack();
    if (!track || selectedNoteIds_.empty()) return;
    if (!beginEdit(factor > 1.0 ? juce::String(u8"Deux fois plus lent")
                                : juce::String(u8"Deux fois plus vite"))) return;
    scaleNoteTimes(track->notes, selectedNoteIds_, factor);
    notifyEdited();
}

void PianoRollComponent::quantizeSelection(float strength, bool alsoQuantizeEnds) {
    // D354 : UN GESTE QUI NE FAIT RIEN LE DIT. Quatre sorties anticipées, toutes
    // muettes : une course qui pressait « Quantifier » lisait « cliqué » et un
    // projet inchangé, et c'est la QUANTIFICATION qu'on allait soupçonner. La
    // règle du dépôt vaut ici comme ailleurs — ce qui est écarté est dit.
    // D355 : LA RAISON EST ÉCRITE AU `fputs`, ET NON PASSÉE À UNE LAMBDA. Le
    // premier jet de D354 confiait les cinq raisons à un `refuser(…)` : aucune ne
    // va à l'écran, mais `tools/inventaire_langue.py` ne lit que l'INSTRUCTION de
    // la chaîne, et cinq messages de banc sont entrés dans le compte ÉCRAN — le
    // chiffre même qui juge la traduction (A9 : 10 → 15). Une mesure qu'on
    // pollue en corrigeant autre chose est pire qu'une mesure absente.
    Track* track = activeTrack();
    if (!track) {
        std::fputs("VSM_QUANTIFIER : rien fait \xe2\x80\x94 aucune piste active\n", stderr);
        return;
    }
    if (!project_) {
        std::fputs("VSM_QUANTIFIER : rien fait \xe2\x80\x94 aucun projet\n", stderr);
        return;
    }
    if (selectedNoteIds_.empty()) {
        std::fputs("VSM_QUANTIFIER : rien fait \xe2\x80\x94 aucune note s\xc3\xa9lectionn\xc3\xa9""e\n", stderr);
        return;
    }

    QuantizeSettings settings;
    settings.grid = gridResolution_;
    settings.strength = strength;
    settings.swing = swing_;
    settings.quantizeNoteStart = true;
    settings.quantizeNoteEnd = alsoQuantizeEnds;

    // quantizeNotes() travaille sur un vecteur entier : on lui passe une copie
    // de la seule sélection, puis on réinjecte -- plutôt que de dupliquer sa
    // logique (swing, force partielle) ici, où elle ne serait pas testée.
    std::vector<Note> selected;
    for (const auto& n : track->notes)
        if (selectedNoteIds_.count(n.id) > 0) selected.push_back(n);
    if (selected.empty()) {
        std::fputs("VSM_QUANTIFIER : rien fait \xe2\x80\x94 la s\xc3\xa9lection ne d\xc3\xa9signe "
                   "aucune note de cette piste\n", stderr);
        return;
    }

    if (!beginEdit("Quantifier")) {
        std::fputs("VSM_QUANTIFIER : rien fait \xe2\x80\x94 l'\xc3\xa9""dition a \xc3\xa9t\xc3\xa9 "
                   "refus\xc3\xa9""e (piste verrouill\xc3\xa9""e ?)\n", stderr);
        return;
    }
    quantizeNotes(selected, settings, project_->ticksPerQuarterNote);
    for (auto& n : track->notes) {
        auto it = std::find_if(selected.begin(), selected.end(),
                                [&n](const Note& q) { return q.id == n.id; });
        if (it != selected.end()) n = *it;
    }
    std::fputs((juce::String("VSM_QUANTIFIER : ") + juce::String(static_cast<int>(selected.size()))
                + " note(s) quantifi\xc3\xa9" "es"
                + "\n").toRawUTF8(), stderr);
    notifyEdited();
}

void PianoRollComponent::humanizeSelection(float timingTicks, float velocityAmount) {
    Track* track = activeTrack();
    if (!track || selectedNoteIds_.empty()) return;

    HumanizeSettings settings;
    settings.seed = 0x5EED1234u;
    settings.timingAmountTicks = timingTicks;
    settings.velocityAmount = velocityAmount;

    std::vector<Note> selected;
    for (const auto& n : track->notes)
        if (selectedNoteIds_.count(n.id) > 0) selected.push_back(n);
    if (selected.empty()) return;

    if (!beginEdit("Humaniser")) return;
    humanizeNotes(selected, settings);
    for (auto& n : track->notes) {
        auto it = std::find_if(selected.begin(), selected.end(),
                                [&n](const Note& q) { return q.id == n.id; });
        if (it != selected.end()) n = *it;
    }
    notifyEdited();
}

void PianoRollComponent::applyLegatoToSelection() {
    Track* track = activeTrack();
    if (!track || selectedNoteIds_.empty()) return;
    if (!beginEdit("Legato")) return;
    applyLegato(track->notes, selectedNoteIds_);
    notifyEdited();
}

void PianoRollComponent::removeOverlapsInSelection() {
    Track* track = activeTrack();
    if (!track || selectedNoteIds_.empty()) return;
    if (!beginEdit("Retirer chevauchements")) return;
    removeOverlaps(track->notes, selectedNoteIds_);
    notifyEdited();
}

void PianoRollComponent::splitSelectionAtPlayhead() {
    Track* track = activeTrack();
    if (!track || !project_ || selectedNoteIds_.empty()) return;
    if (!beginEdit(juce::String::fromUTF8(reinterpret_cast<const char*>(u8"Couper à la tête de lecture")))) return;   // D439
    uint64_t idCounter = project_->peekNextNoteId() - 1;
    NoteSelection created;
    const size_t made = splitNotes(track->notes, selectedNoteIds_, playheadTick_, idCounter, &created);
    project_->ensureNoteIdAbove(idCounter);
    if (made > 0)
        for (uint64_t id : created) selectedNoteIds_.insert(id);
    notifyEdited();
}

void PianoRollComponent::joinSelection() {
    Track* track = activeTrack();
    if (!track || selectedNoteIds_.size() < 2) return;
    if (!beginEdit("Fusionner")) return;
    joinNotes(track->notes, selectedNoteIds_);
    notifyEdited();
}

void PianoRollComponent::reverseSelection() {
    Track* track = activeTrack();
    if (!track || selectedNoteIds_.size() < 2) return;
    if (!beginEdit(u8"Rétrograder")) return;
    reverseNotesInTime(track->notes, selectedNoteIds_);
    notifyEdited();
}

void PianoRollComponent::mirrorSelectionPitch() {
    Track* track = activeTrack();
    if (!track || selectedNoteIds_.empty()) return;
    if (!beginEdit("Miroir des hauteurs")) return;
    mirrorNotesPitch(track->notes, selectedNoteIds_);
    notifyEdited();
}

void PianoRollComponent::selectNotes(const NoteSelection& ids) {
    selectedNoteIds_ = ids;
    repaint();
    if (onEditStateChanged) onEditStateChanged();
}

void PianoRollComponent::setSelectionVelocity(uint8_t velocity) {
    Track* track = activeTrack();
    if (!track || selectedNoteIds_.empty()) return;
    if (!beginEdit(u8"Vélocité")) return;
    setVelocity(track->notes, selectedNoteIds_, velocity);
    notifyEdited();
}

void PianoRollComponent::scaleSelectionVelocity(float factor) {
    Track* track = activeTrack();
    if (!track || selectedNoteIds_.empty()) return;
    if (!beginEdit(juce::String(u8"Vélocité x%1").replace("%1", juce::String(factor, 2)))) return;
    scaleVelocity(track->notes, selectedNoteIds_, factor);
    notifyEdited();
}

void PianoRollComponent::rampSelectionVelocity(uint8_t from, uint8_t to) {
    Track* track = activeTrack();
    if (!track || selectedNoteIds_.empty()) return;
    if (!beginEdit(from < to ? "Crescendo" : "Decrescendo")) return;
    rampVelocity(track->notes, selectedNoteIds_, from, to);
    notifyEdited();
}

void PianoRollComponent::compressSelectionVelocity(float amount) {
    Track* track = activeTrack();
    if (!track || selectedNoteIds_.empty()) return;
    if (!beginEdit(amount <= 0.0f ? juce::String(u8"Égaliser les nuances")
                                  : juce::String(u8"Resserrer les nuances"))) return;
    compressVelocity(track->notes, selectedNoteIds_, amount);
    notifyEdited();
}

void PianoRollComponent::limitSelectionVelocity(uint8_t bas, uint8_t haut) {
    Track* track = activeTrack();
    if (!track || selectedNoteIds_.empty()) return;
    if (!beginEdit(u8"Contenir les nuances")) return;
    limitVelocity(track->notes, selectedNoteIds_, bas, haut);
    notifyEdited();
}

void PianoRollComponent::randomizeSelectionVelocity(int amount) {
    Track* track = activeTrack();
    if (!track || selectedNoteIds_.empty()) return;
    if (!beginEdit(u8"Vélocité aléatoire")) return;
    randomizeVelocity(track->notes, selectedNoteIds_, amount, 0xA11CEu);
    notifyEdited();
}

void PianoRollComponent::constrainSelectionToScale() {
    Track* track = activeTrack();
    if (!track || selectedNoteIds_.empty()) return;
    if (!beginEdit(u8"Contraindre à la gamme")) return;
    constrainNotesToScale(track->notes, selectedNoteIds_, scale_);
    notifyEdited();
}

void PianoRollComponent::toggleSelectionMuted() {
    Track* track = activeTrack();
    if (!track || selectedNoteIds_.empty()) return;
    if (!beginEdit("Rendre muet")) return;
    toggleNotesMuted(track->notes, selectedNoteIds_);
    notifyEdited();
}

void PianoRollComponent::arpeggiateSelection(ArpeggioMode mode) {
    Track* track = activeTrack();
    if (!track || selectedNoteIds_.empty()) return;
    if (!beginEdit(u8"Arpéger")) return;
    arpeggiateNotes(track->notes, selectedNoteIds_, gridTicks(), mode);
    notifyEdited();
}

void PianoRollComponent::insertChordAtPlayhead(ChordType type, uint8_t rootNote) {
    Track* track = activeTrack();
    if (!track || !project_) return;
    if (!beginEdit(u8"Insérer accord")) return;
    uint64_t idCounter = project_->peekNextNoteId() - 1;
    NoteSelection created = insertChord(track->notes, snapTick(playheadTick_), gridTicks() * 4,
                                         rootNote, type, track->channel, defaultVelocity_, idCounter);
    project_->ensureNoteIdAbove(idCounter);
    selectedNoteIds_ = std::move(created);
    notifyEdited();
}

void PianoRollComponent::setStepInputEnabled(bool enabled) {
    if (stepInput_ == enabled) return;
    stepInput_ = enabled;
    if (onStepInputChanged) onStepInputChanged(enabled);
    repaint();
}

void PianoRollComponent::stepInputNote(uint8_t note, uint8_t velocity) {
    Track* track = activeTrack();
    if (!track || !project_ || !stepInput_) return;
    if (!beginEdit(u8"Saisie pas à pas")) return;
    const vsm::midi::Tick debut = snapTick(playheadTick_);
    const vsm::midi::Tick pas = std::max<vsm::midi::Tick>(1, gridTicks());
    Note n;
    n.startTick = debut;
    n.endTick = debut + pas;
    n.number = note;
    n.velocity = velocity > 0 ? velocity : defaultVelocity_;
    n.channel = track->channel;
    n.id = project_->peekNextNoteId();
    project_->ensureNoteIdAbove(n.id);
    track->notes.push_back(n);
    selectedNoteIds_ = {n.id};
    avancerLaSaisie(debut + pas);
    notifyEdited();
}

void PianoRollComponent::stepInputRest() {
    if (!stepInput_) return;
    avancerLaSaisie(snapTick(playheadTick_) + std::max<vsm::midi::Tick>(1, gridTicks()));
    repaint();
}

void PianoRollComponent::stepInputBack() {
    if (!stepInput_) return;
    avancerLaSaisie(std::max<vsm::midi::Tick>(0, snapTick(playheadTick_) - std::max<vsm::midi::Tick>(1, gridTicks())));
    repaint();
}

void PianoRollComponent::demanderToutVoir() {
    // D502 : « ZOOM : TOUT VOIR (LES DEUX VUES) » — c'est le nom de Ctrl+0 dans la
    // table. Le piano roll reçoit la touche d'abord quand il a le clavier (D492)
    // et ne cadrait que lui ; il DEMANDE désormais à l'application, qui cadre les
    // deux. Sans application (un aperçu hors écran), il se cadre.
    if (onToutVoirDemande) onToutVoirDemande();
    else zoomToFit();
}

void PianoRollComponent::avancerLaSaisie(vsm::midi::Tick tick) {
    // D500 : LA POSITION D'INSERTION EST LA TÊTE DU TRANSPORT (D13.5), et c'est
    // elle qu'on déplace — par le chemin du clic sur la règle. Déplacer la seule
    // tête DESSINÉE ne tenait qu'un tour de minuterie : l'application y repousse
    // la position du transport trente fois par seconde, et des notes tapées à une
    // demi-seconde d'intervalle tombaient toutes au même tick.
    if (onPlayheadRequested) onPlayheadRequested(tick);
    setPlayheadTick(tick);
}

// ---------------------------------------------------------------------------
// Menu contextuel
// ---------------------------------------------------------------------------

juce::PopupMenu PianoRollComponent::buildContextMenu() const {
    // D80 : le menu Édition EST ce menu ; 64 entrées écrites sans `tr()`
    // le laissaient entièrement en français dans l'interface anglaise.
    using vsm::app::ui::tr;
    juce::PopupMenu menu;
    const bool sel = hasSelection();
    // D471 : UNE ENTRÉE QUI FAIT CE QUE FAIT UNE TOUCHE AFFICHE CETTE TOUCHE —
    // la touche EFFECTIVE, lue dans la table de l'utilisateur et dessinée par
    // JUCE à droite (D155). L'appariement se fait par l'ACTION : dans
    // `performShortcut` et `performContextMenuAction`, les deux appellent la
    // même fonction avec les mêmes arguments. `tools/touches-du-menu-edition.py`
    // le vérifie ; ajouter une entrée appariée par `addItem` nu la fait échouer.
    using vsm::app::ui::ajouterAvecRaccourci;
    using vsm::app::ui::ajouterAvecToucheFixe;
    using Id = vsm::interchange::ShortcutId;

    ajouterAvecRaccourci(menu, kCtxUndo, canUndo() ? tr(u8"Annuler : ") + vsm::app::ui::trGeste(undoLabel()) : tr("Annuler"),
                         shortcuts_, Id::EditUndo, canUndo());
    ajouterAvecRaccourci(menu, kCtxRedo, canRedo() ? tr(u8"Rétablir : ") + vsm::app::ui::trGeste(redoLabel()) : tr(u8"Rétablir"),
                         shortcuts_, Id::EditRedo, canRedo());
    menu.addSeparator();
    ajouterAvecRaccourci(menu, kCtxCut, tr("Couper"), shortcuts_, Id::EditCut, sel);
    ajouterAvecRaccourci(menu, kCtxCopy, tr("Copier"), shortcuts_, Id::EditCopy, sel);
    ajouterAvecRaccourci(menu, kCtxPaste, tr(u8"Coller à la tête de lecture"), shortcuts_, Id::EditPaste, !clipboard_.empty());
    ajouterAvecRaccourci(menu, kCtxDuplicate, tr("Dupliquer"), shortcuts_, Id::EditDuplicate, sel);
    ajouterAvecRaccourci(menu, kCtxDelete, tr("Supprimer"), shortcuts_, Id::EditDelete, sel);
    menu.addSeparator();

    juce::PopupMenu selectMenu;
    ajouterAvecRaccourci(selectMenu, kCtxSelectAll, tr(u8"Tout sélectionner"), shortcuts_, Id::EditSelectAll);
    ajouterAvecRaccourci(selectMenu, kCtxSelectNone, tr(u8"Tout désélectionner"), shortcuts_, Id::EditSelectNone, sel);
    ajouterAvecRaccourci(selectMenu, kCtxSelectInvert, tr(u8"Inverser la sélection"), shortcuts_, Id::EditInvertSelection);
    selectMenu.addItem(kCtxSelectSamePitch, tr(u8"Toutes les notes de même hauteur"), sel);
    // Les notes douteuses de la transcription (étape 11.3) : on y VA, une par
    // une, au lieu de les chercher à l'œil sur un morceau entier.
    const size_t douteuses = doubtfulNoteCount();
    selectMenu.addSeparator();
    // D358 : la touche vient de la TABLE, pas du libellé -- « (D) » et « (Maj+D) »
    // mentaient dès qu'on remaniait la touche. La « précédente » est la MÊME
    // commande avec Maj : sa composition suit donc la touche de base.
    selectMenu.addItem(kCtxSelectNextDoubtful,
                        vsm::app::ui::libelleAvecTouche(tr("Note douteuse suivante"), shortcuts_,
                                                         vsm::interchange::ShortcutId::NavNextDoubtful),
                        douteuses > 0);
    selectMenu.addItem(kCtxSelectPrevDoubtful,
                        vsm::app::ui::libelleAvecTouche(tr(u8"Note douteuse précédente"), shortcuts_,
                                                         vsm::interchange::ShortcutId::NavNextDoubtful,
                                                         tr("Maj+")),
                        douteuses > 0);
    selectMenu.addItem(kCtxSelectDoubtful,
                       douteuses > 0 ? tr("Toutes les notes douteuses") + " (" + juce::String(static_cast<int>(douteuses)) + ")"
                                     : tr("Toutes les notes douteuses"),
                       douteuses > 0);
    // D223 (A39) : LES PIRES D'ABORD. Mesuré sur six reconstructions, « douteuse »
    // couvre de 53 à 91 % des notes : l'entrée ci-dessus en choisit 2 600 sur
    // b4wuzthen, ce qui ne dit plus par où commencer. Celle-ci en désigne toujours
    // une minorité, par leur RANG de confiance et non par un seuil, et son libellé
    // dit combien -- c'est le geste qu'on attend d'une marque de doute.
    const size_t notesDeLaPiste = activeTrack() != nullptr ? activeTrack()->notes.size() : 0;
    const size_t dixPourCent = notesDeLaPiste == 0 ? 0
                                                   : std::max<size_t>(1, (notesDeLaPiste + 5) / 10);
    selectMenu.addItem(kCtxSelectLeastConfident,
                       dixPourCent > 0
                           ? tr(u8"Les 10 % les moins sûres") + " (" + juce::String(static_cast<int>(dixPourCent)) + ")"
                           : tr(u8"Les 10 % les moins sûres"),
                       dixPourCent > 0);
    // D21.1 : LES NOTES FANTÔMES D'UNE TRANSCRIPTION -- faibles, ou d'un
    // soixante-quatrième -- se choisissent d'un coup, à des seuils FIXES
    // plutôt que par une boîte de dialogue, pour que le geste enchaîne avec
    // Supprimer. Chaque entrée dit combien de notes elle prendrait.
    {
        const Track* track = activeTrack();
        const std::vector<Note> aucune;
        const std::vector<Note>& notes = track ? track->notes : aucune;
        const Tick grille = gridTicks();
        auto compte = [](const NoteSelection& s) { return " (" + juce::String(static_cast<int>(s.size())) + ")"; };
        selectMenu.addSeparator();
        selectMenu.addItem(kCtxSelectWeak64, tr(u8"Notes plus faibles que 64") + compte(selectNotesBelowVelocity(notes, 64)));
        selectMenu.addItem(kCtxSelectWeak32, tr(u8"Notes plus faibles que 32") + compte(selectNotesBelowVelocity(notes, 32)));
        selectMenu.addItem(kCtxSelectWeak16, tr(u8"Notes plus faibles que 16") + compte(selectNotesBelowVelocity(notes, 16)));
        selectMenu.addItem(kCtxSelectShortGrid, tr(u8"Notes plus courtes que la grille") + compte(selectNotesShorterThan(notes, grille)));
        selectMenu.addItem(kCtxSelectShortHalfGrid, tr(u8"Notes plus courtes que la moitié de la grille")
                                                        + compte(selectNotesShorterThan(notes, std::max<Tick>(1, grille / 2))));
    }
    menu.addSubMenu(tr(u8"Sélection"), selectMenu);

    juce::PopupMenu pitchMenu;
    // D471 : les flèches, touches FIXES — ↑ avec une sélection appelle
    // `transposeSelection(1)`, Maj+↑ `(12)`, exactement comme ces entrées.
    ajouterAvecToucheFixe(pitchMenu, kCtxTransposeUp, tr("Transposer +1 demi-ton"), "cursor up", sel);
    ajouterAvecToucheFixe(pitchMenu, kCtxTransposeDown, tr("Transposer -1 demi-ton"), "cursor down", sel);
    ajouterAvecToucheFixe(pitchMenu, kCtxOctaveUp, tr("Octave +"), "shift + cursor up", sel);
    ajouterAvecToucheFixe(pitchMenu, kCtxOctaveDown, tr("Octave -"), "shift + cursor down", sel);
    pitchMenu.addItem(kCtxMirror, tr("Miroir des hauteurs"), sel);
    pitchMenu.addItem(kCtxScaleConstrain, tr(u8"Contraindre à la gamme"), sel && scale_.type != ScaleType::Chromatic);
    menu.addSubMenu(tr("Hauteur"), pitchMenu);

    juce::PopupMenu timeMenu;
    ajouterAvecRaccourci(timeMenu, kCtxQuantizeFull, tr("Quantifier (100 %)"), shortcuts_, Id::EditQuantize, sel);
    timeMenu.addItem(kCtxQuantizeHalf, tr("Quantifier (50 %)"), sel);
    timeMenu.addItem(kCtxQuantizeEnds, tr(u8"Quantifier début ET fin"), sel);
    timeMenu.addItem(kCtxHumanize, tr("Humaniser"), sel);
    timeMenu.addSeparator();
    timeMenu.addItem(kCtxLengthToGrid, tr(u8"Durée = pas de grille"), sel);
    timeMenu.addItem(kCtxLengthDouble, tr(u8"Durée x2"), sel);
    timeMenu.addItem(kCtxLengthHalve, tr(u8"Durée /2"), sel);
    // D22.3 : LA PHRASE À MOITIÉ DE VITESSE, pas chaque note deux fois plus
    // longue -- les départs bougent aussi, depuis le premier de la sélection.
    timeMenu.addItem(kCtxTimesDouble, tr(u8"Deux fois plus lent (départs et durées ×2)"), sel);
    timeMenu.addItem(kCtxTimesHalve, tr(u8"Deux fois plus vite (départs et durées ÷2)"), sel);
    ajouterAvecRaccourci(timeMenu, kCtxLegato, tr("Legato"), shortcuts_, Id::EditLegato, sel);
    timeMenu.addItem(kCtxRemoveOverlaps, tr("Retirer les chevauchements"), sel);
    timeMenu.addSeparator();
    ajouterAvecRaccourci(timeMenu, kCtxSplit, tr(u8"Couper à la tête de lecture"), shortcuts_, Id::EditSplitAtPlayhead, sel);
    ajouterAvecRaccourci(timeMenu, kCtxJoin, tr("Fusionner"), shortcuts_, Id::EditJoin, selectedNoteIds_.size() >= 2);
    timeMenu.addItem(kCtxReverse, tr(u8"Rétrograder"), selectedNoteIds_.size() >= 2);
    menu.addSubMenu(tr(u8"Temps et durée"), timeMenu);

    juce::PopupMenu velocityMenu;
    velocityMenu.addItem(kCtxVelocityFull, tr(u8"Vélocité 127"), sel);
    velocityMenu.addItem(kCtxVelocityHalf, tr(u8"Vélocité 64"), sel);
    velocityMenu.addItem(kCtxVelocityUp, tr(u8"Vélocité +10 %"), sel);
    velocityMenu.addItem(kCtxVelocityDown, tr(u8"Vélocité -10 %"), sel);
    velocityMenu.addItem(kCtxVelocityRampUp, tr("Crescendo"), sel);
    velocityMenu.addItem(kCtxVelocityRampDown, tr("Decrescendo"), sel);
    velocityMenu.addItem(kCtxVelocityRandom, tr(u8"Aléatoire (±20)"), sel);
    velocityMenu.addSeparator();
    // D19.1 : les deux gestes qui servent une TRANSCRIPTION plutôt qu'une
    // intention musicale. Le libellé dit ce que ça fait, pas comment ça
    // s'appelle : « resserrer de moitié » se comprend sans savoir qu'un
    // compresseur a un rapport.
    velocityMenu.addItem(kCtxVelocityCompress, tr(u8"Resserrer les nuances de moitié"), sel);
    velocityMenu.addItem(kCtxVelocityCompressFull, tr(u8"Égaliser les nuances (toutes à la moyenne)"), sel);
    velocityMenu.addItem(kCtxVelocityLimit, tr(u8"Contenir entre 20 et 100"), sel);
    menu.addSubMenu(tr(u8"Vélocité"), velocityMenu);

    juce::PopupMenu arpMenu;
    arpMenu.addItem(kCtxArpUp, tr(u8"Arpéger : montant"), sel);
    arpMenu.addItem(kCtxArpDown, tr(u8"Arpéger : descendant"), sel);
    arpMenu.addItem(kCtxArpUpDown, tr(u8"Arpéger : aller-retour"), sel);
    arpMenu.addItem(kCtxArpRandom, tr(u8"Arpéger : aléatoire"), sel);
    menu.addSubMenu(tr(u8"Arpèges"), arpMenu);

    juce::PopupMenu chordMenu;
    const auto chordTypes = allChordTypes();
    for (size_t i = 0; i < chordTypes.size(); ++i)
        chordMenu.addItem(kCtxChordBase + static_cast<int>(i),
                           // D80 : traduit à l'affichage ; le nom reste français dans `core/`, et
                           // `tr()` lit en UTF-8 le « Diminué » que `juce::String` lisait en Latin-1.
                           tr(chordTypeName(chordTypes[i])) + tr(" sur ") +
                           juce::String(noteNumberToName(static_cast<uint8_t>(60 + scale_.root))));
    menu.addSubMenu(tr(u8"Insérer un accord"), chordMenu);

    menu.addSeparator();
    ajouterAvecRaccourci(menu, kCtxMute, tr("Rendre muet / audible"), shortcuts_, Id::EditToggleMute, sel);
    menu.addSeparator();
    ajouterAvecRaccourci(menu, kCtxZoomFit, tr("Zoom : tout voir"), shortcuts_, Id::ViewZoomToFit);
    menu.addItem(kCtxZoomSelection, tr(u8"Zoom : sur la sélection"), sel);
    // D497 : LE ZOOM ± ET LES SIX OUTILS, les dernières commandes de la table sans
    // entrée de menu. Ils n'agissent que sur le piano roll — ce menu est le sien
    // (D80) —, portent les libellés de la table (D355) et leur touche (D155), et
    // appellent ce que la touche appelle (`performShortcut`). L'outil courant est
    // coché : la barre le marque en couleur, le menu le dit.
    ajouterAvecRaccourci(menu, kCtxZoomIn, tr("Zoom avant"), shortcuts_, Id::ViewZoomIn);
    ajouterAvecRaccourci(menu, kCtxZoomOut, tr(u8"Zoom arrière"), shortcuts_, Id::ViewZoomOut);
    {
        juce::PopupMenu outils;
        ajouterAvecRaccourci(outils, kCtxToolSelect, tr(u8"Sélection"), shortcuts_, Id::ToolSelect, true, tool_ == Tool::Select);
        ajouterAvecRaccourci(outils, kCtxToolDraw, tr("Crayon"), shortcuts_, Id::ToolDraw, true, tool_ == Tool::Draw);
        ajouterAvecRaccourci(outils, kCtxToolErase, tr("Gomme"), shortcuts_, Id::ToolErase, true, tool_ == Tool::Erase);
        ajouterAvecRaccourci(outils, kCtxToolSplit, tr("Ciseaux"), shortcuts_, Id::ToolSplit, true, tool_ == Tool::Split);
        ajouterAvecRaccourci(outils, kCtxToolGlue, tr("Colle"), shortcuts_, Id::ToolGlue, true, tool_ == Tool::Glue);
        ajouterAvecRaccourci(outils, kCtxToolMute, tr("Muet"), shortcuts_, Id::ToolMute, true, tool_ == Tool::Mute);
        menu.addSubMenu(tr("Outils"), outils);
    }
    // D20.2 : REPLIER. L'entrée dit combien de hauteurs elle montrerait --
    // ou qu'il n'y en a aucune, auquel cas elle est grisée avec sa raison.
    {
        std::set<uint8_t> jouees;
        if (const Track* t = activeTrack()) for (const auto& n : t->notes) jouees.insert(n.number);
        menu.addItem(kCtxFold,
                     fold_ ? tr(u8"Déplier (toutes les hauteurs)")
                           : jouees.empty() ? tr(u8"Replier sur les hauteurs jouées (aucune note)")
                                            : tr(u8"Replier sur les hauteurs jouées") + " ("
                                                  + juce::String(static_cast<int>(jouees.size())) + ")",
                     fold_ || !jouees.empty(), fold_);
    }
    return menu;
}

bool PianoRollComponent::actionDeMenuPourCapture(const juce::String& libelle) {
    // D276 : « ? » ne fait rien et LISTE, comme les menus de clip (D222) et ceux
    // des règles (D218). Le menu du piano roll était le dernier de l'application
    // qu'aucune course ne pouvait LIRE : ses libellés se devinaient dans le code,
    // et un libellé qu'on devine se devine mal — c'est ce qui avait fait chercher
    // « Renommer… » sous trois orthographes avant D222. Le menu est construit une
    // seule fois et gardé : le reconstruire pour l'exécuter ensuite ferait lister
    // un menu et agir sur un autre.
    const juce::PopupMenu menu = buildContextMenu();
    if (libelle == "?") {
        juce::StringArray libelles;
        for (juce::PopupMenu::MenuItemIterator it(menu, true); it.next();)
            if (it.getItem().itemID != 0)
                libelles.add(it.getItem().text
                             + (it.getItem().shortcutKeyDescription.isNotEmpty()
                                    ? juce::String(" {") + it.getItem().shortcutKeyDescription + "}"
                                    : juce::String())
                             + (it.getItem().isEnabled ? "" : juce::String(" [grisee]")));
        std::fputs(("VSM_MENU_CONTEXTE : pianoroll = " + libelles.joinIntoString(" | ")
                    + "\n").toRawUTF8(), stderr);
        return true;
    }
    const int choix = vsm::app::ui::entreeParLibelle(menu, libelle);
    if (choix == 0) return false;
    performContextMenuAction(choix);
    return true;
}

void PianoRollComponent::performContextMenuAction(int menuItemId) {
    const auto chordTypes = allChordTypes();
    if (menuItemId >= kCtxChordBase && menuItemId < kCtxChordBase + static_cast<int>(chordTypes.size())) {
        insertChordAtPlayhead(chordTypes[static_cast<size_t>(menuItemId - kCtxChordBase)],
                               static_cast<uint8_t>(60 + scale_.root));
        return;
    }

    switch (menuItemId) {
        case kCtxUndo:             undo(); break;
        case kCtxRedo:             redo(); break;
        case kCtxCut:              cutSelection(); break;
        case kCtxCopy:             copySelection(); break;
        case kCtxPaste:            paste(); break;
        case kCtxDelete:           deleteSelection(); break;
        case kCtxDuplicate:        duplicateSelection(); break;
        case kCtxSelectAll:        selectAll(); break;
        case kCtxSelectNone:       selectNone(); break;
        case kCtxSelectInvert:     invertSelection(); break;
        case kCtxSelectSamePitch:  selectSamePitch(); break;
        case kCtxSelectNextDoubtful: selectNextDoubtfulNote(true); break;
        case kCtxSelectPrevDoubtful: selectNextDoubtfulNote(false); break;
        case kCtxSelectDoubtful:   selectDoubtfulNotes(); break;
        case kCtxSelectLeastConfident: selectLeastConfidentNotes(); break;   // D223
        case kCtxSelectWeak64:     selectBelowVelocity(64); break;
        case kCtxSelectWeak32:     selectBelowVelocity(32); break;
        case kCtxSelectWeak16:     selectBelowVelocity(16); break;
        case kCtxSelectShortGrid:  selectShorterThan(gridTicks()); break;
        case kCtxSelectShortHalfGrid: selectShorterThan(std::max<Tick>(1, gridTicks() / 2)); break;
        case kCtxTransposeUp:      transposeSelection(1); break;
        case kCtxTransposeDown:    transposeSelection(-1); break;
        case kCtxOctaveUp:         transposeSelection(12); break;
        case kCtxOctaveDown:       transposeSelection(-12); break;
        case kCtxQuantizeFull:     quantizeSelection(1.0f, false); break;
        case kCtxQuantizeHalf:     quantizeSelection(0.5f, false); break;
        case kCtxQuantizeEnds:     quantizeSelection(1.0f, true); break;
        case kCtxHumanize:         humanizeSelection(static_cast<float>(gridTicks()) * 0.12f, 12.0f); break;
        case kCtxLegato:           applyLegatoToSelection(); break;
        case kCtxRemoveOverlaps:   removeOverlapsInSelection(); break;
        case kCtxLengthToGrid:     setSelectionLengthToGrid(); break;
        case kCtxLengthDouble:     scaleSelectionLength(2.0f); break;
        case kCtxLengthHalve:      scaleSelectionLength(0.5f); break;
        case kCtxTimesDouble:      scaleSelectionTime(2.0); break;
        case kCtxTimesHalve:       scaleSelectionTime(0.5); break;
        case kCtxSplit:            splitSelectionAtPlayhead(); break;
        case kCtxJoin:             joinSelection(); break;
        case kCtxReverse:          reverseSelection(); break;
        case kCtxMirror:           mirrorSelectionPitch(); break;
        case kCtxMute:             toggleSelectionMuted(); break;
        case kCtxVelocityFull:     setSelectionVelocity(127); break;
        case kCtxVelocityHalf:     setSelectionVelocity(64); break;
        case kCtxVelocityUp:       scaleSelectionVelocity(1.1f); break;
        case kCtxVelocityDown:     scaleSelectionVelocity(0.9f); break;
        case kCtxVelocityRampUp:   rampSelectionVelocity(30, 120); break;
        case kCtxVelocityRampDown: rampSelectionVelocity(120, 30); break;
        case kCtxVelocityRandom:   randomizeSelectionVelocity(20); break;
        case kCtxVelocityCompress:     compressSelectionVelocity(0.5f); break;
        case kCtxVelocityCompressFull: compressSelectionVelocity(0.0f); break;
        case kCtxVelocityLimit:        limitSelectionVelocity(20, 100); break;
        case kCtxScaleConstrain:   constrainSelectionToScale(); break;
        case kCtxArpUp:            arpeggiateSelection(ArpeggioMode::Up); break;
        case kCtxArpDown:          arpeggiateSelection(ArpeggioMode::Down); break;
        case kCtxArpUpDown:        arpeggiateSelection(ArpeggioMode::UpDown); break;
        case kCtxArpRandom:        arpeggiateSelection(ArpeggioMode::Random); break;
        case kCtxZoomFit:          demanderToutVoir(); break;   // D502
        case kCtxZoomSelection:    zoomToSelection(); break;
        case kCtxZoomIn:           zoomHorizontally(1.25f); break;   // D497 : comme `performShortcut`
        case kCtxZoomOut:          zoomHorizontally(0.8f); break;
        case kCtxToolSelect:       setTool(Tool::Select); break;
        case kCtxToolDraw:         setTool(Tool::Draw); break;
        case kCtxToolErase:        setTool(Tool::Erase); break;
        case kCtxToolSplit:        setTool(Tool::Split); break;
        case kCtxToolGlue:         setTool(Tool::Glue); break;
        case kCtxToolMute:         setTool(Tool::Mute); break;
        case kCtxFold:             setFoldEnabled(!fold_); break;
        default: break;
    }
    // D233 : TOUTE SÉLECTION DIT SON COMPTE. Une sélection ne se lit pas sans
    // souris : elle ne change ni le titre, ni un texte, et le piano roll la PEINT
    // (la leçon de D149).
    //
    // D357 : LA LIGNE EST PARTIE DANS `notifyEditState`, où elle couvre les trois
    // chemins au lieu d'un. La garder ICI en plus ferait écrire deux lignes pour
    // un seul geste, et tout banc qui les compte compterait double.
}

// ---------------------------------------------------------------------------
// Souris
// ---------------------------------------------------------------------------

void PianoRollComponent::mouseDown(const juce::MouseEvent& event) {
    grabKeyboardFocus();
    Track* track = activeTrack();
    if (!track || !project_) return;

    const juce::Point<float> pos = event.position;
    dragStartMousePos_ = pos;

    // --- Clavier de gauche : écoute d'une note ----------------------------
    if (pos.x < static_cast<float>(keyboardWidth())) {
        dragMode_ = DragMode::Audition;
        startAudition(yToNote(pos.y), defaultVelocity_);
        repaint();
        return;
    }

    // --- Bouton du milieu (ou espace enfoncé) : déplacement de la vue -----
    if (event.mods.isMiddleButtonDown()) {
        dragMode_ = DragMode::Pan;
        panStartTick_ = scrollTick_;
        panStartTopNote_ = topNote_;
        return;
    }

    // --- Clic droit : menu contextuel -------------------------------------
    if (event.mods.isPopupMenu()) {
        // Si le clic droit tombe sur une note non sélectionnée, on la
        // sélectionne d'abord : agir sur une note qu'on ne voit pas
        // sélectionnée serait déroutant.
        if (Note* hit = findNoteAt(pos)) {
            if (selectedNoteIds_.count(hit->id) == 0) {
                selectedNoteIds_.clear();
                selectedNoteIds_.insert(hit->id);
                repaint();
            }
        }
        buildContextMenu().showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                                          [this](int result) { if (result != 0) performContextMenuAction(result); });
        dragMode_ = DragMode::None;
        return;
    }

    bool nearRight = false, nearLeft = false;
    Note* hit = findNoteAt(pos, &nearRight, &nearLeft);

    // --- Outils dédiés ----------------------------------------------------
    switch (tool_) {
        case Tool::Erase:
            if (hit) {
                if (!beginEdit("Effacer")) return;
                const uint64_t id = hit->id;
                track->notes.erase(std::remove_if(track->notes.begin(), track->notes.end(),
                                                   [id](const Note& n) { return n.id == id; }),
                                    track->notes.end());
                selectedNoteIds_.erase(id);
                notifyEdited();
            }
            dragMode_ = DragMode::Erase;
            return;

        case Tool::Split:
            if (hit) {
                if (!beginEdit("Couper une note")) return;   // D439 : les ciseaux scindent
                const Tick cut = snapTick(xToTick(pos.x));
                uint64_t idCounter = project_->peekNextNoteId() - 1;
                NoteSelection one{hit->id};
                splitNotes(track->notes, one, cut, idCounter, nullptr);
                project_->ensureNoteIdAbove(idCounter);
                notifyEdited();
            }
            dragMode_ = DragMode::None;
            return;

        case Tool::Glue:
            if (hit) {
                // Colle la note cliquée avec la suivante de même hauteur.
                NoteSelection pair{hit->id};
                const Note reference = *hit;
                const Note* best = nullptr;
                for (const auto& other : track->notes) {
                    if (other.id == reference.id || other.number != reference.number) continue;
                    if (other.startTick < reference.endTick) continue;
                    if (!best || other.startTick < best->startTick) best = &other;
                }
                if (best) {
                    pair.insert(best->id);
                    if (!beginEdit("Coller les notes")) return;
                    joinNotes(track->notes, pair);
                    selectedNoteIds_ = pair;
                    notifyEdited();
                }
            }
            dragMode_ = DragMode::None;
            return;

        case Tool::Mute:
            if (hit) {
                if (!beginEdit("Rendre muet")) return;
                toggleNotesMuted(track->notes, NoteSelection{hit->id});
                notifyEdited();
            }
            dragMode_ = DragMode::None;
            return;

        case Tool::Draw:
        case Tool::Select:
        default:
            break;
    }

    if (hit && tool_ == Tool::Select) {
        if (!event.mods.isShiftDown() && selectedNoteIds_.count(hit->id) == 0)
            selectedNoteIds_.clear();
        if (event.mods.isShiftDown() && selectedNoteIds_.count(hit->id) > 0)
            selectedNoteIds_.erase(hit->id); // Maj sur une note déjà prise = la retirer
        else
            selectedNoteIds_.insert(hit->id);

        draggedNoteId_ = hit->id;
        dragStartTick_ = xToTick(pos.x);
        dragStartNoteNumber_ = yToNote(pos.y);
        dragDidCopy_ = false;

        dragSnapshot_.clear();
        for (const auto& n : track->notes)
            if (selectedNoteIds_.count(n.id) > 0) dragSnapshot_.push_back(n);

        if (nearRight)      dragMode_ = DragMode::ResizeRight;
        else if (nearLeft)  dragMode_ = DragMode::ResizeLeft;
        else                dragMode_ = DragMode::Move;

        // L'état d'avant-glissement est mémorisé maintenant : le glissement
        // entier (déplacement continu) compte pour UNE seule annulation.
        if (!beginEdit(dragMode_ == DragMode::Move ? juce::String(u8"Déplacer") : juce::String("Redimensionner"))) return;
        startAudition(hit->number, hit->velocity);
    } else if (!hit && (tool_ == Tool::Draw || (tool_ == Tool::Select && !event.mods.isCommandDown()))) {
        // Création d'une note, puis glissement immédiat sur sa durée.
        if (!beginEdit(u8"Créer une note")) return;
        const Tick startTick = snapTick(xToTick(pos.x));
        const uint8_t noteNumber = yToNote(pos.y);
        Note newNote{startTick, startTick + gridTicks(), track->channel, noteNumber,
                      defaultVelocity_, 64, project_->nextNoteId()};
        track->notes.push_back(newNote);

        selectedNoteIds_.clear();
        selectedNoteIds_.insert(newNote.id);
        draggedNoteId_ = newNote.id;
        dragSnapshot_ = { newNote };
        dragMode_ = DragMode::ResizeRight;
        startAudition(noteNumber, defaultVelocity_);
        notifyEdited();
    } else {
        if (!event.mods.isShiftDown()) selectedNoteIds_.clear();
        dragMode_ = DragMode::RubberBandSelect;
        rubberBandRect_ = { pos.x, pos.y, 0.0f, 0.0f };
    }
    repaint();
}

void PianoRollComponent::mouseDrag(const juce::MouseEvent& event) {
    Track* track = activeTrack();
    if (!track || !project_) return;
    const juce::Point<float> pos = event.position;

    switch (dragMode_) {
        case DragMode::Audition: {
            const uint8_t note = yToNote(pos.y);
            if (auditionNote_ != static_cast<int>(note)) startAudition(note, defaultVelocity_);
            break;
        }

        case DragMode::Pan: {
            const double deltaTicks = static_cast<double>(dragStartMousePos_.x - pos.x) / pixelsPerTick_;
            scrollTick_ = std::max<Tick>(0, panStartTick_ + static_cast<Tick>(deltaTicks));
            const int deltaNotes = static_cast<int>((pos.y - dragStartMousePos_.y) / static_cast<float>(noteHeight_));
            topNote_ = juce::jlimit(12, 127, panStartTopNote_ + deltaNotes);
            updateScrollBars();
            repaint();
            break;
        }

        case DragMode::Erase: {
            // Balayage : efface toutes les notes survolées, sans confirmation.
            if (Note* hit = findNoteAt(pos)) {
                if (!beginEdit("Effacer")) return;
                const uint64_t id = hit->id;
                track->notes.erase(std::remove_if(track->notes.begin(), track->notes.end(),
                                                   [id](const Note& n) { return n.id == id; }),
                                    track->notes.end());
                selectedNoteIds_.erase(id);
                notifyEdited();
            }
            break;
        }

        case DragMode::Move: {
            // Alt maintenu = déplacer une COPIE (le geste "dupliquer en
            // glissant" universel). La copie n'est créée qu'une fois, au
            // premier mouvement, sinon chaque pixel parcouru en créerait une.
            if (event.mods.isAltDown() && !dragDidCopy_) {
                uint64_t idCounter = project_->peekNextNoteId() - 1;
                NoteSelection copies = duplicateNotes(track->notes, selectedNoteIds_, 0, idCounter);
                project_->ensureNoteIdAbove(idCounter);
                // Les ORIGINAUX restent en place ; on déplace les copies.
                selectedNoteIds_ = copies;
                dragSnapshot_.clear();
                for (const auto& n : track->notes)
                    if (selectedNoteIds_.count(n.id) > 0) dragSnapshot_.push_back(n);
                dragDidCopy_ = true;
            }

            const Tick newAnchorTick = snapTick(xToTick(pos.x));
            const Tick deltaTick = newAnchorTick - snapTick(dragStartTick_);
            const int deltaNote = static_cast<int>(yToNote(pos.y)) - static_cast<int>(dragStartNoteNumber_);

            for (const auto& snap : dragSnapshot_) {
                auto it = std::find_if(track->notes.begin(), track->notes.end(),
                                        [&snap](const Note& n) { return n.id == snap.id; });
                if (it == track->notes.end()) continue;
                const Tick duration = snap.durationTicks();
                it->startTick = std::max<Tick>(0, snap.startTick + deltaTick);
                it->endTick = it->startTick + duration;
                it->number = static_cast<uint8_t>(juce::jlimit(0, 127, static_cast<int>(snap.number) + deltaNote));
                if (it->id == draggedNoteId_) startAudition(it->number, it->velocity);
            }
            dragEdite_ = true;
            if (onNotesEdited) onNotesEdited();
            repaint();
            break;
        }

        case DragMode::ResizeRight: {
            const Tick newEnd = snapTick(xToTick(pos.x));
            for (const auto& snap : dragSnapshot_) {
                auto it = std::find_if(track->notes.begin(), track->notes.end(),
                                        [&snap](const Note& n) { return n.id == snap.id; });
                if (it == track->notes.end()) continue;
                // Toutes les notes sélectionnées suivent le même DELTA de
                // durée, pas la même fin absolue : redimensionner un accord
                // doit préserver ses durées relatives.
                const Tick delta = newEnd - dragSnapshot_.front().endTick;
                it->endTick = std::max(snap.endTick + delta, it->startTick + 1);
            }
            dragEdite_ = true;
            if (onNotesEdited) onNotesEdited();
            repaint();
            break;
        }

        case DragMode::ResizeLeft: {
            const Tick newStart = snapTick(xToTick(pos.x));
            for (const auto& snap : dragSnapshot_) {
                auto it = std::find_if(track->notes.begin(), track->notes.end(),
                                        [&snap](const Note& n) { return n.id == snap.id; });
                if (it == track->notes.end()) continue;
                const Tick delta = newStart - dragSnapshot_.front().startTick;
                it->startTick = std::max<Tick>(0, std::min(snap.startTick + delta, it->endTick - 1));
            }
            dragEdite_ = true;
            if (onNotesEdited) onNotesEdited();
            repaint();
            break;
        }

        case DragMode::RubberBandSelect: {
            rubberBandRect_ = juce::Rectangle<float>(dragStartMousePos_, pos);
            selectedNoteIds_.clear();
            for (const auto& note : track->notes) {
                const float x1 = tickToX(note.startTick), x2 = tickToX(note.endTick);
                const float y = static_cast<float>(noteToY(note.number));
                const juce::Rectangle<float> noteRect(x1, y, std::max(2.0f, x2 - x1), static_cast<float>(noteHeight_));
                if (rubberBandRect_.intersects(noteRect)) selectedNoteIds_.insert(note.id);
            }
            notifyEditState();
            repaint();
            break;
        }

        case DragMode::None:
            break;
    }
    updateStatusText(pos, true);
}

void PianoRollComponent::mouseUp(const juce::MouseEvent&) {
    stopAudition();
    const bool edite = dragEdite_;
    dragMode_ = DragMode::None;
    rubberBandRect_ = {};
    dragDidCopy_ = false;
    dragEdite_ = false;
    // D337 : le glissement a déplacé ou redimensionné des notes -- une dernière
    // notification, HORS glissement, pour que le clip couvre la note où elle est
    // posée (et non tout le chemin qu'elle a parcouru).
    if (edite && onNotesEdited) onNotesEdited();
    notifyEditState();
    repaint();
}

void PianoRollComponent::mouseMove(const juce::MouseEvent& event) {
    bool nearRight = false, nearLeft = false;
    const Note* hit = findNoteAt(event.position, &nearRight, &nearLeft);
    const uint64_t newHover = hit ? hit->id : 0;
    if (newHover != hoveredNoteId_) { hoveredNoteId_ = newHover; repaint(); }

    if (event.position.x < static_cast<float>(keyboardWidth()))
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    else if (tool_ == Tool::Erase)
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
    else if (hit && (nearRight || nearLeft))
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else if (hit)
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);

    updateStatusText(event.position, true);
}

void PianoRollComponent::mouseExit(const juce::MouseEvent& event) {
    hoveredNoteId_ = 0;
    updateStatusText(event.position, false);
    repaint();
}

void PianoRollComponent::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) {
    if (event.mods.isCtrlDown() || event.mods.isCommandDown()) {
        const float factor = wheel.deltaY > 0 ? 1.15f : 1.0f / 1.15f;
        if (event.mods.isShiftDown()) zoomVertically(factor);
        else                          zoomHorizontally(factor);
    } else if (event.mods.isShiftDown()) {
        const double ticksPerNotch = 480.0 / std::max(0.01, pixelsPerTick_ * 4.0);
        scrollTick_ = std::max<Tick>(0, scrollTick_ - static_cast<Tick>(wheel.deltaY * ticksPerNotch));
        updateScrollBars();
        repaint();
    } else {
        topNote_ = juce::jlimit(12, 127, topNote_ + (wheel.deltaY > 0 ? 2 : -2));
        updateScrollBars();
        repaint();
    }
}

// ---------------------------------------------------------------------------
// Clavier
// ---------------------------------------------------------------------------

bool PianoRollComponent::keyPressed(const juce::KeyPress& key) {
    // D492 : LE CLAVIER D'ORDINATEUR ACTIF PREND SES LETTRES, même ici (D11.7 :
    // « il emprunte les lettres aux raccourcis ») -- la touche remonte à
    // l'application, qui la joue.
    if (toucheDuClavier && toucheDuClavier(key)) return false;
    // D501 : EN SAISIE PAS À PAS, ENTRÉE ET RETOUR ARRIÈRE SONT DES TOUCHES DE
    // SAISIE, ici comme dans l'application (D13.5 : un silence, un pas en arrière).
    // L'application les traite quand elle a le clavier ; le piano roll, qui le
    // reçoit d'abord (D492), faisait de Retour arrière « Supprimer » — et la note
    // qu'on venait de saisir, choisie, était effacée au lieu de reculer.
    if (stepInput_ && !key.getModifiers().isAnyModifierKeyDown()) {
        if (key.getKeyCode() == juce::KeyPress::returnKey) { stepInputRest(); return true; }
        if (key.getKeyCode() == juce::KeyPress::backspaceKey) { stepInputBack(); return true; }
    }
    const auto mods = key.getModifiers();

    // LA TOUCHE NE DÉCIDE PLUS DE RIEN (D10.3) : elle désigne une COMMANDE, et
    // c'est la table qui fait la correspondance. Le `switch` sur des codes de
    // touches qui vivait ici était le seul endroit où l'on pouvait apprendre ce
    // que fait « Ctrl+J » -- en le lisant.
    if (shortcuts_ != nullptr) {
        vsm::interchange::ShortcutId commande{};
        if (vsm::app::ui::lookupShortcut(*shortcuts_, key, commande)
            && performShortcut(commande, mods))
            return true;
    }

    // LES FLÈCHES NE SONT PAS DES COMMANDES, ET C'EST POURQUOI ELLES NE SONT PAS
    // DANS LA TABLE : leur sens EST leur direction. Les réassigner produirait
    // une flèche gauche qui monte. La page des raccourcis les liste quand même,
    // marquées comme fixes -- taire quatre touches serait mentir davantage que
    // de dire « celles-ci ne bougent pas ».
    // D433 : LE CODE DE LA TOUCHE, PAS `key == KeyPress::upKey`. L'`operator==(int)`
    // de JUCE exige qu'AUCUN modificateur ne soit tenu : la branche Maj ci-dessous
    // (une octave, quatre pas) n'a jamais été atteinte, alors que la page des
    // raccourcis la promet. Ctrl et Alt restent à la table des raccourcis.
    const auto fleche = [&key, &mods](int code) {
        return key.getKeyCode() == code && !mods.isCommandDown() && !mods.isCtrlDown() && !mods.isAltDown();
    };
    const Tick step = mods.isShiftDown() ? gridTicks() * 4 : gridTicks();
    if (fleche(juce::KeyPress::leftKey)) {
        if (hasSelection()) nudgeSelection(-static_cast<int64_t>(step));
        else { scrollTick_ = std::max<Tick>(0, scrollTick_ - step); updateScrollBars(); repaint(); }
        return true;
    }
    if (fleche(juce::KeyPress::rightKey)) {
        if (hasSelection()) nudgeSelection(static_cast<int64_t>(step));
        else { scrollTick_ += step; updateScrollBars(); repaint(); }
        return true;
    }
    if (fleche(juce::KeyPress::upKey)) {
        if (hasSelection()) transposeSelection(mods.isShiftDown() ? 12 : 1);
        else { topNote_ = juce::jlimit(12, 127, topNote_ + 1); updateScrollBars(); repaint(); }
        return true;
    }
    if (fleche(juce::KeyPress::downKey)) {
        if (hasSelection()) transposeSelection(mods.isShiftDown() ? -12 : -1);
        else { topNote_ = juce::jlimit(12, 127, topNote_ - 1); updateScrollBars(); repaint(); }
        return true;
    }
    return false;
}

bool PianoRollComponent::performShortcut(vsm::interchange::ShortcutId id,
                                          const juce::ModifierKeys& mods) {
    using Id = vsm::interchange::ShortcutId;
    switch (id) {
        case Id::EditDelete:          deleteSelection(); return true;
        case Id::EditSelectNone:      selectNone(); return true;
        case Id::EditUndo:            undo(); return true;
        case Id::EditRedo:            redo(); return true;
        case Id::EditSelectAll:       selectAll(); return true;
        case Id::EditInvertSelection: invertSelection(); return true;
        case Id::EditCopy:            copySelection(); return true;
        case Id::EditCut:             cutSelection(); return true;
        case Id::EditPaste:           paste(); return true;
        case Id::EditDuplicate:       duplicateSelection(); return true;
        case Id::EditLegato:          applyLegatoToSelection(); return true;
        case Id::EditQuantize:        quantizeSelection(1.0f, false); return true;
        case Id::EditToggleMute:      toggleSelectionMuted(); return true;
        case Id::EditJoin:            joinSelection(); return true;
        case Id::EditSplitAtPlayhead: splitSelectionAtPlayhead(); return true;
        case Id::EditToggleSnap:      setSnapEnabled(!snapEnabled_); notifyEditState(); return true;
        case Id::ToolSelect:          setTool(Tool::Select); return true;
        case Id::ToolDraw:            setTool(Tool::Draw); return true;
        case Id::ToolErase:           setTool(Tool::Erase); return true;
        case Id::ToolSplit:           setTool(Tool::Split); return true;
        case Id::ToolGlue:            setTool(Tool::Glue); return true;
        case Id::ToolMute:            setTool(Tool::Mute); return true;
        case Id::ViewZoomToFit:       demanderToutVoir(); return true;   // D502 : les deux vues
        case Id::ViewZoomIn:          zoomHorizontally(1.25f); return true;
        case Id::ViewZoomOut:         zoomHorizontally(0.8f); return true;
        // « D » comme douteuse : la suivante, Maj+D la précédente. C'est un
        // geste de relecture qu'on répète des dizaines de fois sur un morceau
        // transcrit, d'où l'absence de modificateur.
        case Id::NavNextDoubtful:     selectNextDoubtfulNote(!mods.isShiftDown()); return true;
        // Ce qui appartient à l'application (enregistrer, transport, écoute
        // A/B) n'est pas rendu ici : le piano roll répond faux, et la touche
        // remonte à qui sait quoi en faire.
        default: return false;
    }
}

// ---------------------------------------------------------------------------
// Barre d'état
// ---------------------------------------------------------------------------

void PianoRollComponent::updateStatusText(juce::Point<float> mousePos, bool mouseInside) {
    derniereSouris_ = mousePos;   // D515
    sourisDedans_ = mouseInside;
    if (!onStatusChanged || !project_) return;
    juce::String text;

    if (mouseInside) {
        const Tick tick = std::max<Tick>(0, xToTick(mousePos.x));
        const uint8_t note = yToNote(mousePos.y);
        // Position musicale lisible : mesure.temps.tick, comme un séquenceur.
        const Tick ticksPerBeat = project_->ticksPerQuarterNote;
        Tick ticksPerBar = project_->timeSignatureMap.ticksPerBar(tick, project_->ticksPerQuarterNote);
        if (ticksPerBar <= 0) ticksPerBar = ticksPerBeat * 4;
        const Tick bar = tick / ticksPerBar + 1;
        const Tick beat = (tick % ticksPerBar) / ticksPerBeat + 1;
        const Tick rest = (tick % ticksPerBeat);
        text << vsm::app::ui::tr("Mes ") << juce::String(static_cast<int>(bar)) << "." << juce::String(static_cast<int>(beat))
             << "." << juce::String(static_cast<int>(rest)) << "   " << noteName(note);
    }

    if (const Track* track = activeTrack()) {
        const SelectionStats stats = computeSelectionStats(track->notes, selectedNoteIds_);
        if (stats.count > 0) {
            if (text.isNotEmpty()) text << "   |   ";
            text << vsm::app::ui::tr(u8"%1 note(s) sélectionnée(s) : %2 - %3, vélocité moyenne %4")
                        .replace("%1", juce::String(static_cast<int>(stats.count)))
                        .replace("%2", noteName(stats.lowestNote))
                        .replace("%3", noteName(stats.highestNote))
                        .replace("%4", juce::String(std::lround(stats.averageVelocity)));
        } else {
            if (text.isNotEmpty()) text << "   |   ";
            text << vsm::app::ui::tr(u8"%1 note(s) sur la piste").replace("%1", juce::String(static_cast<int>(track->notes.size())));
        }
        // Le compte des notes douteuses reste affiché tant qu'il en reste :
        // c'est le travail de relecture qu'il reste à faire, et « D » y mène.
        if (const size_t douteuses = countDoubtfulNotes(track->notes); douteuses > 0)
            text << "   |   " << vsm::app::ui::tr(u8"%1 douteuse(s) — D : la suivante")
                                     .replace("%1", juce::String(static_cast<int>(douteuses)));
    }
    onStatusChanged(text);
}

// ---------------------------------------------------------------------------
// Rendu
// ---------------------------------------------------------------------------

void PianoRollComponent::paint(juce::Graphics& g) {
    g.fillAll(Palette::background);
    refreshFoldRows();   // les hauteurs jouées ont pu changer depuis le dernier dessin

    if (!project_ || !activeTrack()) {
        g.setColour(Palette::textSecondary);
        g.setFont(16.0f);
        g.drawText(vsm::app::ui::tr(u8"Sélectionnez une piste pour éditer ses notes"),
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    // UNE PISTE AUDIO N'A PAS DE NOTES À ÉDITER ICI, et il faut le dire :
    // sans ce mot, l'écran montrait une grille vide et les notes fantômes
    // d'une autre piste, comme si la voix reportée avait perdu ses notes.
    if (activeTrack()->kind == Track::Kind::Group) {
        drawGrid(g);
        drawKeyboard(g);
        g.setColour(Palette::textSecondary);
        g.setFont(16.0f);
        g.drawFittedText(vsm::app::ui::tr(u8"Bus de groupe : il additionne les pistes routées vers lui.\n"
                            u8"Il n'a pas de notes ; ses effets et son fader se règlent dans le mixeur."),
                         getLocalBounds().withTrimmedLeft(keyboardWidth()).reduced(24),
                         juce::Justification::centred, 3);
        return;
    }
    if (activeTrack()->kind == Track::Kind::Audio) {
        drawGrid(g);
        drawKeyboard(g);
        g.setColour(Palette::textSecondary);
        g.setFont(16.0f);
        g.drawFittedText(vsm::app::ui::tr(u8"Piste audio : son matériau se voit et se coupe dans l'arrangement.\n"
                            u8"Il n'y a pas de notes à éditer ici."),
                         getLocalBounds().withTrimmedLeft(keyboardWidth()).reduced(24),
                         juce::Justification::centred, 3);
        return;
    }

    drawGrid(g);
    drawLoopRegion(g);
    if (ghostNotes_) drawGhostNotes(g);
    drawNotes(g);
    drawPlayhead(g);
    drawKeyboard(g);

    if (dragMode_ == DragMode::RubberBandSelect) {
        g.setColour(Palette::accentTeal.withAlpha(0.15f));
        g.fillRect(rubberBandRect_);
        g.setColour(Palette::accentTeal);
        g.drawRect(rubberBandRect_, 1.0f);
    }
}

void PianoRollComponent::resized() {
    const auto bounds = getLocalBounds();
    horizontalScrollBar_.setBounds(keyboardWidth(), bounds.getBottom() - kScrollBarThickness,
                                    bounds.getWidth() - keyboardWidth() - kScrollBarThickness, kScrollBarThickness);
    verticalScrollBar_.setBounds(bounds.getRight() - kScrollBarThickness, 0,
                                  kScrollBarThickness, bounds.getHeight() - kScrollBarThickness);
    // D461 : un cadrage que personne n'a déplacé depuis suit la taille du panneau.
    // Une vue déplacée, ou restaurée depuis le projet (D369), a une autre note du
    // haut : elle n'est pas touchée, et le souvenir du cadrage s'efface.
    if (hauteurCentree_ >= 0 && topNote_ == topNoteDuCadrage_ && !fold_) {
        const int lignesVisibles = std::max(1, contentArea().getHeight() / std::max(1, noteHeight_));
        topNote_ = juce::jlimit(12, 127, hauteurCentree_ + lignesVisibles / 2);
        topNoteDuCadrage_ = topNote_;
    } else {
        hauteurCentree_ = -1;
    }
    updateScrollBars();
}

juce::Rectangle<int> PianoRollComponent::contentArea() const {
    return getLocalBounds().withTrimmedLeft(keyboardWidth())
                            .withTrimmedRight(kScrollBarThickness)
                            .withTrimmedBottom(kScrollBarThickness);
}

void PianoRollComponent::drawKeyboard(juce::Graphics& g) const {
    const auto bounds = getLocalBounds();
    g.setColour(Palette::panel);
    g.fillRect(0, 0, keyboardWidth(), bounds.getHeight());

    // Touches "enfoncées" : les notes de la piste active qui couvrent la tête
    // de lecture. Dérivé du MODÈLE, pas de l'état du moteur audio -- ça
    // reflète ce qui est programmé à cette position, transport arrêté compris.
    std::array<bool, 128> keyPressed{};
    if (const Track* track = activeTrack()) {
        for (const auto& note : track->notes)
            if (!note.muted && note.startTick <= playheadTick_ && playheadTick_ < note.endTick && note.number < 128)
                keyPressed[note.number] = true;
    }
    if (auditionNote_ >= 0 && auditionNote_ < 128) keyPressed[static_cast<size_t>(auditionNote_)] = true;
    const Track* pisteDeBatterie = nullptr;
    if (const Track* track = activeTrack(); track && track->channel == 9) pisteDeBatterie = track;

    // Rangée par rangée, de l'aiguë à la grave : repliée (D20.2), une rangée
    // est une hauteur JOUÉE, et il n'y en a que quelques-unes.
    const int premiereRangee = rowOfNote(topNote_);
    const int derniereRangee = std::min(rowCount() - 1,
                                        premiereRangee + bounds.getHeight() / noteHeight_ + 1);
    for (int rangee = premiereRangee; rangee <= derniereRangee; ++rangee) {
        const int note = noteOfRow(rangee);
        const int y = noteToY(static_cast<uint8_t>(note));
        const bool black = isBlackKey(note);
        const bool pressed = keyPressed[static_cast<size_t>(note)];
        const int width = black ? (keyboardWidth() * 2 / 3) : keyboardWidth();

        juce::Colour keyColour = black ? Palette::pianoKeyBlack : Palette::pianoKeyWhite;
        if (scaleHighlight_ && scale_.type != ScaleType::Chromatic &&
            isNoteInScale(static_cast<uint8_t>(note), scale_))
            keyColour = keyColour.brighter(0.18f);
        if (pressed) keyColour = black ? Palette::accentAmber.darker(0.35f) : Palette::accentAmber;

        g.setColour(keyColour);
        g.fillRect(0, y, width, noteHeight_ - 1);

        if (pressed) {
            g.setColour(Palette::accentAmber.brighter(0.4f));
            g.drawRect(0.0f, static_cast<float>(y), static_cast<float>(width),
                        static_cast<float>(noteHeight_ - 1), 1.5f);
        }
        // Nom de note sur les do, et sur toutes les touches si la place le permet.
        // SUR UNE PISTE DE BATTERIE (canal 10), LA PIÈCE PLUTÔT QUE LA HAUTEUR :
        // « charleston fermé » se lit, « F#2 » se devine. La pièce vient de la
        // machine assignée, sinon de la convention General MIDI.
        juce::String etiquette;
        if (pisteDeBatterie) {
            const std::string piece = vsm::app::ui::drumVoiceName(pisteDeBatterie->instrumentId,
                                                    static_cast<uint8_t>(note));
            // D338 : une pièce se nomme dès que son rang porte 12 pt (ou repliée).
            if (!piece.empty() && (noteHeight_ >= kRangNomme || folded()))   // D109 : dans la langue de l'interface
                etiquette = vsm::app::ui::tr(juce::String::fromUTF8(piece.c_str()));
        }
        // Repliée, chaque rangée est nommée : elles ne se suivent pas.
        // D338 : LA POLICE NE DESCEND PLUS AVEC LE RANG. Un nom de touche est à
        // 12 pt (D323) quel que soit le zoom ; sous `kRangNomme`, seuls les do
        // sont écrits, et leur étiquette déborde sur la touche noire voisine
        // plutôt que de rétrécir jusqu'à l'illisible (« Zoom : tout voir » sur
        // une piste de six octaves donnait des rangs de 9 px et des noms de 6 pt).
        if (etiquette.isNotEmpty() || note % 12 == 0 || noteHeight_ >= kRangNomme || folded()) {
            g.setColour(pressed ? juce::Colours::black
                                 : (etiquette.isNotEmpty() || note % 12 == 0 ? Palette::textPrimary
                                                                             : Palette::textSecondary));
            g.setFont(12.0f);   // D323 : plancher 12 pt ; D338 : jamais moins, à tout rang
            const int hauteurTexte = std::max(noteHeight_, kRangNomme - 1);
            g.drawText(etiquette.isNotEmpty() ? etiquette : noteName(static_cast<uint8_t>(note)),
                       4, y + (noteHeight_ - hauteurTexte) / 2, keyboardWidth() - 8, hauteurTexte,
                       juce::Justification::centredLeft);
        }
    }
    g.setColour(Palette::border);
    g.drawLine(static_cast<float>(keyboardWidth()), 0.0f, static_cast<float>(keyboardWidth()),
                static_cast<float>(bounds.getHeight()), 1.0f);
}

void PianoRollComponent::drawGrid(juce::Graphics& g) const {
    const auto bounds = getLocalBounds();

    // Rangée par rangée, de l'aiguë à la grave : repliée (D20.2), une rangée
    // est une hauteur JOUÉE, et il n'y en a que quelques-unes.
    const int premiereRangee = rowOfNote(topNote_);
    const int derniereRangee = std::min(rowCount() - 1,
                                        premiereRangee + bounds.getHeight() / noteHeight_ + 1);
    for (int rangee = premiereRangee; rangee <= derniereRangee; ++rangee) {
        const int note = noteOfRow(rangee);
        const int y = noteToY(static_cast<uint8_t>(note));
        const bool isC = (note % 12 == 0);

        juce::Colour rowColour = isBlackKey(note) ? Palette::panel.withAlpha(0.4f) : juce::Colours::transparentBlack;
        if (scaleHighlight_ && scale_.type != ScaleType::Chromatic) {
            // Les degrés HORS gamme sont assombris plutôt que les degrés dans
            // la gamme éclaircis : on garde ainsi le contraste des notes.
            if (!isNoteInScale(static_cast<uint8_t>(note), scale_))
                rowColour = juce::Colours::black.withAlpha(0.28f);
            else if (((note - scale_.root) % 12 + 12) % 12 == 0)
                rowColour = Palette::accentTeal.withAlpha(0.10f); // la fondamentale
        }
        g.setColour(rowColour);
        g.fillRect(keyboardWidth(), y, bounds.getWidth() - keyboardWidth(), noteHeight_);

        g.setColour(isC ? Palette::gridLineStrong : Palette::gridLine);
        g.drawLine(static_cast<float>(keyboardWidth()), static_cast<float>(y),
                    static_cast<float>(bounds.getWidth()), static_cast<float>(y), isC ? 1.2f : 0.6f);
    }

    const Tick grid = gridTicks();
    const Tick startTick = std::max<Tick>(0, xToTick(static_cast<float>(keyboardWidth())));
    const Tick endTick = xToTick(static_cast<float>(bounds.getWidth()));
    Tick barTicks = project_->timeSignatureMap.ticksPerBar(startTick, project_->ticksPerQuarterNote);
    if (barTicks <= 0) barTicks = project_->ticksPerQuarterNote * 4;
    const Tick beatTicks = project_->ticksPerQuarterNote;

    // Trois niveaux de lignes, chacun n'apparaissant que s'il reste lisible :
    // sous ~3 px d'écart, une grille devient une bouillie grise.
    const bool subGridVisible = (static_cast<double>(grid) * pixelsPerTick_) > 3.0;
    const bool beatVisible = (static_cast<double>(beatTicks) * pixelsPerTick_) > 6.0;

    if (subGridVisible) {
        for (Tick t = (startTick / grid) * grid; t <= endTick; t += grid) {
            if (t % beatTicks == 0) continue; // dessinée plus bas, plus marquée
            g.setColour(Palette::gridLine);
            const float x = tickToX(t);
            g.drawLine(x, 0.0f, x, static_cast<float>(bounds.getHeight()), 0.5f);
        }
    }
    if (beatVisible) {
        for (Tick t = (startTick / beatTicks) * beatTicks; t <= endTick; t += beatTicks) {
            if (t % barTicks == 0) continue;
            g.setColour(Palette::gridLine.brighter(0.25f));
            const float x = tickToX(t);
            g.drawLine(x, 0.0f, x, static_cast<float>(bounds.getHeight()), 0.8f);
        }
    }
    for (Tick t = (startTick / barTicks) * barTicks; t <= endTick; t += barTicks) {
        g.setColour(Palette::gridLineStrong);
        const float x = tickToX(t);
        g.drawLine(x, 0.0f, x, static_cast<float>(bounds.getHeight()), 1.4f);
    }
}

void PianoRollComponent::drawGhostNotes(juce::Graphics& g) const {
    if (!project_) return;
    for (size_t i = 0; i < project_->tracks.size(); ++i) {
        if (i == activeTrackIndex_) continue;
        const Track& track = project_->tracks[i];
        const juce::Colour c = juce::Colour(track.colorRgba).withAlpha(0.16f);
        for (const auto& note : track.notes) {
            const float x1 = tickToX(note.startTick), x2 = tickToX(note.endTick);
            if (x2 < static_cast<float>(keyboardWidth()) || x1 > static_cast<float>(getWidth())) continue;
            const float y = static_cast<float>(noteToY(note.number));
            if (y + static_cast<float>(noteHeight_) < 0.0f || y > static_cast<float>(getHeight())) continue;
            g.setColour(c);
            g.fillRoundedRectangle(x1, y, std::max(2.0f, x2 - x1), static_cast<float>(noteHeight_ - 1), 2.0f);
        }
    }
}

void PianoRollComponent::drawNoteRectangle(juce::Graphics& g, const Note& note, bool selected) const {
    const float x1 = tickToX(note.startTick), x2 = tickToX(note.endTick);
    const float y = static_cast<float>(noteToY(note.number));
    const auto rect = juce::Rectangle<float>(x1, y, std::max(3.0f, x2 - x1), static_cast<float>(noteHeight_ - 1));

    const Track* track = activeTrack();
    const juce::Colour base = track ? juce::Colour(track->colorRgba) : Palette::accentTeal;
    // La vélocité se lit à la LUMINOSITÉ : une nuance se repère d'un coup
    // d'œil sur tout un passage, sans ouvrir la lane de vélocité.
    const float brightness = 0.45f + 0.55f * (static_cast<float>(note.velocity) / 127.0f);
    juce::Colour fill = base.withMultipliedBrightness(brightness);
    if (note.muted) fill = fill.withSaturation(0.05f).withAlpha(0.35f);

    // D164 (A27) : SOUS QUATRE PIXELS DE LARGE, UN RECTANGLE PLEIN.
    //
    // POURQUOI. Ajusté à la fenêtre, un morceau de 452 s tient dans 560 px : une
    // note mesure moins d'un pixel de large et se dessine à la largeur plancher
    // de 2 px. Un rectangle de 2 px avec des coins de 2,5 px de rayon est une
    // figure DÉGÉNÉRÉE -- les coins consomment toute la forme, le rasteriseur
    // paye un chemin courbe complet, et l'œil reçoit un pâté de deux pixels.
    // D163 l'a chiffré : 30,98 ms pour le seul piano roll, contre 2,56 ms au zoom
    // d'ouverture, les quatre autres panneaux inchangés.
    //
    // LE SEUIL EST LE RAYON, PAS UN GOÛT : à 5 px de large la figure est déjà
    // entièrement faite de coins. Au-delà du seuil, le chemin ordinaire reprend
    // à l'identique -- c'est le témoin du remède, et l'image au zoom d'ouverture
    // doit rester la même au pixel.
    //
    // CE QUI RESTE ICI, parce que c'est ce qui se voit sur deux pixels : la barre
    // ambre d'une note douteuse (le commentaire du chemin ordinaire le dit
    // lui-même) et les hachures d'une note muette. Ce qui tombe est le liseré
    // arrondi réduit d'un pixel, dont la largeur est NULLE à cette échelle : il
    // ne dessinait rien. Une note sélectionnée ou survolée garde le chemin
    // ordinaire quelle que soit sa largeur : il y en a quelques-unes, jamais
    // trois mille.
    if (rect.getWidth() < kLargeurMinimaleArrondie && !selected && note.id != hoveredNoteId_) {
        g.setColour(fill);
        g.fillRect(rect);
        if (note.muted) {
            g.setColour(Palette::background.withAlpha(0.5f));
            for (float x = rect.getX(); x < rect.getRight(); x += 5.0f)
                g.drawLine(x, rect.getY(), x + rect.getHeight(), rect.getBottom(), 1.0f);
        }
        if (note.confidence < kDoubtfulNoteThreshold) {
            const float force = juce::jlimit(
                0.7f, 1.0f, (kDoubtfulNoteThreshold - note.confidence) / kDoubtfulNoteThreshold + 0.4f);
            g.setColour(Palette::accentAmber.withAlpha(force));
            const float largeur = std::min(3.0f, rect.getWidth() * 0.5f);
            g.fillRect(juce::Rectangle<float>(rect.getX(), rect.getY() + 1.0f,
                                              largeur, rect.getHeight() - 2.0f));
        }
        return;
    }

    g.setColour(fill);
    g.fillRoundedRectangle(rect, 2.5f);

    if (note.muted) {
        // Hachures : une note muette doit se distinguer même en noir et blanc,
        // et même quand la couleur de piste est déjà pâle.
        g.setColour(Palette::background.withAlpha(0.5f));
        for (float x = rect.getX(); x < rect.getRight(); x += 5.0f)
            g.drawLine(x, rect.getY(), x + rect.getHeight(), rect.getBottom(), 1.0f);
    }

    if (note.id == hoveredNoteId_ && !selected) {
        g.setColour(Palette::textPrimary.withAlpha(0.35f));
        g.drawRoundedRectangle(rect, 2.5f, 1.0f);
    }
    g.setColour(selected ? Palette::accentAmber : base.darker(0.5f));
    g.drawRoundedRectangle(rect, 2.5f, selected ? 2.0f : 1.0f);

    // NOTE DOUTEUSE : la transcription a hésité sur celle-ci (étape 11.3).
    //
    // Dessinée APRÈS le contour ordinaire, et c'est nécessaire : placée avant,
    // elle était intégralement recouverte par lui. Le rendu hors écran l'a
    // montré du premier coup, là où le code compilait et paraissait juste.
    //
    // Marquée par un liseré et un coin replié, PAS par une couleur de
    // remplissage : le remplissage porte déjà la vélocité, et la couleur de
    // piste distingue les pistes entre elles. Empiler un troisième sens sur la
    // même teinte les rendrait tous les trois illisibles.
    //
    // Elle reste une note ORDINAIRE par ailleurs : elle se joue, s'exporte et
    // s'édite comme les autres. On signale un doute, on ne décide pas à la
    // place de l'utilisateur.
    if (note.confidence < kDoubtfulNoteThreshold) {
        // Plus la confiance est basse, plus le marqueur est franc -- mais
        // jamais transparent au point de disparaître. Une première version
        // descendait à 0,3 d'opacité sur un liseré d'un pixel et demi : le
        // rendu hors écran a montré qu'on ne voyait RIEN, alors que le code
        // s'exécutait bel et bien.
        const float force = juce::jlimit(
            0.7f, 1.0f, (kDoubtfulNoteThreshold - note.confidence) / kDoubtfulNoteThreshold + 0.4f);
        g.setColour(Palette::accentAmber.withAlpha(force));
        g.drawRoundedRectangle(rect.reduced(1.0f), 2.0f, 2.0f);

        // Barre verticale sur le bord gauche : c'est elle qui rend le
        // marquage visible sur une note très courte, où un liseré seul se
        // réduit à un trait et se confond avec la sélection.
        const float largeur = std::min(3.0f, rect.getWidth() * 0.5f);
        g.fillRect(juce::Rectangle<float>(rect.getX(), rect.getY() + 1.0f,
                                           largeur, rect.getHeight() - 2.0f));
    }

    // SÉLECTION : un halo clair AUTOUR du contour ambre. Le rendu hors écran
    // l'a exigé : le contour de sélection et le marqueur de doute étaient tous
    // deux ambre, et une note douteuse sélectionnée -- exactement ce que « D »
    // produit -- ne se distinguait en rien de la même note non sélectionnée.
    // Le halo est d'une autre teinte, et à l'EXTÉRIEUR : il ne recouvre ni le
    // liseré ni la barre de doute, et il se voit sur une note de trois pixels.
    if (selected) {
        g.setColour(Palette::textPrimary.withAlpha(0.9f));
        g.drawRoundedRectangle(rect.expanded(2.0f), 3.5f, 1.5f);
    }


    // Nom de la note dans le rectangle, dès qu'il y a la place.
    if (noteHeight_ >= kRangNomme + 1 && rect.getWidth() > 34.0f) {   // D338 : 12 pt ou rien
        g.setColour(juce::Colours::black.withAlpha(0.75f));
        g.setFont(12.0f);   // D323
        g.drawText(noteName(note.number), rect.reduced(4.0f, 0.0f), juce::Justification::centredLeft, false);
    }
}

void PianoRollComponent::drawNotes(juce::Graphics& g) const {
    const Track* track = activeTrack();
    if (!track) return;
    // Les notes sélectionnées sont dessinées EN DERNIER : pendant un
    // déplacement, elles passent au-dessus de celles qu'elles survolent.
    for (const auto& note : track->notes) {
        const float x1 = tickToX(note.startTick), x2 = tickToX(note.endTick);
        if (x2 < static_cast<float>(keyboardWidth()) || x1 > static_cast<float>(getWidth())) continue;
        const float y = static_cast<float>(noteToY(note.number));
        if (y + static_cast<float>(noteHeight_) < 0.0f || y > static_cast<float>(getHeight())) continue;
        if (selectedNoteIds_.count(note.id) == 0) drawNoteRectangle(g, note, false);
    }
    for (const auto& note : track->notes) {
        if (selectedNoteIds_.count(note.id) == 0) continue;
        const float x1 = tickToX(note.startTick), x2 = tickToX(note.endTick);
        if (x2 < static_cast<float>(keyboardWidth()) || x1 > static_cast<float>(getWidth())) continue;
        drawNoteRectangle(g, note, true);
    }
}

void PianoRollComponent::drawLoopRegion(juce::Graphics& g) const {
    if (!loopActive_) return;
    const float x1 = tickToX(loopStartTick_);
    const float x2 = tickToX(loopEndTick_);
    g.setColour(Palette::accentTeal.withAlpha(0.10f));
    g.fillRect(juce::Rectangle<float>(x1, 0.0f, x2 - x1, static_cast<float>(getHeight())));
    g.setColour(Palette::accentTeal.withAlpha(0.6f));
    g.drawLine(x1, 0.0f, x1, static_cast<float>(getHeight()), 1.5f);
    g.drawLine(x2, 0.0f, x2, static_cast<float>(getHeight()), 1.5f);
}

void PianoRollComponent::drawPlayhead(juce::Graphics& g) const {
    const float x = tickToX(playheadTick_);
    if (x < static_cast<float>(keyboardWidth()) || x > static_cast<float>(getWidth())) return;
    g.setColour(Palette::accentAmber);
    g.drawLine(x, 0.0f, x, static_cast<float>(getHeight()), 1.5f);
}

// ---------------------------------------------------------------------------
// Barres de défilement
// ---------------------------------------------------------------------------

void PianoRollComponent::updateScrollBars() {
    if (updatingScrollBars_) return;
    updatingScrollBars_ = true; // les setRangeLimits/setCurrentRange rappellent scrollBarMoved

    const auto area = contentArea();
    const double visibleTicks = pixelsPerTick_ > 0.0 ? static_cast<double>(area.getWidth()) / pixelsPerTick_ : 1.0;
    Tick contentEnd = project_ ? project_->lastUsedTick() : 0;
    contentEnd = std::max<Tick>(contentEnd, scrollTick_ + static_cast<Tick>(visibleTicks));
    contentEnd += static_cast<Tick>(visibleTicks * 0.5); // marge pour composer au-delà de la fin

    horizontalScrollBar_.setRangeLimits(0.0, static_cast<double>(contentEnd), juce::dontSendNotification);
    horizontalScrollBar_.setCurrentRange(static_cast<double>(scrollTick_), visibleTicks, juce::dontSendNotification);

    const double visibleNotes = static_cast<double>(area.getHeight()) / std::max(1, noteHeight_);
    // L'axe vertical est inversé (les aigus en haut) : la barre travaille sur
    // "note la plus grave visible", d'où la conversion.
    // Repliée (D20.2), la barre travaille en RANGÉES depuis le bas : « la
    // note la plus grave visible » n'a pas de sens sur des hauteurs qui ne se
    // suivent pas.
    const double total = static_cast<double>(rowCount());
    const double lowestVisible =
        folded() ? total - (static_cast<double>(rowOfNote(topNote_)) + visibleNotes)
                 : static_cast<double>(topNote_) - visibleNotes;
    verticalScrollBar_.setRangeLimits(0.0, total, juce::dontSendNotification);
    verticalScrollBar_.setCurrentRange(juce::jlimit(0.0, std::max(0.0, total - visibleNotes), lowestVisible),
                                        std::min(visibleNotes, total), juce::dontSendNotification);
    updatingScrollBars_ = false;
}

void PianoRollComponent::scrollBarMoved(juce::ScrollBar* bar, double newRangeStart) {
    if (updatingScrollBars_) return;
    if (bar == &horizontalScrollBar_) {
        scrollTick_ = std::max<Tick>(0, static_cast<Tick>(newRangeStart));
    } else if (bar == &verticalScrollBar_) {
        const double visibleNotes = static_cast<double>(contentArea().getHeight()) / std::max(1, noteHeight_);
        if (folded())
            topNote_ = noteOfRow(static_cast<int>(std::lround(static_cast<double>(rowCount()) - newRangeStart - visibleNotes)));
        else
            topNote_ = juce::jlimit(12, 127, static_cast<int>(std::lround(newRangeStart + visibleNotes)));
    }
    repaint();
}
