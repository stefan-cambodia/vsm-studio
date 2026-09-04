#pragma once
#include <JuceHeader.h>
#include "vsm/audio/dsp/SpectrumAnalyzer.h"
#include "vsm/audio/dsp/SpectrumTap.h"
#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace vsm::app::ui {

/// L'ANALYSEUR DE SPECTRE DU MASTER (D15.3), ce que Live appelle Spectrum et
/// Cubase SuperVision : la console dit des niveaux, cette fenêtre dit où ils
/// sont. Axe des fréquences logarithmique de 20 Hz à la moitié de la cadence,
/// axe des niveaux en dB où un sinus plein-échelle lit 0, une courbe vive et
/// une courbe tenue qui redescend lentement, et la crête nommée en hertz.
///
/// TOUT LE CALCUL EST ICI, SUR LE FIL DE L'INTERFACE, sur une copie des
/// 4 096 derniers échantillons que la prise (`SpectrumTap`) rend sans verrou :
/// le fil audio ne fait qu'y déposer le bus final. Vingt-cinq images par
/// seconde ; rien tant que la fenêtre n'est pas montrée.
class SpectrumComponent : public juce::Component, private juce::Timer {
public:
    SpectrumComponent();
    ~SpectrumComponent() override;

    void setTap(const vsm::audio::dsp::SpectrumTap* tap) { tap_ = tap; }
    std::function<double()> sampleRateProvider;

    void paint(juce::Graphics& g) override;

private:
    using Analyseur = vsm::audio::dsp::SpectrumAnalyzer<4096>;
    void timerCallback() override;
    float xPour(double hz, juce::Rectangle<float> zone) const;
    float yPour(float db, juce::Rectangle<float> zone) const;

    const vsm::audio::dsp::SpectrumTap* tap_ = nullptr;
    std::unique_ptr<Analyseur> analyseur_;
    std::vector<float> fenetre_;
    std::array<float, Analyseur::kBins> courbe_{}, tenue_{};
    double sampleRate_ = 48000.0;
    double creteHz_ = 0.0;
    float creteDb_ = Analyseur::kFloorDb;
    bool aDuSignal_ = false;
};

} // namespace vsm::app::ui
