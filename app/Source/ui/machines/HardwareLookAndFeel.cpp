#include "HardwareLookAndFeel.h"
#include <cmath>

HardwareLookAndFeel::HardwareLookAndFeel() {
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
}

void HardwareLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                            float sliderPosProportional, float rotaryStartAngle,
                                            float rotaryEndAngle, juce::Slider& slider) {
    const juce::Colour knobColour = slider.findColour(juce::Slider::thumbColourId);
    // Le trait se lit PAR CONTRASTE avec le bouton : blanc sur bouton noir,
    // noir sur bouton crème. Le prendre sur la couleur du texte du panneau
    // donnerait un trait invisible dès que les deux se ressemblent.
    const juce::Colour pointerColour = knobColour.contrasting(0.75f);
    const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                static_cast<float>(width), static_cast<float>(height))
                            .reduced(3.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    const juce::Colour accent = slider.findColour(juce::Slider::rotarySliderFillColourId);

    // Graduations autour du bouton : sur une machine, ce sont elles qui
    // permettent de reproduire un réglage sans le lire chiffre par chiffre.
    const int ticks = 11;
    g.setColour(pointerColour.withAlpha(0.35f));
    for (int i = 0; i < ticks; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(ticks - 1);
        const float tickAngle = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
        const float inner = radius + 1.0f;
        const float outer = radius + (i == 0 || i == ticks - 1 || i == ticks / 2 ? 4.0f : 2.5f);
        g.drawLine(centre.x + std::sin(tickAngle) * inner, centre.y - std::cos(tickAngle) * inner,
                    centre.x + std::sin(tickAngle) * outer, centre.y - std::cos(tickAngle) * outer, 1.0f);
    }

    // Corps : léger relief obtenu par un dégradé sombre en bas, comme un
    // capuchon moulé éclairé par le haut.
    juce::ColourGradient body(knobColour.brighter(0.18f), centre.x, centre.y - radius,
                               knobColour.darker(0.35f), centre.x, centre.y + radius, false);
    g.setGradientFill(body);
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.2f);

    // Arc de valeur, discret : il complète le trait sans le remplacer.
    juce::Path arc;
    arc.addCentredArc(centre.x, centre.y, radius + 3.0f, radius + 3.0f, 0.0f, rotaryStartAngle, angle, true);
    g.setColour(accent.withAlpha(0.9f));
    g.strokePath(arc, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Trait de position : l'élément qui compte, du centre jusqu'au bord.
    const juce::Point<float> tip(centre.x + std::sin(angle) * (radius - 2.0f),
                                  centre.y - std::cos(angle) * (radius - 2.0f));
    g.setColour(pointerColour);
    g.drawLine(centre.x + std::sin(angle) * radius * 0.25f,
                centre.y - std::cos(angle) * radius * 0.25f, tip.x, tip.y, 2.4f);
}

void HardwareLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                            float sliderPos, float, float,
                                            juce::Slider::SliderStyle style, juce::Slider& slider) {
    const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                static_cast<float>(width), static_cast<float>(height));
    const bool vertical = (style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical);
    const juce::Colour capColour = slider.findColour(juce::Slider::thumbColourId);
    const juce::Colour trackColour = slider.findColour(juce::Slider::textBoxTextColourId).withAlpha(0.4f);
    const juce::Colour accent = slider.findColour(juce::Slider::rotarySliderFillColourId);

    // Rainure creusée, avec graduations : le curseur d'un synthétiseur coulisse
    // dans une fente, il ne glisse pas sur une barre.
    const float slotThickness = 5.0f;
    juce::Rectangle<float> slot = vertical
        ? juce::Rectangle<float>(bounds.getCentreX() - slotThickness * 0.5f, bounds.getY() + 6.0f,
                                  slotThickness, bounds.getHeight() - 12.0f)
        : juce::Rectangle<float>(bounds.getX() + 6.0f, bounds.getCentreY() - slotThickness * 0.5f,
                                  bounds.getWidth() - 12.0f, slotThickness);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillRoundedRectangle(slot, 2.0f);
    g.setColour(trackColour);
    for (int i = 0; i <= 10; ++i) {
        const float t = static_cast<float>(i) / 10.0f;
        if (vertical) {
            const float ty = slot.getBottom() - t * slot.getHeight();
            g.drawLine(bounds.getX() + 2.0f, ty, bounds.getX() + 6.0f, ty, i % 5 == 0 ? 1.2f : 0.6f);
        } else {
            const float tx = slot.getX() + t * slot.getWidth();
            g.drawLine(tx, bounds.getY() + 2.0f, tx, bounds.getY() + 6.0f, i % 5 == 0 ? 1.2f : 0.6f);
        }
    }

    // Capuchon : rectangle avec un trait central, comme un vrai bouton de
    // curseur -- c'est le trait qui donne la position exacte.
    const float capLength = vertical ? 18.0f : 14.0f;
    const float capWidth = vertical ? bounds.getWidth() * 0.7f : bounds.getHeight() * 0.7f;
    juce::Rectangle<float> cap = vertical
        ? juce::Rectangle<float>(bounds.getCentreX() - capWidth * 0.5f, sliderPos - capLength * 0.5f,
                                  capWidth, capLength)
        : juce::Rectangle<float>(sliderPos - capLength * 0.5f, bounds.getCentreY() - capWidth * 0.5f,
                                  capLength, capWidth);
    juce::ColourGradient capBody(capColour.brighter(0.2f), cap.getX(), cap.getY(),
                                  capColour.darker(0.3f), cap.getRight(), cap.getBottom(), false);
    g.setGradientFill(capBody);
    g.fillRoundedRectangle(cap, 2.5f);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawRoundedRectangle(cap, 2.5f, 1.0f);
    g.setColour(accent);
    if (vertical) g.drawLine(cap.getX() + 2.0f, cap.getCentreY(), cap.getRight() - 2.0f, cap.getCentreY(), 1.8f);
    else          g.drawLine(cap.getCentreX(), cap.getY() + 2.0f, cap.getCentreX(), cap.getBottom() - 2.0f, 1.8f);
}

void HardwareLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                            bool shouldDrawButtonAsHighlighted, bool) {
    const auto bounds = button.getLocalBounds().toFloat().reduced(2.0f);
    const bool on = button.getToggleState();
    const juce::Colour body = button.findColour(juce::TextButton::buttonColourId);
    const juce::Colour accent = button.findColour(juce::TextButton::buttonOnColourId);

    // Interrupteur à bascule : la position haute/basse se voit à la
    // silhouette, pas seulement à la couleur -- lisible même en noir et blanc.
    const auto body_ = bounds.withSizeKeepingCentre(bounds.getWidth() * 0.55f, bounds.getHeight());
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillRoundedRectangle(body_, 3.0f);

    auto lever = body_.reduced(2.0f).withHeight(body_.getHeight() * 0.45f);
    if (!on) lever = lever.withY(body_.getBottom() - lever.getHeight() - 2.0f);
    juce::ColourGradient metal(body.brighter(0.35f), lever.getX(), lever.getY(),
                                body.darker(0.25f), lever.getX(), lever.getBottom(), false);
    g.setGradientFill(metal);
    g.fillRoundedRectangle(lever, 2.5f);
    g.setColour(on ? accent : juce::Colours::black.withAlpha(0.5f));
    g.drawRoundedRectangle(lever, 2.5f, on ? 1.6f : 1.0f);

    if (shouldDrawButtonAsHighlighted) {
        g.setColour(accent.withAlpha(0.25f));
        g.fillRoundedRectangle(body_, 3.0f);
    }
}

juce::Label* HardwareLookAndFeel::createSliderTextBox(juce::Slider& slider) {
    auto* label = LookAndFeel_V4::createSliderTextBox(slider);
    label->setJustificationType(juce::Justification::centred);
    label->setFont(juce::Font(juce::FontOptions(11.0f)));
    return label;
}
