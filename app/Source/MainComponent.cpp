#include "MainComponent.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/midi/MidiFileParser.h"
#include "vsm/midi/MidiFileWriter.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/audio/io/WavFileWriter.h"
#include "vsm/audio/effect/Reverb.h"
#include "vsm/audio/effect/Delay.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include "vsm/audio/effect/EffectFactory.h"
#include "vsm/interchange/ParameterDescriptor.h"
#include "vsm/interchange/EffectDescription.h"
#include "vsm/interchange/OfflineReconstruction.h"
#include "vsm/interchange/ProjectBundle.h"
#include "vsm/interchange/ReconstructionReport.h"
#include "vsm/interchange/SynthPreset.h"
#include "audio/ReferenceAudioLoader.h"
#include "vsm/audio/io/AudioTrackLoader.h"
#include "vsm/audio/io/WavFileReader.h"
#include "ui/UiScale.h"

using namespace vsm::sequencer;
using namespace vsm::midi;

MainComponent::MainComponent()
    : transport_(*this),
      transportBar_(transport_),
      velocityLane_(pianoRoll_),
      pianoRollPanel_(pianoRoll_, velocityLane_),
      trackListWindow_("Pistes", trackList_),
      pianoRollWindow_("Piano Roll", pianoRollPanel_),
      synthRackWindow_("Synth Rack", synthRack_),
      mixerWindow_("Mixer", bottomTabs_) {
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel_);

    // Piste de démonstration visible dès le lancement (section 4 du cahier
    // des charges) -- "vsm.minimoog" est un id RÉELLEMENT enregistré
    // auprès de PluginRegistry, pas un nom cosmétique.
    project_.title = "Nouveau projet";
    Track demoTrack;
    demoTrack.name = "Bass";
    demoTrack.channel = 0;
    demoTrack.colorRgba = 0xffE3A24Du;
    demoTrack.instrumentId = "vsm.minimoog";
    project_.tracks.push_back(demoTrack);

    addAndMakeVisible(transportBar_);

    for (auto* label : { &mixerPlaceholder_, &automationPlaceholder_, &midiCcPlaceholder_ }) {
        label->setJustificationType(juce::Justification::centred);
        label->setColour(juce::Label::textColourId, vsm::ui::Palette::textSecondary);
    }
    mixerPlaceholder_.setText("MIXER — console de mixage (Phase 2 UI)", juce::dontSendNotification);
    automationPlaceholder_.setText("AUTOMATION — lanes sample-accurate (Phase 2 UI)", juce::dontSendNotification);
    midiCcPlaceholder_.setText("MIDI CC — vue dédiée (Phase 2 UI ; éditable dès maintenant via les lanes du piano roll)",
                                juce::dontSendNotification);
    bottomTabs_.addTab("Mixer", vsm::ui::Palette::panel, &mixer_, false);
    bottomTabs_.addTab("Automation", vsm::ui::Palette::panel, &automation_, false);
    bottomTabs_.addTab("Effets", vsm::ui::Palette::panel, &effectChain_, false);
    bottomTabs_.addTab("MIDI CC", vsm::ui::Palette::panel, &midiCcPlaceholder_, false);

    transportBar_.onOpenMidiFile = [this] { openMidiFile(); };
    transportBar_.onExportMidiFile = [this] { exportMidiFile(); };
    transportBar_.onCycleListening = [this] { cycleReferenceMode(); };
    refreshListeningIndicator();

    trackList_.onTrackSelected = [this](size_t idx) {
        pianoRoll_.setActiveTrackIndex(idx);
        updateSynthRackForSelection();
        effectChain_.setActiveTrack(static_cast<int>(idx));
        audioEngine_.setLiveInputTrack(idx); // un clavier MIDI joue la piste sélectionnée
    };
    trackList_.onTracksChanged = [this] { refreshTransportSchedule(); };
    trackList_.onInstrumentChanged = [this](size_t idx, const std::string& pluginId) {
        audioEngine_.processGraph().setTrackInstrument(idx, pluginId);
        if (idx == trackList_.selectedTrackIndex()) updateSynthRackForSelection();
    };
    trackList_.onAddTrack = [this] { addTrack(); };
    trackList_.onRemoveTrack = [this](size_t idx) {
        if (idx < project_.tracks.size()) removeSelectedTrack();
    };

    // Mixer : édite Track (source de vérité) puis republie le snapshot audio
    // sans toucher au transport (coalescé au timer via mixDirty_). Le bus
    // master est piloté directement (setParameter atomique, thread-safe).
    mixer_.masterParamProvider = [this](vsm::audio::plugin::ParamId id) {
        return audioEngine_.processGraph().masterBus().getParameter(id);
    };
    mixer_.onMixEditStarted = [this] { beginProjectEdit("Mixage"); };
    mixer_.onMixChanged = [this] { mixDirty_ = true; };
    mixer_.onMasterParam = [this](vsm::audio::plugin::ParamId id, float v) {
        audioEngine_.processGraph().masterBus().setParameter(id, v);
    };
    mixer_.onMasterEnable = [this](bool on) {
        audioEngine_.processGraph().masterBus().setEnabled(on);
    };

    // MIDI Learn : en mode learn, bouger un knob du synth rack désigne la
    // cible ; le prochain CC matériel s'y lie (voir AudioEngine).
    synthRack_.onLearnModeChanged = [this](bool on) {
        if (!on) audioEngine_.cancelMidiLearn();
    };
    synthRack_.onParamTouched = [this](vsm::audio::plugin::ParamId id) {
        size_t track = trackList_.selectedTrackIndex();
        auto* inst = audioEngine_.processGraph().trackInstrument(track);
        if (inst == nullptr) return;
        vsm::audio::engine::MidiLearnTarget target;
        target.trackIndex = track;
        target.paramId = id;
        for (const auto& info : inst->parameterList())
            if (info.id == id) { target.min = info.minValue; target.max = info.maxValue; }
        target.valid = true;
        audioEngine_.armMidiLearn(target);
    };

    // Éditeur d'automation : liste les paramètres de l'instrument de la piste
    // choisie, et publie les lanes éditées via le chemin RT-safe.
    automation_.instrumentProvider = [this](size_t track) {
        return audioEngine_.processGraph().trackInstrument(track);
    };
    automation_.onAutomationChanged =
        [this](const std::vector<vsm::audio::engine::AutomationLane>& lanes) {
            currentAutomation_ = lanes;
            audioEngine_.processGraph().setAutomationLanes(lanes);
            // ÉCRITE DANS LE PROJET TOUT DE SUITE, comme les effets. Sans
            // cela, `rebuildFromProject()` -- qui repose les courbes DEPUIS le
            // projet après un ajout ou une suppression de piste -- effacerait
            // une automation dessinée et pas encore enregistrée. Une donnée
            // qui n'a qu'une seule copie vivante finit toujours par être
            // écrasée par celle qui en a deux.
            captureSessionIntoProject();
        };

    // Éditeur de chaîne d'effets d'insert (dernière pièce UI de la Phase 2).
    // La chaîne est DÉCRITE dans la piste ; ce composant n'en garde rien.
    effectChain_.onEditStarted = [this](const juce::String& label) { beginProjectEdit(label); };
    effectChain_.onChainChanged =
        [this](size_t track, std::shared_ptr<const EffectChainComponent::Chain> chain) {
            audioEngine_.processGraph().setTrackEffectChain(track, chain);
        };

    // Le projet est donné APRÈS le rappel : la toute première publication des
    // chaînes part alors vers le moteur au lieu de tomber dans le vide.
    effectChain_.setProject(&project_);

    // Bus de sends par défaut : Reverb sur le send A, Delay sur le send B
    // (préparés sur le thread UI avant publication). Les knobs "send" de
    // chaque tranche du mixer y routent le signal.
    {
        auto reverb = std::make_shared<vsm::audio::effect::Reverb>();
        reverb->prepare(48000.0, 512);
        audioEngine_.processGraph().setSendEffect(0, reverb);
        audioEngine_.processGraph().setSendReturn(0, 1.0f);
        auto delay = std::make_shared<vsm::audio::effect::Delay>();
        delay->prepare(48000.0, 512);
        audioEngine_.processGraph().setSendEffect(1, delay);
        audioEngine_.processGraph().setSendReturn(1, 1.0f);
    }

    // REPÈRES : posés sur la règle, nommés tout de suite. Un repère sans nom
    // ne repère rien, et c'est pourquoi l'interface demande le nom au moment de
    // la pose plutôt que d'en créer un « Repère 3 » à renommer plus tard.
    pianoRollPanel_.onMarkerRequested = [this](vsm::midi::Tick tick) {
        auto fenetre = std::make_shared<juce::AlertWindow>(
            "Poser un repere", "Nom du repere :", juce::AlertWindow::NoIcon);
        fenetre->addTextEditor("nom", "", "");
        fenetre->addButton("Poser", 1, juce::KeyPress(juce::KeyPress::returnKey));
        fenetre->addButton("Annuler", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        fenetre->enterModalState(true, juce::ModalCallbackFunction::create(
            [this, tick, fenetre](int resultat) {
                const juce::String nom = fenetre->getTextEditorContents("nom").trim();
                fenetre->exitModalState(resultat);
                fenetre->setVisible(false);
                if (resultat != 1 || nom.isEmpty()) return;
                beginProjectEdit("Poser un repere");
                project_.markers.push_back({tick, nom.toStdString()});
                std::sort(project_.markers.begin(), project_.markers.end(),
                           [](const vsm::sequencer::Marker& a, const vsm::sequencer::Marker& b) {
                               return a.tick < b.tick;
                           });
                pianoRollPanel_.refresh();
            }), false);
    };
    pianoRollPanel_.onMarkerRemoved = [this](size_t index) {
        if (index >= project_.markers.size()) return;
        beginProjectEdit("Retirer un repere");
        project_.markers.erase(project_.markers.begin() + static_cast<long>(index));
        pianoRollPanel_.refresh();
    };

    pianoRoll_.setHistory(&history_);
    pianoRoll_.onProjectRestored = [this] { rebuildFromProject(false); };
    pianoRoll_.setProject(&project_);
    pianoRoll_.onNotesEdited = [this] { refreshTransportSchedule(); };
    synthRack_.onPatternEdited = [this] {
        refreshTransportSchedule();
        pianoRoll_.repaint(); // le piano roll montre les mêmes notes
    };
    velocityLane_.onVelocityEdited = [this] { refreshTransportSchedule(); };
    pianoRollPanel_.onVelocityEdited = [this] { refreshTransportSchedule(); };

    // Écoute : cliquer une touche du clavier du piano roll, ou dessiner une
    // note, la fait sonner tout de suite sur l'instrument de la piste -- même
    // transport à l'arrêt (voir ProcessGraph::sendLiveNote).
    pianoRoll_.onAudition = [this](uint8_t note, uint8_t velocity, bool noteOn) {
        audioEngine_.processGraph().sendLiveNote(
            vsm::audio::engine::ProcessGraph::LiveNoteSource::Ui,
            trackList_.selectedTrackIndex(), note, velocity, noteOn);
    };

    // Clic sur la règle : déplacer la tête de lecture, en gardant les deux
    // transports d'accord (voir ARCHITECTURE.md section 6).
    pianoRoll_.onPlayheadRequested = [this](vsm::midi::Tick tick) {
        transport_.seekToTick(tick);
        audioEngine_.processGraph().seekSeconds(project_.ticksToSeconds(tick));
    };
    // La région de boucle est publiée aux DEUX transports, dans leurs unités
    // respectives : l'horloge audio est celle qui reboucle réellement, le
    // transport MIDI la suit pour rester cohérent en mode sans carte son.
    transportBar_.onLoopToggled = [this](bool active) {
        // Sans région définie, boucler sur tout le morceau : demander à
        // l'utilisateur de tirer d'abord sur une règle pour que le bouton
        // serve à quelque chose reviendrait à le laisser inerte.
        vsm::midi::Tick start = project_.loopStartTick;
        vsm::midi::Tick end = project_.loopEndTick;
        if (end <= start) { start = 0; end = project_.lastUsedTick(); }
        if (end <= start) { transportBar_.setLooping(false); return; }  // projet vide
        project_.loopEnabled = active;
        project_.loopStartTick = start;
        project_.loopEndTick = end;
        transport_.setLoopRegion(start, end, active);
        audioEngine_.processGraph().setLoopRegion(project_.ticksToSeconds(start),
                                                   project_.ticksToSeconds(end), active);
        pianoRoll_.setLoopRegion(start, end, active);
        pianoRollPanel_.refresh();
    };
    pianoRoll_.onLoopRegionChanged = [this](vsm::midi::Tick start, vsm::midi::Tick end, bool active) {
        // Écrite dans le projet AUSSI : c'est une donnée de morceau, et elle
        // disparaissait à la fermeture alors que le format savait l'écrire.
        project_.loopEnabled = active;
        project_.loopStartTick = start;
        project_.loopEndTick = end;
        transport_.setLoopRegion(start, end, active);
        audioEngine_.processGraph().setLoopRegion(project_.ticksToSeconds(start),
                                                   project_.ticksToSeconds(end), active);
        transportBar_.setLooping(active);
        pianoRollPanel_.refresh();
    };

    rebuildFromProject();
    // Le périphérique retenu au dernier lancement, s'il y en a un.
    {
        auto etat = std::unique_ptr<juce::XmlElement>(
            vsm::app::ui::UiScale::properties().getXmlValue("audioDeviceState"));
        audioEngine_.start(etat.get()); // échec silencieux et non bloquant
    }

#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(this);
    setSize(1000, 56);
#else
    addAndMakeVisible(menuBarComponent_);
    menuBarComponent_.setModel(this);
    setSize(1000, 56 + 26);
#endif

    startTimerHz(30);
}

