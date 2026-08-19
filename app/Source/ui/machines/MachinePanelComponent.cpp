#include "MachinePanelComponent.h"
#include <algorithm>

using namespace vsm::panels;

namespace {

/// std::string -> juce::String en UTF-8 EXPLICITE (voir StepSequencerComponent).
juce::String toJuce(const std::string& text) { return juce::String::fromUTF8(text.c_str()); }

juce::Colour colourFrom(const std::string& hex, juce::Colour fallback = juce::Colours::grey) {
    if (hex.size() != 7 || hex[0] != '#') return fallback;
    return juce::Colour::fromString("ff" + juce::String(hex.substr(1)));
}

/// Un paramètre dont la plage est 0..N entiers (formes d'onde, modes) se
/// pilote par pas entiers : laisser un continu produirait des valeurs
/// intermédiaires que la machine arrondit de toute façon, avec un affichage
/// qui ne correspondrait à aucune position réelle.
bool isDiscrete(const vsm::audio::plugin::ParameterInfo& info) {
    const float span = info.maxValue - info.minValue;
    return span > 0.0f && span <= 8.0f && std::abs(span - std::round(span)) < 1e-4f
        && std::abs(info.minValue - std::round(info.minValue)) < 1e-4f;
}

const vsm::audio::plugin::ParameterInfo* findParameter(vsm::audio::plugin::ISynthPlugin& synth,
                                                        const std::string& name) {
    for (const auto& info : synth.parameterList())
        if (info.name == name) return &info;
    return nullptr;
}

} // namespace

MachinePanelComponent::MachinePanelComponent() {
    setOpaque(true);
    valueReadout_.setJustificationType(juce::Justification::centredRight);
    valueReadout_.setFont(juce::Font(juce::FontOptions(12.0f)));
    addAndMakeVisible(valueReadout_);
    addAndMakeVisible(sequencer_);
    sequencer_.setVisible(false);
    sequencer_.onPatternEdited = [this] { if (onPatternEdited) onPatternEdited(); };
    startTimerHz(15); // suit les changements venus d'ailleurs (automation, MIDI, presets)
}

MachinePanelComponent::~MachinePanelComponent() {
    // Les composants enfants pointent vers ce LookAndFeel : il faut les en
    // détacher AVANT sa destruction, sinon JUCE déréférence un objet mort.
    for (auto& control : controls_)
        if (control.widget) control.widget->setLookAndFeel(nullptr);
}

void MachinePanelComponent::setPanel(const MachinePanel* panel, vsm::audio::plugin::ISynthPlugin* synth) {
    panel_ = panel;
    synth_ = synth;
    rebuild();
}

double MachinePanelComponent::aspectRatio() const {
    if (!panel_ || panel_->gridRows <= 0) return 2.0;
    return static_cast<double>(panel_->gridColumns) / static_cast<double>(panel_->gridRows);
}

