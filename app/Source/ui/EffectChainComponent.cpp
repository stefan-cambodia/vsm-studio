#include "EffectChainComponent.h"
#include "vsm/audio/effect/EffectFactory.h"

using namespace vsm::ui;
using vsm::audio::effect::EffectFactory;

EffectChainComponent::EffectChainComponent() {
    titleLabel_.setText("Effets - aucune piste", juce::dontSendNotification);
    titleLabel_.setFont(juce::Font(juce::FontOptions(14.0f).withStyle("Bold")));
    titleLabel_.setColour(juce::Label::textColourId, Palette::textPrimary);
    addAndMakeVisible(titleLabel_);

    addLabel_.setText("Ajouter :", juce::dontSendNotification);
    addLabel_.setColour(juce::Label::textColourId, Palette::textSecondary);
    addLabel_.setFont(juce::Font(juce::FontOptions(11.0f)));
    addAndMakeVisible(addLabel_);

    int id = 1;
    for (const auto& info : EffectFactory::available())
        addBox_.addItem(juce::String(info.displayName), id++);
    addBox_.setTextWhenNothingSelected("choisir un effet");
    addBox_.onChange = [this] {
        const int idx = addBox_.getSelectedItemIndex();
        Chain* chain = activeChain();
        if (idx < 0 || chain == nullptr) { addBox_.setSelectedId(0, juce::dontSendNotification); return; }
        const auto& info = EffectFactory::available()[static_cast<size_t>(idx)];
        auto fx = EffectFactory::create(info.id);
        if (fx) {
            fx->prepare(sampleRate_, blockSize_);     // prepare AVANT publication (thread UI)
            chain->push_back(std::move(fx));
            selectedEffect_ = static_cast<int>(chain->size()) - 1;
            publishActiveChain();
            rebuildEffectList();
            rebuildParamControls();
        }
        addBox_.setSelectedId(0, juce::dontSendNotification);
    };
    addAndMakeVisible(addBox_);

    paramHeader_.setColour(juce::Label::textColourId, Palette::textSecondary);
    paramHeader_.setFont(juce::Font(juce::FontOptions(11.0f).withStyle("Bold")));
    addAndMakeVisible(paramHeader_);
}

EffectChainComponent::Chain* EffectChainComponent::activeChain() {
    if (activeTrack_ < 0) return nullptr;
    return &chains_[activeTrack_];
}

void EffectChainComponent::setActiveTrack(int trackIndex) {
    activeTrack_ = trackIndex;
    selectedEffect_ = -1;
    titleLabel_.setText(trackIndex < 0 ? "Effets - aucune piste"
                                       : "Effets - piste " + juce::String(trackIndex + 1),
                        juce::dontSendNotification);
    rebuildEffectList();
    rebuildParamControls();
}

void EffectChainComponent::publishActiveChain() {
    if (!onChainChanged || activeTrack_ < 0) return;
    Chain* chain = activeChain();
    // Copie immuable publiée au moteur (RT-safe).
    auto published = std::make_shared<const Chain>(*chain);
    onChainChanged(static_cast<size_t>(activeTrack_), published);
}

void EffectChainComponent::rebuildEffectList() {
    rows_.clear();
    Chain* chain = activeChain();
    if (chain == nullptr) { resized(); return; }

    for (size_t i = 0; i < chain->size(); ++i) {
        EffectRow row;
        const auto index = static_cast<int>(i);

        row.select = std::make_unique<juce::TextButton>((*chain)[i]->effectName());
        row.select->setColour(juce::TextButton::buttonOnColourId, Palette::accentTeal);
        row.select->setClickingTogglesState(true);
        row.select->setToggleState(index == selectedEffect_, juce::dontSendNotification);
        row.select->onClick = [this, index] { selectedEffect_ = index; rebuildEffectList(); rebuildParamControls(); };
        addAndMakeVisible(*row.select);

        row.up = std::make_unique<juce::TextButton>("^");
        row.up->onClick = [this, index] {
            Chain* c = activeChain();
            if (c && index > 0) { std::swap((*c)[static_cast<size_t>(index)], (*c)[static_cast<size_t>(index - 1)]);
                selectedEffect_ = index - 1; publishActiveChain(); rebuildEffectList(); rebuildParamControls(); }
        };
        addAndMakeVisible(*row.up);

        row.down = std::make_unique<juce::TextButton>("v");
        row.down->onClick = [this, index] {
            Chain* c = activeChain();
            if (c && index + 1 < static_cast<int>(c->size())) {
                std::swap((*c)[static_cast<size_t>(index)], (*c)[static_cast<size_t>(index + 1)]);
                selectedEffect_ = index + 1; publishActiveChain(); rebuildEffectList(); rebuildParamControls(); }
        };
        addAndMakeVisible(*row.down);

        row.remove = std::make_unique<juce::TextButton>("X");
        row.remove->setColour(juce::TextButton::buttonColourId, Palette::accentRed.darker(0.3f));
        row.remove->onClick = [this, index] {
            Chain* c = activeChain();
            if (c && index < static_cast<int>(c->size())) {
                c->erase(c->begin() + index);
                selectedEffect_ = -1;
                publishActiveChain(); rebuildEffectList(); rebuildParamControls(); }
        };
        addAndMakeVisible(*row.remove);

        rows_.push_back(std::move(row));
    }
    resized();
}

