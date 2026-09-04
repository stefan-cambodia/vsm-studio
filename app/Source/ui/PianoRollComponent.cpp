#include "PianoRollComponent.h"
#include "DrumVoiceNames.h"
#include "Shortcuts.h"
#include "LookAndFeel/VsmLookAndFeel.h"
#include <algorithm>
#include <array>
#include <cmath>

using namespace vsm::sequencer;
using namespace vsm::midi;
using namespace vsm::ui;

namespace {

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
    kCtxSelectNextDoubtful, kCtxSelectPrevDoubtful, kCtxSelectDoubtful,
    kCtxTransposeUp = 100030, kCtxTransposeDown, kCtxOctaveUp, kCtxOctaveDown,
    kCtxQuantizeFull = 100040, kCtxQuantizeHalf, kCtxQuantizeEnds, kCtxHumanize,
    kCtxLegato = 100050, kCtxRemoveOverlaps, kCtxLengthToGrid, kCtxLengthDouble, kCtxLengthHalve,
    kCtxSplit = 100060, kCtxJoin, kCtxReverse, kCtxMirror, kCtxMute,
    kCtxVelocityFull = 100070, kCtxVelocityHalf, kCtxVelocityUp, kCtxVelocityDown,
    kCtxVelocityRampUp, kCtxVelocityRampDown, kCtxVelocityRandom,
    kCtxScaleConstrain = 100080,
    kCtxArpUp = 100090, kCtxArpDown, kCtxArpUpDown, kCtxArpRandom,
    kCtxChordBase = 100100, // + index dans allChordTypes()
    kCtxZoomFit = 100300, kCtxZoomSelection,
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
    // CADRER LES NOTES DE LA PISTE QU'ON VIENT DE CHOISIR, verticalement
    // seulement. Le piano roll s'ouvrait toujours sur C6 en haut, quelle que
    // soit la piste : une basse reconstruite (octave 1) montrait une fenêtre
    // vide, ou ses seules notes fantômes de transcription dans l'aigu. La
    // hauteur visée est la MÉDIANE des hauteurs pondérée par la durée -- pas
    // le milieu de l'étendue, qu'une note fantôme deux octaves plus haut
    // déplacerait. Le zoom horizontal, lui, ne bouge pas : c'est le choix de
    // l'utilisateur, pas celui de la piste.
    if (autrePiste) cadrerSurLesNotes();
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

int PianoRollComponent::noteToY(uint8_t note) const {
    return (topNote_ - static_cast<int>(note)) * noteHeight_;
}

uint8_t PianoRollComponent::yToNote(float y) const {
    const int n = topNote_ - static_cast<int>(std::floor(y / static_cast<float>(noteHeight_)));
    return static_cast<uint8_t>(juce::jlimit(0, 127, n));
}

Tick PianoRollComponent::gridTicks() const {
    if (!project_) return 120;
    return gridResolutionToTicks(gridResolution_, project_->ticksPerQuarterNote);
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
    if (project_ != nullptr && history_ != nullptr)
        history_->beginEdit(*project_, label.toStdString());
    return true;
}

bool PianoRollComponent::activeTrackLocked() const {
    const Track* track = activeTrack();
    return track != nullptr && track->locked;
}

void PianoRollComponent::notifyEdited() {
    if (onNotesEdited) onNotesEdited();
    notifyEditState();
    updateScrollBars();
    repaint();
}

void PianoRollComponent::notifyEditState() {
    if (onEditStateChanged) onEditStateChanged();
}

void PianoRollComponent::undo() {
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
void PianoRollComponent::setSnapEnabled(bool enabled) { snapEnabled_ = enabled; }
void PianoRollComponent::setSwing(float swing) { swing_ = swing; repaint(); }
void PianoRollComponent::setScale(Scale scale) { scale_ = scale; repaint(); }
void PianoRollComponent::setScaleHighlightEnabled(bool enabled) { scaleHighlight_ = enabled; repaint(); }
void PianoRollComponent::setGhostNotesVisible(bool visible) { ghostNotes_ = visible; repaint(); }
void PianoRollComponent::setFollowPlayhead(bool follow) { followPlayhead_ = follow; }

void PianoRollComponent::setLoopRegion(Tick start, Tick end, bool active) {
    loopStartTick_ = start;
    loopEndTick_ = end;
    loopActive_ = active;
    repaint();
}

void PianoRollComponent::setPlayheadTick(Tick tick) {
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
    repaint();
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
    noteHeight_ = juce::jlimit(4, 48, area.getHeight() / noteSpan);
    topNote_ = juce::jlimit(12, 127, highest + 1);
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
    noteHeight_ = juce::jlimit(4, 48, area.getHeight() / noteSpan);
    topNote_ = juce::jlimit(12, 127, static_cast<int>(stats.highestNote) + 1);
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

void PianoRollComponent::deleteSelection() {
    Track* track = activeTrack();
    if (!track || selectedNoteIds_.empty()) return;
    if (!beginEdit("Supprimer")) return;
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
    deleteSelection();
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
    if (!beginEdit(semitones > 0 ? "Transposer +" : "Transposer -")) return;
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
    if (!beginEdit(juce::String(u8"Durée x") + juce::String(factor, 2))) return;
    scaleNoteLengths(track->notes, selectedNoteIds_, factor);
    notifyEdited();
}

void PianoRollComponent::quantizeSelection(float strength, bool alsoQuantizeEnds) {
    Track* track = activeTrack();
    if (!track || !project_ || selectedNoteIds_.empty()) return;

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
    if (selected.empty()) return;

    if (!beginEdit("Quantifier")) return;
    quantizeNotes(selected, settings, project_->ticksPerQuarterNote);
    for (auto& n : track->notes) {
        auto it = std::find_if(selected.begin(), selected.end(),
                                [&n](const Note& q) { return q.id == n.id; });
        if (it != selected.end()) n = *it;
    }
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
    if (!beginEdit("Couper")) return;
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
    if (!beginEdit(juce::String(u8"Vélocité x") + juce::String(factor, 2))) return;
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
    setPlayheadTick(debut + pas);
    notifyEdited();
}

void PianoRollComponent::stepInputRest() {
    if (!stepInput_) return;
    setPlayheadTick(snapTick(playheadTick_) + std::max<vsm::midi::Tick>(1, gridTicks()));
    repaint();
}

void PianoRollComponent::stepInputBack() {
    if (!stepInput_) return;
    setPlayheadTick(std::max<vsm::midi::Tick>(0, snapTick(playheadTick_) - std::max<vsm::midi::Tick>(1, gridTicks())));
    repaint();
}

// ---------------------------------------------------------------------------
// Menu contextuel
// ---------------------------------------------------------------------------

juce::PopupMenu PianoRollComponent::buildContextMenu() const {
    juce::PopupMenu menu;
    const bool sel = hasSelection();

    menu.addItem(kCtxUndo, "Annuler" + (canUndo() ? " : " + undoLabel() : juce::String()), canUndo());
    menu.addItem(kCtxRedo, juce::String(u8"Rétablir") + (canRedo() ? " : " + redoLabel() : juce::String()), canRedo());
    menu.addSeparator();
    menu.addItem(kCtxCut, "Couper", sel);
    menu.addItem(kCtxCopy, "Copier", sel);
    menu.addItem(kCtxPaste, u8"Coller à la tête de lecture", !clipboard_.empty());
    menu.addItem(kCtxDuplicate, "Dupliquer", sel);
    menu.addItem(kCtxDelete, "Supprimer", sel);
    menu.addSeparator();

    juce::PopupMenu selectMenu;
    selectMenu.addItem(kCtxSelectAll, u8"Tout sélectionner");
    selectMenu.addItem(kCtxSelectNone, u8"Tout désélectionner", sel);
    selectMenu.addItem(kCtxSelectInvert, u8"Inverser la sélection");
    selectMenu.addItem(kCtxSelectSamePitch, u8"Toutes les notes de même hauteur", sel);
    // Les notes douteuses de la transcription (étape 11.3) : on y VA, une par
    // une, au lieu de les chercher à l'œil sur un morceau entier.
    const size_t douteuses = doubtfulNoteCount();
    selectMenu.addSeparator();
    selectMenu.addItem(kCtxSelectNextDoubtful, "Note douteuse suivante (D)", douteuses > 0);
    selectMenu.addItem(kCtxSelectPrevDoubtful, u8"Note douteuse précédente (Maj+D)", douteuses > 0);
    selectMenu.addItem(kCtxSelectDoubtful,
                       douteuses > 0 ? "Toutes les notes douteuses (" + juce::String(static_cast<int>(douteuses)) + ")"
                                     : juce::String("Toutes les notes douteuses"),
                       douteuses > 0);
    menu.addSubMenu(u8"Sélection", selectMenu);

    juce::PopupMenu pitchMenu;
    pitchMenu.addItem(kCtxTransposeUp, "Transposer +1 demi-ton", sel);
    pitchMenu.addItem(kCtxTransposeDown, "Transposer -1 demi-ton", sel);
    pitchMenu.addItem(kCtxOctaveUp, "Octave +", sel);
    pitchMenu.addItem(kCtxOctaveDown, "Octave -", sel);
    pitchMenu.addItem(kCtxMirror, "Miroir des hauteurs", sel);
    pitchMenu.addItem(kCtxScaleConstrain, u8"Contraindre à la gamme", sel && scale_.type != ScaleType::Chromatic);
    menu.addSubMenu("Hauteur", pitchMenu);

    juce::PopupMenu timeMenu;
    timeMenu.addItem(kCtxQuantizeFull, "Quantifier (100 %)", sel);
    timeMenu.addItem(kCtxQuantizeHalf, "Quantifier (50 %)", sel);
    timeMenu.addItem(kCtxQuantizeEnds, u8"Quantifier début ET fin", sel);
    timeMenu.addItem(kCtxHumanize, "Humaniser", sel);
    timeMenu.addSeparator();
    timeMenu.addItem(kCtxLengthToGrid, u8"Durée = pas de grille", sel);
    timeMenu.addItem(kCtxLengthDouble, u8"Durée x2", sel);
    timeMenu.addItem(kCtxLengthHalve, u8"Durée /2", sel);
    timeMenu.addItem(kCtxLegato, "Legato", sel);
    timeMenu.addItem(kCtxRemoveOverlaps, "Retirer les chevauchements", sel);
    timeMenu.addSeparator();
    timeMenu.addItem(kCtxSplit, u8"Couper à la tête de lecture", sel);
    timeMenu.addItem(kCtxJoin, "Fusionner", selectedNoteIds_.size() >= 2);
    timeMenu.addItem(kCtxReverse, u8"Rétrograder", selectedNoteIds_.size() >= 2);
    menu.addSubMenu(u8"Temps et durée", timeMenu);

    juce::PopupMenu velocityMenu;
    velocityMenu.addItem(kCtxVelocityFull, u8"Vélocité 127", sel);
    velocityMenu.addItem(kCtxVelocityHalf, u8"Vélocité 64", sel);
    velocityMenu.addItem(kCtxVelocityUp, u8"Vélocité +10 %", sel);
    velocityMenu.addItem(kCtxVelocityDown, u8"Vélocité -10 %", sel);
    velocityMenu.addItem(kCtxVelocityRampUp, "Crescendo", sel);
    velocityMenu.addItem(kCtxVelocityRampDown, "Decrescendo", sel);
    velocityMenu.addItem(kCtxVelocityRandom, u8"Aléatoire (±20)", sel);
    menu.addSubMenu(u8"Vélocité", velocityMenu);

    juce::PopupMenu arpMenu;
    arpMenu.addItem(kCtxArpUp, u8"Arpéger : montant", sel);
    arpMenu.addItem(kCtxArpDown, u8"Arpéger : descendant", sel);
    arpMenu.addItem(kCtxArpUpDown, u8"Arpéger : aller-retour", sel);
    arpMenu.addItem(kCtxArpRandom, u8"Arpéger : aléatoire", sel);
    menu.addSubMenu(u8"Arpèges", arpMenu);

    juce::PopupMenu chordMenu;
    const auto chordTypes = allChordTypes();
    for (size_t i = 0; i < chordTypes.size(); ++i)
        chordMenu.addItem(kCtxChordBase + static_cast<int>(i),
                           juce::String(chordTypeName(chordTypes[i])) + " sur " +
                           juce::String(noteNumberToName(static_cast<uint8_t>(60 + scale_.root))));
    menu.addSubMenu(u8"Insérer un accord", chordMenu);

    menu.addSeparator();
    menu.addItem(kCtxMute, "Rendre muet / audible", sel);
    menu.addSeparator();
    menu.addItem(kCtxZoomFit, "Zoom : tout voir");
    menu.addItem(kCtxZoomSelection, u8"Zoom : sur la sélection", sel);
    return menu;
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
        case kCtxScaleConstrain:   constrainSelectionToScale(); break;
        case kCtxArpUp:            arpeggiateSelection(ArpeggioMode::Up); break;
        case kCtxArpDown:          arpeggiateSelection(ArpeggioMode::Down); break;
        case kCtxArpUpDown:        arpeggiateSelection(ArpeggioMode::UpDown); break;
        case kCtxArpRandom:        arpeggiateSelection(ArpeggioMode::Random); break;
        case kCtxZoomFit:          zoomToFit(); break;
        case kCtxZoomSelection:    zoomToSelection(); break;
        default: break;
    }
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
                if (!beginEdit("Couper")) return;
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
    dragMode_ = DragMode::None;
    rubberBandRect_ = {};
    dragDidCopy_ = false;
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
    const Tick step = mods.isShiftDown() ? gridTicks() * 4 : gridTicks();
    if (key == juce::KeyPress::leftKey) {
        if (hasSelection()) nudgeSelection(-static_cast<int64_t>(step));
        else { scrollTick_ = std::max<Tick>(0, scrollTick_ - step); updateScrollBars(); repaint(); }
        return true;
    }
    if (key == juce::KeyPress::rightKey) {
        if (hasSelection()) nudgeSelection(static_cast<int64_t>(step));
        else { scrollTick_ += step; updateScrollBars(); repaint(); }
        return true;
    }
    if (key == juce::KeyPress::upKey) {
        if (hasSelection()) transposeSelection(mods.isShiftDown() ? 12 : 1);
        else { topNote_ = juce::jlimit(12, 127, topNote_ + 1); updateScrollBars(); repaint(); }
        return true;
    }
    if (key == juce::KeyPress::downKey) {
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
        case Id::ViewZoomToFit:       zoomToFit(); return true;
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
        text << "Mes " << juce::String(static_cast<int>(bar)) << "." << juce::String(static_cast<int>(beat))
             << "." << juce::String(static_cast<int>(rest)) << "   " << noteName(note);
    }

    if (const Track* track = activeTrack()) {
        const SelectionStats stats = computeSelectionStats(track->notes, selectedNoteIds_);
        if (stats.count > 0) {
            if (text.isNotEmpty()) text << "   |   ";
            text << juce::String(static_cast<int>(stats.count)) << juce::String(u8" note(s) sélectionnée(s) : ")
                 << noteName(stats.lowestNote) << " - " << noteName(stats.highestNote)
                 << juce::String(u8", vélocité moyenne ") << juce::String(std::lround(stats.averageVelocity));
        } else {
            if (text.isNotEmpty()) text << "   |   ";
            text << juce::String(static_cast<int>(track->notes.size())) << " note(s) sur la piste";
        }
        // Le compte des notes douteuses reste affiché tant qu'il en reste :
        // c'est le travail de relecture qu'il reste à faire, et « D » y mène.
        if (const size_t douteuses = countDoubtfulNotes(track->notes); douteuses > 0)
            text << "   |   " << juce::String(static_cast<int>(douteuses))
                 << juce::String(u8" douteuse(s) — D : la suivante");
    }
    onStatusChanged(text);
}

// ---------------------------------------------------------------------------
// Rendu
// ---------------------------------------------------------------------------

void PianoRollComponent::paint(juce::Graphics& g) {
    g.fillAll(Palette::background);

    if (!project_ || !activeTrack()) {
        g.setColour(Palette::textSecondary);
        g.setFont(16.0f);
        g.drawText(u8"Sélectionnez une piste pour éditer ses notes",
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
        g.drawFittedText(u8"Bus de groupe : il additionne les pistes routées vers lui.\n"
                         u8"Il n'a pas de notes ; ses effets et son fader se règlent dans le mixeur.",
                         getLocalBounds().withTrimmedLeft(keyboardWidth()).reduced(24),
                         juce::Justification::centred, 3);
        return;
    }
    if (activeTrack()->kind == Track::Kind::Audio) {
        drawGrid(g);
        drawKeyboard(g);
        g.setColour(Palette::textSecondary);
        g.setFont(16.0f);
        g.drawFittedText(u8"Piste audio : son matériau se voit et se coupe dans l'arrangement.\n"
                         u8"Il n'y a pas de notes à éditer ici.",
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

    const int bottomNote = topNote_ - bounds.getHeight() / noteHeight_ - 1;
    for (int note = std::max(0, bottomNote); note <= std::min(127, topNote_); ++note) {
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
            if (!piece.empty() && noteHeight_ >= 9) etiquette = juce::String::fromUTF8(piece.c_str());
        }
        if (etiquette.isNotEmpty() || note % 12 == 0 || noteHeight_ >= 14) {
            g.setColour(pressed ? juce::Colours::black
                                 : (etiquette.isNotEmpty() || note % 12 == 0 ? Palette::textPrimary
                                                                             : Palette::textSecondary));
            g.setFont(std::min(11.0f, static_cast<float>(noteHeight_) - 3.0f));
            g.drawText(etiquette.isNotEmpty() ? etiquette : noteName(static_cast<uint8_t>(note)),
                       4, y, keyboardWidth() - 8, noteHeight_, juce::Justification::centredLeft);
        }
    }
    g.setColour(Palette::border);
    g.drawLine(static_cast<float>(keyboardWidth()), 0.0f, static_cast<float>(keyboardWidth()),
                static_cast<float>(bounds.getHeight()), 1.0f);
}

void PianoRollComponent::drawGrid(juce::Graphics& g) const {
    const auto bounds = getLocalBounds();

    const int bottomNote = topNote_ - bounds.getHeight() / noteHeight_ - 1;
    for (int note = std::max(0, bottomNote); note <= std::min(127, topNote_); ++note) {
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
    if (noteHeight_ >= 13 && rect.getWidth() > 34.0f) {
        g.setColour(juce::Colours::black.withAlpha(0.75f));
        g.setFont(std::min(11.0f, static_cast<float>(noteHeight_) - 4.0f));
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
    const double lowestVisible = static_cast<double>(topNote_) - visibleNotes;
    verticalScrollBar_.setRangeLimits(0.0, 128.0, juce::dontSendNotification);
    verticalScrollBar_.setCurrentRange(juce::jlimit(0.0, 128.0 - visibleNotes, lowestVisible),
                                        visibleNotes, juce::dontSendNotification);
    updatingScrollBars_ = false;
}

void PianoRollComponent::scrollBarMoved(juce::ScrollBar* bar, double newRangeStart) {
    if (updatingScrollBars_) return;
    if (bar == &horizontalScrollBar_) {
        scrollTick_ = std::max<Tick>(0, static_cast<Tick>(newRangeStart));
    } else if (bar == &verticalScrollBar_) {
        const double visibleNotes = static_cast<double>(contentArea().getHeight()) / std::max(1, noteHeight_);
        topNote_ = juce::jlimit(12, 127, static_cast<int>(std::lround(newRangeStart + visibleNotes)));
    }
    repaint();
}
