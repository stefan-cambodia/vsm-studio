#include "TransportBarComponent.h"
#include "LookAndFeel/VsmLookAndFeel.h"

using namespace vsm::sequencer;
using namespace vsm::ui;

TransportBarComponent::TransportBarComponent(RealtimeTransport& transport) : transport_(transport) {
    addAndMakeVisible(playButton_);
    addAndMakeVisible(stopButton_);
    addAndMakeVisible(recordButton_);
    addAndMakeVisible(loopButton_);
    addAndMakeVisible(listenButton_);
    addAndMakeVisible(openButton_);
    addAndMakeVisible(exportButton_);

    loopButton_.setClickingTogglesState(true);
    recordButton_.setColour(juce::TextButton::buttonOnColourId, Palette::accentRed);

    for (auto* label : { &positionLabel_, &bpmLabel_, &timeSigLabel_, &cpuLabel_, &sampleRateLabel_ }) {
        addAndMakeVisible(label);
        label->setJustificationType(juce::Justification::centredLeft);
        label->setFont(juce::Font(juce::FontOptions(15.0f).withName(juce::Font::getDefaultMonospacedFontName())));
    }

    playButton_.onClick = [this] { transport_.play(); };
    stopButton_.onClick = [this] { transport_.stop(); };
    loopButton_.onClick = [this] {
        // Le calcul réel de la région de boucle (bornes en ticks) est piloté
        // par le Piano Roll (loop region visuel) ; ici on ne fait que
        // basculer l'état demandé par l'utilisateur.
    };
    listenButton_.onClick = [this] { if (onCycleListening) onCycleListening(); };
    listenButton_.setTooltip("Écoute A/B : reconstruction, les deux, original (touche R)");
    setListening("Écoute A/B : pas d'original", false, false);
    openButton_.onClick = [this] { if (onOpenMidiFile) onOpenMidiFile(); };
    exportButton_.onClick = [this] { if (onExportMidiFile) onExportMidiFile(); };

    setBpm(120.0);
    setTimeSignature(4, 4);
    setCpuUsage(0.0f);
    setSampleRate(48000.0);

    startTimerHz(30); // rafraîchit position/CPU à 30 Hz (affichage uniquement, jamais le chemin audio)
}

TransportBarComponent::~TransportBarComponent() { stopTimer(); }

void TransportBarComponent::paint(juce::Graphics& g) {
    g.fillAll(Palette::panel);
    g.setColour(Palette::border);
    g.drawLine(0.0f, static_cast<float>(getHeight() - 1), static_cast<float>(getWidth()),
               static_cast<float>(getHeight() - 1), 1.0f);
}

void TransportBarComponent::resized() {
    auto area = getLocalBounds().reduced(8, 6);

    auto transportArea = area.removeFromLeft(320);
    playButton_.setBounds(transportArea.removeFromLeft(70));
    transportArea.removeFromLeft(4);
    stopButton_.setBounds(transportArea.removeFromLeft(70));
    transportArea.removeFromLeft(4);
    recordButton_.setBounds(transportArea.removeFromLeft(60));
    transportArea.removeFromLeft(4);
    loopButton_.setBounds(transportArea.removeFromLeft(60));

    area.removeFromLeft(12);
    listenButton_.setBounds(area.removeFromLeft(230));
    area.removeFromLeft(16);
    positionLabel_.setBounds(area.removeFromLeft(140));
    area.removeFromLeft(16);
    bpmLabel_.setBounds(area.removeFromLeft(110));
    area.removeFromLeft(8);
    timeSigLabel_.setBounds(area.removeFromLeft(70));

    exportButton_.setBounds(area.removeFromRight(150));
    area.removeFromRight(8);
    openButton_.setBounds(area.removeFromRight(150));
    area.removeFromRight(16);
    sampleRateLabel_.setBounds(area.removeFromRight(120));
    area.removeFromRight(8);
    cpuLabel_.setBounds(area.removeFromRight(90));
}

void TransportBarComponent::setListening(const juce::String& label, bool enabled, bool active) {
    listenButton_.setButtonText(label);
    listenButton_.setEnabled(enabled);
    listenButton_.setColour(juce::TextButton::buttonColourId, active ? Palette::accentAmber : Palette::panelRaised);
    listenButton_.setColour(juce::TextButton::textColourOffId, active ? juce::Colours::black : Palette::textPrimary);
}

void TransportBarComponent::setBpm(double bpm) {
    bpm_ = bpm;
    bpmLabel_.setText(juce::String(bpm, 1) + " BPM", juce::dontSendNotification);
}

void TransportBarComponent::setTimeSignature(int numerator, int denominator) {
    tsNumerator_ = numerator;
    tsDenominator_ = denominator;
    timeSigLabel_.setText(juce::String(numerator) + "/" + juce::String(denominator), juce::dontSendNotification);
}

void TransportBarComponent::setCpuUsage(float percent) {
    cpuLabel_.setText("CPU " + juce::String(percent, 1) + "%", juce::dontSendNotification);
}

void TransportBarComponent::setSampleRate(double sampleRate) {
    sampleRateLabel_.setText(juce::String(sampleRate / 1000.0, 1) + " kHz", juce::dontSendNotification);
}

void TransportBarComponent::timerCallback() {
    Tick tick = transport_.currentTick();
    double seconds = transport_.currentSeconds();
    int minutes = static_cast<int>(seconds) / 60;
    double secsRemainder = seconds - minutes * 60.0;

    juce::String text = juce::String::formatted("%02d:%06.3f  |  tick %lld",
                                                  minutes, secsRemainder, static_cast<long long>(tick));
    positionLabel_.setText(text, juce::dontSendNotification);

    bool playing = transport_.state() == TransportState::Playing;
    playButton_.setToggleState(playing, juce::dontSendNotification);
}