void EffectChainComponent::rebuildParamControls() {
    params_.clear();
    paramHeader_.setText("", juce::dontSendNotification);

    Chain* chain = activeChain();
    if (chain == nullptr || selectedEffect_ < 0 || selectedEffect_ >= static_cast<int>(chain->size())) {
        resized();
        return;
    }
    auto* fx = (*chain)[static_cast<size_t>(selectedEffect_)].get();
    paramHeader_.setText(juce::String(fx->effectName()) + " - parametres", juce::dontSendNotification);

    for (const auto& info : fx->parameterList()) {
        ParamControl pc;
        pc.slider = std::make_unique<juce::Slider>();
        pc.slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        pc.slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 14);
        pc.slider->setRange(info.minValue, info.maxValue,
                            (info.maxValue - info.minValue) / 1000.0);
        pc.slider->setValue(fx->getParameter(info.id), juce::dontSendNotification);
        if (!info.unit.empty()) pc.slider->setTextValueSuffix(" " + juce::String(info.unit));
        const auto pid = info.id;
        juce::Slider* raw = pc.slider.get();
        raw->onValueChange = [fx, raw, pid] { fx->setParameter(pid, static_cast<float>(raw->getValue())); };
        addAndMakeVisible(*pc.slider);

        pc.label = std::make_unique<juce::Label>();
        pc.label->setText(juce::String(info.name), juce::dontSendNotification);
        pc.label->setJustificationType(juce::Justification::centred);
        pc.label->setColour(juce::Label::textColourId, Palette::textSecondary);
        pc.label->setFont(juce::Font(juce::FontOptions(10.0f)));
        addAndMakeVisible(*pc.label);

        params_.push_back(std::move(pc));
    }
    resized();
}

void EffectChainComponent::paint(juce::Graphics& g) {
    g.fillAll(Palette::background);
}

void EffectChainComponent::resized() {
    auto area = getLocalBounds().reduced(8);
    titleLabel_.setBounds(area.removeFromTop(22));

    auto addRow = area.removeFromTop(26);
    addLabel_.setBounds(addRow.removeFromLeft(56));
    addBox_.setBounds(addRow.removeFromLeft(200));
    area.removeFromTop(6);

    // Liste des effets (rangées de 24 px).
    for (auto& row : rows_) {
        auto r = area.removeFromTop(26).reduced(0, 1);
        row.remove->setBounds(r.removeFromRight(28).reduced(1));
        row.down->setBounds(r.removeFromRight(26).reduced(1));
        row.up->setBounds(r.removeFromRight(26).reduced(1));
        row.select->setBounds(r.reduced(1));
    }

    area.removeFromTop(8);
    paramHeader_.setBounds(area.removeFromTop(18));

    // Grille de knobs de paramètres (colonnes de 84 px).
    const int knobW = 84, knobH = 74;
    const int cols = juce::jmax(1, area.getWidth() / knobW);
    for (size_t i = 0; i < params_.size(); ++i) {
        const int col = static_cast<int>(i) % cols;
        const int rowIdx = static_cast<int>(i) / cols;
        juce::Rectangle<int> cell(area.getX() + col * knobW, area.getY() + rowIdx * knobH, knobW, knobH);
        params_[i].label->setBounds(cell.removeFromBottom(14));
        params_[i].slider->setBounds(cell.reduced(4));
    }
}
