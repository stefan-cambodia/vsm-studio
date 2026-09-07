#include "TransportBarComponent.h"
#include "LookAndFeel/VsmLookAndFeel.h"

using namespace vsm::sequencer;
using namespace vsm::ui;
using vsm::audio::engine::TransportState;

TransportBarComponent::TransportBarComponent(vsm::audio::engine::Transport& transport)
    : transport_(transport) {
    addAndMakeVisible(playButton_);
    addAndMakeVisible(stopButton_);
    addAndMakeVisible(recordButton_);
    addAndMakeVisible(loopButton_);
    addAndMakeVisible(metronomeButton_);
    addAndMakeVisible(tapButton_);
    addAndMakeVisible(speedBox_);
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

    addAndMakeVisible(xrunLabel_);
    xrunLabel_.setVisible(false);   // rien tant qu'il n'y a rien à dire
    for (auto* label : { &positionLabel_, &bpmLabel_, &timeSigLabel_, &cpuLabel_, &sampleRateLabel_,
                          &xrunLabel_ }) {
        addAndMakeVisible(label);
        label->setJustificationType(juce::Justification::centredLeft);
        label->setFont(juce::Font(juce::FontOptions(15.0f).withName(juce::Font::getDefaultMonospacedFontName())));
    }

    // D22.2 : le double-clic sur la position remonte au composant (voir
    // mouseDoubleClick) ; un Label intercepte sinon les clics et ne dit rien.
    positionLabel_.setInterceptsMouseClicks(false, false);
    positionLabel_.setTooltip(u8"Double-clic : aller à une mesure (Maj+P)");

    playButton_.onClick = [this] { transport_.play(); };
    stopButton_.onClick = [this] {
        transport_.stop();
        // L'application doit l'apprendre : c'est l'arrêt qui clôt une prise.
        if (onStopPressed) onStopPressed();
    };
    loopButton_.onClick = [this] {
        if (onLoopToggled) onLoopToggled(loopButton_.getToggleState());
    };
    loopButton_.setTooltip(u8"Boucle. La région se règle en tirant sur la règle "
                            u8"du piano roll avec Maj ; sans région, la boucle "
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
    // D18.5 : LA VITESSE DE LECTURE. « x1 » est le défaut et le reste : un
    // varispeed qu'on oublie allumé fait chercher longtemps pourquoi le
    // morceau ne sonne pas juste, et c'est pourquoi la valeur non normale
    // s'affiche en ROUGE plutôt que discrètement.
    {
        const double vitesses[] = {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0};
        int id = 1;
        for (double v : vitesses) {
            speedBox_.addItem(juce::String("x") + juce::String(v, v == 1.0 ? 0 : 2), id);
            if (v == 1.0) speedBox_.setSelectedId(id, juce::dontSendNotification);
            ++id;
        }
        speedBox_.setTooltip(juce::String(
            u8"Vitesse de lecture (varispeed) : ralentir pour relever un passage.\n"
            u8"Le morceau et le tempo ne changent pas. Les instruments CALCULÉS gardent "
            u8"leur hauteur ; ce qui est lu dans un fichier change de hauteur, comme une "
            u8"bande qu'on ralentit."));
        speedBox_.onChange = [this, vitesses] {
            const int index = speedBox_.getSelectedItemIndex();
            const int nombre = static_cast<int>(sizeof(vitesses) / sizeof(vitesses[0]));
            if (index < 0 || index >= nombre) return;
            const double v = vitesses[index];
            speedBox_.setColour(juce::ComboBox::textColourId,
                                 v == 1.0 ? Palette::textPrimary
                                          : Palette::accentRed);
            if (onPlaybackSpeedChanged) onPlaybackSpeedChanged(v);
        };
    }

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
    listenButton_.setTooltip(u8"Écoute A/B : reconstruction, les deux, original (touche R)");
    setListening(u8"Écoute A/B : pas d'original", false, false);
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
        g.setColour(inputMonitoring_ ? Palette::accentTeal : Palette::border);
        g.drawRect(inputMeterBounds_.expanded(inputMonitoring_ ? 1 : 0), inputMonitoring_ ? 2 : 1);
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

    // D22.4 : LES VOYANTS MIDI. Le texte reste lisible éteint (on doit savoir
    // que le voyant existe pour remarquer qu'il ne s'allume pas) ; allumé, le
    // fond prend la couleur d'accent et le texte s'inverse.
    const auto maintenant = juce::Time::getMillisecondCounter();
    auto voyant = [&](juce::Rectangle<int> r, const char* texte, bool allume) {
        if (r.isEmpty()) return;
        g.setColour(allume ? Palette::accentTeal : Palette::background);
        g.fillRoundedRectangle(r.toFloat(), 3.0f);
        g.setColour(Palette::border);
        g.drawRoundedRectangle(r.toFloat(), 3.0f, 1.0f);
        g.setColour(allume ? Palette::background : Palette::textSecondary);
        g.setFont(juce::Font(juce::FontOptions(12.0f).withStyle("Bold")));
        g.drawText(texte, r, juce::Justification::centred, false);
    };
    midiInLit_ = midiInUntil_ > maintenant;
    midiOutLit_ = midiOutUntil_ > maintenant;
    voyant(midiInBounds_, "IN", midiInLit_);
    voyant(midiOutBounds_, "OUT", midiOutLit_);
}

void TransportBarComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    if (positionLabel_.getBounds().contains(e.getPosition()) && onPositionDoubleClicked)
        onPositionDoubleClicked();
}

