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
    /// `sourceName` : le nom de la piste dont celle-ci publie une sortie
    /// (D18.7b), vide sinon. Passé plutôt que déduit, pour la même raison que
    /// `groupes` -- la rangée n'a pas besoin du projet entier pour dire ce
    /// qu'elle porte.
    TrackRowComponent(vsm::sequencer::Track& track, size_t trackIndex,
                       const std::vector<std::pair<int, std::string>>& groupes,
                       const juce::String& sourceName = {});

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
    /// D19.4 : le repli d'un DOSSIER. Présent sur les seules pistes dossier,
    /// et c'est lui qui range ou déploie tout ce qu'elles contiennent.
    juce::TextButton folderButton_ { "" };
    juce::TextButton armButton_  { "R" };
    juce::Slider volumeSlider_;
    juce::Slider panSlider_;
};

/// Liste verticale de pistes (Track Editor, section 4). Reconstruit ses
/// lignes à partir du Project quand loadProject() est appelé (ex : après
/// un import MIDI).
class TrackListComponent : public juce::Component,
                            public juce::DragAndDropTarget {
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

    /// D10.1 : QUELQUE CHOSE A ÉTÉ LÂCHÉ SUR UNE PISTE. La description vient du
    /// navigateur (`BrowserComponent`) ; la liste ne l'interprète pas, elle dit
    /// seulement SUR QUELLE PISTE. Lui faire charger un preset la rendrait
    /// dépendante de l'interop, et une liste de pistes n'a pas à savoir ce
    /// qu'est un `*.synth.json`.
    std::function<void(size_t, const juce::String&)> onBrowserItemDropped;

    // juce::DragAndDropTarget
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

    size_t selectedTrackIndex() const { return selectedIndex_; }

    /// Sélectionne une piste par index (met à jour l'état visuel et notifie
    /// via onTrackSelected). Sans effet si l'index est hors bornes.
    void selectTrackIndex(size_t idx);
    /// Fait défiler la liste juste assez pour montrer la piste `idx` entière.
    void faireVoirLaPiste(size_t idx);
    int aMontrer_ = -1;   ///< piste à faire voir dès que la liste aura une hauteur

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
    /// D19.2 : LE FILTRE DE LA LISTE. Un état de SÉANCE et non du morceau —
    /// il n'est écrit nulle part, et rouvrir un projet ne cache jamais une
    /// piste. C'est précisément ce qui le distingue de `Track::hidden`
    /// (D17.4), lequel appartient au morceau et se sauvegarde.
    juce::TextEditor filterBox_;
    /// D19.2 : PANNE MUETTE INTERDITE, jusque dans une liste vide. Un filtre
    /// qui ne trouve rien laisse un panneau vierge, et un panneau vierge
    /// ressemble à des pistes supprimées. Il dit donc pourquoi il est vide.
    juce::Label emptyLabel_;
    /// Vrai quand le filtre est posé et que le nom de la piste ne lui répond
    /// pas. N'a AUCUN effet sur le son : la piste continue de jouer, elle
    /// n'est simplement plus dans la liste — masquer une piste et la taire
    /// sont deux gestes différents, et les confondre ferait disparaître un
    /// instrument d'un mélange pour avoir cherché son voisin.
    bool masqueeParLeFiltre(size_t index) const;
public:
    /// D19.2 : pose le filtre sans souris, pour que la capture d'écran puisse
    /// le MONTRER À L'ŒUVRE et pas seulement montrer un champ vide. Même
    /// raison d'être que `VSM_VUE` : sous Wayland, une interface qu'on ne peut
    /// pas piloter sans souris est une interface qu'on ne peut pas juger.
    void setFilterText(const juce::String& texte) {
        filterBox_.setText(texte, juce::dontSendNotification);
        resized();
        repaint();
    }
private:
    size_t selectedIndex_ = 0;
    /// La piste survolée pendant un glisser, ou -1. Sans ce retour, on lâche à
    /// l'aveugle et on découvre après coup sur laquelle.
    int dropRow_ = -1;
    /// L'index de piste sous un point de la liste, ou -1.
    int trackIndexAt(juce::Point<int> position) const;

    static constexpr int kRowHeight = 88;
    static constexpr int kToolbarHeight = 36;
    /// D19.2 : la ligne du filtre, sous la barre d'outils.
    static constexpr int kFilterHeight = 30;
};
