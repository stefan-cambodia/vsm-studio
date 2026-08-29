#include "TransportBarComponent.h"
#include "LookAndFeel/VsmLookAndFeel.h"

using namespace vsm::sequencer;
using namespace vsm::ui;

TransportBarComponent::TransportBarComponent(RealtimeTransport& transport) : transport_(transport) {
    addAndMakeVisible(playButton_);
    addAndMakeVisible(stopButton_);
    addAndMakeVisible(recordButton_);
    addAndMakeVisible(loopButton_);
    addAndMakeVisible(metronomeButton_);
    addAndMakeVisible(tapButton_);
    addAndMakeVisible(listenButton_);
    addAndMakeVisible(openButton_);
    addAndMakeVisible(exportButton_);

    loopButton_.setClickingTogglesState(true);
    recordButton_.setColour(juce::TextButton::buttonOnColourId, Palette::accentRed);

    // L'ENREGISTREMENT EXISTE (D3.3). Le bouton est resté deux phases affiché,
    // rouge et sans gestionnaire, en le disant dans son infobulle -- une
    // commande qui promet une fonction absente est pire que la fonction
    // absente. Il agit désormais, et reste désactivé tant qu'aucune piste n'est
    // armée : sans piste armée, il n'y a nulle part où écrire.
    recordButton_.setClickingTogglesState(true);
    recordButton_.onClick = [this] {
        if (onRecordToggled) onRecordToggled(recordButton_.getToggleState());
    };
    setRecordAvailable(false, 0);

    for (auto* label : { &positionLabel_, &bpmLabel_, &timeSigLabel_, &cpuLabel_, &sampleRateLabel_ }) {
        addAndMakeVisible(label);
        label->setJustificationType(juce::Justification::centredLeft);
        label->setFont(juce::Font(juce::FontOptions(15.0f).withName(juce::Font::getDefaultMonospacedFontName())));
    }

    playButton_.onClick = [this] { transport_.play(); };
    stopButton_.onClick = [this] {
        transport_.stop();
        // L'application doit l'apprendre : c'est l'arrêt qui clôt une prise.
        if (onStopPressed) onStopPressed();
    };
    loopButton_.onClick = [this] {
        if (onLoopToggled) onLoopToggled(loopButton_.getToggleState());
    };
    loopButton_.setTooltip("Boucle. La région se règle en tirant sur la règle "
                            "du piano roll avec Maj ; sans région, la boucle "
                            "couvre tout le morceau.");
    metronomeButton_.setClickingTogglesState(true);
    metronomeButton_.setColour(juce::TextButton::buttonOnColourId, Palette::accentTeal);
    metronomeButton_.setTooltip("Metronome : un clic par temps, plus aigu sur le premier "
                                 "de la mesure. Jamais present dans un export.");
    metronomeButton_.onClick = [this] {
        if (onMetronomeToggled) onMetronomeToggled(metronomeButton_.getToggleState());
    };

    // TAP TEMPO. La moyenne des intervalles des quatre dernières frappes : une
    // seule mesure est trop bruyante pour être jouable, et davantage rendrait
    // le bouton paresseux quand on cherche le tempo.
    tapButton_.setTooltip("Frapper le tempo. Deux frappes suffisent ; une pause d'une "
                           "seconde et demie recommence le compte.");
    tapButton_.onClick = [this] {
        const double maintenant = juce::Time::getMillisecondCounterHiRes() * 0.001;
        if (!tapTimes_.isEmpty() && maintenant - tapTimes_.getLast() > 1.5)
            tapTimes_.clear();     // on a hésité : on repart de zéro
        tapTimes_.add(maintenant);
        while (tapTimes_.size() > 5) tapTimes_.remove(0);
        if (tapTimes_.size() < 2) return;
        const double duree = tapTimes_.getLast() - tapTimes_.getFirst();
        const double intervalle = duree / static_cast<double>(tapTimes_.size() - 1);
        if (intervalle <= 0.0) return;
        const double bpm = juce::jlimit(20.0, 300.0, 60.0 / intervalle);
        setBpm(bpm);
        if (onTempoChanged) onTempoChanged(bpm);
    };

    // LE TEMPO S'ÉDITE. Double-clic sur la valeur, ou la frapper au bouton.
    bpmLabel_.setEditable(false, true, false);
    bpmLabel_.setTooltip("Double-cliquer pour changer le tempo.");
    bpmLabel_.onTextChange = [this] {
        const double bpm = bpmLabel_.getText().retainCharacters("0123456789.").getDoubleValue();
        if (bpm < 20.0 || bpm > 300.0) { setBpm(dernierBpm_); return; }   // valeur refusée, pas devinée
        dernierBpm_ = bpm;
        setBpm(bpm);
        if (onTempoChanged) onTempoChanged(bpm);
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

    // Élargi : la rangée porte maintenant le métronome, le tap tempo et le
    // témoin d'entrée en plus du transport.
    auto transportArea = area.removeFromLeft(460);
    playButton_.setBounds(transportArea.removeFromLeft(70));
    transportArea.removeFromLeft(4);
    stopButton_.setBounds(transportArea.removeFromLeft(70));
    transportArea.removeFromLeft(4);
    recordButton_.setBounds(transportArea.removeFromLeft(60));
    transportArea.removeFromLeft(4);
    loopButton_.setBounds(transportArea.removeFromLeft(60));
    transportArea.removeFromLeft(4);
    metronomeButton_.setBounds(transportArea.removeFromLeft(56));
    transportArea.removeFromLeft(4);
    tapButton_.setBounds(transportArea.removeFromLeft(50));
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
                ? juce::String(channels) + " entree(s) ouverte(s). L'enregistrement "
                  "AUDIO arrive en D3.4 ; l'enregistrement MIDI, lui, ne depend "
                  "pas de ces entrees mais du clavier branche."
                : "Aucune entree audio : la carte n'en donne pas. "
                  "Voir Fichier > Reglages audio.");
    }
    repaint(inputMeterBounds_);
}

