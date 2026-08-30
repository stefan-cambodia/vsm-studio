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

TrackRowComponent::TrackRowComponent(Track& track, size_t trackIndex,
                                      const std::vector<std::pair<int, std::string>>& groupes)
    : track_(track), index_(trackIndex), audio_(track.kind == Track::Kind::Audio) {
    addAndMakeVisible(nameLabel_);
    nameLabel_.setText(track_.name.empty() ? ("Piste " + std::to_string(trackIndex + 1)) : track_.name,
                        juce::dontSendNotification);
    nameLabel_.setEditable(false, true, false);
    nameLabel_.onTextChange = [this] { track_.name = nameLabel_.getText().toStdString(); };

    addAndMakeVisible(channelLabel_);
    channelLabel_.setText(audio_ ? juce::String("Audio")
                                  : juce::String("Ch " + juce::String(track_.channel + 1)),
                           juce::dontSendNotification);
    channelLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
    channelLabel_.setColour(juce::Label::textColourId, Palette::textSecondary);

    // UNE PISTE AUDIO N'A PAS D'INSTRUMENT, et lui présenter un sélecteur de
    // machine serait lui promettre un choix sans effet : son matériau est un
    // fichier, pas des notes. Elle affiche donc ce fichier -- ou le fait qu'elle
    // n'en a pas encore, ce qui est exactement ce qu'on a besoin de savoir avant
    // d'appuyer sur Rec.
    if (audio_) {
        addAndMakeVisible(audioSourceLabel_);
        audioSourceLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
        audioSourceLabel_.setColour(juce::Label::textColourId, Palette::textSecondary);
        refreshAudioSource();
    } else {
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
    }

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
    armButton_.setTooltip(
        audio_ ? "Armer la piste : la prochaine prise ecrit l'entree audio dans un "
                 "fichier du dossier du projet. Une seule piste audio a la fois."
               : "Armer la piste : elle recoit alors le clavier MIDI, "
                 "a l'ecoute comme a l'enregistrement.");

    // OÙ VA CETTE PISTE (D4.2). Un groupe, lui, va toujours au master : les
    // groupes imbriqués demanderaient un ordre topologique pour un besoin que
    // rien n'a exprimé, et proposer le choix laisserait croire le contraire.
    if (track_.kind != Track::Kind::Group) {
        addAndMakeVisible(outputBox_);
        outputBox_.addItem("-> Master", 1);
        int selection = 1;
        for (size_t i = 0; i < groupes.size(); ++i) {
            outputBox_.addItem("-> " + juce::String(groupes[i].second), static_cast<int>(i) + 2);
            if (groupes[i].first == track_.outputGroup) selection = static_cast<int>(i) + 2;
        }
        outputBox_.setSelectedId(selection, juce::dontSendNotification);
        outputBox_.setTooltip("Ou va cette piste : le master, ou un groupe.");
        outputBox_.onChange = [this, groupes] {
            const int choix = outputBox_.getSelectedItemIndex();
            track_.outputGroup = (choix <= 0 || choix > static_cast<int>(groupes.size()))
                                     ? -1
                                     : groupes[static_cast<size_t>(choix - 1)].first;
            if (onOutputChanged) onOutputChanged();
        };
    }

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

void TrackRowComponent::refreshAudioSource() {
    if (!audio_) return;
    const juce::String chemin(track_.audio.path);
    audioSourceLabel_.setText(
        chemin.isEmpty() ? juce::String(u8"(aucun fichier — armer et enregistrer)")
                         : chemin.fromLastOccurrenceOf("/", false, false),
        juce::dontSendNotification);
    audioSourceLabel_.setTooltip(chemin.isEmpty()
                                     ? juce::String(u8"Cette piste audio n'a pas encore de matériau.")
                                     : chemin);
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
    if (audio_) audioSourceLabel_.setBounds(secondRow.removeFromLeft(170));
    else        instrumentBox_.setBounds(secondRow.removeFromLeft(170));
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
    thirdRow.removeFromLeft(8);
    if (track_.kind != Track::Kind::Group) outputBox_.setBounds(thirdRow.removeFromLeft(130));
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

    // La liste des groupes, calculée UNE fois : chaque ligne la reçoit pour
    // remplir son sélecteur de sortie.
    std::vector<std::pair<int, std::string>> groupes;
    for (size_t i = 0; i < project_->tracks.size(); ++i)
        if (project_->tracks[i].kind == Track::Kind::Group)
            groupes.emplace_back(static_cast<int>(i),
                                  project_->tracks[i].name.empty()
                                      ? "Groupe " + std::to_string(i + 1)
                                      : project_->tracks[i].name);

    for (size_t i = 0; i < project_->tracks.size(); ++i) {
        auto* row = rows_.add(new TrackRowComponent(project_->tracks[i], i, groupes));
        rowContainer_.addAndMakeVisible(row);
        row->onSelected = [this](size_t idx) {
            selectedIndex_ = idx;
            for (int r = 0; r < rows_.size(); ++r)
                rows_[r]->setSelected(static_cast<size_t>(r) == idx);
            if (onTrackSelected) onTrackSelected(idx);
        };
        row->onChanged = [this] { if (onTracksChanged) onTracksChanged(); };
        row->onArmChanged = [this] { if (onArmChanged) onArmChanged(); };
        row->onOutputChanged = [this] { if (onOutputChanged) onOutputChanged(); };
        row->onInstrumentChanged = [this](size_t idx, const std::string& pluginId) {
            if (onInstrumentChanged) onInstrumentChanged(idx, pluginId);
        };
    }
    if (!rows_.isEmpty()) rows_[0]->setSelected(true);
    removeButton_.setEnabled(!rows_.isEmpty());

    resized();
}

void TrackListComponent::refreshTrackRow(size_t idx) {
    if (idx >= static_cast<size_t>(rows_.size())) return;
    rows_[static_cast<int>(idx)]->refreshAudioSource();
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

    // LA PISTE SURVOLÉE PENDANT UN GLISSER (D10.1). Sans ce retour, on lâche à
    // l'aveugle et on découvre après coup sur laquelle -- ce qui, pour un
    // preset, veut dire qu'on vient de changer le son de la mauvaise.
    if (dropRow_ >= 0 && dropRow_ < rows_.size()) {
        auto zone = rows_[dropRow_]->getBounds()
                        .translated(viewport_.getX(), viewport_.getY() - viewport_.getViewPositionY());
        g.setColour(juce::Colours::gold.withAlpha(0.25f));
        g.fillRect(zone);
        g.setColour(juce::Colours::gold);
        g.drawRect(zone, 2);
    }
}

// --- D10.1 : recevoir ce que le navigateur laisse tomber --------------------

int TrackListComponent::trackIndexAt(juce::Point<int> position) const {
    // La position est relative à CE composant ; les lignes vivent dans le
    // conteneur du `Viewport`, qui a son propre défilement.
    const auto dansConteneur = position - viewport_.getPosition()
                               + juce::Point<int>(0, viewport_.getViewPositionY());
    for (int i = 0; i < rows_.size(); ++i)
        if (rows_[i]->getBounds().contains(dansConteneur)) return i;
    return -1;
}

bool TrackListComponent::isInterestedInDragSource(const SourceDetails& details) {
    // Seul le navigateur produit ces descriptions. Accepter n'importe quoi
    // ferait clignoter la liste sous des glissers qui ne la concernent pas.
    return details.description.toString().startsWith("vsm-browser:");
}

void TrackListComponent::itemDragEnter(const SourceDetails& details) { itemDragMove(details); }

void TrackListComponent::itemDragMove(const SourceDetails& details) {
    const int rang = trackIndexAt(details.localPosition);
    if (rang == dropRow_) return;
    dropRow_ = rang;
    repaint();
}

void TrackListComponent::itemDragExit(const SourceDetails&) {
    dropRow_ = -1;
    repaint();
}

void TrackListComponent::itemDropped(const SourceDetails& details) {
    const int rang = trackIndexAt(details.localPosition);
    dropRow_ = -1;
    repaint();
    if (rang < 0) return;
    selectTrackIndex(static_cast<size_t>(rang));
    if (onBrowserItemDropped) onBrowserItemDropped(static_cast<size_t>(rang),
                                                    details.description.toString());
}