void TransportBarComponent::setMidiActivity(bool in, bool out) {
    const auto maintenant = juce::Time::getMillisecondCounter();
    if (in) midiInUntil_ = maintenant + 250;
    if (out) midiOutUntil_ = maintenant + 250;
    // Redessiné seulement quand l'état VISIBLE change -- l'appel arrive à
    // 30 Hz, et repeindre deux rectangles trente fois par seconde pour rien
    // serait le genre de charge qu'on ne remarque qu'en la cumulant.
    if ((midiInUntil_ > maintenant) != midiInLit_) repaint(midiInBounds_);
    if ((midiOutUntil_ > maintenant) != midiOutLit_) repaint(midiOutBounds_);
}

void TransportBarComponent::resized() {
    auto area = getLocalBounds().reduced(8, 6);

    // LA RANGÉE DOIT TENIR DANS LA FENÊTRE, À 150 % AUSSI. Elle était posée
    // en largeurs fixes -- 1 062 px à gauche, 542 à droite -- et sur un écran
    // de 1 920 px à l'échelle par défaut (1 280 px logiques), les boutons de
    // droite recouvraient la signature rythmique et le tempo : « Ouvrir… »
    // disparaissait, le compteur de charge se lisait « 0 … ». Les commandes
    // sont posées d'abord ; les deux étiquettes d'information (charge,
    // fréquence) ne s'affichent que s'il reste de la place, et les largeurs
    // secondaires se resserrent avant qu'un bouton ne soit coupé.
    const int largeur = area.getWidth();
    const bool serre = largeur < 1500;
    const bool tresSerre = largeur < 1300;

    // D22.4 : la zone était de 460 px pour 466 px de boutons -- le témoin
    // d'entrée, dernier servi, n'avait plus de place et ne se dessinait plus.
    // Élargie pour lui et pour les deux voyants MIDI ; la place est reprise
    // sur le bouton d'écoute A/B et l'écart qui le suit.
    auto transportArea = area.removeFromLeft(tresSerre ? 494 : 540);
    const int boutonPlay = tresSerre ? 62 : 70;
    playButton_.setBounds(transportArea.removeFromLeft(boutonPlay));
    transportArea.removeFromLeft(4);
    stopButton_.setBounds(transportArea.removeFromLeft(boutonPlay));
    transportArea.removeFromLeft(4);
    recordButton_.setBounds(transportArea.removeFromLeft(tresSerre ? 54 : 60));
    transportArea.removeFromLeft(4);
    loopButton_.setBounds(transportArea.removeFromLeft(tresSerre ? 54 : 60));
    transportArea.removeFromLeft(4);
    metronomeButton_.setBounds(transportArea.removeFromLeft(tresSerre ? 50 : 56));
    transportArea.removeFromLeft(4);
    tapButton_.setBounds(transportArea.removeFromLeft(tresSerre ? 46 : 50));
    transportArea.removeFromLeft(4);
    speedBox_.setBounds(transportArea.removeFromLeft(tresSerre ? 62 : 70));
    transportArea.removeFromLeft(6);
    inputMeterBounds_ = transportArea.removeFromLeft(10).reduced(0, 2);
    transportArea.removeFromLeft(6);
    midiInBounds_ = transportArea.removeFromLeft(tresSerre ? 26 : 28).reduced(0, 4);
    transportArea.removeFromLeft(3);
    midiOutBounds_ = transportArea.removeFromLeft(tresSerre ? 34 : 36).reduced(0, 4);

    area.removeFromLeft(serre ? 6 : 10);
    listenButton_.setBounds(area.removeFromLeft(serre ? 170 : 210));
    area.removeFromLeft(serre ? 10 : 16);
    positionLabel_.setBounds(area.removeFromLeft(serre ? 130 : 140));
    area.removeFromLeft(serre ? 10 : 16);
    bpmLabel_.setBounds(area.removeFromLeft(serre ? 100 : 110));
    area.removeFromLeft(8);
    timeSigLabel_.setBounds(area.removeFromLeft(serre ? 60 : 70));

    // À droite, par ordre d'importance : exporter, ouvrir, puis les deux
    // étiquettes seulement si elles tiennent.
    const int bouton = serre ? 120 : 150;
    auto poser = [&](juce::Component& c, int w, int ecart) {
        if (area.getWidth() < w) { c.setVisible(false); c.setBounds({}); return; }
        c.setVisible(true);
        c.setBounds(area.removeFromRight(w));
        area.removeFromRight(ecart);
    };
    // D41.2 : LA CHARGE PASSE AVANT LA FRÉQUENCE D'ÉCHANTILLONNAGE, ET AVANT
    // LES DEUX BOUTONS.
    //
    // Elle était posée en DERNIER, « seulement si elle tient » : à la largeur
    // de fenêtre de tous les autoportraits de ce document, elle était
    // invisible. C'est le seul indicateur qui dise si le morceau va JOUER, et
    // D41 a mesuré qu'à 64 pistes de `vsm.additive` on est à 125 % du budget --
    // c'est-à-dire que le son craque. Le faire disparaître avant deux boutons
    // qui ont chacun leur entrée de menu était le mauvais ordre.
    //
    // ET LA FRÉQUENCE D'ÉCHANTILLONNAGE EST LA BONNE CHOSE À ROGNER : elle ne
    // change jamais en cours de séance, alors que la charge change à chaque
    // note. Une étiquette qui ne varie pas n'a pas besoin d'être sous les yeux.
    // LES CRAQUEMENTS PASSENT MÊME AVANT LA CHARGE : la charge dit un risque,
    // le compte dit un dégât déjà fait.
    if (xrunLabel_.isVisible()) poser(xrunLabel_, serre ? 110 : 130, 8);
    poser(cpuLabel_, serre ? 76 : 90, 8);
    poser(exportButton_, bouton, 8);
    poser(openButton_, bouton, serre ? 10 : 16);
    poser(sampleRateLabel_, serre ? 90 : 120, 0);
}

