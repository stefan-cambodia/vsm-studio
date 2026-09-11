#include "PianoRollToolbar.h"
#include "Langue.h"
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
// D74 : DES LETTRES, ET DANS LES DEUX LANGUES. Ce sélecteur écrivait la
// tonique en solfège — « Do# » — pendant que le clavier du piano roll, à trois
// centimètres au-dessous, écrivait « C#2 », et que la liste des événements et
// le séquenceur des boîtes à rythmes écrivaient des lettres eux aussi. Trois
// surfaces sur quatre : c'était celle-ci l'exception.
//
// CE N'EST PAS UNE ENTRÉE DE LA TABLE DE TRADUCTION, et c'est délibéré. Cubase
// et Live en français affichent « C3 » ; le § 2 de ce document dit que le DAW
// se juge à leur aune, et sur ce point l'usage du métier a tranché avant nous.
// Surtout, `noteNumberToName` vit dans `core/` et sert à NOMMER les pistes que
// « Éclater par hauteur » fabrique : ce nom part dans le fichier de projet, et
// un nom qui changerait selon la langue changerait le contenu enregistré.
const char8_t* kNoteNames[12] = { u8"C", u8"C#", u8"D", u8"D#", u8"E", u8"F",
                                  u8"F#", u8"G", u8"G#", u8"A", u8"A#", u8"B" };

/// La rangée fait 26 px visibles, plus un pixel de marge en haut et en bas.
/// C'est exactement ce que valaient les trois rangées de D29.4 (92 px pour
/// trois, marges comprises) : là où tout tenait déjà, rien ne bouge.
constexpr int kHauteurRangee = 28;
constexpr int kMargeX = 6;
constexpr int kMargeY = 4;
} // namespace