void MachinePanelComponent::rebuild() {
    for (auto& control : controls_)
        if (control.widget) control.widget->setLookAndFeel(nullptr);
    controls_.clear();
    sectionTitles_.clear();
    // removeAllChildren() emporte AUSSI les enfants permanents (afficheur de
    // valeur, séquenceur) ajoutés au constructeur : il faut les remettre,
    // sinon ils disparaissent au premier changement de machine -- panne
    // silencieuse, puisque le composant existe toujours, simplement détaché.
    removeAllChildren();
    addAndMakeVisible(valueReadout_);
    addAndMakeVisible(sequencer_);
    if (!panel_ || !synth_) { repaint(); return; }

    const juce::Colour textColour = colourFrom(panel_->textColour, juce::Colours::white);
    const juce::Colour knobColour = colourFrom(panel_->knobColour, textColour);

    size_t sectionIndex = 0;
    for (const auto& section : panel_->sections) {
        auto title = std::make_unique<juce::Label>();
        title->setText(toJuce(section.title), juce::dontSendNotification);
        title->setJustificationType(juce::Justification::centredLeft);
        title->setColour(juce::Label::textColourId, colourFrom(section.accentColour, textColour));
        title->setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
        addAndMakeVisible(*title);
        sectionTitles_.push_back(std::move(title));

        for (const auto& spec : section.controls) {
            const auto* info = findParameter(*synth_, spec.parameterName);
            if (!info) continue; // façade et machine désynchronisées : un test l'empêche

            Control control;
            control.paramId = info->id;
            control.style = spec.style;
            control.cellInSection = juce::Rectangle<float>(
                static_cast<float>(spec.column), static_cast<float>(spec.row),
                static_cast<float>(spec.columnSpan), static_cast<float>(spec.rowSpan));
            control.sectionIndex = sectionIndex;

            const juce::Colour accent = colourFrom(section.accentColour, textColour);

            if (spec.style == ControlStyle::Toggle) {
                auto button = std::make_unique<juce::ToggleButton>();
                button->setColour(juce::TextButton::buttonColourId, knobColour.contrasting(0.35f));
                button->setColour(juce::TextButton::buttonOnColourId, accent);
                button->setToggleState(synth_->getParameter(info->id) > (info->minValue + info->maxValue) * 0.5f,
                                        juce::dontSendNotification);
                const auto paramId = info->id;
                const float low = info->minValue, high = info->maxValue;
                auto* raw = button.get();
                button->onClick = [this, paramId, low, high, raw] {
                    synth_->setParameter(paramId, raw->getToggleState() ? high : low);
                    if (onParamTouched) onParamTouched(paramId);
                };
                button->setLookAndFeel(&lookAndFeel_);
                addAndMakeVisible(*button);
                control.widget = std::move(button);
            } else {
                auto slider = std::make_unique<juce::Slider>();
                const bool linear = (spec.style == ControlStyle::VerticalSlider ||
                                      spec.style == ControlStyle::HorizontalSlider);
                slider->setSliderStyle(linear
                    ? (spec.style == ControlStyle::VerticalSlider ? juce::Slider::LinearVertical
                                                                   : juce::Slider::LinearHorizontal)
                    : juce::Slider::RotaryHorizontalVerticalDrag);
                // AUCUN afficheur sous la commande : une façade de machine
                // n'en a pas, et douze nombres à sept décimales rendent le
                // panneau illisible. La valeur s'affiche à la demande, dans
                // l'afficheur unique en bas de façade.
                slider->setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
                slider->setRange(static_cast<double>(info->minValue), static_cast<double>(info->maxValue),
                                  isDiscrete(*info) ? 1.0 : 0.0);
                slider->setValue(static_cast<double>(synth_->getParameter(info->id)),
                                  juce::dontSendNotification);
                if (!info->unit.empty()) slider->setTextValueSuffix(" " + info->unit);
                slider->setColour(juce::Slider::thumbColourId, knobColour);
                slider->setColour(juce::Slider::rotarySliderFillColourId, accent);
                slider->setColour(juce::Slider::textBoxTextColourId, textColour);
                const auto paramId = info->id;
                const juce::String readoutCaption =
                    toJuce(spec.caption.empty() ? spec.parameterName : spec.caption);
                const juce::String unit = juce::String(info->unit);
                auto* raw = slider.get();
                raw->setTooltip(readoutCaption);
                slider->onValueChange = [this, paramId, raw, readoutCaption, unit] {
                    synth_->setParameter(paramId, static_cast<float>(raw->getValue()));
                    showValueReadout(readoutCaption, raw->getValue(), unit);
                    if (onParamTouched) onParamTouched(paramId);
                };
                slider->setLookAndFeel(&lookAndFeel_);
                addAndMakeVisible(*slider);
                control.widget = std::move(slider);
            }

            auto caption = std::make_unique<juce::Label>();
            caption->setText(toJuce(spec.caption.empty() ? spec.parameterName : spec.caption),
                              juce::dontSendNotification);
            caption->setJustificationType(juce::Justification::centredTop);
            caption->setColour(juce::Label::textColourId, textColour.withAlpha(0.8f));
            caption->setFont(juce::Font(juce::FontOptions(10.0f)));
            // La sérigraphie se rétrécit plutôt que de se faire couper : un
            // libellé tronqué en "OSC 1 WAVEF..." ne sert plus à rien.
            caption->setMinimumHorizontalScale(0.55f);
            addAndMakeVisible(*caption);
            control.caption = std::move(caption);

            controls_.push_back(std::move(control));
        }
        ++sectionIndex;
    }
    // Séquenceur intégré : présent uniquement si la machine en a un.
    const bool hasSequencer = panel_ && panel_->sequencer.kind != vsm::panels::SequencerKind::None;
    sequencer_.setVisible(hasSequencer);
    if (hasSequencer)
        sequencer_.configure(panel_->sequencer, track_, colourFrom(panel_->panelColour),
                              colourFrom(panel_->textColour, juce::Colours::white));

    resized();
    repaint();
}

