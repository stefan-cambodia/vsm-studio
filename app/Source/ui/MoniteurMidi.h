#pragma once
#include <JuceHeader.h>
#include "../audio/AudioEngine.h"
#include <deque>
#include <functional>

namespace vsm::app::ui {

/// D578.3 : LE MONITEUR MIDI (le *MIDI Monitor* de Cubase) — une ligne par événement que le moteur REÇOIT : l'heure,
/// la source, le canal, la nature et les données ; les plus récents en bas, 500 au plus. La fenêtre ne recueille que
/// pendant qu'elle est ouverte : `demarrer` vide ce qui traînait dans la file, puis la relève à son minuteur ; le fil
/// MIDI ne touche jamais à ce composant.
class MoniteurMidi : public juce::Component, private juce::ListBoxModel, private juce::Timer {
public:
    using Evenement = ::AudioEngine::MidiMonitorEvent;
    static constexpr size_t kLignesMax = 500;

    explicit MoniteurMidi(std::function<bool(Evenement&)> tirer);

    void demarrer();
    void arreter();
    void effacer();
    void retraduire();
    size_t nombre() const { return lignes_.size(); }
    /// Les lignes telles qu'elles sont PEINTES, une par ligne — la file vidée d'abord, pour qu'un relevé fait juste
    /// après un événement le voie (un panneau qui peint ses lignes est invisible au relevé des composants, D149).
    juce::String releverPourCapture();
    /// « 11:05:03.120 · (interne) · canal 1 · CC 7 = 100 » : ce que la ligne montre.
    static juce::String decrire(const Evenement& e);

    /// Le compte a changé (le titre de la fenêtre le porte).
    std::function<void(size_t)> onCountChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
    void timerCallback() override { tirerTout(); }
    void tirerTout();

    std::function<bool(Evenement&)> tirer_;
    std::deque<juce::String> lignes_;
    juce::ListBox liste_;
    juce::TextButton effacer_;
    juce::Label explication_;
};

} // namespace vsm::app::ui
