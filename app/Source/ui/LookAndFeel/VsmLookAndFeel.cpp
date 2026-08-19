#include "VsmLookAndFeel.h"

using namespace vsm::ui;

VsmLookAndFeel::VsmLookAndFeel() {
    setColour(juce::ResizableWindow::backgroundColourId, Palette::background);
    setColour(juce::DocumentWindow::backgroundColourId, Palette::background);
    setColour(juce::TextButton::buttonColourId, Palette::panelRaised);
    setColour(juce::TextButton::buttonOnColourId, Palette::accentAmber);
    setColour(juce::TextButton::textColourOffId, Palette::textPrimary);
    setColour(juce::TextButton::textColourOnId, juce::Colour(0xff17171b));
    setColour(juce::Label::textColourId, Palette::textPrimary);
    setColour(juce::Slider::rotarySliderFillColourId, Palette::accentTeal);
    setColour(juce::Slider::rotarySliderOutlineColourId, Palette::border);
    setColour(juce::Slider::thumbColourId, Palette::accentAmber);
    setColour(juce::ComboBox::backgroundColourId, Palette::panelRaised);
    setColour(juce::ComboBox::textColourId, Palette::textPrimary);
    setColour(juce::ScrollBar::thumbColourId, Palette::border);
}

void VsmLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                           const juce::Colour& backgroundColour,
                                           bool shouldDrawButtonAsHighlighted,
                                           bool shouldDrawButtonAsDown) {
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    float corner = 4.0f;

    juce::Colour fill = backgroundColour;
    if (shouldDrawButtonAsDown)
        fill = fill.darker(0.25f);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter(0.08f);

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, corner);

    g.setColour(Palette::border);
    g.drawRoundedRectangle(bounds, corner, 1.0f);
}

void VsmLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float minSliderPos, float maxSliderPos,
                                       juce::Slider::SliderStyle style, juce::Slider& slider) {
    const bool vertical = (style == juce::Slider::LinearVertical);
    if (!vertical) {
        // Repli sur le rendu par défaut pour les styles non verticaux
        // (ex. faders horizontaux du Track Editor).
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                          minSliderPos, maxSliderPos, style, slider);
        return;
    }

    auto area = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                        static_cast<float>(width), static_cast<float>(height));
    const float cx = area.getCentreX();

    // Rail central.
    juce::Rectangle<float> rail(cx - 2.0f, area.getY() + 4.0f, 4.0f, area.getHeight() - 8.0f);
    g.setColour(Palette::pianoKeyBlack);
    g.fillRoundedRectangle(rail, 2.0f);

    // Portion "remplie" jusqu'au curseur (accent chaud).
    g.setColour(Palette::accentAmber.withAlpha(0.55f));
    g.fillRoundedRectangle({rail.getX(), sliderPos, rail.getWidth(), rail.getBottom() - sliderPos}, 2.0f);

    // Poignée de fader (capuchon large, façon console).
    const float capW = static_cast<float>(width) * 0.8f;
    const float capH = 14.0f;
    juce::Rectangle<float> cap(cx - capW * 0.5f, sliderPos - capH * 0.5f, capW, capH);
    g.setColour(Palette::panelRaised);
    g.fillRoundedRectangle(cap, 3.0f);
    g.setColour(Palette::border);
    g.drawRoundedRectangle(cap, 3.0f, 1.0f);
    g.setColour(Palette::accentAmber);
    g.fillRect(cap.getX() + 2.0f, cap.getCentreY() - 0.5f, cap.getWidth() - 4.0f, 1.5f);
}

void VsmLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPosProportional, float rotaryStartAngle,
                                       float rotaryEndAngle, juce::Slider& slider) {
    auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                          static_cast<float>(width), static_cast<float>(height)).reduced(4.0f);
    float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto centre = bounds.getCentre();
    float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    // Corps du knob (façon potentiomètre hardware, pas un dégradé "glassy")
    g.setColour(Palette::panelRaised);
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour(Palette::border);
    g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.5f);

    // Arc de valeur
    juce::Path arc;
    arc.addCentredArc(centre.x, centre.y, radius - 3.0f, radius - 3.0f, 0.0f,
                       rotaryStartAngle, angle, true);
    g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId));
    g.strokePath(arc, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Indicateur de position (aiguille)
    juce::Point<float> tip(centre.x + std::sin(angle) * (radius - 6.0f),
                            centre.y - std::cos(angle) * (radius - 6.0f));
    g.setColour(Palette::textPrimary);
    g.drawLine({centre, tip}, 2.0f);
}
