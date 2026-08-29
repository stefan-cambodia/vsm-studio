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

    // L'ENREGISTREMENT N'EXISTE PAS ENCORE, ET LE BOUTON LE DIT. Il était
    // affiché, coloré en rouge, et sans le moindre gestionnaire : une commande
    // qui promet une fonction absente est pire que la fonction absente, parce
    // qu'elle se découvre en la cherchant. Il redeviendra actif en D3 de
    // docs/ROADMAP-daw.md, quand la carte son ouvrira des entrées.
    recordButton_.setEnabled(false);
    recordButton_.setTooltip("Enregistrement : pas encore implémenté "
                              "(phase D3 de docs/ROADMAP-daw.md)");

    for (auto* label : { &positionLabel_, &bpmLabel_, &timeSigLabel_, &cpuLabel_, &sampleRateLabel_ }) {
        addAndMakeVisible(label);
        label->setJustificationType(juce::Justification::centredLeft);
        label->setFont(juce::Font(juce::FontOptions(15.0f).withName(juce::Font::getDefaultMonospacedFontName())));
    }

    playButton_.onClick = [this] { transport_.play(); };
    stopButton_.onClick = [this] { transport_.stop(); };
    loopButton_.onClick = [this] {
        if (onLoopToggled) onLoopToggled(loopButton_.getToggleState());
    };
    loopButton_.setTooltip("Boucle. La région se règle en tirant sur la règle "
                            "du piano roll avec Maj ; sans région, la boucle "
                            "couvre tout le morceau.");
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

    // TÉMOIN D'ENTRÉE. Éteint et barré quand la carte n'ouvre aucune entrée :
    // un bargraphe vide voudrait dire « rien n'arrive », ce qui n'est pas la
    // même chose que « rien ne peut arriver ».
    if (!inputMeterBounds_.isEmpty()) {
        g.setColour(Palette::background);
        g.fillRect(inputMeterBounds_);
        g.setColour(Palette::border);
        g.drawRect(inputMeterBounds_, 1);
        if (inputChannels_ <= 0) {
            g.setColour(Palette::textSecondary.withAlpha(0.5f));
            g.drawLine(static_cast<float>(inputMeterBounds_.getX()),
                       static_cast<float>(inputMeterBounds_.getBottom()),
                       static_cast<float>(inputMeterBounds_.getRight()),
                       static_cast<float>(inputMeterBounds_.getY()), 1.0f);
        } else {
            const int hauteur = static_cast<int>(
                std::min(1.0f, inputPeak_) * static_cast<float>(inputMeterBounds_.getHeight()));
            g.setColour(inputPeak_ > 0.98f ? Palette::accentRed : Palette::accentTeal);
            g.fillRect(inputMeterBounds_.withTop(inputMeterBounds_.getBottom() - hauteur).reduced(1, 0));
        }
    }
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
    transportArea.removeFromLeft(6);
    inputMeterBounds_ = transportArea.removeFromLeft(10).reduced(0, 2);

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

void TransportBarComponent::setInputLevel(float peak, int channels) {
    // Décroissance douce : une crête qui disparaît au bloc suivant ne se voit
    // pas. On garde la plus forte des deux, puis on laisse retomber.
    inputPeak_ = std::max(peak, inputPeak_ * 0.82f);
    if (channels != inputChannels_) {
        inputChannels_ = channels;
        recordButton_.setTooltip(
            channels > 0
                ? juce::String(channels) + " entree(s) ouverte(s) — l'enregistrement "
                  "lui-meme arrive en D3.3 de docs/ROADMAP-daw.md"
                : "Aucune entree audio : la carte n'en donne pas. "
                  "Voir Fichier > Reglages audio.");
    }
    repaint(inputMeterBounds_);
}

void TransportBarComponent::setLooping(bool active) {
    loopButton_.setToggleState(active, juce::dontSendNotification);
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