void MachinePanelComponent::setTrack(vsm::sequencer::Track* track) {
    track_ = track;
    if (panel_ && panel_->sequencer.kind != vsm::panels::SequencerKind::None)
        sequencer_.configure(panel_->sequencer, track_, colourFrom(panel_->panelColour),
                              colourFrom(panel_->textColour, juce::Colours::white));
}

void MachinePanelComponent::setPlayheadTick(vsm::midi::Tick tick) {
    if (!panel_ || panel_->sequencer.kind == vsm::panels::SequencerKind::None) return;
    // Position convertie en numéro de pas ; hors motif, aucun pas n'est
    // éclairé plutôt qu'un pas faux.
    const vsm::midi::Tick stepTicks = 120; // double croche à 480 PPQ, résolution de ces machines
    const vsm::midi::Tick length = static_cast<vsm::midi::Tick>(panel_->sequencer.stepCount) * stepTicks;
    if (length <= 0 || tick < 0) { sequencer_.setPlayheadStep(-1); return; }
    sequencer_.setPlayheadStep(static_cast<int>((tick % length) / stepTicks));
}

void MachinePanelComponent::showValueReadout(const juce::String& caption, double value,
                                              const juce::String& unit) {
    // Mise en forme sobre : deux décimales sous 100, aucune au-delà -- lire
    // "1200 Hz" et "0.45 s" plutôt que "1200.0000000" et "0.4500000".
    juce::String text = std::abs(value) >= 100.0 ? juce::String(juce::roundToInt(value))
                                                  : juce::String(value, 2);
    if (unit.isNotEmpty()) text += " " + unit;
    valueReadout_.setText(caption + " : " + text, juce::dontSendNotification);
}

juce::Rectangle<float> MachinePanelComponent::gridToPixels(juce::Rectangle<float> gridBounds) const {
    if (!panel_) return {};
    auto area = getLocalBounds().toFloat().reduced(14.0f);
    area.removeFromBottom(18.0f); // bandeau de l'afficheur de valeur

    // Les rangées réservées au séquenceur sortent de la zone des commandes ET
    // du diviseur : les diviser quand même par le total laisserait un vide
    // entre les potentiomètres et la grille.
    int controlRows = panel_->gridRows;
    if (panel_->sequencer.kind != vsm::panels::SequencerKind::None) {
        const float rowHeight = area.getHeight() / static_cast<float>(panel_->gridRows);
        area.removeFromBottom(rowHeight * static_cast<float>(panel_->sequencer.rowSpan));
        controlRows = std::max(1, panel_->gridRows - panel_->sequencer.rowSpan);
    }
    const float cellWidth = area.getWidth() / static_cast<float>(panel_->gridColumns);
    const float cellHeight = area.getHeight() / static_cast<float>(controlRows);
    return { area.getX() + gridBounds.getX() * cellWidth,
             area.getY() + gridBounds.getY() * cellHeight,
             gridBounds.getWidth() * cellWidth,
             gridBounds.getHeight() * cellHeight };
}

