#pragma once
#include <JuceHeader.h>

// Palette et LookAndFeel partagés par toute l'application : évite le rendu
// "DAW générique" demandé en section 21 (interface sombre, élégante, avec
// des accents chauds façon panneaux de synthés vintage plutôt que le bleu
// néon habituel des DAW modernes).
namespace vsm::ui::Palette {
    static const juce::Colour background      { 0xff17171b };
    static const juce::Colour panel           { 0xff1f1f24 };
    static const juce::Colour panelRaised     { 0xff26262c };
    static const juce::Colour border          { 0xff33333a };
    static const juce::Colour textPrimary     { 0xffe8e6df };
    static const juce::Colour textSecondary   { 0xff8a8892 };
    static const juce::Colour accentAmber     { 0xffe3a24d }; // accent chaud "vintage"
    static const juce::Colour accentTeal      { 0xff4bb3a6 };
    static const juce::Colour accentRed       { 0xffd66358 }; // record / mute
    static const juce::Colour gridLine        { 0xff2a2a30 };
    static const juce::Colour gridLineStrong  { 0xff3a3a42 };
    static const juce::Colour pianoKeyWhite   { 0xff2c2c33 };
    static const juce::Colour pianoKeyBlack   { 0xff1a1a1f };
}

class VsmLookAndFeel : public juce::LookAndFeel_V4 {
public:
    VsmLookAndFeel();

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
};