bool TransportBarComponent::toggleRecord() {
    if (!recordButton_.isEnabled()) return false;
    recordButton_.triggerClick();
    return true;
}

void TransportBarComponent::toggleLoop() { loopButton_.triggerClick(); }
void TransportBarComponent::toggleMetronome() { metronomeButton_.triggerClick(); }

void TransportBarComponent::setInputMonitoring(bool on) {
    if (on == inputMonitoring_) return;
    inputMonitoring_ = on;
    repaint(inputMeterBounds_.expanded(2));
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
    // D41.2 : UN NOMBRE GRIS NE DIT PAS LE DANGER. Trois états, parce que deux
    // ne suffisent pas : au-delà de 90 % le son craque DÉJÀ, et entre 70 et
    // 90 il ne craque pas encore mais la moindre note de plus le fera. Un
    // témoin qui n'alerte qu'une fois le mal fait arrive trop tard pour servir.
    //
    // LES SEUILS SONT CEUX DE LA MESURE, et la mesure a été REFAITE.
    //
    // Une première version de ce commentaire citait 125 % et 258 % du budget
    // pour `vsm.additive` et `vsm.plate` à 64 pistes. Ces chiffres étaient
    // MONO-CŒUR : le banc ne réglait pas les fils de rendu, que l'application
    // met sur « automatique » (jusqu'à huit). Remesuré comme joue le logiciel :
    // Minimoog 5,5 %, CS-80 26,2 %, additif 28,6 %, plaque 53,5 % -- tout tient
    // largement, et la plus chère laisse encore la moitié du budget.
    //
    // LES SEUILS RESTENT LES MÊMES, et pour une raison qui ne dépend pas de ces
    // chiffres-là : 90 % est l'endroit où un bloc finit par arriver en retard,
    // quelle que soit la machine qui a consommé le temps. Ce que la correction
    // change, c'est l'idée qu'on s'en fait -- on n'atteint ces seuils qu'avec
    // des inserts, des effets et un projet chargé, pas avec 64 pistes nues.
    const juce::Colour couleur = percent >= 90.0f ? vsm::ui::Palette::accentRed
                               : percent >= 70.0f ? vsm::ui::Palette::accentAmber
                                                  : vsm::ui::Palette::textSecondary;
    cpuLabel_.setColour(juce::Label::textColourId, couleur);
    cpuLabel_.setTooltip(percent >= 90.0f
        ? juce::String::fromUTF8(u8"Le moteur n'a plus le temps de calculer un bloc : le son craque. "
                                  u8"« Piste ▸ Geler la piste » libère son instrument, "
                                  u8"ou agrandir le tampon audio.")
        : juce::String::fromUTF8(u8"Part du temps réel consommée par le calcul du son."));
}