void MachinePanelComponent::resized() {
    if (!panel_) return;
    valueReadout_.setBounds(getLocalBounds().removeFromBottom(20).reduced(18, 2));

    if (sequencer_.isVisible()) {
        // Le séquenceur occupe le bas de la façade, sur la hauteur que la
        // description lui réserve -- comme la rangée de pas d'une boîte à
        // rythmes, sous les réglages de ses voix.
        auto full = getLocalBounds().reduced(14, 0).withTrimmedBottom(22);
        const float rowHeight = static_cast<float>(full.getHeight()) / static_cast<float>(panel_->gridRows);
        const int sequencerHeight =
            static_cast<int>(std::lround(rowHeight * static_cast<float>(panel_->sequencer.rowSpan)));
        sequencer_.setBounds(full.removeFromBottom(sequencerHeight));
    }

    constexpr float kTitleHeight = 18.0f;

    // Surface utile de chaque bloc (sous son titre), puis grille INTERNE au
    // bloc : c'est ce découpage en deux temps qui donne aux commandes toute la
    // place disponible. Les répartir sur la grille de la façade entière
    // laissait des boutons minuscules perdus dans de grands cadres.
    std::vector<juce::Rectangle<float>> sectionContent;
    sectionContent.reserve(panel_->sections.size());

    for (size_t i = 0; i < panel_->sections.size(); ++i) {
        const auto& section = panel_->sections[i];
        auto bounds = gridToPixels({ static_cast<float>(section.column), static_cast<float>(section.row),
                                      static_cast<float>(section.columnSpan),
                                      static_cast<float>(section.rowSpan) }).reduced(6.0f);
        if (i < sectionTitles_.size())
            sectionTitles_[i]->setBounds(bounds.removeFromTop(kTitleHeight).toNearestInt());
        else
            bounds.removeFromTop(kTitleHeight);
        sectionContent.push_back(bounds.reduced(2.0f));
    }

    for (auto& control : controls_) {
        if (control.sectionIndex >= sectionContent.size()) continue;
        const auto& section = panel_->sections[control.sectionIndex];

        // Taille de la grille interne : déduite des commandes elles-mêmes, pour
        // qu'un bloc à deux boutons leur donne la moitié de sa surface chacun.
        int columns = 1, rows = 1;
        for (const auto& spec : section.controls) {
            columns = std::max(columns, spec.column + spec.columnSpan);
            rows = std::max(rows, spec.row + spec.rowSpan);
        }
        if (section.contentColumns > 0) columns = section.contentColumns;

        const auto area = sectionContent[control.sectionIndex];
        const float cellWidth = area.getWidth() / static_cast<float>(columns);
        // Le PAS des rangées ne dépend jamais d'un plafond : positionner la
        // rangée 1 sous une rangée 0 "plafonnée" ferait remonter ses commandes
        // par-dessus celles d'à côté qui, elles, occupent toute leur hauteur.
        // C'est exactement le chevauchement qu'on a vu sur le Jupiter-8, où un
        // curseur et un potentiomètre voisins ne partaient pas du même y.
        const float rowPitch = area.getHeight() / static_cast<float>(rows);

        auto cell = juce::Rectangle<float>(area.getX() + control.cellInSection.getX() * cellWidth,
                                            area.getY() + control.cellInSection.getY() * rowPitch,
                                            control.cellInSection.getWidth() * cellWidth,
                                            control.cellInSection.getHeight() * rowPitch)
                        .reduced(3.0f);

        // Le plafond ne s'applique qu'à la TAILLE dessinée, en haut de la
        // cellule : un potentiomètre isolé dans un bloc haut ne doit pas
        // flotter loin de son intitulé -- mais un curseur, lui, garde toute sa
        // course, c'est sa raison d'être.
        const bool isSlider = control.style == ControlStyle::VerticalSlider ||
                               control.style == ControlStyle::HorizontalSlider;
        if (!isSlider) {
            const float maxHeight = cellWidth * 1.45f;
            if (cell.getHeight() > maxHeight) cell = cell.withHeight(maxHeight);
        }

        // La sérigraphie prend une part FIXE de la cellule : sur une petite
        // façade elle rétrécit avec le reste, au lieu de dévorer le bouton.
        const float captionHeight = juce::jlimit(12.0f, 26.0f, cell.getHeight() * 0.26f);
        control.caption->setBounds(cell.removeFromBottom(captionHeight).toNearestInt());
        control.caption->setFont(juce::Font(juce::FontOptions(juce::jlimit(8.0f, 11.0f, captionHeight * 0.45f))));

        if (control.style == ControlStyle::Knob || control.style == ControlStyle::LargeKnob ||
            control.style == ControlStyle::Selector) {
            // Potentiomètre CARRÉ et centré : c'est le diamètre qui rend la
            // position lisible, un ovale ne ressemble à rien.
            const float diameter = std::min(cell.getWidth(), cell.getHeight());
            control.widget->setBounds(cell.withSizeKeepingCentre(diameter, diameter).toNearestInt());
        } else if (control.style == ControlStyle::Toggle) {
            control.widget->setBounds(cell.withSizeKeepingCentre(std::min(36.0f, cell.getWidth()),
                                                                  std::min(52.0f, cell.getHeight())).toNearestInt());
        } else {
            control.widget->setBounds(cell.toNearestInt());
        }
    }
}

