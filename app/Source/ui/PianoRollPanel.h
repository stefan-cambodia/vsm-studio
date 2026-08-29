#pragma once
#include <JuceHeader.h>
#include "PianoRollComponent.h"
#include "PianoRollRulerComponent.h"
#include "PianoRollToolbar.h"
#include "VelocityLaneComponent.h"
#include "LookAndFeel/VsmLookAndFeel.h"

/// Assemble l'éditeur complet dans une seule fenêtre flottante : barre
/// d'outils, règle temporelle, piano roll, lane de vélocité, barre d'état.
///
/// Le panneau est aussi le point de raccordement des callbacks entre ces
/// composants (la règle déplace la tête de lecture du piano roll, une édition
/// de vélocité repeint le roll, etc.) : chacun reste ignorant des autres,
/// conformément à la façon dont le reste de l'application est câblée.
class PianoRollPanel : public juce::Component {
public:
    PianoRollPanel(PianoRollComponent& pianoRoll, VelocityLaneComponent& velocityLane)
        : pianoRoll_(pianoRoll), velocityLane_(velocityLane),
          toolbar_(pianoRoll), ruler_(pianoRoll) {
        addAndMakeVisible(toolbar_);
        addAndMakeVisible(ruler_);
        addAndMakeVisible(pianoRoll_);
        addAndMakeVisible(velocityLane_);
        addAndMakeVisible(statusLabel_);

        statusLabel_.setColour(juce::Label::textColourId, vsm::ui::Palette::textSecondary);
        statusLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
        statusLabel_.setText(u8"Prêt", juce::dontSendNotification);

        pianoRoll_.onStatusChanged = [this](const juce::String& text) {
            statusLabel_.setText(text, juce::dontSendNotification);
        };
        pianoRoll_.onEditStateChanged = [this] { toolbar_.refreshFromPianoRoll(); };

        ruler_.onPlayheadRequested = [this](vsm::midi::Tick tick) {
            if (pianoRoll_.onPlayheadRequested) pianoRoll_.onPlayheadRequested(tick);
            pianoRoll_.setPlayheadTick(tick);
            ruler_.repaint();
        };
        ruler_.onLoopRegionChanged = [this](vsm::midi::Tick start, vsm::midi::Tick end, bool active) {
            pianoRoll_.setLoopRegion(start, end, active);
            if (pianoRoll_.onLoopRegionChanged) pianoRoll_.onLoopRegionChanged(start, end, active);
        };
        ruler_.onPunchRegionChanged = [this](vsm::midi::Tick start, vsm::midi::Tick end, bool active) {
            if (pianoRoll_.onPunchRegionChanged) pianoRoll_.onPunchRegionChanged(start, end, active);
        };
        ruler_.onMarkerRequested = [this](vsm::midi::Tick tick) {
            if (onMarkerRequested) onMarkerRequested(tick);
        };
        ruler_.onMarkerRemoved = [this](size_t index) {
            if (onMarkerRemoved) onMarkerRemoved(index);
        };
        velocityLane_.onVelocityEdited = [this] {
            pianoRoll_.repaint();
            if (onVelocityEdited) onVelocityEdited();
        };
    }

    /// Rafraîchit règle et barre d'outils (appelé quand la tête de lecture
    /// bouge ou qu'un projet est chargé).
    void refresh() {
        ruler_.repaint();
        toolbar_.refreshFromPianoRoll();
    }

    /// La région de punch appartient au PROJET : la règle la dessine, mais c'est
    /// l'application qui la détient. Voir `vsm::sequencer::Project::punchEnabled`.
    void setPunchRegion(vsm::midi::Tick start, vsm::midi::Tick end, bool active) {
        ruler_.setPunchRegion(start, end, active);
    }

    std::function<void()> onVelocityEdited;
    /// Poser un repère à ce tick (l'application demande son nom), ou retirer
    /// celui d'index donné.
    std::function<void(vsm::midi::Tick)> onMarkerRequested;
    std::function<void(size_t)> onMarkerRemoved;

    void resized() override {
        auto area = getLocalBounds();
        toolbar_.setBounds(area.removeFromTop(64));
        statusLabel_.setBounds(area.removeFromBottom(20).reduced(8, 0));
        velocityLane_.setBounds(area.removeFromBottom(110));
        ruler_.setBounds(area.removeFromTop(22));
        pianoRoll_.setBounds(area);
    }

    void paint(juce::Graphics& g) override { g.fillAll(vsm::ui::Palette::background); }

private:
    PianoRollComponent& pianoRoll_;
    VelocityLaneComponent& velocityLane_;
    PianoRollToolbar toolbar_;
    PianoRollRulerComponent ruler_;
    juce::Label statusLabel_;
};
