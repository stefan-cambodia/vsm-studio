#include "SynthRackComponent.h"
#include "Langue.h"
#include "LookAndFeel/VsmLookAndFeel.h"

using namespace vsm::audio::plugin;
using namespace vsm::ui;

SynthRackComponent::SynthRackComponent() {
    addAndMakeVisible(titleLabel_);
    titleLabel_.setFont(juce::Font(juce::FontOptions(16.0f)));
    titleLabel_.setColour(juce::Label::textColourId, Palette::accentAmber);
    titleLabel_.setJustificationType(juce::Justification::centred);

    addAndMakeVisible(machineNameLabel_);
    machineNameLabel_.setFont(juce::Font(juce::FontOptions(13.0f)));
    machineNameLabel_.setColour(juce::Label::textColourId, Palette::textSecondary);
    machineNameLabel_.setJustificationType(juce::Justification::centred);

    addAndMakeVisible(learnButton_);
    learnButton_.setClickingTogglesState(true);
    learnButton_.setColour(juce::TextButton::buttonOnColourId, Palette::accentRed);
    learnButton_.onClick = [this] {
        learnMode_ = learnButton_.getToggleState();
        if (onLearnModeChanged) onLearnModeChanged(learnMode_);
    };

    vueFacade_.setViewedComponent(&machinePanel_, false);
    vueFacade_.setScrollBarsShown(true, false);
    addChildComponent(vueFacade_);
    machinePanel_.setVisible(true);
    machinePanel_.onParamTouched = [this](vsm::audio::plugin::ParamId id) {
        if (learnMode_ && onParamTouched) onParamTouched(id);
    };
    machinePanel_.onPatternEdited = [this] { if (onPatternEdited) onPatternEdited(); };
    machinePanel_.onEditStarted = [this](const juce::String& libelle) {
        if (onEditStarted) onEditStarted(libelle);
    };
    // D133 : l'afficheur de la façade passe au pied du rack, hors de `vueFacade_`.
    machinePanel_.setAfficheurExterne(true);
    machinePanel_.onValueReadout = [this](const juce::String& texte) {
        afficheur_.setText(texte, juce::dontSendNotification);
    };
    afficheur_.setJustificationType(juce::Justification::centredRight);
    afficheur_.setFont(juce::Font(juce::FontOptions(13.0f)));
    afficheur_.setColour(juce::Label::textColourId, Palette::textPrimary);
    addChildComponent(afficheur_);
    addAndMakeVisible(viewport_);
    viewport_.setViewedComponent(&controlContainer_, false);
    viewport_.setScrollBarsShown(true, false);

    setSynth(nullptr, {});
}

void SynthRackComponent::setSynth(ISynthPlugin* synth, const juce::String& trackName,
                                   const std::string& pluginId) {
    synth_ = synth;

    // Façade dédiée si la machine en a une ; sinon la liste générique, qui
    // reste indispensable : elle couvre les machines sans façade ET les
    // plugins tiers chargés en CLAP, dont on ne connaît pas la disposition.
    const vsm::panels::MachinePanel* panel = synth_ ? vsm::panels::findMachinePanel(pluginId) : nullptr;
    usingMachinePanel_ = (panel != nullptr);
    machinePanel_.setPanel(panel, synth_);
    vueFacade_.setVisible(usingMachinePanel_);
    viewport_.setVisible(!usingMachinePanel_);
    afficheur_.setVisible(usingMachinePanel_);            // D133 : le générique n'a pas d'afficheur
    afficheur_.setText({}, juce::dontSendNotification);   // une autre machine, une autre valeur

    if (synth_) {
        titleLabel_.setText(trackName.isEmpty() ? juce::String("SYNTH RACK") : trackName,
                             juce::dontSendNotification);
        // `fromUTF8` ET NON `juce::String(const char*)` : le second traite les
        // octets comme du Latin-1 et rend « s'éclaircit » en « s'Â©claircit ».
        // Le défaut se voyait sur toute machine dont le nom porte un accent —
        // `vsm.modal` (« l'objet frappé ») et `vsm.plate` — et aucun des
        // 1 361 tests ne pouvait l'attraper : il n'apparaît qu'à l'écran.
        machineNameLabel_.setText(vsm::app::ui::tr(juce::String::fromUTF8(synth_->machineName())),
                                  juce::dontSendNotification);   // D103
    } else {
        titleLabel_.setText("SYNTH RACK", juce::dontSendNotification);
        machineNameLabel_.setText(vsm::app::ui::tr(u8"(aucun instrument assigné)"),
                                  juce::dontSendNotification);
    }

    rebuildControls();
    resized();
    repaint();
}

void SynthRackComponent::retraduire() {
    // D94 : le seul texte que le rack écrit lui-même ; le reste vient de la
    // machine (son nom, ses paramètres), c'est-à-dire du moteur.
    if (!synth_)
        machineNameLabel_.setText(vsm::app::ui::tr(u8"(aucun instrument assigné)"),
                                  juce::dontSendNotification);
    else   // D103 : le nom de la machine suit la bascule
        machineNameLabel_.setText(vsm::app::ui::tr(juce::String::fromUTF8(synth_->machineName())),
                                  juce::dontSendNotification);
}

void SynthRackComponent::setTrack(vsm::sequencer::Track* track) {
    machinePanel_.setTrack(track);
}

void SynthRackComponent::setPlayheadTick(vsm::midi::Tick tick) {
    if (usingMachinePanel_) machinePanel_.setPlayheadTick(tick);
}

