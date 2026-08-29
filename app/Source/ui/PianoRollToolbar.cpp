#include "PianoRollToolbar.h"
#include "LookAndFeel/VsmLookAndFeel.h"

using namespace vsm::sequencer;
using namespace vsm::ui;

namespace {
/// Les valeurs de note proposées dans le menu Grille, dans l'ordre.
const std::vector<std::pair<NoteValue, const char*>>& gridChoices() {
    static const std::vector<std::pair<NoteValue, const char*>> choices = {
        { NoteValue::Whole, "1/1" },      { NoteValue::Half, "1/2" },
        { NoteValue::Quarter, "1/4" },    { NoteValue::Eighth, "1/8" },
        { NoteValue::Sixteenth, "1/16" }, { NoteValue::ThirtySecond, "1/32" },
        { NoteValue::SixtyFourth, "1/64" }, { NoteValue::HundredTwentyEighth, "1/128" },
    };
    return choices;
}
const char8_t* kNoteNames[12] = { u8"Do", u8"Do#", u8"Ré", u8"Ré#", u8"Mi", u8"Fa",
                                  u8"Fa#", u8"Sol", u8"Sol#", u8"La", u8"La#", u8"Si" };
} // namespace

PianoRollToolbar::PianoRollToolbar(PianoRollComponent& pianoRoll) : pianoRoll_(pianoRoll) {
    auto tool = [this](juce::TextButton& button, PianoRollComponent::Tool t, const juce::String& tip) {
        configureButton(button, tip);
        button.onClick = [this, t] { pianoRoll_.setTool(t); refreshFromPianoRoll(); };
    };
    tool(selectTool_, PianoRollComponent::Tool::Select, u8"Sélection / déplacement (1)");
    tool(drawTool_,   PianoRollComponent::Tool::Draw,   "Dessiner des notes (2)");
    tool(eraseTool_,  PianoRollComponent::Tool::Erase,  "Effacer, y compris en balayant (3)");
    tool(splitTool_,  PianoRollComponent::Tool::Split,  "Couper une note au clic (4)");
    tool(glueTool_,   PianoRollComponent::Tool::Glue,   u8"Coller une note à la suivante (5)");
    tool(muteTool_,   PianoRollComponent::Tool::Mute,   "Rendre une note muette (6)");

    configureButton(undoButton_, "Annuler (Ctrl+Z)");
    undoButton_.onClick = [this] { pianoRoll_.undo(); refreshFromPianoRoll(); };
    configureButton(redoButton_, u8"Rétablir (Ctrl+Maj+Z)");
    redoButton_.onClick = [this] { pianoRoll_.redo(); refreshFromPianoRoll(); };

    configureButton(quantizeButton_, u8"Quantifier la sélection sur la grille (Ctrl+Q)");
    quantizeButton_.onClick = [this] { pianoRoll_.quantizeSelection(1.0f, false); refreshFromPianoRoll(); };
    configureButton(legatoButton_, u8"Étendre chaque note jusqu'à la suivante (Ctrl+L)");
    legatoButton_.onClick = [this] { pianoRoll_.applyLegatoToSelection(); refreshFromPianoRoll(); };
    configureButton(humanizeButton_, u8"Décaler légèrement timing et vélocité, de façon reproductible");
    humanizeButton_.onClick = [this] {
        pianoRoll_.humanizeSelection(static_cast<float>(pianoRoll_.gridTicks()) * 0.12f, 12.0f);
        refreshFromPianoRoll();
    };
    configureButton(chordButton_, u8"Insérer un accord à la tête de lecture");
    chordButton_.onClick = [this] {
        juce::PopupMenu menu;
        const auto types = allChordTypes();
        for (size_t i = 0; i < types.size(); ++i)
            menu.addItem(static_cast<int>(i) + 1, chordTypeName(types[i]));
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(chordButton_),
                            [this, types](int result) {
                                if (result <= 0) return;
                                const Scale scale = pianoRoll_.scale();
                                pianoRoll_.insertChordAtPlayhead(types[static_cast<size_t>(result - 1)],
                                                                  static_cast<uint8_t>(60 + scale.root));
                                refreshFromPianoRoll();
                            });
    };
    configureButton(moreButton_, u8"Toutes les opérations d'édition");
    moreButton_.onClick = [this] {
        pianoRoll_.buildContextMenu().showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(moreButton_),
            [this](int result) { if (result != 0) { pianoRoll_.performContextMenuAction(result); refreshFromPianoRoll(); } });
    };

    configureButton(zoomInButton_, "Zoom avant (+)");
    zoomInButton_.onClick = [this] { pianoRoll_.zoomHorizontally(1.25f); };
    configureButton(zoomOutButton_, u8"Zoom arrière (-)");
    zoomOutButton_.onClick = [this] { pianoRoll_.zoomHorizontally(0.8f); };
    configureButton(zoomFitButton_, "Afficher toute la piste (Ctrl+0)");
    zoomFitButton_.onClick = [this] { pianoRoll_.zoomToFit(); };

    for (auto* toggle : { &snapButton_, &ghostButton_, &followButton_, &scaleHighlightButton_ })
        addAndMakeVisible(*toggle);
    snapButton_.setToggleState(pianoRoll_.snapEnabled(), juce::dontSendNotification);
    snapButton_.onClick = [this] { pianoRoll_.setSnapEnabled(snapButton_.getToggleState()); };
    ghostButton_.setToggleState(pianoRoll_.ghostNotesVisible(), juce::dontSendNotification);
    ghostButton_.onClick = [this] { pianoRoll_.setGhostNotesVisible(ghostButton_.getToggleState()); };
    followButton_.setToggleState(pianoRoll_.followPlayhead(), juce::dontSendNotification);
    followButton_.onClick = [this] { pianoRoll_.setFollowPlayhead(followButton_.getToggleState()); };
    scaleHighlightButton_.setToggleState(false, juce::dontSendNotification);
    scaleHighlightButton_.onClick = [this] { pianoRoll_.setScaleHighlightEnabled(scaleHighlightButton_.getToggleState()); };

    addAndMakeVisible(gridCombo_);
    for (size_t i = 0; i < gridChoices().size(); ++i)
        gridCombo_.addItem(gridChoices()[i].second, static_cast<int>(i) + 1);
    gridCombo_.setSelectedId(5, juce::dontSendNotification); // 1/16
    gridCombo_.onChange = [this] { applyGridFromCombos(); };

    addAndMakeVisible(gridModifierCombo_);
    gridModifierCombo_.addItem("Droit", 1);
    gridModifierCombo_.addItem("Triolet", 2);
    gridModifierCombo_.addItem(u8"Pointé", 3);
    gridModifierCombo_.setSelectedId(1, juce::dontSendNotification);
    gridModifierCombo_.onChange = [this] { applyGridFromCombos(); };

    addAndMakeVisible(scaleRootCombo_);
    for (int i = 0; i < 12; ++i) scaleRootCombo_.addItem(kNoteNames[i], i + 1);
    scaleRootCombo_.setSelectedId(1, juce::dontSendNotification);
    scaleRootCombo_.onChange = [this] { applyScaleFromCombos(); };

    addAndMakeVisible(scaleTypeCombo_);
    const auto scales = allScaleTypes();
    for (size_t i = 0; i < scales.size(); ++i)
        scaleTypeCombo_.addItem(scaleTypeName(scales[i]), static_cast<int>(i) + 1);
    scaleTypeCombo_.setSelectedId(1, juce::dontSendNotification);
    scaleTypeCombo_.onChange = [this] { applyScaleFromCombos(); };

    addAndMakeVisible(swingSlider_);
    swingSlider_.setRange(0.0, 0.75, 0.01);
    swingSlider_.setValue(0.0, juce::dontSendNotification);
    swingSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
    swingSlider_.onValueChange = [this] { pianoRoll_.setSwing(static_cast<float>(swingSlider_.getValue())); };

    addAndMakeVisible(velocitySlider_);
    velocitySlider_.setRange(1.0, 127.0, 1.0);
    velocitySlider_.setValue(100.0, juce::dontSendNotification);
    velocitySlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
    velocitySlider_.onValueChange = [this] {
        pianoRoll_.setDefaultVelocity(static_cast<uint8_t>(velocitySlider_.getValue()));
    };

    auto label = [this](juce::Label& l, const juce::String& text) {
        l.setText(text, juce::dontSendNotification);
        l.setColour(juce::Label::textColourId, Palette::textSecondary);
        l.setFont(juce::Font(juce::FontOptions(11.0f)));
        addAndMakeVisible(l);
    };
    label(gridLabel_, "Grille");
    label(swingLabel_, "Swing");
    label(velocityLabel_, u8"Vél.");
    label(scaleLabel_, "Gamme");

    refreshFromPianoRoll();
}