MainComponent::~MainComponent() {
    // AVANT d'arrêter le moteur : une fois le périphérique fermé, il n'y a plus
    // d'état à écrire.
    saveAudioDeviceState();
    audioEngine_.stop(); // arrête le thread audio temps réel EN PREMIER, avant toute autre destruction
    stopTimer();
    transport_.stop();
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(nullptr);
#else
    menuBarComponent_.setModel(nullptr);
#endif
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(vsm::ui::Palette::background);
}

void MainComponent::resized() {
    auto area = getLocalBounds();
#if !JUCE_MAC
    menuBarComponent_.setBounds(area.removeFromTop(26));
#endif
    transportBar_.setBounds(area);
}

void MainComponent::showFloatingPanels() {
    // Raccourcis globaux : chaque fenêtre flottante remonte ses touches non
    // consommées ici (voir keyPressed). Idempotent : JUCE ignore un écouteur
    // déjà inscrit.
    for (auto* fenetre : { &trackListWindow_, &pianoRollWindow_, &synthRackWindow_, &mixerWindow_ })
        fenetre->addKeyListener(this);
    addKeyListener(this);
    // Appelée par Main.cpp APRÈS que la fenêtre socle a été positionnée à
    // l'écran (centreWithSize + setVisible) : avant ça, getScreenBounds()
    // renverrait des coordonnées non définies (la fenêtre n'existe pas
    // encore visuellement), et les panneaux flottants se positionneraient
    // n'importe où.
    juce::Rectangle<int> screenArea(0, 0, 1600, 1000);
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        screenArea = display->userArea;

    int topY = getScreenBounds().getBottom() + 10;
    int leftW = 320, rightW = 360, mixerH = 300;
    int centerW = juce::jmax(500, screenArea.getWidth() - leftW - rightW - 40);
    int centerH = juce::jmax(360, screenArea.getBottom() - topY - mixerH - 20);

    trackListWindow_.setBounds(screenArea.getX() + 10, topY, leftW, centerH);
    pianoRollWindow_.setBounds(screenArea.getX() + leftW + 20, topY, centerW, centerH);
    synthRackWindow_.setBounds(screenArea.getX() + leftW + centerW + 30, topY, rightW, centerH);
    mixerWindow_.setBounds(screenArea.getX() + 10, topY + centerH + 10, screenArea.getWidth() - 20, mixerH);

    trackListWindow_.setVisible(true);
    pianoRollWindow_.setVisible(true);
    synthRackWindow_.setVisible(true);
    mixerWindow_.setVisible(true);
}