PianoRollToolbar::PianoRollToolbar(PianoRollComponent& pianoRoll) : pianoRoll_(pianoRoll) {
    // D94 : LES INFOBULLES SONT POSÉES PAR `retraduire()`, appelée en fin de
    // constructeur. Posées ici, en français, la bascule de langue les laissait
    // dans la langue du démarrage -- ce que D78 interdit.
    auto tool = [this](juce::TextButton& button, PianoRollComponent::Tool t) {
        configureButton(button);
        button.onClick = [this, t] { pianoRoll_.setTool(t); refreshFromPianoRoll(); };
    };
    tool(selectTool_, PianoRollComponent::Tool::Select);
    tool(drawTool_,   PianoRollComponent::Tool::Draw);
    tool(eraseTool_,  PianoRollComponent::Tool::Erase);
    tool(splitTool_,  PianoRollComponent::Tool::Split);
    tool(glueTool_,   PianoRollComponent::Tool::Glue);
    tool(muteTool_,   PianoRollComponent::Tool::Mute);

    configureButton(undoButton_);
    undoButton_.onClick = [this] { pianoRoll_.undo(); refreshFromPianoRoll(); };
    configureButton(redoButton_);
    redoButton_.onClick = [this] { pianoRoll_.redo(); refreshFromPianoRoll(); };

    configureButton(quantizeButton_);
    quantizeButton_.onClick = [this] { pianoRoll_.quantizeSelection(1.0f, false); refreshFromPianoRoll(); };
    configureButton(legatoButton_);
    legatoButton_.onClick = [this] { pianoRoll_.applyLegatoToSelection(); refreshFromPianoRoll(); };
    configureButton(humanizeButton_);
    humanizeButton_.onClick = [this] {
        pianoRoll_.humanizeSelection(static_cast<float>(pianoRoll_.gridTicks()) * 0.12f, 12.0f);
        refreshFromPianoRoll();
    };
    configureButton(chordButton_);
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
    configureButton(moreButton_);
    moreButton_.onClick = [this] {
        pianoRoll_.buildContextMenu().showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(moreButton_),
            [this](int result) { if (result != 0) { pianoRoll_.performContextMenuAction(result); refreshFromPianoRoll(); } });
    };

    configureButton(zoomInButton_);
    zoomInButton_.onClick = [this] { pianoRoll_.zoomHorizontally(1.25f); };
    configureButton(zoomOutButton_);
    zoomOutButton_.onClick = [this] { pianoRoll_.zoomHorizontally(0.8f); };
    configureButton(zoomFitButton_);
    zoomFitButton_.onClick = [this] { pianoRoll_.zoomToFit(); };

    for (auto* toggle : { &snapButton_, &ghostButton_, &foldButton_, &followButton_, &scaleHighlightButton_, &stepButton_ })
        addAndMakeVisible(*toggle);
    snapButton_.setToggleState(pianoRoll_.snapEnabled(), juce::dontSendNotification);
    snapButton_.onClick = [this] { pianoRoll_.setSnapEnabled(snapButton_.getToggleState()); };
    stepButton_.onClick = [this] { pianoRoll_.setStepInputEnabled(stepButton_.getToggleState()); };
    ghostButton_.setToggleState(pianoRoll_.ghostNotesVisible(), juce::dontSendNotification);
    ghostButton_.onClick = [this] { pianoRoll_.setGhostNotesVisible(ghostButton_.getToggleState()); };
    // D20.2 : REPLIER. Refusé sur une piste sans note -- le bouton revient
    // alors, et le piano roll a dit pourquoi dans sa ligne d'état.
    foldButton_.onClick = [this] {
        if (!pianoRoll_.setFoldEnabled(foldButton_.getToggleState()))
            foldButton_.setToggleState(false, juce::dontSendNotification);
    };
    followButton_.setToggleState(pianoRoll_.followPlayhead(), juce::dontSendNotification);
    followButton_.onClick = [this] { pianoRoll_.setFollowPlayhead(followButton_.getToggleState()); };
    scaleHighlightButton_.setToggleState(false, juce::dontSendNotification);
    scaleHighlightButton_.onClick = [this] { pianoRoll_.setScaleHighlightEnabled(scaleHighlightButton_.getToggleState()); };

    addAndMakeVisible(gridCombo_);
    for (size_t i = 0; i < gridChoices().size(); ++i)
        gridCombo_.addItem(gridChoices()[i].second, static_cast<int>(i) + 1);
    gridCombo_.addItem(vsm::app::ui::tr("Auto"), 100);   // D29.5 : la grille suit le zoom
    gridCombo_.setSelectedId(5, juce::dontSendNotification); // 1/16
    gridCombo_.onChange = [this] { applyGridFromCombos(); };

    addAndMakeVisible(gridModifierCombo_);
    gridModifierCombo_.addItem(vsm::app::ui::tr("Droit"), 1);
    gridModifierCombo_.addItem(vsm::app::ui::tr("Triolet"), 2);
    gridModifierCombo_.addItem(vsm::app::ui::tr(u8"Pointé"), 3);
    gridModifierCombo_.setSelectedId(1, juce::dontSendNotification);
    gridModifierCombo_.onChange = [this] { applyGridFromCombos(); };

    addAndMakeVisible(scaleRootCombo_);
    for (int i = 0; i < 12; ++i) scaleRootCombo_.addItem(kNoteNames[i], i + 1);
    scaleRootCombo_.setSelectedId(1, juce::dontSendNotification);
    scaleRootCombo_.onChange = [this] { applyScaleFromCombos(); };

    addAndMakeVisible(scaleTypeCombo_);
    const auto scales = allScaleTypes();
    for (size_t i = 0; i < scales.size(); ++i)
        // D78 : TRADUIT À L'AFFICHAGE, le nom reste français dans `core/` ; et
        // `tr()` lit l'UTF-8 que `juce::String(const char*)` lisait en Latin-1.
        scaleTypeCombo_.addItem(vsm::app::ui::tr(scaleTypeName(scales[i])), static_cast<int>(i) + 1);
    scaleTypeCombo_.setSelectedId(1, juce::dontSendNotification);
    scaleTypeCombo_.onChange = [this] { applyScaleFromCombos(); };

    addAndMakeVisible(swingSlider_);
    swingSlider_.setRange(0.0, 0.75, 0.01);
    swingSlider_.setSliderSnapsToMousePosition(false);   // D139 : suit le glissé, ne saute pas au clic
    swingSlider_.setName("pianoroll.swing");   // D139 : le nom par lequel le banc le désigne (appuyer:)
    swingSlider_.setValue(0.0, juce::dontSendNotification);
    swingSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
    swingSlider_.onValueChange = [this] { pianoRoll_.setSwing(static_cast<float>(swingSlider_.getValue())); };

    addAndMakeVisible(velocitySlider_);
    velocitySlider_.setRange(1.0, 127.0, 1.0);
    velocitySlider_.setSliderSnapsToMousePosition(false);   // D139 : suit le glissé, ne saute pas au clic
    velocitySlider_.setValue(100.0, juce::dontSendNotification);
    velocitySlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
    // D29.4 : LA LIGNE D'INFORMATION. Trois champs éditables, relus huit fois
    // par seconde ; l'édition d'un champ ne pose que ce champ.
    infoLabel_.setText(vsm::app::ui::tr(u8"Note :"), juce::dontSendNotification);
    infoLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(infoLabel_);
    int champ = 0;
    for (auto* e : { &debutEdit_, &dureeEdit_, &veloEdit_ }) {
        e->setEditable(false, true, false);
        e->setJustificationType(juce::Justification::centred);
        e->setColour(juce::Label::outlineColourId, Palette::border);   // l'infobulle : `retraduire()`
        const int ce = champ;
        e->onEditorShow = [this] { infoEnEdition_ = true; };
        e->onEditorHide = [this] { infoEnEdition_ = false; };
        e->onTextChange = [this, ce] { applyInfoLine(ce); };
        addAndMakeVisible(e);
        ++champ;
    }
    startTimerHz(8);

    velocitySlider_.onValueChange = [this] {
        pianoRoll_.setDefaultVelocity(static_cast<uint8_t>(velocitySlider_.getValue()));
    };

    auto label = [this](juce::Label& l, const juce::String& text) {
        l.setText(text, juce::dontSendNotification);
        l.setColour(juce::Label::textColourId, Palette::textSecondary);
        l.setFont(juce::Font(juce::FontOptions(11.0f)));
        addAndMakeVisible(l);
    };
    label(gridLabel_, vsm::app::ui::tr("Grille"));
    label(swingLabel_, "Swing");
    label(velocityLabel_, vsm::app::ui::tr(u8"Vél."));
    label(scaleLabel_, vsm::app::ui::tr("Gamme"));   // D78

    refreshFromPianoRoll();
    // D74 : les libellés des initialiseurs de membres sont en français ; la
    // langue étant déjà posée au démarrage, on les repose ici.
    retraduire();
}