void PianoRollToolbar::configureButton(juce::Button& button, const juce::String& tooltip) {
    button.setTooltip(tooltip);
    addAndMakeVisible(button);
}

void PianoRollToolbar::applyGridFromCombos() {
    GridResolution grid;
    const int index = juce::jlimit(1, static_cast<int>(gridChoices().size()), gridCombo_.getSelectedId());
    grid.value = gridChoices()[static_cast<size_t>(index - 1)].first;
    grid.triplet = gridModifierCombo_.getSelectedId() == 2;
    grid.dotted = gridModifierCombo_.getSelectedId() == 3;
    pianoRoll_.setGridResolution(grid);
}

void PianoRollToolbar::applyScaleFromCombos() {
    Scale scale;
    scale.root = static_cast<uint8_t>(juce::jlimit(1, 12, scaleRootCombo_.getSelectedId()) - 1);
    const auto scales = allScaleTypes();
    const int typeIndex = juce::jlimit(1, static_cast<int>(scales.size()), scaleTypeCombo_.getSelectedId());
    scale.type = scales[static_cast<size_t>(typeIndex - 1)];
    pianoRoll_.setScale(scale);
}

void PianoRollToolbar::refreshFromPianoRoll() {
    const auto currentTool = pianoRoll_.tool();
    auto mark = [](juce::TextButton& b, bool active) {
        b.setColour(juce::TextButton::buttonColourId, active ? Palette::accentTeal : Palette::panelRaised);
        b.setColour(juce::TextButton::textColourOffId, active ? juce::Colours::black : Palette::textPrimary);
    };
    mark(selectTool_, currentTool == PianoRollComponent::Tool::Select);
    mark(drawTool_,   currentTool == PianoRollComponent::Tool::Draw);
    mark(eraseTool_,  currentTool == PianoRollComponent::Tool::Erase);
    mark(splitTool_,  currentTool == PianoRollComponent::Tool::Split);
    mark(glueTool_,   currentTool == PianoRollComponent::Tool::Glue);
    mark(muteTool_,   currentTool == PianoRollComponent::Tool::Mute);

    undoButton_.setEnabled(pianoRoll_.canUndo());
    redoButton_.setEnabled(pianoRoll_.canRedo());
    const bool hasSelection = pianoRoll_.hasSelection();
    quantizeButton_.setEnabled(hasSelection);
    legatoButton_.setEnabled(hasSelection);
    humanizeButton_.setEnabled(hasSelection);
    snapButton_.setToggleState(pianoRoll_.snapEnabled(), juce::dontSendNotification);
    repaint();
}

