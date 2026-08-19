#pragma once
#include <JuceHeader.h>

// Apparence « matériel » des façades par machine.
//
// Ce qui distingue un potentiomètre de synthétiseur d'un curseur d'interface
// ordinaire, ce n'est pas un dégradé brillant : c'est la LISIBILITÉ DE LA
// POSITION. Sur une vraie machine, on lit la valeur d'un coup d'œil à
// l'inclinaison du trait, de loin, sans survoler quoi que ce soit. Ce
// LookAndFeel privilégie donc le repère physique -- trait de position marqué,
// graduations, capuchon de curseur -- plutôt que l'effet.
//
// Les couleurs viennent de la façade elle-même (chaque machine a les siennes)
// et sont poussées via les couleurs du composant, pas codées en dur ici.
class HardwareLookAndFeel : public juce::LookAndFeel_V4 {
public:
    HardwareLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    juce::Label* createSliderTextBox(juce::Slider&) override;
};
