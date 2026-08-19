#pragma once
#include <JuceHeader.h>
#include "PianoRollComponent.h"

/// Règle temporelle du piano roll : numéros de mesure, tête de lecture,
/// région de boucle. Partage les conversions tick <-> pixel du piano roll
/// (tickToX/keyboardWidth), donc tout reste aligné à la colonne près quel que
/// soit le zoom -- une règle qui aurait son propre calcul finirait toujours
/// par dériver d'un pixel ou deux.
///
/// Interactions : clic (ou glissé) = déplacer la tête de lecture ;
/// Maj + glissé = définir la région de boucle ; double-clic = désactiver la
/// boucle.
class PianoRollRulerComponent : public juce::Component {
public:
    explicit PianoRollRulerComponent(PianoRollComponent& pianoRoll);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

    void setLoopRegion(vsm::midi::Tick start, vsm::midi::Tick end, bool active);

    std::function<void(vsm::midi::Tick)> onPlayheadRequested;
    std::function<void(vsm::midi::Tick start, vsm::midi::Tick end, bool active)> onLoopRegionChanged;

private:
    PianoRollComponent& pianoRoll_;
    vsm::midi::Tick loopStart_ = 0, loopEnd_ = 0;
    bool loopActive_ = false;
    vsm::midi::Tick loopDragAnchor_ = 0;
};
