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
    /// Le tempo, changé à la main ou frappé au bouton « Tap ».
    ///
    /// Il était AFFICHÉ ET RIEN D'AUTRE : un `juce::Label` jamais rendu
    /// éditable, dont la valeur venait du projet importé. On ne pouvait donc
    /// pas commencer un morceau à partir de rien, ce qui est le premier geste
    /// d'un studio.
    std::function<void(double)> onTempoChanged;
    /// Le métronome. Il n'existait pas.
    std::function<void(bool)> onMetronomeToggled;
    /// Reflète l'état réel de la boucle (l'utilisateur peut aussi la définir
    /// depuis la règle).
    void setLooping(bool active);

    /// Écoute A/B (étape 11.2) : le bouton DIT ce qu'on entend -- reconstruction,
    /// les deux, original -- et bascule au clic. Le menu Fichier le permettait
    /// déjà, mais un menu se referme : pendant une écoute comparative, il faut
    /// pouvoir lire d'un coup d'œil laquelle des deux versions joue, sinon on
    /// juge la mauvaise. `active` colore le bouton quand l'original est audible.
    /// Niveau de l'entrée audio, et nombre de canaux ouverts.
    ///
    /// POURQUOI C'EST DANS LA BARRE DE TRANSPORT, à côté du bouton Rec. Brancher
    /// un micro et ne rien voir est le premier échec possible d'un
    /// enregistrement, et il n'a rien à voir avec l'enregistrement lui-même :
    /// c'est la carte, le câble, ou le canal. Un témoin permanent sépare les
    /// deux questions avant qu'on ne les confonde.
    void setInputLevel(float peak, int channels);

    void setListening(const juce::String& label, bool enabled, bool active);
    std::function<void()> onCycleListening;

private:
    void timerCallback() override; // rafraîchit l'affichage de la position de lecture

    vsm::sequencer::RealtimeTransport& transport_;

    juce::TextButton playButton_   { "Play" };
    juce::TextButton stopButton_   { "Stop" };
    juce::TextButton recordButton_ { "Rec" };
    juce::TextButton metronomeButton_ { "Clic" };
    juce::TextButton tapButton_ { "Tap" };
    /// Instants des dernières frappes du bouton « Tap », pour en tirer un
    /// tempo. Une frappe isolée ne dit rien ; il en faut au moins deux.
    juce::Array<double> tapTimes_;
    double dernierBpm_ = 120.0;
    float inputPeak_ = 0.0f;
    int inputChannels_ = 0;
    juce::Rectangle<int> inputMeterBounds_;
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
