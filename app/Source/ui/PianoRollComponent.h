#pragma once
#include <JuceHeader.h>
#include "vsm/sequencer/EditHistory.h"
#include "vsm/sequencer/NoteEdit.h"
#include "vsm/sequencer/Project.h"
#include "vsm/sequencer/Quantizer.h"
#include <functional>

// Piano roll complet (section 3 du cahier des charges).
//
// RÉPARTITION DES RESPONSABILITÉS, et c'est la clé de ce fichier : ce
// composant ne contient AUCUNE logique musicale. Transposer, quantifier,
// arpéger, contraindre à une gamme, couper, fusionner... tout cela vit dans
// `vsm::sequencer` (NoteEdit.h), testé sans JUCE ni serveur graphique. Ici on
// ne trouve que ce qui est vraiment de l'interface : convertir des pixels en
// ticks, décider ce qu'un clic veut dire, dessiner, et appeler l'opération
// correspondante. Une régression sur « le legato déborde d'une note » se
// diagnostique donc dans un test de 10 lignes, pas en cliquant dans l'app.
//
// PERFORMANCE : le rendu ne parcourt que la plage de ticks visible (culling),
// et aucune allocation n'a lieu dans paint()/mouseDrag() -- les seules
// allocations suivent une action explicite de l'utilisateur (créer, coller,
// annuler). Un projet de plusieurs dizaines de milliers de notes reste fluide.
/// En dessous de cette confiance, une note est signalée comme douteuse dans le
/// piano roll. 0,55 : au-dessus, la transcription est franche dans les cas
/// mesurés ; en dessous, elle a hésité. Le seuil est ici, en un seul endroit,
/// pour qu'il se règle sans chercher.
inline constexpr float kDoubtfulNoteThreshold = 0.55f;

