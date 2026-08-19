#include "PianoRollRulerComponent.h"
#include "LookAndFeel/VsmLookAndFeel.h"

using namespace vsm::midi;
using namespace vsm::ui;

PianoRollRulerComponent::PianoRollRulerComponent(PianoRollComponent& pianoRoll)
    : pianoRoll_(pianoRoll) {}

void PianoRollRulerComponent::setLoopRegion(Tick start, Tick end, bool active) {
    loopStart_ = start;
    loopEnd_ = end;
    loopActive_ = active;
    repaint();
}

void PianoRollRulerComponent::paint(juce::Graphics& g) {
    g.fillAll(Palette::panel);
    const auto bounds = getLocalBounds();

    if (loopActive_ && loopEnd_ > loopStart_) {
        const float x1 = pianoRoll_.tickToX(loopStart_);
        const float x2 = pianoRoll_.tickToX(loopEnd_);
        g.setColour(Palette::accentTeal.withAlpha(0.30f));
        g.fillRect(juce::Rectangle<float>(x1, 0.0f, x2 - x1, static_cast<float>(bounds.getHeight())));
    }

    // Graduations : une par mesure, avec son numéro ; les temps n'apparaissent
    // que si le zoom laisse la place de les distinguer.
    const Tick startTick = std::max<Tick>(0, pianoRoll_.xToTick(static_cast<float>(pianoRoll_.keyboardWidth())));
    const Tick endTick = pianoRoll_.xToTick(static_cast<float>(bounds.getWidth()));
    const Tick barTicks = pianoRoll_.ticksPerBarAt(startTick);
    const Tick beatTicks = pianoRoll_.ticksPerBeat();
    const double pxPerTick = pianoRoll_.pixelsPerTick();

    if (beatTicks * pxPerTick > 12.0) {
        g.setColour(Palette::gridLine);
        for (Tick t = (startTick / beatTicks) * beatTicks; t <= endTick; t += beatTicks) {
            if (t % barTicks == 0) continue;
            const float x = pianoRoll_.tickToX(t);
            g.drawLine(x, static_cast<float>(bounds.getHeight()) * 0.6f, x, static_cast<float>(bounds.getHeight()), 1.0f);
        }
    }

    g.setFont(11.0f);
    for (Tick t = (startTick / barTicks) * barTicks; t <= endTick; t += barTicks) {
        const float x = pianoRoll_.tickToX(t);
        g.setColour(Palette::gridLineStrong);
        g.drawLine(x, 0.0f, x, static_cast<float>(bounds.getHeight()), 1.2f);
        g.setColour(Palette::textSecondary);
        g.drawText(juce::String(static_cast<int>(t / barTicks) + 1),
                    static_cast<int>(x) + 3, 1, 40, bounds.getHeight() - 2,
                    juce::Justification::centredLeft, false);
    }

    const float playX = pianoRoll_.tickToX(pianoRoll_.playheadTick());
    if (playX >= static_cast<float>(pianoRoll_.keyboardWidth())) {
        g.setColour(Palette::accentAmber);
        g.drawLine(playX, 0.0f, playX, static_cast<float>(bounds.getHeight()), 1.5f);
        // Petit triangle : la tête de lecture reste repérable même immobile
        // au milieu de graduations.
        juce::Path marker;
        marker.addTriangle(playX - 5.0f, 0.0f, playX + 5.0f, 0.0f, playX, 8.0f);
        g.fillPath(marker);
    }

    g.setColour(Palette::border);
    g.drawLine(0.0f, static_cast<float>(bounds.getHeight()) - 0.5f,
                static_cast<float>(bounds.getWidth()), static_cast<float>(bounds.getHeight()) - 0.5f, 1.0f);
}

void PianoRollRulerComponent::mouseDown(const juce::MouseEvent& event) {
    if (event.position.x < static_cast<float>(pianoRoll_.keyboardWidth())) return;
    const Tick tick = std::max<Tick>(0, pianoRoll_.xToTick(event.position.x));

    if (event.mods.isShiftDown()) {
        loopDragAnchor_ = tick;
        loopStart_ = loopEnd_ = tick;
        loopActive_ = true;
        if (onLoopRegionChanged) onLoopRegionChanged(loopStart_, loopEnd_, loopActive_);
        repaint();
        return;
    }
    if (onPlayheadRequested) onPlayheadRequested(tick);
}

void PianoRollRulerComponent::mouseDrag(const juce::MouseEvent& event) {
    if (event.position.x < static_cast<float>(pianoRoll_.keyboardWidth())) return;
    const Tick tick = std::max<Tick>(0, pianoRoll_.xToTick(event.position.x));

    if (event.mods.isShiftDown()) {
        loopStart_ = std::min(loopDragAnchor_, tick);
        loopEnd_ = std::max(loopDragAnchor_, tick);
        if (onLoopRegionChanged) onLoopRegionChanged(loopStart_, loopEnd_, loopActive_);
        repaint();
        return;
    }
    if (onPlayheadRequested) onPlayheadRequested(tick);
}

void PianoRollRulerComponent::mouseDoubleClick(const juce::MouseEvent&) {
    loopActive_ = false;
    if (onLoopRegionChanged) onLoopRegionChanged(loopStart_, loopEnd_, false);
    repaint();
}
