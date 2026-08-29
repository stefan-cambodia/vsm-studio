#pragma once
#include <JuceHeader.h>
#include "vsm/sequencer/ProjectHistory.h"
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
/// Le seuil de doute vit dans le cœur (`vsm/sequencer/NoteEdit.h`), avec la
/// règle de parcours « note douteuse suivante » : le composant ne fait que le
/// dessiner. Alias pour le code de rendu, qui n'a pas à connaître l'espace de
/// noms du séquenceur.
using vsm::sequencer::kDoubtfulNoteThreshold;

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
    size_t activeTrackIndex() const { return activeTrackIndex_; }
    /// Le projet édité, pour ce qui n'appartient à aucune piste : les repères
    /// de la ligne de temps, que la règle dessine.
    vsm::sequencer::Project* project() const { return project_; }

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
    /// Ouvre une action annulable pour un éditeur VOISIN qui modifie les mêmes
    /// notes -- aujourd'hui la lane de vélocité.
    ///
    /// POURQUOI C'EST PUBLIC. La lane peignait les nuances directement dans
    /// `note.velocity` sans jamais passer par l'historique : ces éditions
    /// n'étaient pas annulables, et -- plus grave -- elles rendaient la pile
    /// INCOHÉRENTE. L'annulation travaille par instantanés : annuler le geste
    /// suivant restaurait un vecteur de notes capturé AVANT les nuances
    /// peintes, qui disparaissaient donc en même temps qu'une action qui
    /// n'avait rien à voir avec elles. Une pile d'annulation dans laquelle on
    /// ne peut pas avoir confiance est pire qu'une absence d'annulation.
    ///
    /// À appeler une fois par GESTE, au mouseDown : un glissement continu est
    /// une action, pas trois cents.
    void beginExternalEdit(const juce::String& label) { beginEdit(label); }

    /// L'historique du PROJET, détenu par l'application et partagé avec tout
    /// ce qui modifie le projet (mixeur, pistes, effets, repères). Le piano
    /// roll n'a plus le sien : une annulation qui ne couvrirait que la piste
    /// affichée devrait être vidée dès qu'on regarde ailleurs.
    void setHistory(vsm::sequencer::ProjectHistory* history) { history_ = history; }
    /// Appelé après un annuler/rétablir : le projet entier a pu changer
    /// (pistes ajoutées ou supprimées, mixage, effets), et l'application doit
    /// tout republier.
    std::function<void()> onProjectRestored;

    bool canUndo() const { return history_ && history_->canUndo(); }
    bool canRedo() const { return history_ && history_->canRedo(); }
    juce::String undoLabel() const { return history_ ? juce::String(history_->undoLabel()) : juce::String(); }
    juce::String redoLabel() const { return history_ ? juce::String(history_->redoLabel()) : juce::String(); }

    // --- Sélection ---------------------------------------------------------
    void selectAll();
    void selectNone();
    void invertSelection();
    void selectSamePitch();
    /// Notes douteuses (étape 11.3) : celles sur lesquelles la transcription a
    /// hésité. Y ALLER, une par une, dans l'ordre du morceau -- la vue défile
    /// jusqu'à la note sans changer le zoom -- ou les prendre toutes.
    void selectNextDoubtfulNote(bool forward);
    void selectDoubtfulNotes();
    size_t doubtfulNoteCount() const;
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
    vsm::sequencer::ProjectHistory* history_ = nullptr;

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