void PianoRollToolbar::configureButton(juce::Button& button) {
    addAndMakeVisible(button);
}

void PianoRollToolbar::applyGridFromCombos() {
    GridResolution grid;
    // D29.5 : « Auto » garde 1/16 comme base des modificateurs (triolet, pointé)
    // et laisse le piano roll choisir la valeur selon le zoom.
    pianoRoll_.setAdaptiveGrid(gridCombo_.getSelectedId() == 100);
    const int index = gridCombo_.getSelectedId() == 100 ? 5
                    : juce::jlimit(1, static_cast<int>(gridChoices().size()), gridCombo_.getSelectedId());
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
    foldButton_.setToggleState(pianoRoll_.foldEnabled(), juce::dontSendNotification);
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

void PianoRollToolbar::resized() { disposer(getWidth(), true); }

int PianoRollToolbar::hauteurUtile(int largeur) {
    return 2 * kMargeY + disposer(largeur, false) * kHauteurRangee;
}

int PianoRollToolbar::disposer(int largeurTotale, bool placer) {
    // TROIS BANDES QUI SE REPLIENT, et c'est une CORRECTION (D61). La version
    // d'avant posait chaque bande sur UNE rangée, à coups de
    // `removeFromLeft(largeur)` avec des largeurs constantes : quand la rangée
    // était épuisée, `removeFromLeft` rendait un rectangle vide et tout ce qui
    // suivait recevait une largeur de ZÉRO -- invisible, incliquable, sans un
    // mot. Le commentaire promettait l'inverse (« rien n'est jamais coupé, les
    // éléments se serrent ») ; mesuré le 09/09/2026, la bande du haut réclame
    // 950 px et le volet lui en donne 428 en fenêtre pleine, 243 au plancher :
    // ONZE des vingt-quatre commandes étaient à zéro pixel, à TOUTE taille de
    // fenêtre. Une commande qu'on ne peut pas atteindre est pire qu'une
    // commande absente (D35.5, D59) : elle promet.
    //
    // Ici, ce qui ne tient pas passe à la rangée SUIVANTE, jamais à zéro. La
    // case grandit, le texte ne rétrécit pas -- c'est la règle de D60, à qui
    // le bandeau d'aide devait déjà sa seconde ligne. Le panneau demande
    // `hauteurUtile()` et fait défiler au-delà de son plafond.
    struct Element { juce::Component* composant; int largeur; };
    // Un GROUPE ne se coupe jamais : un intitulé reste avec ce qu'il nomme
    // (« Swing » avec son curseur), et une paire indissociable reste entière
    // (annuler/rétablir, les trois zooms). `rompt` ouvre une bande.
    struct Groupe { std::vector<Element> elements; int ecartAvant; bool rompt; };
    const std::vector<Groupe> groupes = {
        // Bande 1 : les outils, puis les actions.
        { { { &selectTool_, 52 } },   0, true  },
        { { { &drawTool_, 52 } },     0, false },
        { { { &eraseTool_, 52 } },    0, false },
        { { { &splitTool_, 52 } },    0, false },
        { { { &glueTool_, 52 } },     0, false },
        { { { &muteTool_, 52 } },     0, false },
        { { { &undoButton_, 66 }, { &redoButton_, 70 } },        10, false },
        { { { &quantizeButton_, 80 } },                          10, false },
        { { { &legatoButton_, 62 } },                             0, false },
        { { { &humanizeButton_, 78 } },                           0, false },
        { { { &chordButton_, 62 } },                              0, false },
        { { { &moreButton_, 62 } },                               0, false },
        { { { &zoomOutButton_, 26 }, { &zoomInButton_, 26 }, { &zoomFitButton_, 46 } }, 10, false },
        // Bande 2 : les réglages.
        // D74 : 96 ET NON 78. « Droit » tient dans 78 px, « Straight » non -- il
        // sortait « Strai… ». La règle du projet est d'agrandir la case, pas de
        // rétrécir le texte : entre « ça tient » et « ça se lit », c'est la
        // lisibilité qui prime. Une largeur taillée sur une seule langue est un
        // défaut que seule la seconde langue révèle.
        { { { &gridLabel_, 38 }, { &gridCombo_, 66 }, { &gridModifierCombo_, 96 } }, 0, true },
        { { { &snapButton_, 74 } },                               0, false },
        { { { &stepButton_, 88 } },                               0, false },
        { { { &swingLabel_, 40 }, { &swingSlider_, 120 } },       8, false },
        { { { &velocityLabel_, 30 }, { &velocitySlider_, 120 } }, 0, false },
        { { { &scaleLabel_, 44 }, { &scaleRootCombo_, 62 } },     8, false },
        { { { &scaleTypeCombo_, 140 } },                          0, false },
        { { { &scaleHighlightButton_, 74 } },                     0, false },
        { { { &ghostButton_, 86 } },                              8, false },
        { { { &foldButton_, 76 } },                               0, false },
        { { { &followButton_, 76 } },                             0, false },
        // Bande 3 : la ligne d'information des notes choisies (D29.4).
        { { { &infoLabel_, 46 }, { &debutEdit_, 110 } },          0, true },
        { { { &dureeEdit_, 90 } },                                0, false },
        { { { &veloEdit_, 90 } },                                 0, false },
    };

    const int gauche = kMargeX;
    const int droite = std::max(gauche + 60, largeurTotale - kMargeX);
    int x = gauche, y = kMargeY, rangees = 1;
    for (const auto& groupe : groupes) {
        int largeur = 0;
        for (const auto& e : groupe.elements) largeur += e.largeur;
        const bool debutDeRangee = (x == gauche);
        const bool aLaLigne = !debutDeRangee &&
                              (groupe.rompt || x + groupe.ecartAvant + largeur > droite);
        if (aLaLigne) {
            x = gauche;
            y += kHauteurRangee;
            ++rangees;
        } else if (!debutDeRangee) {
            x += groupe.ecartAvant;
        }
        for (const auto& e : groupe.elements) {
            if (placer)
                e.composant->setBounds(
                    juce::Rectangle<int>(x, y + 1, e.largeur, kHauteurRangee - 2).reduced(1, 0));
            x += e.largeur;
        }
    }
    return rangees;
}

// --- D29.4 : la ligne d'information des notes --------------------------------

void PianoRollToolbar::refreshSelectionInfo() {
    if (infoEnEdition_) return;   // on ne réécrit pas sous les doigts
    const auto* track = pianoRoll_.activeTrack();
    const auto* project = pianoRoll_.project();
    const auto& ids = pianoRoll_.selectedNoteIds();
    if (!track || !project || ids.empty()) {
        for (auto* e : { &debutEdit_, &dureeEdit_, &veloEdit_ }) {
            e->setText(juce::String::fromUTF8("\xe2\x80\x94"), juce::dontSendNotification);
            e->setEnabled(false);
        }
        return;
    }
    const vsm::sequencer::Note* premiere = nullptr;
    size_t combien = 0;
    for (const auto& n : track->notes) {
        if (ids.count(n.id) == 0) continue;
        ++combien;
        if (!premiere || n.startTick < premiere->startTick) premiere = &n;
    }
    if (!premiere) return;
    const auto bb = project->timeSignatureMap.barBeatAt(premiere->startTick, project->ticksPerQuarterNote);
    juce::String debut = juce::String(static_cast<long long>(bb.bar + 1)) + "."
                       + juce::String(static_cast<long long>(bb.beat + 1));
    if (bb.tickInBeat != 0) debut += "+" + juce::String(static_cast<long long>(bb.tickInBeat));
    for (auto* e : { &debutEdit_, &dureeEdit_, &veloEdit_ }) e->setEnabled(true);
    debutEdit_.setText(debut, juce::dontSendNotification);
    dureeEdit_.setText(juce::String(static_cast<long long>(premiere->durationTicks())), juce::dontSendNotification);
    veloEdit_.setText(juce::String(static_cast<int>(premiere->velocity))
                          + (combien > 1 ? juce::String::fromUTF8(" \xc2\xb7 ") + juce::String(static_cast<int>(combien)) : juce::String()),
                      juce::dontSendNotification);
}

void PianoRollToolbar::applyInfoLine(int champ) {
    const auto* track = pianoRoll_.activeTrack();
    const auto* project = pianoRoll_.project();
    const auto& ids = pianoRoll_.selectedNoteIds();
    if (!track || !project || ids.empty()) return;
    if (champ == 0) {
        // « 17.3 » ou « 17.3+120 » : la sélection entière est déplacée de l'écart
        // entre son premier départ et la position saisie.
        const juce::String texte = debutEdit_.getText().trim();
        const juce::String position = texte.upToFirstOccurrenceOf("+", false, false);
        const vsm::midi::Tick reste = texte.contains("+") ? static_cast<vsm::midi::Tick>(texte.fromFirstOccurrenceOf("+", false, false).getLargeIntValue()) : 0;
        int64_t mesure = 0, temps = 0;
        if (!vsm::sequencer::parseBarBeat(position.toStdString(), mesure, temps)) return;
        const vsm::midi::Tick voulu = project->timeSignatureMap.tickAtBarBeat(mesure, temps, project->ticksPerQuarterNote) + reste;
        vsm::midi::Tick premier = -1;
        for (const auto& n : track->notes)
            if (ids.count(n.id) > 0 && (premier < 0 || n.startTick < premier)) premier = n.startTick;
        if (premier >= 0 && voulu != premier) pianoRoll_.nudgeSelection(voulu - premier);
    } else if (champ == 1) {
        const auto ticks = dureeEdit_.getText().trim().getLargeIntValue();
        if (ticks > 0) pianoRoll_.setSelectionLength(static_cast<vsm::midi::Tick>(ticks));
    } else {
        const int velo = veloEdit_.getText().trim().upToFirstOccurrenceOf(" ", false, false).getIntValue();
        if (velo >= 1 && velo <= 127) pianoRoll_.setSelectionVelocity(static_cast<uint8_t>(velo));
    }
}

void PianoRollToolbar::retraduire() {
    using vsm::app::ui::tr;
    // LES SIX OUTILS D'ABORD : ce sont ceux qu'on lit à chaque geste, et ils
    // sont abrégés faute de place. L'abrégé anglais n'est pas la traduction de
    // l'abrégé français mais celle du MOT entier -- « Coup. » devient « Cut »
    // et non « Cu. », parce qu'un mot anglais court n'a pas besoin d'être coupé.
    selectTool_.setButtonText(tr(u8"Sél."));
    drawTool_.setButtonText(tr("Dess."));
    eraseTool_.setButtonText(tr("Eff."));
    splitTool_.setButtonText(tr("Coup."));
    glueTool_.setButtonText(tr("Coll."));
    muteTool_.setButtonText(tr("Muet"));
    undoButton_.setButtonText(tr("Annuler"));
    redoButton_.setButtonText(tr(u8"Rétablir"));
    quantizeButton_.setButtonText(tr("Quantifier"));
    legatoButton_.setButtonText(tr("Legato"));
    humanizeButton_.setButtonText(tr("Humaniser"));
    chordButton_.setButtonText(tr("Accord"));
    moreButton_.setButtonText(tr("Plus..."));
    zoomFitButton_.setButtonText(tr("Tout"));
    snapButton_.setButtonText(tr("Aimant"));
    ghostButton_.setButtonText(tr(u8"Fantômes"));
    foldButton_.setButtonText(tr("Replier"));
    followButton_.setButtonText(tr("Suivre"));
    scaleHighlightButton_.setButtonText(tr("Gamme"));
    stepButton_.setButtonText(tr(u8"Pas à pas"));
    gridLabel_.setText(tr("Grille"), juce::dontSendNotification);
    velocityLabel_.setText(tr(u8"Vél."), juce::dontSendNotification);
    infoLabel_.setText(tr(u8"Note :"), juce::dontSendNotification);
    // D94 : LES INFOBULLES, que le constructeur posait en français une fois
    // pour toutes : la bascule les laissait dans la langue du démarrage.
    selectTool_.setTooltip(tr(u8"Sélection / déplacement (1)"));
    drawTool_.setTooltip(tr("Dessiner des notes (2)"));
    eraseTool_.setTooltip(tr("Effacer, y compris en balayant (3)"));
    splitTool_.setTooltip(tr("Couper une note au clic (4)"));
    glueTool_.setTooltip(tr(u8"Coller une note à la suivante (5)"));
    muteTool_.setTooltip(tr("Rendre une note muette (6)"));
    undoButton_.setTooltip(tr("Annuler (Ctrl+Z)"));
    redoButton_.setTooltip(tr(u8"Rétablir (Ctrl+Maj+Z)"));
    quantizeButton_.setTooltip(tr(u8"Quantifier la sélection sur la grille (Ctrl+Q)"));
    legatoButton_.setTooltip(tr(u8"Étendre chaque note jusqu'à la suivante (Ctrl+L)"));
    humanizeButton_.setTooltip(tr(u8"Décaler légèrement timing et vélocité, de façon reproductible"));
    chordButton_.setTooltip(tr(u8"Insérer un accord à la tête de lecture"));
    moreButton_.setTooltip(tr(u8"Toutes les opérations d'édition"));
    zoomInButton_.setTooltip(tr("Zoom avant (+)"));
    zoomOutButton_.setTooltip(tr(u8"Zoom arrière (-)"));
    zoomFitButton_.setTooltip(tr("Afficher toute la piste (Ctrl+0)"));
    stepButton_.setTooltip(tr(u8"Saisie pas à pas : chaque note jouée s'écrit à la tête de lecture, qui avance "
                              u8"d'un pas de grille ; Entrée = silence, Retour arrière = reculer"));
    foldButton_.setTooltip(tr(u8"Ne montrer que les hauteurs jouées sur la piste (Live : Fold)"));
    debutEdit_.setTooltip(tr(u8"Début : mesure.temps (« 17.3 », « 17.3+120 » en ticks) ; double-clic pour "
                             u8"éditer, déplace toute la sélection"));
    dureeEdit_.setTooltip(tr(u8"Durée en ticks, posée sur toutes les notes choisies"));
    veloEdit_.setTooltip(tr(u8"Vélocité (1-127), posée sur toutes les notes choisies"));
    // D78 : LES ENTRÉES DES SÉLECTEURS, que la bascule oubliait. D77 a trouvé
    // « Droit » sur une image basculée en anglais, là où le démarrage écrivait
    // « Straight ». `changeItemText` ne change que l'entrée, pas le texte
    // affiché de celle qui est choisie : reposer la même sélection, sans
    // notification, le rafraîchit sans rien changer au réglage.
    //
    // LA SÉLECTION SE LIT AVANT DE RENOMMER, et c'est ce que la première
    // version ratait : `ComboBox::getSelectedId` ne rend l'identifiant que si
    // le texte affiché est ENCORE celui de l'entrée. Une fois l'entrée
    // renommée, il rendait 0, la condition était fausse, et l'image basculée
    // gardait « Droit » et « Chromatique » -- 527 pixels, mesurés.
    const auto retitrer = [](juce::ComboBox& boite, int id, const juce::String& texte) {
        const bool choisie = boite.getSelectedId() == id;
        boite.changeItemText(id, texte);
        if (choisie) boite.setSelectedId(id, juce::dontSendNotification);
    };
    retitrer(gridCombo_, 100, tr("Auto"));
    retitrer(gridModifierCombo_, 1, tr("Droit"));
    retitrer(gridModifierCombo_, 2, tr("Triolet"));
    retitrer(gridModifierCombo_, 3, tr(u8"Pointé"));
    const auto gammes = vsm::sequencer::allScaleTypes();
    for (size_t i = 0; i < gammes.size(); ++i)
        retitrer(scaleTypeCombo_, static_cast<int>(i) + 1, tr(vsm::sequencer::scaleTypeName(gammes[i])));
    scaleLabel_.setText(tr("Gamme"), juce::dontSendNotification);
    repaint();
}