void TransportBarComponent::setXrunCount(int count) {
    if (count == derniersXruns_) return;
    derniersXruns_ = count;
    if (count <= 0) {                 // aucun, ou pilote muet : rien à dire
        xrunLabel_.setVisible(false);
        return;
    }
    xrunLabel_.setVisible(true);
    xrunLabel_.setText(juce::String(count) + (count > 1 ? " craquements" : " craquement"),
                        juce::dontSendNotification);
    xrunLabel_.setColour(juce::Label::textColourId, Palette::accentRed);
    xrunLabel_.setTooltip(juce::String::fromUTF8(
        u8"Le moteur n'a pas rendu un bloc à temps : ce que vous avez entendu comportait un trou. "
        u8"Compté depuis l'ouverture du périphérique audio."));
    resized();
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

    // D11.3 : la position MUSICALE à côté de la position en temps. « tick
    // 4 215 » ne dit rien à personne ; « mes. 33 · 2 », si.
    juce::String text = juce::String::formatted("%02d:%06.3f  |  ", minutes, secsRemainder)
                        + (positionInBarsProvider ? positionInBarsProvider(tick)
                                                  : "tick " + juce::String(static_cast<long long>(tick)));
    positionLabel_.setText(text, juce::dontSendNotification);

    bool playing = transport_.state() == TransportState::Playing;
    playButton_.setToggleState(playing, juce::dontSendNotification);
}
