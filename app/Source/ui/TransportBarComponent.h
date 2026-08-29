#pragma once
#include <JuceHeader.h>
#include "vsm/sequencer/RealtimeTransport.h"

// TOP BAR de la section 21 : Transport / Play / Stop / Record / Loop / BPM /
// Time Signature / CPU / Sample Rate. Composant "dumb" : il ne connaît que
// le RealtimeTransport (Phase 1) qu'on lui injecte ; il n'a aucune logique
// de layout global (ça, c'est le rôle de MainComponent).
class TransportBarComponent : public juce::Component, private juce::Timer {
public:
    explicit TransportBarComponent(vsm::sequencer::RealtimeTransport& transport);
    ~TransportBarComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void setBpm(double bpm);
    void setTimeSignature(int numerator, int denominator);
    void setCpuUsage(float percent);      // câblé sur AudioEngine en Phase 2
    void setSampleRate(double sampleRate); // idem

    std::function<void()> onOpenMidiFile;
    std::function<void()> onExportMidiFile;

    /// Bascule de la boucle. Le bouton portait un gestionnaire VIDE : il
    /// s'allumait au clic et ne bouclait rien. La région, elle, ne s'obtenait
    /// qu'en tirant sur la règle du piano roll avec la touche Maj -- geste que
    /// rien n'indiquait.
    std::function<void(bool)> onLoopToggled;
    /// Reflète l'état réel de la boucle (l'utilisateur peut aussi la définir
    /// depuis la règle).
    void setLooping(bool active);

    /// Écoute A/B (étape 11.2) : le bouton DIT ce qu'on entend -- reconstruction,
    /// les deux, original -- et bascule au clic. Le menu Fichier le permettait
    /// déjà, mais un menu se referme : pendant une écoute comparative, il faut
    /// pouvoir lire d'un coup d'œil laquelle des deux versions joue, sinon on
    /// juge la mauvaise. `active` colore le bouton quand l'original est audible.
    void setListening(const juce::String& label, bool enabled, bool active);
    std::function<void()> onCycleListening;

private:
    void timerCallback() override; // rafraîchit l'affichage de la position de lecture

    vsm::sequencer::RealtimeTransport& transport_;

    juce::TextButton playButton_   { "Play" };
    juce::TextButton stopButton_   { "Stop" };
    juce::TextButton recordButton_ { "Rec" };
    juce::TextButton loopButton_   { "Loop" };
    juce::TextButton listenButton_ { "Écoute A/B : pas d'original" };
    juce::TextButton openButton_   { "Ouvrir MIDI..." };
    juce::TextButton exportButton_ { "Exporter MIDI..." };

    juce::Label positionLabel_;
    juce::Label bpmLabel_;
    juce::Label timeSigLabel_;
    juce::Label cpuLabel_;
    juce::Label sampleRateLabel_;

    double bpm_ = 120.0;
    int tsNumerator_ = 4, tsDenominator_ = 4;
};
