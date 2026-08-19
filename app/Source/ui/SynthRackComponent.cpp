#include "SynthRackComponent.h"
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

    addAndMakeVisible(machinePanel_);
    machinePanel_.setVisible(false);
    machinePanel_.onParamTouched = [this](vsm::audio::plugin::ParamId id) {
        if (learnMode_ && onParamTouched) onParamTouched(id);
    };
    machinePanel_.onPatternEdited = [this] { if (onPatternEdited) onPatternEdited(); };
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
    machinePanel_.setVisible(usingMachinePanel_);
    viewport_.setVisible(!usingMachinePanel_);

    if (synth_) {
        titleLabel_.setText(trackName.isEmpty() ? juce::String("SYNTH RACK") : trackName,
                             juce::dontSendNotification);
        machineNameLabel_.setText(juce::String(synth_->machineName()), juce::dontSendNotification);
    } else {
        titleLabel_.setText("SYNTH RACK", juce::dontSendNotification);
        machineNameLabel_.setText("(aucun instrument assigne)", juce::dontSendNotification);
    }

    rebuildControls();
    resized();
    repaint();
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
        machinePanel_.setBounds(area);
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
