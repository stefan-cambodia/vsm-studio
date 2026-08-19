#pragma once
#include <JuceHeader.h>
#include "vsm/panels/MachinePanel.h"
#include "vsm/sequencer/StepPattern.h"
#include "vsm/sequencer/Track.h"
#include <functional>

/// Grille de pas d'une machine : les seize boutons d'une boîte à rythmes, ou
/// la ligne unique d'un TB-303.
///
/// La grille est une VUE sur les notes de la piste : allumer un pas écrit une
/// note, et une note dessinée dans le piano roll rallume son pas. Il n'existe
/// pas de "motif" stocké à côté du morceau -- deux vérités finiraient par
/// diverger, et personne ne saurait laquelle joue (voir StepPattern.h).
///
/// Gestes, calqués sur ce que ces machines permettent réellement :
///   clic          : allumer / éteindre le pas
///   Maj + clic    : accent (leur seule nuance)
///   Alt + clic    : slide (TB-303 uniquement)
///   molette       : hauteur du pas (motif mélodique)
class StepSequencerComponent : public juce::Component {
public:
    StepSequencerComponent();

    void configure(const vsm::panels::SequencerSpec& spec, vsm::sequencer::Track* track,
                    juce::Colour panelColour, juce::Colour textColour);

    /// Position de lecture, pour éclairer le pas en cours (-1 = à l'arrêt).
    void setPlayheadStep(int step);

    /// Une édition a modifié les notes de la piste : l'application doit
    /// reconstruire le planning de lecture.
    std::function<void()> onPatternEdited;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct Hit { int lane = -1; int step = -1; };
    /// Nommé `stepAt` et non `hitTest` : ce dernier est une méthode virtuelle
    /// de juce::Component (test d'appartenance d'un point au composant), que
    /// l'on masquerait sans le vouloir.
    Hit stepAt(juce::Point<float> position) const;
    juce::Rectangle<float> stepBounds(int lane, int step) const;
    void reloadFromTrack();
    void commitToTrack();

    vsm::panels::SequencerSpec spec_;
    vsm::sequencer::Track* track_ = nullptr;
    vsm::sequencer::StepPattern pattern_;
    juce::Colour panelColour_ { juce::Colours::darkgrey };
    juce::Colour textColour_ { juce::Colours::white };
    int playheadStep_ = -1;
    static constexpr int kLaneLabelWidth = 92;
};
