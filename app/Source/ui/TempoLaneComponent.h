#pragma once
#include <JuceHeader.h>
#include "LookAndFeel/VsmLookAndFeel.h"
#include "vsm/sequencer/Project.h"
#include "vsm/sequencer/ProjectHistory.h"
#include <functional>
#include <vector>

/// LA PISTE DE TEMPO DESSINÉE : l'onglet « Tempo » du bas.
///
/// D3.2 la promettait ; le tempo est resté une valeur unique, écrite dans la
/// barre de transport, alors que le modèle porte une carte de tempo
/// (`TempoMap`), que le moteur la suit à chaque bloc (`bpmAt(tick)`) et que
/// l'import d'un `.als` ou d'un SMF la remplit. Un morceau qui ralentit à la
/// coda se jouait donc juste et ne pouvait ni se voir ni se corriger.
///
/// Même grammaire que l'éditeur de CC : des points (tick, BPM) en PALIERS --
/// un tempo vaut jusqu'au suivant --, clic pour ajouter, glisser pour
/// déplacer, clic droit pour supprimer. Le point au tick 0 existe toujours
/// (c'est le tempo de départ) : il se déplace en valeur, pas dans le temps, et
/// ne se supprime pas. Chaque geste passe par l'historique et republie le
/// projet au moteur.
class TempoLaneComponent : public juce::Component {
public:
    TempoLaneComponent();

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

    void setProject(vsm::sequencer::Project* project);
    void setHistory(vsm::sequencer::ProjectHistory* history) { history_ = history; }
    /// Relit la carte de tempo (après un changement venu d'ailleurs : transport, annuler).
    void refresh();

    /// Émis après chaque édition : la carte de tempo a changé.
    std::function<void()> onTempoEdited;

    static constexpr double kBpmMin = 40.0, kBpmMax = 240.0;

private:
    void loadPoints();
    void commit(const juce::String& label);

    juce::Rectangle<int> editorArea() const;
    int   tickToX(vsm::midi::Tick tick) const;
    vsm::midi::Tick xToTick(int x) const;
    int   bpmToY(double bpm) const;
    double yToBpm(int y) const;
    int   findPointNear(juce::Point<int> p) const;

    vsm::sequencer::Project* project_ = nullptr;
    vsm::sequencer::ProjectHistory* history_ = nullptr;

    juce::Label titleLabel_, hintLabel_;

    struct Point { vsm::midi::Tick tick; double bpm; bool ramp = false; };
    void toggleRamp(size_t index);
    std::vector<Point> points_;
    int dragIndex_ = -1;
    bool dragged_ = false;
    vsm::midi::Tick maxTick_ = 1920 * 4;

    static constexpr int kPointRadius = 5;
};