void MainComponent::onMidiEvent(size_t trackIndex, const MidiEventData& data) {
    juce::ignoreUnused(trackIndex, data);
}

void MainComponent::timerCallback() {
    // UNE SEULE HORLOGE FAIT RÉFÉRENCE : celle du moteur audio (Phase 6,
    // "unification des transports", ARCHITECTURE.md § 6). Elle est
    // échantillon-exacte -- elle compte les échantillons réellement produits
    // par la carte son -- là où RealtimeTransport dérive nécessairement, son
    // thread se réveillant à la milliseconde près. Afficher la position du
    // second pendant qu'on entend le premier, c'était garantir un décalage
    // visible entre le curseur et le son au bout de quelques minutes.
    //
    // RealtimeTransport reste la source de repli quand aucune carte son n'est
    // ouverte : l'application doit rester utilisable (édition, défilement,
    // export) sur une machine sans audio, et c'est lui qui pilote encore la
    // sortie MIDI (IMidiEventSink).
    // La carte son peut ouvrir à une autre fréquence que celle qu'on croit, et
    // en changer en cours de route (réglages audio). Les effets suivent.
    applyAudioConfig();

    const bool audioClockAvailable = audioEngine_.isDeviceOpen();
    const vsm::midi::Tick playhead =
        audioClockAvailable ? project_.secondsToTicks(audioEngine_.processGraph().currentSeconds())
                            : transport_.currentTick();
    transportBar_.setInputLevel(audioEngine_.readInputPeak(),
                                 audioEngine_.currentInputChannels());
    pianoRoll_.setPlayheadTick(playhead);
    synthRack_.setPlayheadTick(playhead); // éclaire le pas en cours sur les grilles
    pianoRollPanel_.refresh(); // règle + barre d'outils suivent la tête de lecture et l'historique

    bool playing = (transport_.state() == TransportState::Playing);
    if (playing != audioWasPlaying_) {
        if (playing) {
            audioEngine_.processGraph().seekSeconds(transport_.currentSeconds());
            audioEngine_.processGraph().setPlaying(true);
        } else {
            audioEngine_.processGraph().setPlaying(false);
            audioEngine_.processGraph().seekSeconds(0.0);
        }
        audioWasPlaying_ = playing;
    }

    transportBar_.setCpuUsage(audioEngine_.currentCpuUsagePercent());
    transportBar_.setSampleRate(audioEngine_.currentSampleRate());

    // Republication coalescée des changements de mix (fader/pan/mute/solo)
    // sans interrompre la lecture (contrairement à refreshTransportSchedule).
    if (mixDirty_) {
        audioEngine_.processGraph().setProject(project_);
        mixDirty_ = false;
    }

    auto& mb = audioEngine_.processGraph().masterBus();
    mixer_.updateMeters(
        [this](size_t i) { return audioEngine_.processGraph().readMeterPeak(i); },
        mb.integratedLufs(), mb.outputPeak());

    // Le bouton MIDI Learn se désarme tout seul une fois un CC lié côté moteur.
    synthRack_.setLearnArmed(audioEngine_.isMidiLearnArmed());
}

// --- Menu ------------------------------------------------------------------

juce::StringArray MainComponent::getMenuBarNames() {
    return { "Fichier", "Édition", "Piste", "Affichage", "Aide" };
}

juce::PopupMenu MainComponent::getMenuForIndex(int topLevelMenuIndex, const juce::String&) {
    juce::PopupMenu menu;
    switch (topLevelMenuIndex) {
        case 0:
            menu.addItem(kMenuFileNewProject, "Nouveau projet");
            menu.addItem(kMenuFileOpen, "Ouvrir MIDI...");
            menu.addItem(kMenuFileOpenBundle, "Ouvrir un projet VSM...");
            menu.addItem(kMenuFileSave, "Enregistrer" +
                          juce::String(currentProjectFolder_ == juce::File() ? "..." : "")
                          + " (Ctrl+S)");
            menu.addItem(kMenuFileSaveAs, "Enregistrer sous...");
            menu.addSeparator();
            // Écoute A/B : l'enregistrement d'origine en regard de la
            // reconstruction. Les trois modes sont dans le même menu, cochés,
            // pour qu'on voie d'un coup d'œil ce qu'on est en train d'écouter.
            menu.addItem(kMenuFileLoadReference, "Charger l'original (référence A/B)...");
            {
                const bool aUneReference = audioEngine_.processGraph().referenceTrack().hasAudio();
                const auto mode = audioEngine_.processGraph().referenceTrack().mode();
                using Mode = vsm::audio::engine::ReferenceTrack::Mode;
                if (aUneReference && referenceDescription_.isNotEmpty()) {
                    menu.addSectionHeader(referenceDescription_);
                }
                menu.addItem(kMenuFileReferenceOff, "Écoute : reconstruction", aUneReference,
                              mode == Mode::Off);
                menu.addItem(kMenuFileReferenceMix, "Écoute : les deux", aUneReference,
                              mode == Mode::Mix);
                menu.addItem(kMenuFileReferenceSolo, "Écoute : original", aUneReference,
                              mode == Mode::Solo);
                menu.addItem(kMenuFileReferenceCycle, "Basculer l'écoute A/B (touche R)", aUneReference);
            }
            menu.addItem(kMenuFileExport, "Exporter MIDI...");
            menu.addItem(kMenuFileExportWav, "Exporter audio (WAV)...");
            menu.addSeparator();
            menu.addItem(kMenuFileAudioSettings, "Réglages audio...");
            menu.addSeparator();
            menu.addItem(kMenuFileQuit, "Quitter");
            break;
        case 1:
            // Le menu Édition EST le menu contextuel du piano roll : une seule
            // définition, donc aucun risque qu'une opération existe à un
            // endroit et pas à l'autre, ou que les deux divergent.
            menu = pianoRoll_.buildContextMenu();
            break;
        case 2:
            menu.addItem(kMenuTrackAdd, "Ajouter une piste");
            menu.addItem(kMenuTrackRemove, "Supprimer la piste sélectionnée",
                         !project_.tracks.empty());
            break;
        case 3:
            menu.addItem(kMenuViewTracks, "Pistes", true, trackListWindow_.isVisible());
            menu.addItem(kMenuViewPianoRoll, "Piano Roll", true, pianoRollWindow_.isVisible());
            menu.addItem(kMenuViewSynthRack, "Synth Rack", true, synthRackWindow_.isVisible());
            menu.addItem(kMenuViewMixer, "Mixer", true, mixerWindow_.isVisible());
            menu.addSeparator();
            {
                // TAILLE DE L'INTERFACE. Le facteur agrandit texte ET cases
                // dans le même rapport (voir ui/UiScale.h) : c'est la seule
                // façon d'agrandir l'écriture sans tronquer les légendes des
                // façades, qui sont dimensionnées d'après leur case.
                juce::PopupMenu tailles;
                const auto& paliers = vsm::app::ui::UiScale::steps();
                const float actuelle = vsm::app::ui::UiScale::current();
                for (int i = 0; i < paliers.size(); ++i) {
                    tailles.addItem(kMenuViewScaleFirst + i,
                                    vsm::app::ui::UiScale::label(paliers[i]),
                                    true,
                                    std::abs(paliers[i] - actuelle) < 1.0e-3f);
                }
                menu.addSubMenu("Taille de l'interface", tailles);
            }
            break;
        case 4:
            menu.addItem(kMenuHelpAbout, "À propos de Vintage Synth MIDI Studio");
            break;
        default:
            break;
    }
    return menu;
}