void PianoRollToolbar::paint(juce::Graphics& g) {
    g.fillAll(Palette::panel);
    g.setColour(Palette::border);
    g.drawLine(0.0f, static_cast<float>(getHeight()) - 0.5f,
                static_cast<float>(getWidth()), static_cast<float>(getHeight()) - 0.5f, 1.0f);
}

void PianoRollToolbar::resized() {
    // Deux rangées : outils et actions en haut, réglages en bas. La barre
    // reste utilisable sur une fenêtre étroite -- rien n'est jamais coupé,
    // les éléments se serrent.
    auto area = getLocalBounds().reduced(6, 4);
    auto top = area.removeFromTop(area.getHeight() / 2).reduced(0, 1);
    auto bottom = area.reduced(0, 1);

    auto place = [](juce::Rectangle<int>& row, juce::Component& c, int width) {
        c.setBounds(row.removeFromLeft(width).reduced(1, 0));
    };
    for (auto* b : { &selectTool_, &drawTool_, &eraseTool_, &splitTool_, &glueTool_, &muteTool_ })
        place(top, *b, 52);
    top.removeFromLeft(10);
    place(top, undoButton_, 66);
    place(top, redoButton_, 70);
    top.removeFromLeft(10);
    place(top, quantizeButton_, 80);
    place(top, legatoButton_, 62);
    place(top, humanizeButton_, 78);
    place(top, chordButton_, 62);
    place(top, moreButton_, 62);
    top.removeFromLeft(10);
    place(top, zoomOutButton_, 26);
    place(top, zoomInButton_, 26);
    place(top, zoomFitButton_, 46);

    place(bottom, gridLabel_, 38);
    place(bottom, gridCombo_, 66);
    place(bottom, gridModifierCombo_, 78);
    place(bottom, snapButton_, 74);
    bottom.removeFromLeft(8);
    place(bottom, swingLabel_, 40);
    place(bottom, swingSlider_, 120);
    place(bottom, velocityLabel_, 30);
    place(bottom, velocitySlider_, 120);
    bottom.removeFromLeft(8);
    place(bottom, scaleLabel_, 44);
    place(bottom, scaleRootCombo_, 62);
    place(bottom, scaleTypeCombo_, 140);
    place(bottom, scaleHighlightButton_, 74);
    bottom.removeFromLeft(8);
    place(bottom, ghostButton_, 86);
    place(bottom, followButton_, 76);
}
