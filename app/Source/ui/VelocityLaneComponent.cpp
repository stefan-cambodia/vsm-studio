#include "VelocityLaneComponent.h"
#include "LookAndFeel/VsmLookAndFeel.h"
#include <algorithm>

using namespace vsm::sequencer;
using namespace vsm::ui;

VelocityLaneComponent::VelocityLaneComponent(PianoRollComponent& pianoRoll) : pianoRoll_(pianoRoll) {
    setOpaque(true);
}

uint8_t VelocityLaneComponent::yToVelocity(float y) const {
    const float usable = static_cast<float>(getHeight()) - 8.0f;
    const float ratio = 1.0f - juce::jlimit(0.0f, 1.0f, (y - 4.0f) / std::max(1.0f, usable));
    return static_cast<uint8_t>(juce::jlimit(1, 127, static_cast<int>(std::lround(ratio * 127.0f))));
}

bool VelocityLaneComponent::isEditable(const Note& note) const {
    const auto& selection = pianoRoll_.selectedNoteIds();
    return selection.empty() || selection.count(note.id) > 0;
}

void VelocityLaneComponent::paint(juce::Graphics& g) {
    g.fillAll(Palette::background.darker(0.2f));

    const Track* track = pianoRoll_.activeTrack();
    if (!track) {
        g.setColour(Palette::textSecondary);
        g.setFont(12.0f);
        g.drawText("Vélocité", getLocalBounds(), juce::Justification::centred);
        return;
    }

    // Repères horizontaux à 32 / 64 / 96 : lire une nuance sans compter les
    // pixels, et vérifier d'un coup d'œil qu'un passage est homogène.
    for (int level : { 32, 64, 96 }) {
        const float y = static_cast<float>(getHeight()) - 4.0f
                      - (static_cast<float>(level) / 127.0f) * (static_cast<float>(getHeight()) - 8.0f);
        g.setColour(Palette::gridLine);
        g.drawLine(static_cast<float>(pianoRoll_.keyboardWidth()), y, static_cast<float>(getWidth()), y, 0.5f);
    }

    g.setColour(Palette::panel);
    g.fillRect(0, 0, pianoRoll_.keyboardWidth(), getHeight());
    g.setColour(Palette::textSecondary);
    g.setFont(11.0f);
    g.drawText("Vél.", 4, 2, pianoRoll_.keyboardWidth() - 8, 14, juce::Justification::centredLeft);

    const auto& selection = pianoRoll_.selectedNoteIds();
    const juce::Colour base = juce::Colour(track->colorRgba);

    for (const auto& note : track->notes) {
        const float x = pianoRoll_.tickToX(note.startTick);
        if (x < static_cast<float>(pianoRoll_.keyboardWidth()) || x > static_cast<float>(getWidth())) continue;

        const float noteWidth = std::max(3.0f, pianoRoll_.tickToX(note.endTick) - x);
        const float barWidth = std::min(noteWidth, 9.0f);
        const float height = (static_cast<float>(note.velocity) / 127.0f) * (static_cast<float>(getHeight()) - 8.0f);
        const juce::Rectangle<float> bar(x, static_cast<float>(getHeight()) - 4.0f - height, barWidth, height);

        const bool selected = selection.count(note.id) > 0;
        juce::Colour colour = base.withMultipliedBrightness(0.5f + 0.5f * static_cast<float>(note.velocity) / 127.0f);
        if (note.muted) colour = colour.withSaturation(0.05f).withAlpha(0.35f);
        if (!selection.empty() && !selected) colour = colour.withAlpha(0.35f); // hors sélection : estompé

        g.setColour(colour);
        g.fillRect(bar);
        g.setColour(selected ? Palette::accentAmber : colour.darker(0.6f));
        g.drawRect(bar, selected ? 1.5f : 0.5f);
    }

    g.setColour(Palette::border);
    g.drawLine(0.0f, 0.5f, static_cast<float>(getWidth()), 0.5f, 1.0f);
}

void VelocityLaneComponent::mouseDown(const juce::MouseEvent& event) {
    dragStart_ = event.position;
    lineMode_ = event.mods.isShiftDown();
    if (!lineMode_) paintVelocityAt(event.position, true);
    repaint();
}

void VelocityLaneComponent::mouseDrag(const juce::MouseEvent& event) {
    if (lineMode_) applyVelocityLine(event.position);
    else           paintVelocityAt(event.position, false);
    repaint();
}

void VelocityLaneComponent::mouseUp(const juce::MouseEvent&) {
    lineMode_ = false;
}

void VelocityLaneComponent::paintVelocityAt(juce::Point<float> pos, bool) {
    Track* track = pianoRoll_.activeTrack();
    if (!track) return;

    const vsm::midi::Tick tick = pianoRoll_.xToTick(pos.x);
    const uint8_t velocity = yToVelocity(pos.y);
    // Tolérance en ticks équivalente à ~6 pixels : viser une barre fine à la
    // souris doit rester possible même très dézoomé.
    const double tolerance = 6.0 / std::max(0.0001, pianoRoll_.pixelsPerTick());

    bool changed = false;
    for (auto& note : track->notes) {
        if (!isEditable(note)) continue;
        if (std::abs(static_cast<double>(note.startTick - tick)) > tolerance) continue;
        note.velocity = velocity;
        changed = true;
    }
    if (changed && onVelocityEdited) onVelocityEdited();
}

void VelocityLaneComponent::applyVelocityLine(juce::Point<float> current) {
    Track* track = pianoRoll_.activeTrack();
    if (!track) return;

    const float x1 = std::min(dragStart_.x, current.x), x2 = std::max(dragStart_.x, current.x);
    if (x2 - x1 < 1.0f) return;
    const float y1 = (dragStart_.x <= current.x) ? dragStart_.y : current.y;
    const float y2 = (dragStart_.x <= current.x) ? current.y : dragStart_.y;

    bool changed = false;
    for (auto& note : track->notes) {
        if (!isEditable(note)) continue;
        const float x = pianoRoll_.tickToX(note.startTick);
        if (x < x1 || x > x2) continue;
        const float t = (x - x1) / std::max(1.0f, x2 - x1);
        note.velocity = yToVelocity(y1 + t * (y2 - y1));
        changed = true;
    }
    if (changed && onVelocityEdited) onVelocityEdited();
}