void MainComponent::menuItemSelected(int menuItemID, int /*topLevelMenuIndex*/) {
    // Les entrées du menu Édition proviennent du piano roll et utilisent sa
    // propre numérotation (>= 100, voir PianoRollComponent.cpp) : elles lui
    // sont renvoyées telles quelles.
    if (menuItemID >= 100) {
        pianoRoll_.performContextMenuAction(menuItemID);
        pianoRollPanel_.refresh();
        return;
    }

    switch (menuItemID) {
        case kMenuFileNewProject: newProject(); break;
        case kMenuFileOpen:      openMidiFile(); break;
        case kMenuFileOpenBundle: openProjectBundle(); break;
        case kMenuFileSave:      saveProject(); break;
        case kMenuFileSaveAs:    saveProjectAs(); break;
        case kMenuFileLoadReference: loadReferenceAudio(); break;
        case kMenuFileReferenceOff:
            setReferenceMode(vsm::audio::engine::ReferenceTrack::Mode::Off); break;
        case kMenuFileReferenceMix:
            setReferenceMode(vsm::audio::engine::ReferenceTrack::Mode::Mix); break;
        case kMenuFileReferenceSolo:
            setReferenceMode(vsm::audio::engine::ReferenceTrack::Mode::Solo); break;
        case kMenuFileReferenceCycle: cycleReferenceMode(); break;
        case kMenuFileExport:    exportMidiFile(); break;
        case kMenuFileExportWav: exportAudioFile(); break;
        case kMenuFileAudioSettings: showAudioSettings(); break;
        case kMenuFileQuit:      juce::JUCEApplication::getInstance()->systemRequestedQuit(); break;
        case kMenuTrackAdd:      addTrack(); break;
        case kMenuTrackRemove:   removeSelectedTrack(); break;
        case kMenuViewTracks:    togglePanel(trackListWindow_); break;
        case kMenuViewPianoRoll: togglePanel(pianoRollWindow_); break;
        case kMenuViewSynthRack: togglePanel(synthRackWindow_); break;
        case kMenuViewMixer:     togglePanel(mixerWindow_); break;
        case kMenuHelpAbout:     showAboutDialog(); break;
        default:
            if (menuItemID >= kMenuViewScaleFirst && menuItemID <= kMenuViewScaleLast) {
                const auto& paliers = vsm::app::ui::UiScale::steps();
                const int index = menuItemID - kMenuViewScaleFirst;
                if (index < paliers.size()) setUiScale(paliers[index]);
            }
            break;
    }
}

void MainComponent::togglePanel(PanelWindow& window) {
    bool newVisible = !window.isVisible();
    window.setVisible(newVisible);
    if (newVisible) window.toFront(true);
}

void MainComponent::setUiScale(float factor) {
    vsm::app::ui::UiScale::apply(factor);

    // Les fenêtres déjà à l'écran gardent leur taille EN POINTS ; le facteur
    // ne change que leur rendu. Il reste à les remettre dans l'écran : à
    // 200 %, une fenêtre qui touchait déjà le bord déborderait, et
    // l'utilisateur ne pourrait plus la ramener. On le fait pour la fenêtre
    // socle et pour chaque panneau flottant.
    for (auto* fenetre : { static_cast<juce::Component*>(getTopLevelComponent()),
                            static_cast<juce::Component*>(&trackListWindow_),
                            static_cast<juce::Component*>(&pianoRollWindow_),
                            static_cast<juce::Component*>(&synthRackWindow_),
                            static_cast<juce::Component*>(&mixerWindow_) }) {
        if (fenetre == nullptr || !fenetre->isOnDesktop()) continue;
        if (auto* peer = fenetre->getPeer())
            peer->setBounds(peer->getBounds().constrainedWithin(
                                juce::Desktop::getInstance().getDisplays()
                                    .getPrimaryDisplay()->userArea),
                            false);
    }
}

void MainComponent::showAboutDialog() {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::InfoIcon, "Vintage Synth MIDI Studio",
        "Sequenceur MIDI + rack de synthetiseurs vintage virtuels.\n\n"
        "Version 0.1.0 -- Phases 3 et 4 faites (instruments de reference + extension).");
}

void MainComponent::exportAudioFile() {
    auto chooser = std::make_shared<juce::FileChooser>(
        "Exporter en audio WAV...", juce::File(), "*.wav");

    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser](const juce::FileChooser& fc) {
        juce::File file = fc.getResult();
        if (file == juce::File()) return;

        // L'EXPORT PASSE PAR LE MÊME CODE QUE `vsm-render`, et c'est la seule
        // façon d'être sûr qu'il rende la même chose. La version précédente
        // montait son propre graphe : elle y posait les instruments, le
        // projet, l'automation et le master -- mais ni les inserts ni les
        // départs. Le fichier exporté n'avait donc ni la réverbération ni le
        // delay qu'on venait d'entendre, et rien ne le disait. Deux chemins de
        // rendu, c'est deux vérités ; il n'y en a qu'un.
        captureSessionIntoProject();

        vsm::interchange::LoadedBundle bundle;
        bundle.project = project_;
        bundle.document = vsm::interchange::documentFromProject(project_);
        bundle.folderPath = currentProjectFolder_ == juce::File()
                                ? std::string()
                                : currentProjectFolder_.getFullPathName().toStdString();
        for (size_t i = 0; i < project_.tracks.size(); ++i) {
            if (project_.tracks[i].instrumentId.empty()) continue;
            if (auto* plugin = audioEngine_.processGraph().trackInstrument(i))
                bundle.presetsByTrack[i] = vsm::interchange::capturePreset(
                    *plugin, project_.tracks[i].instrumentId, project_.tracks[i].name);
        }

        vsm::interchange::RenderOptions options;
        options.sampleRate = 48000.0;
        options.blockSize = 512;
        options.format = vsm::audio::io::SampleFormat::Int24;

        const auto rendered = vsm::interchange::renderBundleToWav(
            bundle, file.getFullPathName().toStdString(), options);
        if (!rendered.success) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                     "Erreur d'export audio", rendered.error);
            return;
        }

        // Les avertissements du rendu sont MONTRÉS. Un export qui laisse une
        // piste muette ou saute un effet doit le dire au moment où il le fait.
        juce::String message = "Rendu écrit :\n" + file.getFullPathName() + "\n\n"
                              + juce::String(rendered.renderedSeconds, 1) + " s, 48 kHz, 24 bits, crête "
                              + juce::String(rendered.peakLevel, 3) + ".";
        for (const auto& warning : rendered.warnings)
            message += "\n" + juce::String(warning);
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                                                 "Export audio terminé", message);
    });
}

