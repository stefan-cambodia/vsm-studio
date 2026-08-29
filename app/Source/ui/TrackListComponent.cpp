#include "TrackListComponent.h"
#include "LookAndFeel/VsmLookAndFeel.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

using namespace vsm::sequencer;
using namespace vsm::ui;

namespace {
/// Construit la liste affichée dans le combo "instrument" à partir des
/// plugins RÉELLEMENT enregistrés auprès de PluginRegistry (pas une liste
/// statique) : un nouveau plugin Phase 3+ apparaît ici automatiquement, dès
/// que registerBuiltInPlugins() l'a référencé (voir Main.cpp).
std::vector<std::pair<std::string, std::string>> availableInstruments() {
    auto list = vsm::audio::plugin::PluginRegistry::instance().listAvailable();
    std::sort(list.begin(), list.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; }); // tri par nom affiché
    return list;
}
}

// ---------------------------------------------------------------------------
// TrackRowComponent
// ---------------------------------------------------------------------------

TrackRowComponent::TrackRowComponent(Track& track, size_t trackIndex)
    : track_(track), index_(trackIndex) {
    addAndMakeVisible(nameLabel_);
    nameLabel_.setText(track_.name.empty() ? ("Piste " + std::to_string(trackIndex + 1)) : track_.name,
                        juce::dontSendNotification);
    nameLabel_.setEditable(false, true, false);
    nameLabel_.onTextChange = [this] { track_.name = nameLabel_.getText().toStdString(); };

    addAndMakeVisible(channelLabel_);
    channelLabel_.setText("Ch " + juce::String(track_.channel + 1), juce::dontSendNotification);
    channelLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
    channelLabel_.setColour(juce::Label::textColourId, Palette::textSecondary);

    addAndMakeVisible(instrumentBox_);
    instrumentBox_.addItem("(Aucun)", 1);
    auto instruments = availableInstruments();
    int selectedId = 1;
    for (int i = 0; i < static_cast<int>(instruments.size()); ++i) {
        const auto& [pluginId, displayName] = instruments[static_cast<size_t>(i)];
        instrumentBox_.addItem(displayName, i + 2); // id JUCE 1-based, 1 = "(Aucun)"
        if (pluginId == track_.instrumentId) selectedId = i + 2;
    }
    instrumentBox_.setSelectedId(selectedId, juce::dontSendNotification);
    instrumentBox_.onChange = [this, instruments] {
        int idx = instrumentBox_.getSelectedItemIndex();
        std::string pluginId = (idx <= 0 || idx > static_cast<int>(instruments.size()))
                                    ? ""
                                    : instruments[static_cast<size_t>(idx - 1)].first;
        track_.instrumentId = pluginId;
        if (onInstrumentChanged) onInstrumentChanged(index_, pluginId);
    };

    addAndMakeVisible(muteButton_);
    addAndMakeVisible(soloButton_);
    addAndMakeVisible(armButton_);
    muteButton_.setClickingTogglesState(true);
    soloButton_.setClickingTogglesState(true);
    armButton_.setClickingTogglesState(true);
    muteButton_.setColour(juce::TextButton::buttonOnColourId, Palette::accentRed);
    soloButton_.setColour(juce::TextButton::buttonOnColourId, Palette::accentAmber);
    armButton_.setColour(juce::TextButton::buttonOnColourId, Palette::accentRed);

    muteButton_.onClick = [this] { track_.muted = muteButton_.getToggleState(); if (onChanged) onChanged(); };
    soloButton_.onClick = [this] { track_.solo = soloButton_.getToggleState(); if (onChanged) onChanged(); };
    // ARMEMENT (D3.3). `Track::armed` était écrit ici et LU PAR PERSONNE : on
    // pouvait armer une piste, et rien n'arrivait -- d'où un bouton désactivé
    // qui l'avouait. Il agit maintenant sur deux choses à la fois, et c'est
    // voulu : la piste armée reçoit les notes du clavier À L'ÉCOUTE, et les
    // reçoit aussi PAR ÉCRIT pendant une prise. Jouer sur une piste et
    // enregistrer sur une autre n'aurait aucun sens.
    armButton_.setToggleState(track_.armed, juce::dontSendNotification);
    armButton_.onClick = [this] {
        track_.armed = armButton_.getToggleState();
        if (onArmChanged) onArmChanged();
    };
    armButton_.setTooltip("Armer la piste : elle recoit alors le clavier MIDI, "
                           "a l'ecoute comme a l'enregistrement.");

    addAndMakeVisible(volumeSlider_);
    volumeSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    volumeSlider_.setRange(0.0, 1.5, 0.001);
    volumeSlider_.setValue(track_.volume, juce::dontSendNotification);
    volumeSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    volumeSlider_.onValueChange = [this] { track_.volume = static_cast<float>(volumeSlider_.getValue()); if (onChanged) onChanged(); };

    addAndMakeVisible(panSlider_);
    panSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    panSlider_.setRange(-1.0, 1.0, 0.01);
    panSlider_.setValue(track_.pan, juce::dontSendNotification);
    panSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    panSlider_.onValueChange = [this] { track_.pan = static_cast<float>(panSlider_.getValue()); if (onChanged) onChanged(); };

    setInterceptsMouseClicks(true, true);
}

void TrackRowComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds();
    g.setColour(selected_ ? Palette::panelRaised : Palette::panel);
    g.fillRect(bounds);

    // Bandeau de couleur de piste (à gauche), comme sur une console hardware
    g.setColour(juce::Colour(track_.colorRgba));
    g.fillRect(bounds.removeFromLeft(6));

    g.setColour(Palette::border);
    g.drawLine(0.0f, static_cast<float>(getHeight() - 1), static_cast<float>(getWidth()),
               static_cast<float>(getHeight() - 1), 1.0f);
}

void TrackRowComponent::resized() {
    auto area = getLocalBounds().reduced(12, 8);
    area.removeFromLeft(6); // laisse la place au bandeau de couleur peint dans paint()

    auto topRow = area.removeFromTop(22);
    nameLabel_.setBounds(topRow.removeFromLeft(140));
    topRow.removeFromLeft(8);
    channelLabel_.setBounds(topRow.removeFromLeft(50));

    area.removeFromTop(4);
    auto secondRow = area.removeFromTop(24);
    instrumentBox_.setBounds(secondRow.removeFromLeft(170));
    secondRow.removeFromLeft(8);
    muteButton_.setBounds(secondRow.removeFromLeft(28));
    secondRow.removeFromLeft(4);
    soloButton_.setBounds(secondRow.removeFromLeft(28));
    secondRow.removeFromLeft(4);
    armButton_.setBounds(secondRow.removeFromLeft(28));

    area.removeFromTop(6);
    auto thirdRow = area.removeFromTop(20);
    volumeSlider_.setBounds(thirdRow.removeFromLeft(170));
    thirdRow.removeFromLeft(8);
    panSlider_.setBounds(thirdRow.removeFromLeft(90));
}

// ---------------------------------------------------------------------------
// TrackListComponent
// ---------------------------------------------------------------------------

TrackListComponent::TrackListComponent() {
    addAndMakeVisible(viewport_);
    viewport_.setViewedComponent(&rowContainer_, false);
    viewport_.setScrollBarsShown(true, false);

    // Barre d'outils du Track Editor : ajouter / supprimer une piste. Ces
    // deux boutons comblent le manque d'ergonomie identifié -- le modèle et
    // le moteur sont multi-pistes depuis les Phases 1-2, il ne manquait que
    // l'affordance UI pour créer/retirer une piste sans passer par un import.
    addAndMakeVisible(addButton_);
    addAndMakeVisible(removeButton_);
    addButton_.setColour(juce::TextButton::buttonOnColourId, Palette::accentAmber);
    removeButton_.setColour(juce::TextButton::buttonOnColourId, Palette::accentRed);
    addButton_.onClick = [this] { if (onAddTrack) onAddTrack(); };
    removeButton_.onClick = [this] {
        if (project_ != nullptr && !project_->tracks.empty() && onRemoveTrack)
            onRemoveTrack(selectedIndex_);
    };
}

void TrackListComponent::loadProject(Project& project) {
    project_ = &project;
    rows_.clear();
    selectedIndex_ = 0;

    for (size_t i = 0; i < project_->tracks.size(); ++i) {
        auto* row = rows_.add(new TrackRowComponent(project_->tracks[i], i));
        rowContainer_.addAndMakeVisible(row);
        row->onSelected = [this](size_t idx) {
            selectedIndex_ = idx;
            for (int r = 0; r < rows_.size(); ++r)
                rows_[r]->setSelected(static_cast<size_t>(r) == idx);
            if (onTrackSelected) onTrackSelected(idx);
        };
        row->onChanged = [this] { if (onTracksChanged) onTracksChanged(); };
        row->onArmChanged = [this] { if (onArmChanged) onArmChanged(); };
        row->onInstrumentChanged = [this](size_t idx, const std::string& pluginId) {
            if (onInstrumentChanged) onInstrumentChanged(idx, pluginId);
        };
    }
    if (!rows_.isEmpty()) rows_[0]->setSelected(true);
    removeButton_.setEnabled(!rows_.isEmpty());

    resized();
}

void TrackListComponent::selectTrackIndex(size_t idx) {
    if (idx >= static_cast<size_t>(rows_.size())) return;
    selectedIndex_ = idx;
    for (int r = 0; r < rows_.size(); ++r)
        rows_[r]->setSelected(static_cast<size_t>(r) == idx);
    if (onTrackSelected) onTrackSelected(idx);
}

void TrackListComponent::resized() {
    auto area = getLocalBounds();

    auto toolbar = area.removeFromTop(kToolbarHeight).reduced(8, 6);
    removeButton_.setBounds(toolbar.removeFromRight(96));
    toolbar.removeFromRight(6);
    addButton_.setBounds(toolbar);

    viewport_.setBounds(area);
    int totalHeight = rows_.size() * kRowHeight;
    rowContainer_.setBounds(0, 0, viewport_.getWidth() - viewport_.getScrollBarThickness(), totalHeight);

    for (int i = 0; i < rows_.size(); ++i)
        rows_[i]->setBounds(0, i * kRowHeight, rowContainer_.getWidth(), kRowHeight);
}

void TrackListComponent::paint(juce::Graphics& g) {
    g.fillAll(vsm::ui::Palette::panel);
}