void TransportBarComponent::setRecordAvailable(bool deviceOpen, int armedTrackCount) {
    // DEUX EMPÊCHEMENTS DISTINCTS, DEUX MESSAGES DISTINCTS. « Rec est gris »
    // n'apprend rien ; ce qui compte est de savoir s'il manque une piste armée
    // ou une carte son, parce qu'on ne va pas chercher au même endroit.
    recordButton_.setEnabled(deviceOpen && armedTrackCount > 0);
    if (!deviceOpen)
        recordButton_.setTooltip("Aucune carte son ouverte : le transport n'avance pas, "
                                  "et aucun clavier MIDI n'est ecoute. "
                                  "Voir Fichier > Reglages audio.");
    else if (armedTrackCount <= 0)
        recordButton_.setTooltip("Aucune piste armee : armer une piste avec son bouton R "
                                  "dans la liste des pistes, sinon la prise n'aurait nulle "
                                  "part ou aller.");
    else
        recordButton_.setTooltip("Enregistrer sur " + juce::String(armedTrackCount)
                                  + " piste(s) armee(s). Le decompte et le mode "
                                    "(superposer / remplacer) sont dans le menu Enregistrement.");
}

void TransportBarComponent::setRecording(bool active) {
    recording_ = active;
    recordButton_.setToggleState(active, juce::dontSendNotification);
}

void TransportBarComponent::setCountIn(int beatsRemaining) {
    if (beatsRemaining == countInBeats_) return;
    countInBeats_ = beatsRemaining;
    // Le compte à rebours prend la place de la position : pendant le décompte,
    // la tête de lecture est encore AVANT le morceau, et afficher un tick
    // négatif ne dirait rien à personne.
    if (countInBeats_ > 0)
        positionLabel_.setText("Decompte " + juce::String(countInBeats_),
                                juce::dontSendNotification);
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
    dernierBpm_ = bpm;
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
    // Pendant un décompte, la position affichée est le compte à rebours ; la
    // rafraîchir depuis le transport l'effacerait aussitôt.
    if (countInBeats_ > 0) return;

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