void MainComponent::showAudioSettings() {
    // Sélecteur de device/sample rate standard de JUCE, branché sur
    // l'AudioDeviceManager du moteur.
    //
    // LES ENTRÉES SONT DÉSORMAIS CHOISISSABLES, et les périphériques MIDI
    // aussi. Le sélecteur était verrouillé à zéro entrée et sans onglet MIDI :
    // même une fois le moteur capable de capter, l'utilisateur n'aurait eu
    // aucun moyen de désigner d'où.
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(
        audioEngine_.deviceManager(),
        0, 2,   // entrées min/max
        2, 2,   // sorties min/max
        true,   // choix des entrées MIDI
        false,  // pas de sortie MIDI
        true,   // afficher le choix stéréo
        false); // vue avancée repliée
    selector->setSize(500, 420);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(selector.release());
    options.dialogTitle = "Réglages audio";
    options.dialogBackgroundColour = vsm::ui::Palette::background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync(); // gère lui-même la durée de vie de la fenêtre
}

void MainComponent::saveAudioDeviceState() {
    // LE CHOIX DU PÉRIPHÉRIQUE EST CONSERVÉ. Il ne l'était pas : `initialise`
    // recevait un état sauvegardé nul et rien n'était jamais écrit, si bien
    // qu'il fallait rechoisir sa carte, sa fréquence et sa taille de bloc à
    // chaque lancement. Le fichier de préférences est celui de l'échelle
    // d'interface -- il n'y en a qu'un, et c'est bien ainsi.
    if (auto etat = std::unique_ptr<juce::XmlElement>(audioEngine_.deviceManager().createStateXml()))
        vsm::app::ui::UiScale::properties().setValue("audioDeviceState", etat.get());
    vsm::app::ui::UiScale::properties().saveIfNeeded();
}

// --- Fichier / projet --------------------------------------------------

void MainComponent::openMidiFile() {
    auto chooser = std::make_shared<juce::FileChooser>(
        "Importer un fichier MIDI...", juce::File(), "*.mid;*.midi");
    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(chooserFlags, [this, chooser](const juce::FileChooser& fc) {
        juce::File file = fc.getResult();
        if (file == juce::File()) return;

        try {
            ParsedFile parsed = MidiFileParser::parseFile(file.getFullPathName().toStdString());
            history_.clear();
            project_ = Project::fromParsedFile(parsed);
            project_.title = file.getFileNameWithoutExtension().toStdString();
            rebuildFromProject();
        } catch (const std::exception& e) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                     "Erreur d'import MIDI", e.what());
        }
    });
}

void MainComponent::openProjectBundle() {
    // On choisit un DOSSIER, pas un fichier : un projet VSM est un ensemble
    // (project.json, le MIDI, les presets, les échantillons) et pointer vers
    // l'un de ses fichiers laisserait croire qu'on peut l'ouvrir seul.
    auto chooser = std::make_shared<juce::FileChooser>(
        "Ouvrir un dossier de projet VSM...", juce::File(), "");
    const auto chooserFlags = juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectDirectories;

    chooser->launchAsync(chooserFlags, [this, chooser](const juce::FileChooser& fc) {
        const juce::File folder = fc.getResult();
        if (folder == juce::File()) return;

        auto loaded = vsm::interchange::loadProjectBundle(folder.getFullPathName().toStdString());
        if (!loaded.success) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, "Projet illisible",
                juce::String::fromUTF8(loaded.error.c_str()));
            return;
        }

        history_.clear();
        project_ = loaded.bundle.project;
        if (project_.title.empty())
            project_.title = folder.getFileName().toStdString();
        // Ctrl+S réécrira ICI, sans redemander où.
        currentProjectFolder_ = folder;
        if (auto* window = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent()))
            window->setName("Vintage Synth MIDI Studio -- " + folder.getFileName());
        // rebuildFromProject() assigne les instruments d'après le projet : les
        // machines n'existent donc PAS avant cet appel, et appliquer les
        // presets plus tôt reviendrait à les appliquer à rien.
        rebuildFromProject();

        // --- presets et échantillons, machine par machine --------------------
        juce::StringArray rapport;
        for (const auto& [index, preset] : loaded.bundle.presetsByTrack) {
            // Un `project.json` peut déclarer un preset pour une piste que le
            // MIDI ne contient pas : le fichier a pu être édité à la main, ou
            // produit par une version antérieure. On l'IGNORE en le disant,
            // plutôt que de lire hors des bornes.
            if (index >= project_.tracks.size()) {
                rapport.add("Preset pour une piste inexistante (" + juce::String(static_cast<int>(index) + 1)
                            + ") : ignore");
                continue;
            }
            auto* instrument = audioEngine_.processGraph().trackInstrument(index);
            if (instrument == nullptr) {
                rapport.add("Piste " + juce::String(static_cast<int>(index) + 1)
                            + " : machine indisponible, preset non appliqué");
                continue;
            }
            const auto applique = vsm::interchange::applyPreset(
                preset, *instrument, project_.tracks[index].instrumentId);
            if (applique.unsupportedCount() > 0 || applique.clampedCount() > 0)
                rapport.add("Piste " + juce::String(static_cast<int>(index) + 1) + " : "
                            + juce::String::fromUTF8(applique.summary().c_str()));

            // Échantillons : chargés ICI, sur le thread de l'interface, et
            // jamais depuis le thread audio -- ce sont des lectures de
            // fichiers. La publication vers le thread audio est atomique,
            // c'est l'affaire de la machine.
            const auto echantillons = vsm::interchange::applyPresetSamples(
                preset, *instrument, loaded.bundle.folderPath);
            if (!echantillons.failures.empty())
                rapport.add("Piste " + juce::String(static_cast<int>(index) + 1) + " : "
                            + juce::String::fromUTF8(echantillons.summary().c_str()));
        }

        for (const auto& avertissement : loaded.warnings)
            rapport.add(juce::String::fromUTF8(avertissement.c_str()));

        // --- rapport de reconstruction, s'il y en a un ------------------------
        //
        // Facultatif : un projet ouvert à la main n'en a pas, et c'est normal.
        // Quand il est là, il porte la confiance de la transcription note par
        // note, et le piano roll marque celles sur lesquelles elle a hésité.
        const juce::File fichierRapport = folder.getChildFile("rapport.json");
        if (fichierRapport.existsAsFile()) {
            auto lu = vsm::interchange::loadReconstructionReport(
                fichierRapport.getFullPathName().toStdString());
            if (lu.success) {
                const size_t marquees =
                    vsm::interchange::applyNoteConfidences(lu.report, project_);
                size_t douteuses = 0;
                for (const auto& piste : project_.tracks)
                    douteuses += vsm::sequencer::countDoubtfulNotes(piste.notes);
                if (douteuses > 0)
                    rapport.add(juce::String(static_cast<int>(douteuses))
                                + " note(s) signalée(s) comme douteuses sur "
                                + juce::String(static_cast<int>(marquees))
                                + " transcrite(s) : elles sont marquées dans le piano roll,"
                                + " et la touche D y mène une par une");
                // Le projet a changé : le piano roll doit relire les notes.
                pianoRoll_.repaint();
            } else {
                // Un rapport illisible est DIT : le taire laisserait croire
                // que la transcription était sûre partout.
                rapport.add("Rapport de reconstruction illisible : "
                            + juce::String::fromUTF8(lu.error.c_str()));
            }
        }

        updateSynthRackForSelection();

        // Un projet incomplet s'OUVRE et DIT ce qui lui manque. Le taire
        // donnerait un morceau amputé sans explication -- c'est précisément
        // le genre de panne que ce projet refuse.
        if (!rapport.isEmpty()) {
            juce::String texte;
            for (const auto& ligne : rapport) texte << ligne << "\n";
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon,
                "Projet ouvert, avec des reserves", texte);
        }
    });
}