void SynthRackComponent::rebuildControls() {
    controls_.clear();
    controlContainer_.removeAllChildren();
    if (!synth_) return;

    for (const auto& info : synth_->parameterList()) {
        ParamControl control;
        control.id = info.id;

        auto slider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                       juce::Slider::NoTextBox);
        slider->setRange(static_cast<double>(info.minValue), static_cast<double>(info.maxValue), 0.0);
        slider->setName("rack.parametre");   // D135 : le nom par lequel le banc le désigne (appuyer:)
        slider->setDoubleClickReturnValue(true, static_cast<double>(info.defaultValue));   // D140 : double-clic, valeur d'usine
        slider->textFromValueFunction = [unite = juce::String(info.unit)](double v) {   // D135
            return vsm::app::ui::texteParametre(v, unite);
        };
        control.bulle = std::make_unique<vsm::app::ui::BulleDeValeur>(*slider);
        slider->setValue(static_cast<double>(synth_->getParameter(info.id)), juce::dontSendNotification);

        ParamId paramId = info.id;
        ISynthPlugin* synthPtr = synth_;
        juce::Slider* sliderPtr = slider.get();
        slider->onValueChange = [this, sliderPtr, synthPtr, paramId] {
            synthPtr->setParameter(paramId, static_cast<float>(sliderPtr->getValue()));
            if (learnMode_ && onParamTouched) onParamTouched(paramId);
        };
        controlContainer_.addAndMakeVisible(*slider);

        auto label = std::make_unique<juce::Label>();
        juce::String labelText(info.name);
        if (!info.unit.empty()) labelText += " (" + juce::String(info.unit) + ")";
        label->setText(labelText, juce::dontSendNotification);
        label->setFont(juce::Font(juce::FontOptions(11.0f)));
        label->setJustificationType(juce::Justification::centred);
        label->setColour(juce::Label::textColourId, Palette::textSecondary);
        controlContainer_.addAndMakeVisible(*label);

        control.slider = std::move(slider);
        control.nameLabel = std::move(label);
        controls_.push_back(std::move(control));
    }
}

void SynthRackComponent::paint(juce::Graphics& g) { g.fillAll(Palette::panel); }

void SynthRackComponent::setLearnArmed(bool armed) {
    // Si le moteur a désarmé (un CC vient d'être lié), on éteint le mode.
    if (!armed && learnMode_) {
        learnMode_ = false;
        learnButton_.setToggleState(false, juce::dontSendNotification);
        if (onLearnModeChanged) onLearnModeChanged(false);
    }
}

void SynthRackComponent::resized() {
    auto area = getLocalBounds().reduced(8);
    titleLabel_.setBounds(area.removeFromTop(24));
    machineNameLabel_.setBounds(area.removeFromTop(20));
    learnButton_.setBounds(area.removeFromTop(22).reduced(24, 1));
    area.removeFromTop(8);

    if (usingMachinePanel_) {
        afficheur_.setBounds(area.removeFromBottom(20).reduced(10, 0));   // D133 : hors de ce qui défile
        // D63 : LA FAÇADE REÇOIT LA HAUTEUR QU'ELLE RÉCLAME, ET DÉFILE SOUS
        // ELLE. Elle recevait `area` telle quelle, quelle qu'en soit la
        // hauteur — pendant que la façade GÉNÉRIQUE, juste en dessous, était
        // protégée par son viewport depuis toujours. L'asymétrie était à
        // l'envers du bon sens : c'est la façade DESSINÉE, celle qui a une
        // grille et des blocs, qui ne supporte pas d'être écrasée. Mesuré à
        // 900x660 sur le TB-303 : la section « RÉGLAGES » recevait une
        // cellule de 43x0 — un bouton qui n'existe pas.
        vueFacade_.setBounds(area);
        // D64 : LA LARGEUR NE DÉFILE PAS, et c'est une décision prise APRÈS
        // l'avoir écrite et regardée. Le faire donnait à la façade la largeur
        // qu'elle réclame — de beaux potentiomètres — mais n'en montrait plus
        // que la moitié : sur le Divider, trois blocs sur six et « ENVELO… »
        // coupé au bord. Un rack où l'on cherche un réglage en défilant dans
        // les deux sens est pire qu'un rack où on le voit petit. La hauteur,
        // elle, défile (D63) : une façade tronquée EN BAS reste lisible en
        // haut, une façade tronquée à DROITE perd des blocs entiers.
        const int voulue = machinePanel_.hauteurUtile();
        const bool defile = voulue > area.getHeight();
        machinePanel_.setSize(defile ? area.getWidth() - vueFacade_.getScrollBarThickness()
                                     : area.getWidth(),
                               std::max(voulue, area.getHeight()));
        return;
    }
    viewport_.setBounds(area);

    constexpr int kColumns = 2;
    int columnWidth = (viewport_.getWidth() - viewport_.getScrollBarThickness()) / kColumns;
    int totalRows = (static_cast<int>(controls_.size()) + kColumns - 1) / kColumns;
    controlContainer_.setBounds(0, 0, viewport_.getWidth() - viewport_.getScrollBarThickness(),
                                 std::max(totalRows * kRowHeight, viewport_.getHeight()));

    for (size_t i = 0; i < controls_.size(); ++i) {
        int col = static_cast<int>(i) % kColumns;
        int row = static_cast<int>(i) / kColumns;
        juce::Rectangle<int> cell(col * columnWidth, row * kRowHeight, columnWidth, kRowHeight);
        auto knobArea = cell.removeFromTop(kRowHeight - 18).reduced(6);
        controls_[i].slider->setBounds(knobArea);
        controls_[i].nameLabel->setBounds(cell);
    }
}