void MachinePanelComponent::timerCallback() {
    // L'affichage doit refléter le moteur, quelle que soit l'origine du
    // changement : automation, MIDI Learn, chargement de preset. Sans ça, un
    // potentiomètre resterait figé sur une valeur qui n'est plus la bonne.
    if (!synth_) return;
    for (auto& control : controls_) {
        if (auto* slider = dynamic_cast<juce::Slider*>(control.widget.get())) {
            const double engineValue = static_cast<double>(synth_->getParameter(control.paramId));
            if (std::abs(engineValue - slider->getValue()) > 1e-4)
                slider->setValue(engineValue, juce::dontSendNotification);
        } else if (auto* button = dynamic_cast<juce::ToggleButton*>(control.widget.get())) {
            const bool engineState = synth_->getParameter(control.paramId) > 0.5f;
            if (engineState != button->getToggleState())
                button->setToggleState(engineState, juce::dontSendNotification);
        }
    }
}

void MachinePanelComponent::paint(juce::Graphics& g) {
    if (!panel_ || !synth_) {
        g.fillAll(juce::Colour(0xff17171b));
        g.setColour(juce::Colours::grey);
        g.setFont(14.0f);
        g.drawText("Aucune façade dédiée pour cette machine", getLocalBounds(), juce::Justification::centred);
        return;
    }

    const juce::Colour panelColour = colourFrom(panel_->panelColour);
    const juce::Colour sectionColour = colourFrom(panel_->sectionColour);
    const juce::Colour textColour = colourFrom(panel_->textColour, juce::Colours::white);
    valueReadout_.setColour(juce::Label::textColourId, textColour.withAlpha(0.8f));
    auto bounds = getLocalBounds().toFloat();

    // Flancs : bois pour le Minimoog et le Prophet, tôle pour les boîtes à
    // rythmes. C'est un détail, mais c'est celui qui donne à l'œil l'échelle
    // et la matière de l'objet.
    if (panel_->chassis == Chassis::Wood) {
        const float cheekWidth = std::min(26.0f, bounds.getWidth() * 0.05f);
        juce::ColourGradient wood(juce::Colour(0xff6b4a2f), bounds.getX(), bounds.getY(),
                                   juce::Colour(0xff3f2a1a), bounds.getX() + cheekWidth, bounds.getBottom(), false);
        g.setGradientFill(wood);
        g.fillRect(bounds.removeFromLeft(cheekWidth));
        g.setGradientFill(wood);
        g.fillRect(bounds.removeFromRight(cheekWidth));
    } else if (panel_->chassis == Chassis::Metal) {
        const float lipWidth = 8.0f;
        g.setColour(panelColour.brighter(0.25f));
        g.fillRect(bounds.removeFromLeft(lipWidth));
        g.setColour(panelColour.brighter(0.25f));
        g.fillRect(bounds.removeFromRight(lipWidth));
    }

    // Panneau : dégradé très léger, comme une tôle peinte éclairée du haut.
    juce::ColourGradient face(panelColour.brighter(0.06f), bounds.getCentreX(), bounds.getY(),
                               panelColour.darker(0.12f), bounds.getCentreX(), bounds.getBottom(), false);
    g.setGradientFill(face);
    g.fillRect(bounds);

    for (const auto& section : panel_->sections) {
        const auto area = gridToPixels({ static_cast<float>(section.column), static_cast<float>(section.row),
                                          static_cast<float>(section.columnSpan),
                                          static_cast<float>(section.rowSpan) }).reduced(3.0f);
        g.setColour(sectionColour.withAlpha(0.75f));
        g.fillRoundedRectangle(area, 4.0f);
        g.setColour(colourFrom(section.accentColour, textColour).withAlpha(0.55f));
        g.drawRoundedRectangle(area, 4.0f, 1.2f);
        // Filet sous le titre : la sérigraphie des façades sépare presque
        // toujours l'intitulé du bloc de ses commandes.
        g.drawLine(area.getX() + 8.0f, area.getY() + 20.0f, area.getRight() - 8.0f, area.getY() + 20.0f, 0.8f);
    }

    g.setColour(textColour.withAlpha(0.55f));
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText(toJuce(panel_->displayName), getLocalBounds().reduced(16, 6), juce::Justification::bottomRight, false);
}