void MainComponent::loadReferenceAudio() {
    // Les formats proposés sont ceux que le décodeur sait REELLEMENT lire :
    // la liste vient de lui, elle n'est pas recopiée ici. Proposer un format
    // qu'on refuserait ensuite serait la pire façon de le supporter.
    auto chooser = std::make_shared<juce::FileChooser>(
        "Charger l'enregistrement d'origine (" + vsm::app::referenceAudioFormatList() + ")...",
        juce::File(), vsm::app::referenceAudioFilePatterns());
    const auto chooserFlags = juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(chooserFlags, [this, chooser](const juce::FileChooser& fc) {
        const juce::File file = fc.getResult();
        if (file == juce::File()) return;

        // LECTURE ET DÉCODAGE ICI, sur le thread de l'interface. Le tampon est
        // ensuite publié par échange atomique : le thread audio ne fait que
        // lire un pointeur déjà valide.
        auto result = vsm::app::loadReferenceAudioFile(file);
        if (!result.success || result.buffer.empty()) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, "Enregistrement illisible",
                result.error.isEmpty() ? juce::String("fichier sans echantillon") : result.error);
            return;
        }

        // CE QU'ON A CHARGÉ, ÉCRIT QUELQUE PART. Un MP3 décodé, un FLAC et un
        // WAV donnent le même tampon flottant : rien, à l'écoute, ne dit par
        // quel décodeur on est passé ni à quelle fréquence le fichier était.
        // Le menu le rappelle, parce que comparer sans savoir à quoi, c'est
        // comparer pour rien.
        const double duree = static_cast<double>(result.buffer.numFrames())
                           / juce::jmax(1.0, result.buffer.sampleRate);
        referenceDescription_ = file.getFileName() + "  --  " + result.decoder + ", "
                              + juce::String(result.buffer.sampleRate / 1000.0, 1) + " kHz, "
                              + (result.buffer.isStereo() ? "stereo, " : "mono, ")
                              + juce::String(static_cast<int>(duree) / 60) + ":"
                              + juce::String(static_cast<int>(duree) % 60).paddedLeft('0', 2);

        auto& reference = audioEngine_.processGraph().referenceTrack();
        reference.setAudio(std::make_shared<const vsm::audio::io::SampleBuffer>(std::move(result.buffer)));
        // On passe en écoute comparative tout de suite : charger un original
        // sans l'entendre serait un geste pour rien, et le menu permet de
        // revenir à la reconstruction seule.
        reference.setMode(vsm::audio::engine::ReferenceTrack::Mode::Mix);
        refreshListeningIndicator();
    });
}

void MainComponent::setReferenceMode(vsm::audio::engine::ReferenceTrack::Mode mode) {
    audioEngine_.processGraph().referenceTrack().setMode(mode);
    refreshListeningIndicator();
}

void MainComponent::cycleReferenceMode() {
    using Mode = vsm::audio::engine::ReferenceTrack::Mode;
    auto& reference = audioEngine_.processGraph().referenceTrack();
    if (!reference.hasAudio()) return;          // rien à comparer : la touche ne fait rien, et le bouton est grisé
    switch (reference.mode()) {
        case Mode::Off:  setReferenceMode(Mode::Mix);  break;
        case Mode::Mix:  setReferenceMode(Mode::Solo); break;
        case Mode::Solo: setReferenceMode(Mode::Off);  break;
    }
}

void MainComponent::refreshListeningIndicator() {
    using Mode = vsm::audio::engine::ReferenceTrack::Mode;
    const auto& reference = audioEngine_.processGraph().referenceTrack();
    if (!reference.hasAudio()) {
        transportBar_.setListening("Écoute A/B : pas d'original", false, false);
        return;
    }
    switch (reference.mode()) {
        case Mode::Off:  transportBar_.setListening("Écoute : reconstruction", true, false); break;
        case Mode::Mix:  transportBar_.setListening("Écoute : les deux", true, true); break;
        case Mode::Solo: transportBar_.setListening("Écoute : original", true, true); break;
    }
}

bool MainComponent::keyPressed(const juce::KeyPress& key, juce::Component*) {
    const auto mods = key.getModifiers();
    // Ctrl+S est testé AVANT le filtre ci-dessous, qui rejette tout ce qui
    // porte un modificateur.
    if ((mods.isCommandDown() || mods.isCtrlDown())
        && (key.getKeyCode() == 's' || key.getKeyCode() == 'S')) {
        if (mods.isShiftDown()) saveProjectAs(); else saveProject();
        return true;
    }
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown()) return false;
    switch (key.getKeyCode()) {
        // LA BARRE D'ESPACE LANCE ET ARRÊTE. Elle ne faisait rien, nulle part,
        // alors que c'est le seul raccourci que tout musicien essaie en
        // premier -- et qu'aucun autre raccourci de transport n'existait.
        case ' ':
            if (transport_.state() == vsm::sequencer::TransportState::Playing) transport_.stop();
            else transport_.play();
            return true;
        // R comme « référence » : la bascule A/B, depuis n'importe quelle
        // fenêtre -- on compare en regardant le piano roll, pas le menu.
        case 'r': case 'R': cycleReferenceMode(); return true;
        default: return false;
    }
}

// ---------------------------------------------------------------------------
// Enregistrer (D0.1 de docs/ROADMAP-daw.md)
// ---------------------------------------------------------------------------
//
// `saveProjectBundle()` existait dans `interchange/` depuis la Phase 7 et
// n'était appelée de nulle part : l'application savait OUVRIR un projet et pas
// l'écrire. Tout ce qui n'était pas une note -- mixage, effets, automation,
// boucle -- disparaissait à la fermeture, sans avertissement, et sans que le
// menu Fichier laisse deviner qu'il manquait une entrée.

