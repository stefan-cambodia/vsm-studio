#pragma once
#include <JuceHeader.h>
#include "LookAndFeel/VsmLookAndFeel.h"
#include "vsm/sequencer/Project.h"
#include "vsm/sequencer/ProjectHistory.h"
#include <functional>
#include <vector>

/// ÉDITEUR DE CONTRÔLEURS MIDI (CC) : l'onglet « MIDI CC » du bas.
///
/// Il n'existait qu'un libellé qui promettait une « vue dédiée » et renvoyait
/// aux lanes du piano roll -- qui n'éditent pas les CC. Le modèle les porte
/// pourtant (`Track::controlChanges`), le séquenceur les joue
/// (PlaybackScheduler), l'import et l'export les conservent : seule la vue
/// manquait. Un projet importé d'un autre DAW avec une courbe de coupure sur
/// CC 74 se jouait sans qu'on puisse la voir, encore moins la corriger.
///
/// Même grammaire que l'éditeur d'automation : une piste, un contrôleur, des
/// points -- clic pour ajouter, glisser pour déplacer, clic droit pour
/// supprimer. Deux différences qui tiennent à ce qu'est un CC : la courbe est
/// en PALIERS (un CC vaut jusqu'au suivant, il n'interpole pas) et la valeur
/// va de 0 à 127. Chaque édition passe par l'historique du projet (Ctrl+Z
/// la défait comme une note) et republie le projet au séquenceur.
class MidiCcComponent : public juce::Component {
public:
    MidiCcComponent();

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    /// (Re)lit les pistes depuis le projet ; garde la piste et le contrôleur choisis.
    void setProject(vsm::sequencer::Project* project);
    void setHistory(vsm::sequencer::ProjectHistory* history) { history_ = history; }
    /// Suit la piste choisie dans la liste de pistes, comme l'onglet Effets.
    void setActiveTrackIndex(size_t trackIndex);

    /// Émis après chaque édition : le projet a changé, le séquenceur doit le relire.
    std::function<void()> onCcEdited;

    /// Le nom usuel d'un contrôleur (« 74 · coupure »), ou « CC n » sinon.
    static juce::String controllerName(int controller);
    /// LE PITCH BEND ET L'AFTERTOUCH DE CANAL SONT DES LANES COMME LES AUTRES
    /// (D11, 03/09/2026). Le format les portait (`Track::pitchBends`,
    /// `Track::channelPressure`), le moteur les jouait, et aucune vue ne les
    /// montrait : un bend importé ou joué en direct était invisible et
    /// incorrigible. Deux pseudo-contrôleurs hors de 0..127 les désignent ;
    /// la lane dessine le bend à 7 bits (le centre à 64), et un bend
    /// enregistré garde ses 14 bits tant qu'on ne touche pas la lane.
    static constexpr int kPitchBend = 128;
    static constexpr int kChannelPressure = 129;

private:
    void rebuildTrackBox();
    void rebuildControllerBox();
    void loadPoints();      // remplit points_ depuis la piste et le contrôleur choisis
    void commit(const juce::String& label);   // réécrit la piste, historique, notification

    juce::Rectangle<int> editorArea() const;
    int   tickToX(vsm::midi::Tick tick) const;
    vsm::midi::Tick xToTick(int x) const;
    int   valueToY(int value) const;
    int   yToValue(int y) const;
    int   findPointNear(juce::Point<int> p) const;
    vsm::sequencer::Track* activeTrack() const;

    vsm::sequencer::Project* project_ = nullptr;
    vsm::sequencer::ProjectHistory* history_ = nullptr;

    juce::Label trackLabel_, controllerLabel_, hintLabel_;
    juce::ComboBox trackBox_, controllerBox_;

    struct Point { vsm::midi::Tick tick; int value; };
    std::vector<Point> points_;
    std::vector<int> controllerIds_;   // parallèle aux items de controllerBox_

    size_t selectedTrack_ = 0;
    int selectedController_ = 74;
    int dragIndex_ = -1;
    bool dragged_ = false;
    vsm::midi::Tick maxTick_ = 1920 * 4;

    static constexpr int kPointRadius = 5;
};
