#pragma once
#include <JuceHeader.h>
#include "PianoRollComponent.h"

/// Lane de vélocité : une barre par note de la piste active, alignée
/// horizontalement avec le piano roll (mêmes tickToX/keyboardWidth, donc
/// chaque barre tombe exactement sous sa note quel que soit le zoom).
///
/// Trois gestes, qui couvrent l'essentiel du travail sur les nuances :
///  - clic-glissé vertical sur une barre : régler la vélocité de cette note ;
///  - balayage horizontal : "peindre" les vélocités des notes survolées ;
///  - Maj + glissé : tracer une DROITE entre le point de départ et le point
///    courant, c'est-à-dire un crescendo/decrescendo régulier.
///
/// Quand des notes sont sélectionnées dans le piano roll, seules celles-là
/// sont modifiables : c'est ce qui permet de travailler un accord au milieu
/// d'un passage dense sans toucher au reste.
class VelocityLaneComponent : public juce::Component {
public:
    explicit VelocityLaneComponent(PianoRollComponent& pianoRoll);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    std::function<void()> onVelocityEdited;

private:
    uint8_t yToVelocity(float y) const;
    void paintVelocityAt(juce::Point<float> pos, bool isFirstEvent);
    void applyVelocityLine(juce::Point<float> current);
    bool isEditable(const vsm::sequencer::Note& note) const;

    PianoRollComponent& pianoRoll_;
    juce::Point<float> dragStart_;
    bool lineMode_ = false;
};