void MainComponent::captureSessionIntoProject() {
    // La tranche master : ses quatorze réglages ne vivaient que dans l'objet
    // du moteur, donc ni sauvegardés ni transmis au rendu.
    project_.masterParameters =
        vsm::interchange::describeMasterBus(audioEngine_.processGraph().masterBus());

    // Les effets sont déjà dans les pistes (écrits au fil des gestes par
    // EffectChainComponent), la boucle aussi. Restent les courbes
    // d'automation, que le moteur tient par NUMÉRO de paramètre alors que le
    // disque les nomme par identité sémantique.
    for (auto& track : project_.tracks) track.automation.clear();

    for (const auto& lane : currentAutomation_) {
        if (lane.targetTrackIndex >= project_.tracks.size()) continue;
        auto& track = project_.tracks[lane.targetTrackIndex];
        if (track.instrumentId.empty() || lane.points().empty()) continue;

        const auto profile = vsm::interchange::buildSemanticProfile(track.instrumentId);
        const auto* descriptor = profile.findByParamId(lane.targetParam);
        // Sans identité sémantique, la courbe ne serait écrite que sous un
        // NUMÉRO : une position dans une liste, qui désignerait un autre
        // réglage dès qu'un paramètre serait intercalé. On préfère ne rien
        // écrire plutôt qu'écrire une valeur qui se reposera ailleurs.
        if (descriptor == nullptr || descriptor->semanticId.empty()) continue;

        vsm::sequencer::AutomationCurve curve;
        curve.parameter = descriptor->semanticId;
        for (const auto& point : lane.points())
            curve.points.push_back({point.tick, point.value,
                                     point.curveToNext == vsm::audio::engine::AutomationCurve::Step});
        track.automation.push_back(std::move(curve));
    }
}

void MainComponent::applyAutomationFromProject() {
    currentAutomation_.clear();
    for (size_t i = 0; i < project_.tracks.size(); ++i) {
        const auto& track = project_.tracks[i];
        if (track.automation.empty() || track.instrumentId.empty()) continue;
        const auto profile = vsm::interchange::buildSemanticProfile(track.instrumentId);
        for (const auto& curve : track.automation) {
            const auto* descriptor = profile.findBySemanticId(curve.parameter);
            if (descriptor == nullptr) continue;   // paramètre inconnu de cette machine
            vsm::audio::engine::AutomationLane lane;
            lane.targetTrackIndex = i;
            lane.targetParam = descriptor->paramId;
            for (const auto& point : curve.points)
                lane.addPoint(point.tick, point.value,
                               point.step ? vsm::audio::engine::AutomationCurve::Step
                                          : vsm::audio::engine::AutomationCurve::Linear);
            currentAutomation_.push_back(std::move(lane));
        }
    }
    audioEngine_.processGraph().setAutomationLanes(currentAutomation_);
    automation_.setProject(&project_);
}

bool MainComponent::writeProjectTo(const juce::File& folder) {
    captureSessionIntoProject();

    // Les presets sont capturés depuis les machines VIVANTES : sans cela,
    // `saveProjectBundle` retombe sur l'état PAR DÉFAUT de chaque machine et
    // écrit un projet qui ne sonne pas comme celui qu'on vient de régler.
    std::map<size_t, vsm::interchange::SynthPreset> presets;
    for (size_t i = 0; i < project_.tracks.size(); ++i) {
        const auto& track = project_.tracks[i];
        if (track.instrumentId.empty()) continue;
        if (auto* plugin = audioEngine_.processGraph().trackInstrument(i))
            presets[i] = vsm::interchange::capturePreset(*plugin, track.instrumentId, track.name);
    }

    const auto result = vsm::interchange::saveProjectBundle(
        project_, folder.getFullPathName().toStdString(), presets);
    if (!result.success) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                "Enregistrement impossible", result.error);
        return false;
    }
    currentProjectFolder_ = folder;
    // Le nom du dossier passe dans le titre de la fenêtre : c'est le retour
    // qu'attend un Ctrl+S, et il ne demande pas de cliquer pour disparaître.
    if (auto* window = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent()))
        window->setName("Vintage Synth MIDI Studio -- " + folder.getFileName());
    return true;
}

void MainComponent::saveProject() {
    if (currentProjectFolder_ == juce::File()) { saveProjectAs(); return; }
    writeProjectTo(currentProjectFolder_);
}

void MainComponent::saveProjectAs() {
    auto chooser = std::make_shared<juce::FileChooser>(
        "Enregistrer le projet VSM (dossier)...", currentProjectFolder_);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode
                             | juce::FileBrowserComponent::canSelectDirectories,
                          [this, chooser](const juce::FileChooser& fc) {
        const juce::File folder = fc.getResult();
        if (folder == juce::File()) return;
        folder.createDirectory();
        writeProjectTo(folder);
    });
}

void MainComponent::loadAudioTracks() {
    const double sr = audioEngine_.currentSampleRate() > 0.0 ? audioEngine_.currentSampleRate()
                                                              : 48000.0;
    juce::StringArray manquants;
    for (size_t i = 0; i < project_.tracks.size(); ++i) {
        const auto& track = project_.tracks[i];
        if (track.kind != vsm::sequencer::Track::Kind::Audio || track.audio.empty()) {
            audioEngine_.processGraph().setTrackAudio(i, nullptr);
            continue;
        }
        // Le chemin est RELATIF au dossier du projet. Sans dossier -- projet
        // jamais enregistré --, il n'y a rien à résoudre, et le dire vaut mieux
        // que de chercher au hasard dans le dossier courant.
        if (currentProjectFolder_ == juce::File()) {
            manquants.add(juce::String(track.name) + " (projet jamais enregistre)");
            audioEngine_.processGraph().setTrackAudio(i, nullptr);
            continue;
        }
        const juce::File fichier = currentProjectFolder_.getChildFile(track.audio.path);
        auto charge = vsm::audio::io::loadAudioTrack(fichier.getFullPathName().toStdString(), sr);
        if (!charge.success || !charge.source) {
            manquants.add(juce::String(track.name) + " : " + juce::String(charge.error));
            audioEngine_.processGraph().setTrackAudio(i, nullptr);
            continue;
        }
        // La longueur vient du FICHIER CHARGÉ, pas de ce que le projet déclare :
        // quand les deux divergent, c'est le fichier qui a raison.
        vsm::sequencer::Track pourLesClips = track;
        pourLesClips.audio.sampleRate = sr;
        pourLesClips.audio.frames = charge.source->frames();
        charge.source->clips = vsm::audio::engine::spansFromTrack(
            pourLesClips, sr, [this](int64_t tick) { return project_.ticksToSeconds(tick); });
        audioEngine_.processGraph().setTrackAudio(i, charge.source);
    }
    // UNE PISTE AUDIO QUI NE CHARGE PAS NE SE DISTINGUE PAS, À L'OREILLE, D'UNE
    // PISTE DONT ON AURAIT BAISSÉ LE VOLUME. Elle se dit donc, une fois, au
    // lieu de laisser chercher.
    if (!manquants.isEmpty())
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "Audio non chargé",
            "Ces pistes audio n'ont pas pu être lues :\n\n" + manquants.joinIntoString("\n"));
}

void MainComponent::applyAudioConfig() {
    const double sr = audioEngine_.currentSampleRate();
    if (sr <= 0.0 || std::abs(sr - appliedSampleRate_) < 1.0) return;
    appliedSampleRate_ = sr;

    // Les inserts : refabriqués depuis les descriptions, donc réglés ET
    // préparés à la bonne fréquence.
    const int blockSize = audioEngine_.currentBlockSize();
    effectChain_.setAudioConfig(sr, blockSize);

    // Les pistes audio sont rééchantillonnées à la nouvelle fréquence : leur
    // matériau est décodé pour UNE fréquence, et le graphe ne rééchantillonne
    // pas en temps réel.
    loadAudioTracks();

    // Les effets de bus : mêmes types, mêmes réglages, à la bonne fréquence.
    for (int bus = 0; bus < 2; ++bus) {
        auto fx = vsm::audio::effect::EffectFactory::create(bus == 0 ? "reverb" : "delay");
        if (!fx) continue;
        fx->prepare(sr, blockSize);
        audioEngine_.processGraph().setSendEffect(static_cast<size_t>(bus), std::move(fx));
        audioEngine_.processGraph().setSendReturn(static_cast<size_t>(bus), 1.0f);
    }
}

