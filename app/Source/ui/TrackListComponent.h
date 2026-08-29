#pragma once
#include <JuceHeader.h>
#include "vsm/sequencer/Project.h"
#include <functional>
#include <string>
#include <utility>
#include <vector>

// Ligne représentant une piste. Volontairement "bête" : elle lit/écrit
// directement les champs de vsm::sequencer::Track qu'on lui passe, et
// notifie le parent des changements via des callbacks — aucune logique de
// mixage réelle ici (ça viendra avec le Mixer / AudioEngine en Phase 2).
class TrackRowComponent : public juce::Component {
public:
    /// `groupes` donne, pour chaque piste de groupe du projet, son index et son
    /// nom : c'est ce que le sélecteur de sortie propose. Passé de l'extérieur
    /// parce qu'une ligne ne connaît que SA piste -- lui donner le projet
    /// entier pour lire la liste des groupes serait lui donner de quoi tout
    /// modifier.
    TrackRowComponent(vsm::sequencer::Track& track, size_t trackIndex,
                       const std::vector<std::pair<int, std::string>>& groupes);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override { if (onSelected) onSelected(index_); }

    std::function<void(size_t)> onSelected;
    std::function<void()> onChanged; // mute/solo/volume/pan modifiés -> reconstruire le scheduler
    /// L'armement a changé. SÉPARÉ de `onChanged` : armer ne touche ni au
    /// planning de lecture ni au mixage, et republier le projet au moteur pour
    /// un bouton d'armement couperait le son à chaque clic.
    std::function<void()> onArmChanged;
    /// La sortie de la piste a changé (master ou groupe) : le moteur doit
    /// republier le projet pour que le routage prenne effet.
    std::function<void()> onOutputChanged;
    std::function<void(size_t, const std::string&)> onInstrumentChanged; // trackIndex, pluginId ("" = aucun)

    void setSelected(bool selected) { selected_ = selected; repaint(); }
    /// Réaffiche le fichier de la piste (sans effet sur une piste MIDI).
    /// Appelée après une prise audio, qui vient de lui en donner un.
    void refreshAudioSource();

private:
    vsm::sequencer::Track& track_;
    size_t index_;
    bool selected_ = false;
    /// Figé à la construction : la nature d'une piste ne change pas en cours de
    /// route, et la ligne est reconstruite si le projet change.
    const bool audio_;

    juce::Label nameLabel_;
    juce::Label channelLabel_;
    juce::ComboBox instrumentBox_; // rempli depuis PluginRegistry::listAvailable()
    juce::Label audioSourceLabel_; // à sa place, sur une piste audio
    juce::ComboBox outputBox_;     // master ou groupe (D4.2)
    juce::TextButton muteButton_ { "M" };
    juce::TextButton soloButton_ { "S" };
    juce::TextButton armButton_  { "R" };
    juce::Slider volumeSlider_;
    juce::Slider panSlider_;
};

/// Liste verticale de pistes (Track Editor, section 4). Reconstruit ses
/// lignes à partir du Project quand loadProject() est appelé (ex : après
/// un import MIDI).
class TrackListComponent : public juce::Component {
public:
    TrackListComponent();

    void loadProject(vsm::sequencer::Project& project);
    void resized() override;
    void paint(juce::Graphics&) override;

    std::function<void(size_t)> onTrackSelected;
    std::function<void()> onTracksChanged;
    std::function<void(size_t, const std::string&)> onInstrumentChanged;
    /// L'armement d'une piste a changé (voir TrackRowComponent::onArmChanged).
    std::function<void()> onArmChanged;
    /// La sortie d'une piste a changé (voir TrackRowComponent::onOutputChanged).
    std::function<void()> onOutputChanged;
    std::function<void()> onAddTrack;          // bouton "+ Ajouter une piste"
    std::function<void(size_t)> onRemoveTrack; // bouton "Supprimer" (piste sélectionnée)

    size_t selectedTrackIndex() const { return selectedIndex_; }

    /// Sélectionne une piste par index (met à jour l'état visuel et notifie
    /// via onTrackSelected). Sans effet si l'index est hors bornes.
    void selectTrackIndex(size_t idx);

    /// Réaffiche une seule ligne, sans reconstruire la liste -- reconstruire
    /// remettrait la sélection et le défilement à zéro. Sert après une prise
    /// audio, qui vient de donner un fichier à sa piste.
    void refreshTrackRow(size_t idx);

private:
    vsm::sequencer::Project* project_ = nullptr;
    juce::OwnedArray<TrackRowComponent> rows_;
    juce::Viewport viewport_;
    juce::Component rowContainer_;
    juce::TextButton addButton_ { "+ Ajouter une piste" };
    juce::TextButton removeButton_ { "Supprimer" };
    size_t selectedIndex_ = 0;

    static constexpr int kRowHeight = 88;
    static constexpr int kToolbarHeight = 36;
};
