#pragma once

// ===========================================================================
// LES ACCENTS S'ÉCRIVENT u8"..." — sans exception, dans tout `app/`.
//
// `juce::String(const char*)` traite CHAQUE OCTET comme un point de code
// (du Latin-1) : les deux octets d'un « é » deviennent « Ã » et « © », et
// l'étiquette s'affiche « RÃ©tablir » au lieu de « Rétablir ». Ce n'est pas un
// problème de police ni de fichier source -- le source est bien en UTF-8.
//
// `u8"..."` est un `const char8_t*` en C++20, pour lequel JUCE a un
// constructeur qui décode réellement l'UTF-8. Corollaire : `juce::String +
// u8"..."` ne compile pas, il faut `+ juce::String(u8"...")` -- le compilateur
// attrape donc ces sites au lieu de laisser passer une chaîne illisible.
//
// Voir ARCHITECTURE.md § 6 bis bis pour le détail et la vérification.
// ===========================================================================
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