class PianoRollComponent : public juce::Component,
                            private juce::ScrollBar::Listener {
public:
    /// Outils, dans l'esprit des séquenceurs classiques. L'outil Sélection
    /// sait tout faire (dessiner dans le vide, déplacer, redimensionner) :
    /// les autres existent pour rendre une intention EXPLICITE quand le
    /// contexte est ambigu -- effacer en balayant, couper au clic, etc.
    enum class Tool { Select, Draw, Erase, Split, Glue, Mute };

    PianoRollComponent();

    void setProject(vsm::sequencer::Project* project);
    void setActiveTrackIndex(size_t trackIndex);
    vsm::sequencer::Track* activeTrack() const;

    // --- Callbacks vers l'application ------------------------------------
    /// Les notes ont changé : reconstruire le planning de lecture.
    std::function<void()> onNotesEdited;
    /// Écoute d'une note (clic sur le clavier, dessin d'une note) : l'app la
    /// route vers l'instrument de la piste active.
    std::function<void(uint8_t note, uint8_t velocity, bool noteOn)> onAudition;
    /// Texte d'état (position du curseur, sélection) pour la barre d'info.
    std::function<void(const juce::String&)> onStatusChanged;
    /// L'utilisateur demande à déplacer la tête de lecture (clic sur la règle).
    std::function<void(vsm::midi::Tick)> onPlayheadRequested;
    /// La boucle a été redéfinie à la souris sur la règle.
    std::function<void(vsm::midi::Tick start, vsm::midi::Tick end, bool active)> onLoopRegionChanged;
    /// Quelque chose a changé qui affecte l'état des boutons (annuler/rétablir,
    /// outil courant, sélection vide ou non).
    std::function<void()> onEditStateChanged;

    // --- Conversions partagées (règle, lane de vélocité) ------------------
    float tickToX(vsm::midi::Tick tick) const;
    vsm::midi::Tick xToTick(float x) const;
    int keyboardWidth() const { return kKeyboardWidth; }
    double pixelsPerTick() const { return pixelsPerTick_; }
    vsm::midi::Tick visibleStartTick() const { return scrollTick_; }
    int noteHeight() const { return noteHeight_; }
    const vsm::sequencer::NoteSelection& selectedNoteIds() const { return selectedNoteIds_; }
    vsm::midi::Tick playheadTick() const { return playheadTick_; }
    vsm::midi::Tick gridTicks() const;
    /// Repères musicaux du projet, exposés pour que la règle et la lane de
    /// vélocité affichent EXACTEMENT la même grille que le piano roll.
    vsm::midi::Tick ticksPerBeat() const;
    vsm::midi::Tick ticksPerBarAt(vsm::midi::Tick tick) const;

    // --- Réglages (pilotés par la barre d'outils) -------------------------
    void setTool(Tool tool);
    Tool tool() const { return tool_; }
    void setGridResolution(vsm::sequencer::GridResolution grid);
    vsm::sequencer::GridResolution gridResolution() const { return gridResolution_; }
    void setSnapEnabled(bool enabled);
    bool snapEnabled() const { return snapEnabled_; }
    void setSwing(float swing);
    float swing() const { return swing_; }
    void setDefaultVelocity(uint8_t velocity) { defaultVelocity_ = velocity; }
    uint8_t defaultVelocity() const { return defaultVelocity_; }
    void setScale(vsm::sequencer::Scale scale);
    vsm::sequencer::Scale scale() const { return scale_; }
    void setScaleHighlightEnabled(bool enabled);
    bool scaleHighlightEnabled() const { return scaleHighlight_; }
    void setGhostNotesVisible(bool visible);
    bool ghostNotesVisible() const { return ghostNotes_; }
    void setFollowPlayhead(bool follow);
    bool followPlayhead() const { return followPlayhead_; }

    void setLoopRegion(vsm::midi::Tick start, vsm::midi::Tick end, bool active);
    void setPlayheadTick(vsm::midi::Tick tick);

    // --- Navigation --------------------------------------------------------
    void zoomHorizontally(float factor);
    void zoomVertically(float factor);
    void zoomToFit();       ///< tout le contenu de la piste dans la fenêtre
    void zoomToSelection(); ///< la sélection remplit la fenêtre
    void scrollToPlayhead();

    // --- Historique --------------------------------------------------------
    void undo();
    void redo();
    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }
    juce::String undoLabel() const { return juce::String(history_.undoLabel()); }
    juce::String redoLabel() const { return juce::String(history_.redoLabel()); }

    // --- Sélection ---------------------------------------------------------
    void selectAll();
    void selectNone();
    void invertSelection();
    void selectSamePitch();
    bool hasSelection() const { return !selectedNoteIds_.empty(); }

    // --- Édition (barre d'outils, menu contextuel, raccourcis) ------------
    void deleteSelection();
    void duplicateSelection();
    void copySelection();
    void cutSelection();
    void paste();
    void transposeSelection(int semitones);
    void nudgeSelection(int64_t deltaTicks);
    void setSelectionLengthToGrid();
    void scaleSelectionLength(float factor);
    void quantizeSelection(float strength, bool alsoQuantizeEnds);
    void humanizeSelection(float timingTicks, float velocityAmount);
    void applyLegatoToSelection();
    void removeOverlapsInSelection();
    void splitSelectionAtPlayhead();
    void joinSelection();
    void reverseSelection();
    void mirrorSelectionPitch();
    void setSelectionVelocity(uint8_t velocity);
    void scaleSelectionVelocity(float factor);
    void rampSelectionVelocity(uint8_t from, uint8_t to);
    void randomizeSelectionVelocity(int amount);
    void constrainSelectionToScale();
    void toggleSelectionMuted();
    void arpeggiateSelection(vsm::sequencer::ArpeggioMode mode);
    void insertChordAtPlayhead(vsm::sequencer::ChordType type, uint8_t rootNote);

    /// Menu contextuel complet (aussi accessible depuis le menu Édition).
    juce::PopupMenu buildContextMenu() const;
    void performContextMenuAction(int menuItemId);

    // --- juce::Component ---------------------------------------------------
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    enum class DragMode { None, Move, ResizeLeft, ResizeRight, RubberBandSelect, Pan, Erase, Audition };

    // --- Conversions internes ---------------------------------------------
    int noteToY(uint8_t note) const;
    uint8_t yToNote(float y) const;
    vsm::midi::Tick snapTick(vsm::midi::Tick tick) const;
    vsm::sequencer::Note* findNoteAt(juce::Point<float> pos, bool* nearRightEdge = nullptr,
                                      bool* nearLeftEdge = nullptr);

    // --- Historique / notification ----------------------------------------
    /// À appeler AVANT toute modification des notes : mémorise l'état pour
    /// l'annulation et donne son nom à l'action dans le menu Édition.
    void beginEdit(const juce::String& label);
    void notifyEdited();
    void notifyEditState();
    void updateStatusText(juce::Point<float> mousePos, bool mouseInside);

    // --- Écoute ------------------------------------------------------------
    void startAudition(uint8_t note, uint8_t velocity);
    void stopAudition();

    // --- Rendu -------------------------------------------------------------
    void drawKeyboard(juce::Graphics&) const;
    void drawGrid(juce::Graphics&) const;
    void drawGhostNotes(juce::Graphics&) const;
    void drawNotes(juce::Graphics&) const;
    void drawLoopRegion(juce::Graphics&) const;
    void drawPlayhead(juce::Graphics&) const;
    void drawNoteRectangle(juce::Graphics&, const vsm::sequencer::Note&, bool selected) const;

    // --- Scrollbars --------------------------------------------------------
    void scrollBarMoved(juce::ScrollBar* bar, double newRangeStart) override;
    void updateScrollBars();
    juce::Rectangle<int> contentArea() const;

    vsm::sequencer::Project* project_ = nullptr;
    size_t activeTrackIndex_ = 0;

    // Vue, exprimée en unités musicales (ticks/notes) plutôt qu'en pixels :
    // un changement de zoom ne déplace donc jamais le contenu sous le curseur.
    double pixelsPerTick_ = 0.08;
    vsm::midi::Tick scrollTick_ = 0;
    int noteHeight_ = 16;
    int topNote_ = 84;

    static constexpr int kKeyboardWidth = 62;
    static constexpr int kScrollBarThickness = 12;

    Tool tool_ = Tool::Select;
    vsm::sequencer::GridResolution gridResolution_ { vsm::sequencer::NoteValue::Sixteenth, false, false };
    bool snapEnabled_ = true;
    float swing_ = 0.0f;
    uint8_t defaultVelocity_ = 100;
    vsm::sequencer::Scale scale_ { 0, vsm::sequencer::ScaleType::Chromatic };
    bool scaleHighlight_ = false;
    bool ghostNotes_ = true;
    bool followPlayhead_ = true;

    vsm::sequencer::NoteSelection selectedNoteIds_;
    std::vector<vsm::sequencer::Note> clipboard_;
    vsm::sequencer::EditHistory history_;

    DragMode dragMode_ = DragMode::None;
    uint64_t draggedNoteId_ = 0;
    vsm::midi::Tick dragStartTick_ = 0;
    uint8_t dragStartNoteNumber_ = 60;
    std::vector<vsm::sequencer::Note> dragSnapshot_;
    juce::Point<float> dragStartMousePos_;
    juce::Rectangle<float> rubberBandRect_;
    vsm::midi::Tick panStartTick_ = 0;
    int panStartTopNote_ = 84;
    bool dragDidCopy_ = false;   ///< Alt+glisser : la copie n'est faite qu'une fois
    int auditionNote_ = -1;      ///< note en cours d'écoute (-1 = aucune)
    uint64_t hoveredNoteId_ = 0;

    vsm::midi::Tick loopStartTick_ = 0;
    vsm::midi::Tick loopEndTick_ = 0;
    bool loopActive_ = false;
    vsm::midi::Tick playheadTick_ = 0;

    juce::ScrollBar horizontalScrollBar_ { false };
    juce::ScrollBar verticalScrollBar_ { true };
    bool updatingScrollBars_ = false;
};