void MainComponent::newProject() {
    history_.clear();   // l'annulation d'un autre morceau n'a aucun sens ici
    project_ = Project{};
    project_.title = "Nouveau projet";
    rebuildFromProject();
}

void MainComponent::addTrack() {
    beginProjectEdit("Ajouter une piste");
    // Palette de couleurs cyclique pour distinguer visuellement les pistes.
    static const uint32_t kColors[] = {
        0xffE3A24Du, 0xff6B9BFFu, 0xff8ED081u, 0xffD08BC8u, 0xffE0C15Au, 0xff7FD0C8u
    };
    const size_t n = project_.tracks.size();

    Track t;
    t.name = "Piste " + std::to_string(n + 1);
    t.channel = static_cast<uint8_t>(n % 16);      // canaux MIDI 1..16 en boucle
    t.colorRgba = kColors[n % (sizeof(kColors) / sizeof(kColors[0]))];
    // Pas d'instrument par défaut : l'utilisateur le choisit dans le combo de
    // la piste (le Synth Rack se peuplera automatiquement à la sélection).
    project_.tracks.push_back(t);

    rebuildFromProject();
    trackList_.selectTrackIndex(project_.tracks.size() - 1); // sélectionne la nouvelle piste
}

void MainComponent::removeSelectedTrack() {
    if (project_.tracks.empty()) return;
    size_t idx = trackList_.selectedTrackIndex();
    if (idx >= project_.tracks.size()) return;
    // Après l'instant où l'on sait qu'il y a bien quelque chose à supprimer :
    // un instantané pris pour un geste sans effet ajouterait un pas
    // d'annulation qui ne défait rien.
    beginProjectEdit("Supprimer une piste");

    project_.tracks.erase(project_.tracks.begin() + static_cast<std::ptrdiff_t>(idx));
    rebuildFromProject();

    if (!project_.tracks.empty()) {
        const size_t next = std::min(idx, project_.tracks.size() - 1);
        trackList_.selectTrackIndex(next);
    }
}

void MainComponent::exportMidiFile() {
    auto chooser = std::make_shared<juce::FileChooser>(
        "Exporter en MIDI...", juce::File(), "*.mid");

    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser](const juce::FileChooser& fc) {
        juce::File file = fc.getResult();
        if (file == juce::File()) return;

        try {
            ParsedFile parsed = project_.toParsedFile();
            MidiFileWriter::writeFile(parsed, file.getFullPathName().toStdString());
        } catch (const std::exception& e) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                     "Erreur d'export MIDI", e.what());
        }
    });
}

void MainComponent::beginProjectEdit(const juce::String& label) {
    history_.beginEdit(project_, label.toStdString());
}

void MainComponent::rebuildFromProject(bool stopPlayback) {
    if (stopPlayback) {
        transport_.stop();
        audioEngine_.processGraph().setPlaying(false);
        audioWasPlaying_ = false;
    }

    // LA PISTE REGARDÉE EST CONSERVÉE. Cette fonction est rappelée à chaque
    // republication -- après un annuler, un ajout d'effet, un changement de
    // mixage --, et y remettre la piste 0 renverrait l'utilisateur au début du
    // morceau à chaque geste. Elle n'est ramenée à zéro que si la piste qu'il
    // regardait n'existe plus.
    const size_t regardee = project_.tracks.empty()
                                ? 0
                                : std::min(pianoRoll_.activeTrackIndex(), project_.tracks.size() - 1);
    trackList_.loadProject(project_);
    mixer_.setProject(&project_);
    automation_.setProject(&project_);
    pianoRoll_.setProject(&project_);
    pianoRoll_.setActiveTrackIndex(regardee);
    trackList_.selectTrackIndex(regardee);
    transportBar_.setBpm(project_.tempoMap.bpmAt(0));
    transportBar_.setTimeSignature(project_.timeSignatureMap.numeratorAt(0),
                                    static_cast<int>(project_.timeSignatureMap.denominatorAt(0)));

    // (Ré)assigne l'instrument de CHAQUE piste par son index courant (un
    // instrumentId vide efface le slot côté ProcessGraph), puis nettoie les
    // slots au-delà : après une SUPPRESSION, les pistes suivantes se décalent
    // vers le bas et l'ancien dernier index ne doit pas garder un synthé
    // fantôme. maxAssignedTracks_ mémorise le plus haut nombre de pistes déjà
    // vues pour savoir jusqu'où nettoyer.
    for (size_t i = 0; i < project_.tracks.size(); ++i)
        audioEngine_.processGraph().setTrackInstrument(i, project_.tracks[i].instrumentId);
    for (size_t i = project_.tracks.size(); i < maxAssignedTracks_; ++i)
        audioEngine_.processGraph().setTrackInstrument(i, "");
    maxAssignedTracks_ = std::max(maxAssignedTracks_, project_.tracks.size());

    // Les chaînes d'inserts sont refabriquées EN BLOC depuis les descriptions
    // des pistes : après une suppression, aucune ne peut rester accrochée à un
    // index qui désigne désormais une autre piste.
    if (project_.loopEndTick > project_.loopStartTick) {
        transport_.setLoopRegion(project_.loopStartTick, project_.loopEndTick, project_.loopEnabled);
        audioEngine_.processGraph().setLoopRegion(project_.ticksToSeconds(project_.loopStartTick),
                                                   project_.ticksToSeconds(project_.loopEndTick),
                                                   project_.loopEnabled);
        pianoRoll_.setLoopRegion(project_.loopStartTick, project_.loopEndTick, project_.loopEnabled);
    }
    transportBar_.setLooping(project_.loopEnabled);

    if (!project_.masterParameters.empty())
        vsm::interchange::applyMasterDescription(project_.masterParameters,
                                                  audioEngine_.processGraph().masterBus());
    loadAudioTracks();
    effectChain_.rebuildFromProject();
    for (size_t i = project_.tracks.size(); i < maxAssignedTracks_; ++i)
        audioEngine_.processGraph().setTrackEffectChain(i, nullptr);
    applyAutomationFromProject();

    refreshTransportSchedule();
    updateSynthRackForSelection();
}

void MainComponent::refreshTransportSchedule() {
    bool wasPlaying = transport_.state() == TransportState::Playing;
    transport_.stop();
    transport_.loadProject(project_);
    if (wasPlaying) transport_.play();

    audioEngine_.processGraph().setProject(project_);
}

void MainComponent::updateSynthRackForSelection() {
    size_t idx = trackList_.selectedTrackIndex();
    if (idx >= project_.tracks.size()) {
        synthRack_.setSynth(nullptr, {}, {});
        synthRack_.setTrack(nullptr);
        return;
    }
    auto* synth = audioEngine_.processGraph().trackInstrument(idx);
    std::string name = project_.tracks[idx].name.empty()
                            ? ("Piste " + std::to_string(idx + 1))
                            : project_.tracks[idx].name;
    synthRack_.setSynth(synth, juce::String(name), project_.tracks[idx].instrumentId);
    // La grille de pas édite directement les notes de la piste : c'est la même
    // musique que celle du piano roll, vue autrement (voir StepPattern.h).
    synthRack_.setTrack(&project_.tracks[idx]);
}
