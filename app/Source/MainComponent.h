#pragma once
#include <JuceHeader.h>
#include "vsm/sequencer/Project.h"
#include "vsm/sequencer/MidiRecorder.h"
#include "vsm/sequencer/ProjectHistory.h"
#include "vsm/sequencer/Groove.h"
#include "vsm/audio/engine/Transport.h"
#include "vsm/audio/engine/AudioTrackSource.h"
#include "vsm/interchange/ReconstructionChain.h"
#include "vsm/interchange/SynthPreset.h"
#include "reconstruction/ClipTranscriber.h"
#include "reconstruction/ReconstructionRunner.h"
#include "ui/ReconstructionWindow.h"
#include "ui/MidiLearnWindow.h"
#include "vsm/interchange/MidiLearnStore.h"
#include "project/AutosaveService.h"
#include "vsm/interchange/ShortcutTable.h"
#include "ui/ShortcutsWindow.h"
#include "ui/HistoryWindow.h"
#include "ui/SpectrumComponent.h"
#include "ui/PlayOrderComponent.h"
#include "ui/TakeCompComponent.h"
#include "ui/PreferencesWindow.h"
#include "ui/BrowserComponent.h"
#include "vsm/interchange/BrowserIndex.h"
#include "audio/AudioEngine.h"
#include "audio/ReferenceAudioLoader.h"
#include "ui/TransportBarComponent.h"
#include "ui/TrackListComponent.h"
#include "ui/PianoRollComponent.h"
#include "ui/VelocityLaneComponent.h"
#include "ui/PianoRollPanel.h"
#include "ui/SynthRackComponent.h"
#include "ui/MixerComponent.h"
#include "ui/ArrangementComponent.h"
#include "ui/AutomationComponent.h"
#include "ui/MidiCcComponent.h"
#include "ui/TempoLaneComponent.h"
#include "ui/EffectChainComponent.h"
#include "ui/EventListComponent.h"
#include "ui/PanelWindow.h"
#include "ui/ImportReportComponent.h"
#include "ui/LookAndFeel/VsmLookAndFeel.h"
#include "vsm/audio/engine/ReferenceTrack.h"
#include "vsm/audio/io/WaveformPeaks.h"
#include "vsm/interchange/OfflineReconstruction.h"
#if VSM_WITH_CLAP || VSM_WITH_VST3
#include "plugins/PluginScanner.h"
#endif
#include <map>
#include <memory>

// Composant racine, désormais un simple SOCLE : barre de menu + barre de
// transport. Tous les grands panneaux (Track Editor, Piano Roll, Synth
// Rack, Mixer) sont des FENÊTRES FLOTTANTES indépendantes (PanelWindow),
// affichables/masquables depuis le menu Affichage -- les fermer ne les
// détruit jamais, juste les cache (leur état -- scroll, sélection, zoom --
// est préservé, voir PanelWindow.h).
//
// MainComponent reste le seul endroit qui connaît TOUS les sous-composants
// ET les fenêtres qui les hébergent ; chaque composant continue d'ignorer
// l'existence des autres et communique uniquement via des callbacks
// (std::function), ce qui les garde testables/réutilisables indépendamment.
//
// UN SEUL TRANSPORT (D8.3, voir ARCHITECTURE.md section 6). `Transport` ne
// tient aucune position : il lit celle du graphe audio, qui compte les
// échantillons réellement sortis de la carte, et n'ajoute que ce que le graphe
// n'a pas à connaître -- l'état, les ticks, et la fin du morceau.
class MainComponent : public juce::Component,
                       public juce::MenuBarModel,
                       public juce::FileDragAndDropTarget,
                       private juce::KeyListener,
                       private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // juce::FileDragAndDropTarget — un morceau glissé sur la fenêtre.
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;


    /// À appeler par MainWindow (Main.cpp) UNE FOIS que la fenêtre socle a
    /// une position d'écran réelle (après centreWithSize/setVisible) --
    /// sinon les fenêtres flottantes se positionneraient par rapport à des
    /// coordonnées d'écran non définies (voir le .cpp pour le pourquoi).
    void showFloatingPanels();

    /// Pilote le menu Affichage par son NOM, pour l'autoportrait
    /// (VSM_VUE=arrangement,sans-pistes,... avant VSM_CAPTURE) : chaque état
    /// du menu doit pouvoir être photographié sans souris, sans quoi « ça ne
    /// s'affiche pas » ne se vérifie qu'à la main.
    void applyViewCommand(const juce::String& nom);
    /// Exécute une entrée de menu par son LIBELLÉ (VSM_MENU=libellé;… avant
    /// VSM_CAPTURE), pour la même raison que `applyViewCommand` : les gestes
    /// qui ne vivent que dans un menu doivent pouvoir être photographiés sans
    /// souris. Le premier libellé qui COMMENCE par le texte, tous menus
    /// confondus ; faux -- et dit sur stderr -- quand rien ne correspond ou
    /// que l'entrée est grisée.
    bool runMenuEntryForCapture(const juce::String& libelle);
    /// D22.2 : VSM_POSITION=17.3 -- la tête à une position saisie, sans
    /// souris ; faux et dit sur stderr si le texte n'est pas une position.
    bool goToPositionForCapture(const juce::String& texte) { return goToBarText(texte); }
    /// D22.4 : VSM_LECTURE=1 -- lancer la lecture avant la capture, pour que
    /// le voyant OUT se photographie allumé.
    void startPlaybackForCapture();
    /// D22.4 : VSM_VUE=note:60 -- une note jouée par le chemin du clavier
    /// d'ordinateur, rejouée toutes les 200 ms pendant trois secondes : un
    /// voyant tenu 250 ms ne se photographie que s'il vient de s'allumer.
    void playNoteForCapture(uint8_t note, int restant);
    /// D22.5 : VSM_PRESET_PISTE=nom -- la piste choisie écrite comme preset
    /// sous ce nom, sans boîte de dialogue (elle ne se photographie pas).
    bool saveTrackPresetForCapture(const juce::String& nom) { return saveSelectedTrackAsPreset(nom); }
    /// D39.1 : JOUE UNE TOUCHE, PAR LE CHEMIN DU CLAVIER. Pas un geste de plus
    /// à côté : `juce::KeyPress::createFromDescription` fabrique la touche que
    /// l'utilisateur enfoncerait, et `keyPressed` la traverse comme d'habitude
    /// -- table des raccourcis comprise.
    ///
    /// POURQUOI IL LE FALLAIT : D38 a mesuré son muet par le banc ET par la
    /// capture, et les deux passaient par `TrackListComponent::basculerMuet`.
    /// Le chemin du clavier, resté seul, faisait autre chose sans que rien ne
    /// le dise. Deux instruments braqués au même endroit ne valent pas mieux
    /// qu'un seul.
    bool runKeyForCapture(const juce::String& description) {
        const juce::KeyPress touche = juce::KeyPress::createFromDescription(description);
        if (!touche.isValid()) return false;
        return keyPressed(touche, this);
    }
    /// D36.1 : joue, sans souris, un geste de la LIGNE de piste -- ceux
    /// qu'aucun menu ne porte. Rend false si le geste est inconnu, plutôt que
    /// de ne rien faire en silence.
    bool runTrackGestureForCapture(const juce::String& geste) {
        const size_t piste = trackList_.selectedTrackIndex();
        if (geste.equalsIgnoreCase("muet")) { trackList_.basculerMuet(piste); return true; }
        if (geste.startsWithIgnoreCase("renommer:")) {
            trackList_.renommer(piste, geste.fromFirstOccurrenceOf(":", false, false));
            return true;
        }
        // D38 : CHOISIR PLUSIEURS PISTES SANS SOURIS. « choisir:0,2,3 » pose la
        // sélection ; c'est le seul geste de cette phase qui ne s'atteint ni
        // par un menu ni par un bouton -- il vit dans le Ctrl+clic --, et sans
        // lui la règle de D38.4 ne se photographierait pas.
        if (geste.startsWithIgnoreCase("choisir:")) {
            std::set<size_t> choix;
            juce::StringArray parts;
            parts.addTokens(geste.fromFirstOccurrenceOf(":", false, false), ",", "");
            for (const auto& p : parts)
                if (p.trim().isNotEmpty()) choix.insert(static_cast<size_t>(p.trim().getIntValue()));
            if (choix.empty()) return false;
            trackList_.setSelectedTracks(choix, *choix.begin());
            return true;
        }
        if (geste.startsWithIgnoreCase("machine:")) {
            return trackList_.choisirMachine(piste, geste.fromFirstOccurrenceOf(":", false, false));
        }
        if (geste.startsWithIgnoreCase("couleur:")) {
            appliquerCouleurDePiste(
                piste, juce::Colour::fromString("ff" + geste.fromFirstOccurrenceOf(":", false, false)));
            return true;
        }
        if (geste.startsWithIgnoreCase("volume:")) {
            trackList_.reglerVolume(piste, geste.fromFirstOccurrenceOf(":", false, false).getFloatValue());
            return true;
        }
        return false;
    }
    /// D23.3 : VSM_EXPORT_MIDI_PISTE=fichier.mid -- la piste choisie écrite
    /// en MIDI sans fenêtre, pour que le fichier se relise.
    bool exportTrackMidiForCapture(const juce::File& fichier) { return writeSelectedTrackMidi(fichier); }
    /// D56.1 : LE PROJET ENTIER en MIDI, sans sélecteur de fichier. L'export
    /// complet ne s'atteignait qu'à la souris, si bien que ce qu'il ÉCRIT --
    /// et non ce qu'il annonce -- n'avait jamais été relu par une vérification.
    bool exportProjectMidiForCapture(const juce::File& fichier);
    /// D57 : retirer la prise `index` du tiroir de la piste choisie, en
    /// réparant les tronçons d'assemblage et en DISANT ce que cela a coûté.
    void removeTakeFromSelectedTrack(int index);
    /// D58 : LA TAILLE DEMANDÉE PAR `VSM_TAILLE` EST IMPOSÉE.
    ///
    /// Sans cela elle ne servait à rien : `showFloatingPanels()` recouvre la
    /// fenêtre socle par l'écran entier en mode « fenêtre unique » — le défaut
    /// depuis le 31/08 —, juste après que `Main.cpp` a posé la taille demandée.
    /// Quatre valeurs différentes rendaient la MÊME image de 1264x742.
    void forceWindowSize() { tailleImposee_ = true; }
    /// D57 : republier le panneau d'assemblage depuis la piste choisie.
    void refreshTakeCompPanel();
    /// D24.5 : VSM_IMPORT_AUDIO=fichier.wav -- sur une piste neuve, sans boîte.
    bool importAudioForCapture(const juce::File& fichier) { return importAudioFileOnNewTrack(fichier); }

    /// Ouvre un dossier de projet au démarrage (VSM_PROJET=dossier), pour la
    /// même raison que `applyViewCommand` : ce qu'on a besoin de regarder est
    /// presque toujours un projet précis — une machine dans son rack, un
    /// arrangement — et le sélecteur de machine ne s'atteint qu'à la souris.
    /// Ouvre un projet pour l'autoportrait, et DIT si elle n'y arrive pas :
    /// le mode capture est piloté depuis un terminal, où une alerte graphique
    /// ne se voit pas.
    /// Importe un projet d'un autre DAW et rend le compte rendu affiché, pour
    /// que l'autoportrait puisse le montrer sans souris (VSM_IMPORT).
    bool importDawProjectForCapture(const juce::File& fichier);
    /// Montre le rapport de reconstruction du projet ouvert (§ 4.3 de
    /// docs/CDC-detection-multipiste.md) : partage d'énergie, machine et
    /// DENSITÉ de chaque piste — c'est là que « cette piste porte plusieurs
    /// parties » devient visible dans l'application, et non plus seulement
    /// dans un JSON que personne n'ouvre. Publique pour VSM_RAPPORT : cet
    /// écran doit se photographier sans souris.
    void showReconstructionReport();
    /// D19.2 : pose le filtre de la liste des pistes (VSM_FILTRE), pour que la
    /// capture montre le filtre à l'œuvre.
    /// D21.5 : le niveau demandé à l'export -- tel quel, crête à -1 dBFS,
    /// -14 LUFS (diffusion en flux), -23 LUFS (radiodiffusion).
    enum class ExportLevel { AsIs, PeakMinus1, Lufs14, Lufs23 };
    void setTrackFilterForCapture(const juce::String& texte) { trackList_.setFilterText(texte); }
    /// D20.5 : exporter le projet ouvert dans `file` sans fenêtre
    /// (VSM_EXPORT=fichier.flac), aux réglages par défaut -- la fréquence de
    /// la session, 24 bits, le morceau entier, deux secondes de queue. Le
    /// compte rendu va sur stderr : c'est un terminal qui pilote ce mode.
    bool exportForCapture(const juce::File& file, ExportLevel niveau = ExportLevel::AsIs);

    /// D50 : exporter les stems dans `dossier` sans fenêtre
    /// (VSM_EXPORT_STEMS=dossier, VSM_EXPORT_STEMS_FORMAT=float32|int24|int16,
    /// VSM_EXPORT_STEMS_PAR=piste|groupe). Le menu passe par deux modales que
    /// nulle capture ne traverse ; le compte rendu, lui, doit se relire, et
    /// c'est lui qui dit désormais la crête de chaque stem et ce que le format
    /// choisi rabote.
    bool exportStemsForCapture(const juce::File& dossier,
                                vsm::audio::io::SampleFormat format = vsm::audio::io::SampleFormat::Int24,
                                vsm::interchange::StemGranularity granularite =
                                    vsm::interchange::StemGranularity::Tracks);

    bool openProjectFolderForCapture(const juce::File& dossier) {
        const auto lu = vsm::interchange::loadProjectBundle(dossier.getFullPathName().toStdString());
        if (!lu.success) return false;
        loadProjectBundleFromFolder(dossier);
        return true;
    }

    // juce::MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

private:
    /// Applique en direct la couleur choisie dans le sélecteur à la piste
    /// visée. Un petit objet plutôt qu'une lambda : `ChangeListener` est une
    /// interface, et JUCE veut un objet qui lui survive le temps de la fenêtre.
    class ColourApplier : public juce::ChangeListener {
    public:
        ColourApplier(MainComponent& parent, size_t trackIndex)
            : parent_(parent), index_(trackIndex) {}
        void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    private:
        MainComponent& parent_;
        size_t index_;
    };
    /// Le premier changement de couleur ouvre l'action annulable ; les suivants
    /// -- un glissé dans le sélecteur en produit des dizaines -- s'y ajoutent.
    bool colourEditOpen_ = false;
    /// D11.4 : la même chose pour la couleur d'UN clip.
    class ClipColourApplier : public juce::ChangeListener {
    public:
        ClipColourApplier(MainComponent& parent, size_t trackIndex, uint64_t clipId)
            : parent_(parent), index_(trackIndex), clip_(clipId) {}
        void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    private:
        MainComponent& parent_;
        size_t index_;
        uint64_t clip_;
    };
    /// Le clip d'identifiant donné sur la piste donnée, ou nul.
    vsm::sequencer::Clip* findClip(size_t trackIndex, uint64_t clipId);

    enum MenuItemId {
        kMenuFileNewProject = 1,
        kMenuFileOpen,
    kMenuFileImportMidiIntoProject,
        kMenuFileOpenBundle,
        kMenuFileImportDaw,
        kMenuFileImportReport,
        kMenuFileReconstructionReport,
        kMenuFileParite,
        kMenuFileSave,
        kMenuFileSaveAs,
        kMenuFileLoadReference,
        kMenuFileReferenceOff,
        kMenuFileReferenceMix,
        kMenuFileReferenceSolo,
        kMenuFileReferenceCycle,
        kMenuFileExport,
        kMenuFileExportWav,
        kMenuFileExportStems,
        /// D23.3 : la piste choisie seule, en MIDI.
        kMenuFileExportTrackMidi,
        /// D24.5 : un fichier audio sur une piste neuve.
        kMenuFileImportAudio,
        kMenuFileAudioSettings,
        kMenuFileQuit,
        // D11.6 : modèle de projet et projets récents.
        kMenuFileSaveTemplate,
        kMenuFileStatistics,      ///< D32.5
        kMenuFileNewFromTemplate,
        kMenuFileRecentFirst,
        kMenuFileRecentLast = kMenuFileRecentFirst + 9,
        kMenuTrackAdd,
        kMenuTrackAddAudio,
        kMenuTrackAddGroup,
        kMenuTrackRemove,
        kMenuTrackDuplicate,
        /// D16.1 : créer un clip d'une mesure à la tête de lecture.
        kMenuTrackCreateClip,
        /// D16.5 : verrouiller ou déverrouiller la piste choisie.
        kMenuTrackLock,
        /// D17.4 : masquer la piste choisie, et tout réafficher.
        kMenuTrackHide,
        /// D21.2 : la piste choisie seule en solo (Ctrl+clic sur Solo fait de même).
        kMenuTrackSoloExclusive,
        kMenuTrackSoloSafe,      ///< D30.1
        kMenuTrackDisable,       ///< D30.2
        kMenuTrackCopyChain,     ///< D30.3
        kMenuTrackPasteChain,    ///< D30.3
        kMenuTrackAppendChain,   ///< D30.3
        kMenuTrackThinAutomation, ///< D30.5
        kMenuTrackMidiFxFirst,    ///< D31.4 : un identifiant par type d'effet MIDI
        kMenuTrackMidiFxLast = kMenuTrackMidiFxFirst + 7,
        kMenuTrackMidiFxClear,    ///< D31.4
        kMenuTrackMidiFxBake,     ///< D31.5
        kMenuTrackRenameSeries,   ///< D32.4
        kMenuTrackShowAll,
        /// D22.5 : la piste choisie comme preset, et un identifiant par preset
        /// du dossier `pistes` de la bibliothèque, pour l'appliquer.
        kMenuTrackSavePreset,
        /// D27.4 : le port MIDI de sortie de la piste choisie ((aucune) + 15 ports).
        kMenuTrackMidiOutNone,
        kMenuTrackMidiOutFirst,
        kMenuTrackMidiOutLast = kMenuTrackMidiOutFirst + 15,
        /// D28.2 / D28.3 : le programme MIDI (boîte), le canal d'entrée (Tous + 16).
        kMenuTrackMidiProgram,
        kMenuTrackInputChannelFirst,
        kMenuTrackInputChannelLast = kMenuTrackInputChannelFirst + 16,
        kMenuTrackPresetFirst,
        kMenuTrackPresetLast = kMenuTrackPresetFirst + 63,
        /// D18.3 : le groupe d'édition de la piste choisie (0 = aucun).
        kMenuTrackEditGroupNone,
        kMenuTrackEditGroupLast = kMenuTrackEditGroupNone + 8,
    kMenuEditInsertTimeAtLocators,
    kMenuEditDeleteTimeAtLocators,
    kMenuEditLocatorsFromSelection,
    /// D20.1 : répéter la sélection de l'arrangement, N fois ou jusqu'à la
    /// fin de la boucle ; et « tout sélectionner » dans le menu, pour que la
    /// sélection se fasse aussi sans souris (VSM_MENU).
    kMenuEditSelectAllClips,
    kMenuEditRepeatToLoopEnd,
    kMenuEditRepeatFirst,
    kMenuEditRepeatLast = kMenuEditRepeatFirst + 4,
    /// D20.3 : découper la sélection aux transitoires (clips audio).
    kMenuEditSliceAtOnsets,
    /// D20.4 : transcrire le clip audio choisi en MIDI (Python).
    kMenuEditTranscribeClip,
    /// D21.4 : la signature rythmique à la tête de lecture (2/4 … 7/8), et son retrait.
    kMenuEditSignatureRemove,
    kMenuEditSignatureFirst,
    kMenuEditSignatureLast = kMenuEditSignatureFirst + 5,
    /// D17.8 : LE GROOVE — l'extraire de la piste choisie, l'appliquer à la
    /// sélection du piano roll, l'enregistrer dans la bibliothèque, en charger
    /// un.
    kMenuEditExtractGroove,
    kMenuEditApplyGroove,
    kMenuEditSaveGroove,
    kMenuEditLoadGroove,
    /// D22.2 : aller à une mesure saisie (Maj+P, double-clic sur la position).
    kMenuEditGoToBar,
        kMenuTrackFreeze,
        kMenuTrackBounce,
        /// D18.1 : reporter en audio les CLIPS CHOISIS, sur une piste neuve.
        kMenuTrackBounceSelection,
        kMenuTrackPublishOutputs,
        kMenuTrackExplodeByPitch,
        kMenuTrackNewFolder, kMenuTrackFolderIn, kMenuTrackFolderOut,
        kMenuTrackClapPlugin,
        kMenuTrackVst3Plugin,
        kMenuTrackPluginEditor,
        kMenuTrackScanPlugins,
        kMenuTrackPluginFromCatalogue,
        kMenuRecordCountInNone,
        kMenuRecordCountInOne,
        kMenuRecordCountInTwo,
        kMenuRecordOverdub,
        kMenuRecordReplace,
        kMenuRecordStack,
        kMenuRecordQuantizeTake,
        /// D17.3 : récupérer ce qui vient d'être joué.
        kMenuRecordRetrospective,
        /// D18.2 : assembler les prises de la piste choisie.
        kMenuRecordCompTakes,
        kMenuRecordPunchToggle,
        kMenuRecordPunchFromLoop,
        kMenuRecordPunchClear,
        kMenuRecordMeasureLatency,
        kMenuRecordClearLatency,
        kMenuRecordMonitorInput,
        /// D23.2 : le mode d'écoute de l'entrée -- manuel, automatique, armée.
        kMenuRecordMonitorManual,
        kMenuRecordMonitorAuto,
        kMenuRecordMonitorArmed,
        /// D24.4 : couper toutes les notes (panic).
        kMenuRecordPanic,
        /// Un identifiant par prise de la piste sélectionnée, attribué à la
        /// suite -- comme les paliers d'échelle du menu Affichage.
        kMenuRecordTakeFirst,
        kMenuRecordTakeLast = kMenuRecordTakeFirst + 63,
        /// D57 : un identifiant par prise, pour la RETIRER du tiroir. Même
        /// forme que ci-dessus, et pour la même raison.
        kMenuRecordDeleteTakeFirst,
        kMenuRecordDeleteTakeLast = kMenuRecordDeleteTakeFirst + 63,
        kMenuMixAddSend,
        /// D23.5 : l'écoute en mono du master.
        kMenuMixMonoListen,
        /// D34.1 : la forme des fondus croisés, quatre entrées à la suite.
        /// Elles doivent rester CONTIGUËS et dans l'ordre de `FadeShape` :
        /// `menuItemSelected` en déduit la forme par soustraction.
        kMenuMixCrossfadeLinear,
        kMenuMixCrossfadeEqualPower,
        kMenuMixCrossfadeSlow,
        kMenuMixCrossfadeFast,
        /// Un identifiant par bus, pour le retirer.
        kMenuMixRemoveSendFirst,
        kMenuMixRemoveSendLast = kMenuMixRemoveSendFirst + 7,
        /// Vingt places par bus pour choisir son effet : treize aujourd'hui,
        /// de la marge pour ceux qui viendront.
        /// Un identifiant par bus, pour le commuter pré/post-fader.
        /// Un identifiant par bus, pour rendre son retour audible ou muet.
        kMenuMixSendReturnFirst,
        kMenuMixSendReturnLast = kMenuMixSendReturnFirst + 7,
        kMenuMixSendPreFaderFirst,
        kMenuMixSendPreFaderLast = kMenuMixSendPreFaderFirst + 7,
        kMenuMixSendEffectFirst,
        kMenuMixSendEffectLast = kMenuMixSendEffectFirst + 8 * 20 - 1,
        kMenuViewTracks,
        kMenuViewPianoRoll,
        kMenuViewSynthRack,
        kMenuViewMixer,
        kMenuViewArrangement,
        kMenuViewSingleWindow,
        kMenuViewFullScreen,
        /// D23.4 : toutes les pistes à la fenêtre, et trois hauteurs fixes.
        kMenuViewFitTracks,
        kMenuViewTrackHeightSmall,
        kMenuViewTrackHeightNormal,
        kMenuViewTrackHeightLarge,
        /// D34.4 : la règle de l'arrangement, en mesures ou en minutes:secondes.
        kMenuViewRulerBars,
        kMenuViewRulerTime,
        /// D34.5 : dessiner une automation par une forme. CINQ entrées
        /// CONTIGUËS, dans cet ordre : `menuItemSelected` en déduit la forme et
        /// le sens par soustraction.
        kMenuEditDrawAutomationRampUp,
        kMenuEditDrawAutomationRampDown,
        kMenuEditDrawAutomationSine,
        kMenuEditDrawAutomationTriangle,
        kMenuEditDrawAutomationSquare,
        /// D35.1 : monter et descendre la piste choisie. `moveTrack` existait,
        /// était testée, et aucun geste ne l'appelait.
        kMenuTrackMoveUp,
        kMenuTrackMoveDown,
        kMenuViewComputerKeyboard,
        // Un identifiant par palier d'échelle, attribué à la suite :
        // kMenuViewScaleFirst + index dans UiScale::steps().
        kMenuViewScaleFirst,
        kMenuViewScaleLast = kMenuViewScaleFirst + 15,
        // D73 : LA LANGUE DE L'INTERFACE. Un identifiant par langue, à la
        // suite, comme les paliers d'échelle -- et pour la même raison : la
        // liste est une donnée (`Langue::Choix`), pas une suite d'entrées
        // écrites à la main qu'il faudrait retoucher en ajoutant une langue.
        kMenuViewLangueFirst,
        kMenuViewLangueLast = kMenuViewLangueFirst + 7,
        // THREADS DE RENDU (D8.1). Le premier identifiant est « automatique » ;
        // les suivants valent kMenuAudioThreadsFirst + 1 + n threads auxiliaires.
        kMenuAudioThreadsFirst,
        kMenuAudioThreadsLast = kMenuAudioThreadsFirst + 32,
        kMenuViewMidiLearn,
        kMenuViewShortcuts,
        kMenuViewHistory,
        kMenuViewSpectrum,
        /// D18.6 : le bloc-notes du projet.
        kMenuViewProjectNotes,
        /// D18.4 : l'ordre de jeu.
        kMenuViewPlayOrder,
        kMenuFilePreferences,
        kMenuViewBrowser,
        kMenuFileReconstruct,
        kMenuFileChainFolder,
        kMenuHelpAbout,
    };

    // --- D9 : reconstruire depuis l'application -----------------------------
    //
    // LA RÈGLE QUI COMMANDE : le DAW se compile et fonctionne SANS Python
    // (§ 0, règle n° 2 de ROADMAP-daw.md). Rien ici ne lie l'interpréteur au
    // binaire ; on regarde si les fichiers de la chaîne existent, et on lance
    // un PROCESSUS quand ils existent.
    /// Relit l'état de la chaîne (au démarrage, et après un changement de
    /// chemin). Ne lance rien, ne peut pas échouer.
    void refreshReconstructionChain();
    /// Lance la reconstruction d'un fichier audio. Ne fait rien -- en le
    /// DISANT -- si la chaîne n'est pas disponible.
    void startReconstruction(const juce::File& audioFile);
    /// Demande où se trouve la chaîne, quand la recherche ne l'a pas trouvée.
    void chooseChainFolder();

    vsm::interchange::ReconstructionChain reconstructionChain_;
    vsm::app::ReconstructionRunner reconstructionRunner_;
    /// D20.4 : la transcription d'UN clip audio, dans un processus enfant.
    vsm::app::ClipTranscriber clipTranscriber_;
    vsm::app::ui::ReconstructionWindow reconstructionPanel_;
    std::unique_ptr<PanelWindow> reconstructionWindow_;
    juce::File reconstructionOutput_;

    // --- D10.2 : le MIDI learn se voit, se défait, et se souvient -----------
    /// Relit les associations enregistrées (démarrage).
    void loadMidiLearnMappings();
    /// Les écrit. Appelée dès que la carte change, jamais à la fermeture
    /// seulement : une application qui se termine mal ne doit pas faire perdre
    /// le câblage d'un studio.
    void saveMidiLearnMappings();
    /// Republie la liste dans la fenêtre.
    void refreshMidiLearnList();
    /// Applique ce que le thread MIDI a déposé et que lui seul ne pouvait pas
    /// appliquer : mixage et transport.
    void applyLearnedControls();

    vsm::app::ui::MidiLearnWindow midiLearnPanel_;
    std::unique_ptr<PanelWindow> midiLearnWindow_;
    /// Le nombre d'associations vu au dernier tour : un apprentissage arrive
    /// depuis le thread MIDI, sans prévenir personne, et c'est la seule façon
    /// de s'en apercevoir sans faire signer un contrat au thread MIDI.
    size_t midiLearnSeenCount_ = 0;
    std::vector<AudioEngine::LearnedControl> learnedDrain_;

    // --- D10.4 : sauvegarde automatique et récupération ---------------------
    /// Cherche une session interrompue et propose de la reprendre. Appelée UNE
    /// fois au démarrage, avant d'ouvrir la nôtre.
    void offerCrashRecovery();
    /// Prend une photo si le projet a changé et que le délai est écoulé.
    void autosaveIfNeeded();
    /// Le projet a été modifié depuis la dernière photo.
    void markProjectDirty() { projectDirty_ = true; }
    /// D36.2 : vide l'historique ET remet le repère de la sauvegarde
    /// automatique. Les deux vont ENSEMBLE depuis que le drapeau « sale » se
    /// déduit de la profondeur : la vider seule ferait croire, au prochain
    /// réveil, qu'on vient d'annuler autant de pas qu'elle en contenait.
    void clearHistory() { history_.clear(); lastAutosaveUndoDepth_ = 0; }
    /// D34.1 : la forme des fondus croisés du projet, changée en marche.
    /// Empruntée par l'entrée de menu ET par « VSM_VUE=fondu-croise:… ».
    void setCrossfadeShape(vsm::sequencer::FadeShape forme);
    /// Combien de bords de clips audio portent effectivement un fondu croisé.
    int audioSpansWithCrossfade() const;
    /// D34.2 : combien de clips MIDI partagent leur fenêtre avec un autre.
    int linkedMidiClipCount() const;
    /// D34.3 : combien de pistes audio le projet porte.
    size_t audioTrackCount() const;
    /// D35.1 : monte (`-1`) ou descend (`+1`) la piste choisie, avec ce
    /// qu'elle contient. Rend faux si le geste n'était pas possible.
    bool moveSelectedTrack(int direction);
    /// D35 : l'arbre des pistes sur une ligne — nom, profondeur, muet, solo.
    /// Ce que la vérification lit, faute de pouvoir juger douze lignes sur une
    /// capture d'écran.
    juce::String trackTreeForCapture() const;
    /// D34.4 : la règle en minutes:secondes (`true`) ou en mesures. Conservée
    /// d'une exécution à l'autre.
    void setRulerInTime(bool enTemps);
    /// D34.3 : pose sur des pistes neuves les fichiers audio du dernier dépôt.
    /// Ce que le bouton « Poser » appelle, et ce que la vérification appelle.
    void placeDroppedAudioOnTracks();

    std::unique_ptr<vsm::app::AutosaveService> autosave_;
    bool projectDirty_ = false;
    /// L'heure de la dernière photo. La cadence est de trente secondes : le
    /// critère dit « pas plus d'une minute », et une marge de deux vaut mieux
    /// qu'une marge nulle sur un disque qui hésite.
    double lastAutosaveSeconds_ = 0.0;
    /// D36.2 : la profondeur de l'historique à la dernière photo. Sert à
    /// DÉDUIRE qu'il y a eu édition, plutôt qu'à attendre qu'on le déclare.
    size_t lastAutosaveUndoDepth_ = 0;
    static constexpr double kAutosaveIntervalSeconds = 30.0;

    // --- D10.3 : les raccourcis se lisent et se changent --------------------
    /// La table effective : les défauts du catalogue plus ce que l'utilisateur
    /// a changé. Prêtée au piano roll, qui ne décide plus quelle touche fait
    /// quoi.
    vsm::interchange::ShortcutTable shortcuts_;
    vsm::app::ui::ShortcutsWindow shortcutsPanel_;
    vsm::app::ui::HistoryWindow historyPanel_;
    std::unique_ptr<PanelWindow> historyWindow_;
    /// D15.3 : l'analyseur de spectre du master, fenêtre flottante retenue.
    vsm::app::ui::SpectrumComponent spectrumPanel_;
    std::unique_ptr<PanelWindow> spectrumWindow_;
    void refreshHistoryList();
    /// D37.1 : LE NOM D'UNE PISTE S'AFFICHE À SEPT ENDROITS. Mesuré à l'écran :
    /// deux le suivaient (la ligne elle-même et l'arrangement, qui le lit au
    /// dessin), cinq montraient l'ancien -- le mélangeur, le rack, la liste
    /// déroulante de l'automation, la liste d'événements et la chaîne
    /// d'effets, tous rangeant le nom dans un widget à leur construction.
    /// Un seul endroit les rappelle, pour qu'un huitième panneau n'ait qu'une
    /// ligne à ajouter ici.
    void refreshTrackNamesEverywhere();
    /// D37.3 : pose la couleur d'une piste et la fait voir aux trois panneaux
    /// qui la montrent. Un seul chemin, partagé par le sélecteur de couleur et
    /// par `VSM_GESTE_PISTE` -- un second finirait par oublier un panneau.
    void appliquerCouleurDePiste(size_t index, juce::Colour couleur);
    std::unique_ptr<PanelWindow> shortcutsWindow_;
    void loadShortcuts();
    void saveShortcuts();
    void refreshShortcutList();
    /// Rassemble les réglages éparpillés dans deux menus (D10.3).
    void showPreferences();
    void refreshPreferences();
    vsm::app::ui::PreferencesWindow preferencesPanel_;
    std::unique_ptr<PanelWindow> preferencesWindow_;

    // --- D10.1 : le navigateur ---------------------------------------------
    /// Reconstruit l'inventaire : les machines du parc, puis les fichiers du
    /// projet et de la bibliothèque de l'utilisateur.
    void refreshBrowser();
    /// Applique une entrée à une piste. `trackIndex` vient du glisser-déposer
    /// ou de la sélection courante.
    void applyBrowserItem(const vsm::interchange::BrowserItem& item, size_t trackIndex);
    void applyBrowserDrop(size_t trackIndex, const juce::String& description);
    /// LE MÊME DÉPÔT, MAIS AVEC UNE POSITION (D10.1). C'est ce que la liste des
    /// pistes ne peut pas fournir, et c'est ce qui manquait pour poser un
    /// échantillon : il faut savoir quelle piste il devient ET où il commence.
    void applyBrowserDropAt(size_t trackIndex, vsm::midi::Tick tick,
                             const juce::String& description);
    /// Pose un fichier audio sur une piste, à un tick donné. Renvoie faux ET
    /// dit pourquoi si ce n'est pas possible.
    bool placeSampleOnTrack(size_t trackIndex, vsm::midi::Tick tick, const juce::File& fichier);

    vsm::app::ui::BrowserComponent browserPanel_;
    std::unique_ptr<PanelWindow> browserWindow_;
    /// La commande dont on attend la nouvelle touche, s'il y en a une.
    bool rebindPending_ = false;
    vsm::interchange::ShortcutId rebindTarget_{};
    /// Le morceau d'origine, retenu pour devenir la référence A/B (D9.4).
    juce::File reconstructionSource_;
    /// Le fichier qu'un glisser-déposer vient de proposer, retenu le temps que
    /// l'utilisateur réponde à la question.
    juce::File pendingDroppedAudio_;
    /// D34.3 : TOUS les fichiers audio du même lâcher, pour « Poser sur une
    /// piste ». `pendingDroppedAudio_` reste le seul que la reconstruction
    /// sait traiter -- elle analyse un morceau, pas un lot.
    juce::Array<juce::File> pendingDroppedAudios_;

    void timerCallback() override; // playhead, sync Play/Stop, CPU/sample rate (thread UI uniquement)

    // --- Enregistrement MIDI temps réel (D3.3) -----------------------------
    //
    // TROIS ÉTATS, ET PAS DEUX. Entre « à l'arrêt » et « en train
    // d'enregistrer » il y a le DÉCOMPTE, pendant lequel le transport joue déjà
    // (la tête de lecture est avant le zéro du morceau, voir
    // ProcessGraph::seekSeconds) mais où rien n'est encore capté. Le confondre
    // avec l'enregistrement ferait entrer dans la prise les notes jouées pour
    // se caler.
    enum class RecordPhase { Off, CountIn, Recording };

    // --- Threads de rendu (D8.1) -------------------------------------------
    //
    // LE RÉGLAGE EST « AUTOMATIQUE » PAR DÉFAUT, et il le reste tant que
    // personne n'y touche : la valeur enregistrée est alors -1, et non le
    // nombre calculé au premier lancement. La différence compte -- un chiffre
    // figé au premier démarrage suivrait la machine où le fichier de
    // préférences a été créé, pas celle où l'application tourne.
    static constexpr int kRenderThreadsAutomatic = -1;
    /// Le choix enregistré (-1 = automatique).
    int savedRenderThreadChoice() const;
    /// Combien de threads auxiliaires cela fait réellement, ici et maintenant.
    size_t effectiveRenderThreadCount() const;
    /// Enregistre le choix et l'applique au moteur sans attendre un
    /// redémarrage : le graphe sait changer de nombre de threads en marche.
    void setRenderThreadChoice(int choice);

    /// Les index des pistes dont le bouton R est enfoncé.
    std::vector<size_t> armedTrackIndices() const;
    /// Republie l'armement au moteur et met à jour le bouton Rec.
    void refreshArmedTracks();
    void startRecording();
    /// Clôt la prise et l'écrit dans les pistes armées. Sans effet si aucune
    /// prise n'est en cours.
    void stopRecording();
    /// Vide la file de capture du moteur dans l'enregistreur (thread UI).
    void drainRecording();
    /// Quantifie la dernière prise avec la grille du piano roll.
    void quantizeLastTake();
    /// Durée du décompte, en secondes, à la position d'entrée donnée.
    double countInSeconds(vsm::midi::Tick punchTick) const;
    /// Les pistes armées, réparties par nature : les notes vont aux unes, le
    /// fichier de la prise à l'autre.
    std::vector<size_t> armedTrackIndices(vsm::sequencer::Track::Kind kind) const;
    /// Un nom de fichier libre pour la prochaine prise, dans `audio/` du
    /// dossier de projet. Rend un chemin RELATIF, comme le format l'exige.
    juce::String nextTakeRelativePath(const juce::String& nomDePiste) const;
    /// Écrit la prise audio dans la piste : matériau et clip posé au point
    /// d'entrée. Rend faux si rien n'a été capté.
    bool applyAudioTake(size_t trackIndex, const juce::File& fichier, int64_t frames);
    /// Ferme une passe et l'empile comme prise sur les pistes armées. Appelée à
    /// chaque rebouclage pendant un enregistrement empilé, et une dernière fois
    /// à l'arrêt pour la passe en cours -- qui n'est pas forcément complète.
    void closePass(uint32_t passe, double debutSecondes, double finSecondes);
    /// Le point de sortie de la prise en cours : la fin de la région de punch
    /// si elle est active, sinon l'infini.
    double punchOutSeconds() const;
    /// Dit qu'un débordement du tampon d'écriture a troué le fichier.
    void signalerDisqueTropLent(uint64_t blocsPerdus);
    /// Lance la mesure de latence par boucle physique, puis affiche et adopte
    /// le résultat -- ou le REFUSE s'il n'est pas net, ce qui veut dire que
    /// rien n'est revenu par l'entrée.
    void measureInputLatency();
    /// Ouvre l'action annulable de l'enregistrement, UNE seule fois par prise :
    /// en boucle, les passes modifient le projet au fur et à mesure, et un
    /// instantané pris à l'arrêt ne défairait que la dernière.
    void ouvrirLEditionDEnregistrement();


    void openMidiFile();
    /// Enregistre le projet dans son dossier courant, ou demande où si le
    /// projet n'en a pas encore. Ctrl+S.
    void saveProject();
    void saveProjectAs();
    /// Écrit réellement le dossier de projet. Rend faux et l'a déjà dit à
    /// l'utilisateur en cas d'échec -- une sauvegarde qui échoue en silence
    /// serait pire que pas de sauvegarde du tout.
    bool writeProjectTo(const juce::File& folder);
    /// Rassemble dans `project_` ce que la session tient ailleurs : les
    /// courbes d'automation et la région de boucle. Les effets, eux, y sont
    /// déjà -- ils y sont écrits au fil des gestes.
    void captureSessionIntoProject();
    /// Republie les courbes du projet vers le moteur (chemin inverse du
    /// précédent), après un chargement.
    void applyAutomationFromProject();
    /// Fabrique et publie les effets des bus de départ décrits par le projet.
    ///
    /// Ils étaient DEUX, figés en dur sur une réverbération et un delay dans ce
    /// constructeur, et rien dans le projet ne disait ce que les boutons
    /// « send » du mixeur alimentaient.
    void applySendBuses();

    /// D73 : repose les libellés que les composants écrivent UNE FOIS à leur
    /// construction, après un changement de langue. La barre de menus n'y
    /// figure pas : elle se reconstruit à chaque ouverture.
    void retraduire();
    /// Les noms FRANÇAIS des onglets du dock, gardés parce que ce sont les
    /// clés de la table : relire le nom affiché ne marcherait qu'une fois.
    juce::StringArray nomsDesOnglets_;

    /// D71 : UNE RÉSERVE D'EFFET, NOTÉE PLUTÔT QU'AVALÉE. Un insert ou un bus
    /// de départ que cette version ne sait pas poser tel quel -- type inconnu,
    /// réglage inconnu. La phrase part sur la sortie d'erreur (`VSM_EFFET`)
    /// tout de suite, pour qu'un banc sans écran la relise, et s'accumule dans
    /// `reservesEffets_` que l'ouverture d'un projet verse à son écran de
    /// rapport. Le rendu hors ligne les nommait depuis toujours ; l'ouverture,
    /// non, et le même dossier avait donc deux verdicts.
    void noterReserveDEffet(const juce::String& reserve);
    std::vector<juce::String> reservesEffets_;

    /// D76 : QUELLE PISTE OCCUPE CHAQUE EMPLACEMENT DU GRAPHE (son `Track::uid`,
    /// 0 = aucune). C'est ce qui permet à `rebuildFromProject` de ne PAS
    /// recréer une machine dont la piste n'a pas bougé : il la recréait
    /// toujours, réglages remis à l'usine, à chaque ajout de piste et à chaque
    /// annulation (mesuré : 4 réglages sur 9 perdus par « Ajouter une piste »).
    std::vector<uint64_t> uidParEmplacement_;
    /// D76 : LES RÉGLAGES QU'AUCUNE MACHINE VIVANTE NE PORTE, par piste : ceux
    /// d'une piste désactivée (capturés quand sa machine est libérée), d'une
    /// piste dont la machine est absente de ce build (lus à l'ouverture), ou
    /// d'une piste qui change d'emplacement (capturés avant, reposés après).
    /// Enregistrer les écrit TELS QUELS ; jamais recapturés d'une machine neuve.
    std::map<uint64_t, vsm::interchange::SynthPreset> reglagesGardes_;
    /// Les presets à écrire ou à rendre : ceux des machines vivantes, capturés
    /// avec leurs échantillons, et ceux que `reglagesGardes_` tient pour les
    /// pistes qui n'en ont pas. UN SEUL endroit, pour l'enregistrement, la
    /// sauvegarde automatique et l'export : trois copies de cette boucle ont
    /// existé, et c'est ainsi que les trois oubliaient la même chose.
    std::map<size_t, vsm::interchange::SynthPreset> presetsDeLaSession();
    /// Un autre projet arrive : les emplacements et les réglages gardés de
    /// l'ancien ne le concernent pas.
    void oublierLesMachines();
    /// Republie les bus au moteur ET reconstruit les boutons du mixeur : leur
    /// nombre a pu changer, et une tranche garderait sinon un bouton vers un
    /// bus disparu.
    void sendBusesChanged();
    /// Rend leurs deux bus aux projets d'avant D4.2 qui avaient des niveaux
    /// d'envoi sans pouvoir dire vers quoi. Voir le .cpp pour la règle exacte.
    void adoptDefaultSendsIfNeeded();
    /// Le projet neuf reçoit deux départs, une réverbération et un delay : ce
    /// sont ceux qu'on veut neuf fois sur dix, et un mixeur sans aucun départ
    /// donnerait l'impression que la fonction a disparu.
    static std::vector<vsm::sequencer::SendBusDescription> defaultSendBuses();
    /// Réaccorde tout ce qui dépend de la fréquence d'échantillonnage réelle
    /// du périphérique : chaînes d'inserts et effets de bus. Appelée quand la
    /// carte son change de régime -- 48 kHz était écrit en dur, ce qui rendait
    /// faux tous les temps de delay et de réverbération à 44,1 kHz.
    void applyAudioConfig();
    /// Charge les fichiers des pistes audio et les publie au moteur.
    ///
    /// Sur le thread de l'interface, délibérément : décoder et rééchantillonner
    /// n'a rien à faire dans le rappel audio. Une piste de neuf minutes fait
    /// attendre l'interface le temps de la lire -- c'est visible, et c'est
    /// préférable à un chargement partiel qui jouerait du silence sans le dire.
    void loadAudioTracks();
    /// INSÉRER OU SUPPRIMER LA PLAGE ENTRE LES LOCATEURS (D13.3) : tout le
    /// morceau glisse -- notes, clips, contrôleurs, automation, repères,
    /// tempo, mesures, boucle et punch -- et ce qui est à cheval est coupé.
    /// Les locateurs sont la région de boucle, comme dans Cubase.
    void editTimeAtLocators(bool inserer);
    /// IMPORTER UN MIDI DANS LE PROJET (D14.3) : ses pistes s'ajoutent à la
    /// suite, posées à la tête de lecture ; « Ouvrir MIDI » le REMPLACE.
    void importMidiIntoProject(const juce::File& file);
    void chooseMidiToImport();
    /// LA RÉGION DE BOUCLE, POSÉE PARTOUT où elle se voit et s'entend (D14.1) :
    /// le projet, le transport, le moteur, les deux vues.
    void setLoopRegionEverywhere(vsm::midi::Tick start, vsm::midi::Tick end, bool active);
    /// LES LOCATEURS SUR LA SÉLECTION : les clips de l'arrangement, ou à
    /// défaut les notes du piano roll.
    void locatorsFromSelection();
    /// RETOUR AU DÉBUT À L'ARRÊT (D14.5) : la position d'où la lecture est
    /// partie, et l'état précédent du transport pour voir la transition.
    bool retourAuDepart_ = false;
    bool etaitEnLecture_ = false;
    vsm::midi::Tick departLecture_ = 0;
    /// Ouvre un DOSSIER de projet complet (project.json + MIDI + presets +
    /// échantillons) -- typiquement celui qu'écrit la chaîne d'analyse.
    void openProjectBundle();
    /// Ouvre un dossier de projet déjà désigné (sélecteur, ou chaîne de
    /// reconstruction qui vient de l'écrire — D9.3).
    /// `mediaFolder` diffère de `folder` pour une session récupérée : le
    /// projet vient de sa copie de travail, les médias de leur vrai dossier.
    /// Ouvre le sélecteur, importe, applique et MONTRE LE RAPPORT.
    void importDawProject();
    /// Applique un import déjà lu. Séparé du sélecteur pour la même raison que
    /// `loadProjectBundleFromFolder` : un import doit pouvoir arriver sans que
    /// personne ait cliqué (capture, ligne de commande), et suivre exactement
    /// le même chemin.
    bool applyDawImport(const juce::File& fichier);
    /// Rouvre le dernier rapport d'import de la session. Un rapport qu'on ne
    /// peut plus relire ne sert qu'à la seconde où il s'affiche ; or la
    /// question qu'il répond -- « pourquoi cette piste est-elle muette ? » --
    /// se pose une heure plus tard.
    void showLastImportReport();
    /// Le fichier `rapport.json` du projet ouvert, ou vide s'il n'en a pas.
    /// Un projet ouvert à la main n'en a pas, et c'est normal.
    juce::File rapportReconstruction_;

    /// Le panneau du rapport, POSÉ DANS LA FENÊTRE et non flottant : c'est le
    /// composant de contenu que photographie l'autoportrait (VSM_CAPTURE), donc
    /// la seule place d'où cet écran reste vérifiable sans souris.
    vsm::app::ui::ImportReportComponent importReport_;

    void loadProjectBundleFromFolder(const juce::File& folder,
                                      const juce::File& mediaFolder = juce::File());
    /// Charge l'enregistrement d'origine comme piste de référence, pour
    /// l'écoute A/B (étape 11.2).
    void loadReferenceAudio();
    /// Charge un original DÉJÀ désigné comme référence A/B (D9.4).
    void setReferenceAudioFile(const juce::File& file, bool silencieuxSiIllisible);
    void publierReference(vsm::app::ReferenceAudioResult&& result, const juce::File& file, bool activerEcoute);
    void chargerOriginalDuProjet(const juce::File& folder);
    void setReferenceMode(vsm::audio::engine::ReferenceTrack::Mode mode);
    /// Reconstruction -> les deux -> original -> reconstruction. Touche R,
    /// depuis n'importe quelle fenêtre, et bouton de la barre de transport.
    void cycleReferenceMode();
    /// Le bouton de transport et le menu disent le mode courant.
    void refreshListeningIndicator();

    // juce::KeyListener : les raccourcis GLOBAUX. Les panneaux sont des
    // fenêtres séparées, donc une touche pressée dans le piano roll ne remonte
    // jamais jusqu'ici par la hiérarchie des composants ; MainComponent
    // s'inscrit comme écouteur sur chaque fenêtre, et reçoit ce que le
    // composant qui a le focus n'a pas consommé.
    bool keyPressed(const juce::KeyPress& key, juce::Component* origin) override;
    /// Déplace la tête de lecture partout (transport, graphe) — D11.3.
    void seekAllViews(vsm::midi::Tick tick);
    using juce::Component::keyPressed;   // la surcharge du composant reste visible (sinon -Woverloaded-virtual)
    void newProject();
    // --- D11.6 : projets récents, modèle, plein écran ---------------------
    void rememberRecentProject(const juce::File& folder);
    juce::StringArray recentProjects() const;
    static juce::File templateFolder();
    void saveAsTemplate();
    void newFromTemplate();
    void toggleFullScreen();
    // --- D11.7 : le clavier d'ordinateur joue la piste choisie -------------
    /// Actif, il EMPRUNTE les lettres (A S D F… jouent des notes, Z et X
    /// changent d'octave) ; inactif, elles retrouvent leurs raccourcis.
    bool computerKeyboard_ = false;
    int computerKeyboardOctave_ = 0;      // décalage en octaves autour du do central
    /// Les touches tenues et la note qu'elles jouent : la note s'éteint quand
    /// la touche se relâche, pas quand le clavier répète.
    std::vector<std::pair<int, uint8_t>> computerKeysDown_;
    bool keyStateChanged(bool isKeyDown, juce::Component* origin) override;
    bool handleComputerKeyboard(const juce::KeyPress& key);
    /// Ajoute une piste. Une piste AUDIO n'est pas une autre espèce d'objet :
    /// c'est une piste dont le matériau est un fichier et non des notes (voir
    /// `Track::Kind`). Il n'existait aucun moyen d'en créer une depuis
    /// l'application -- elles ne pouvaient venir que d'un projet importé, ce qui
    /// rendait l'enregistrement audio de D3.4 inatteignable.
    /// `nom` vide = le nom automatique (« Piste 4 », « Audio 5 »). Fourni, il
    /// est posé AVANT que les vues soient refaites (D34.3) : renommer après
    /// coup ne rafraîchissait que la liste des pistes, et le mélangeur gardait
    /// « Audio 5 » sur une piste appelée « prise3 ».
    void addTrack(vsm::sequencer::Track::Kind kind = vsm::sequencer::Track::Kind::Midi,
                   const std::string& nom = {});
    void removeSelectedTrack();
    /// D11.5 : dupliquer la piste choisie, état de l'instrument compris.
    void duplicateSelectedTrack();
    /// GÈLE OU DÉGÈLE la piste sélectionnée (D5.5). Le gel rend ce qu'elle
    /// produit dans un fichier du dossier de projet et cesse de le recalculer ;
    /// le dégel efface le fichier et remet l'instrument en marche. Le matériau
    /// n'est jamais détruit -- ce serait un report, pas un gel.
    void toggleFreezeSelectedTrack();
    /// REPORTE la piste sélectionnée : son rendu REMPLACE son matériau, et elle
    /// devient une piste audio. C'est le même rendu que le gel, suivi d'une
    /// décision -- geler n'en est pas une, reporter en est une.
    void bounceSelectedTrack();
    /// Fait le report, une fois qu'il a été confirmé.
    void performBounce(size_t trackIndex);
    /// Le dossier `gel/` du projet, où vont les rendus de pistes gelées.
    juce::String frozenPathFor(size_t trackIndex) const;
    void exportMidiFile();
    /// D7.1 : charger un plugin CLAP tiers sur la piste sélectionnée.
    void loadClapPluginOnSelectedTrack();
    /// D7.2 : charger un instrument VST3 tiers sur la piste sélectionnée.
    void loadVst3PluginOnSelectedTrack();
    /// D7.3 : laisse choisir un EFFET tiers et rend son identifiant de fabrique.
    /// Propose d'abord ce que le balayage a trouvé (D7.5), le sélecteur de
    /// fichier ensuite.
    void chooseThirdPartyEffect(std::function<void(std::string)> quandChoisi);
    /// La seconde moitié : désigner un fichier à la main.
    void browseForThirdPartyEffect(std::function<void(std::string)> quandChoisi);
    /// D7.4 : ouvre la façade native du plugin de la piste sélectionnée.
    void openPluginEditorForSelectedTrack();
    /// D7.5 : lance le balayage des plugins installés, en tâche de fond.
    void scanInstalledPlugins();
    /// D7.5 : choisit un instrument PARMI CEUX DÉJÀ TROUVÉS, sans rouvrir de
    /// fichier -- c'est tout l'intérêt d'avoir balayé.
    void chooseInstrumentFromCatalogue();

    void exportAudioFile();
    /// Un WAV par piste (D6.2).
    void exportStems();
    /// La session mise en forme de projet chargé, pour le rendu.
    vsm::interchange::LoadedBundle bundleFromSession();
    /// La seconde moitié de l'export : choisir le fichier, puis rendre avec les
    /// options que l'utilisateur vient de fixer (D6.1).
    void exportAudioWithOptions(const vsm::interchange::RenderOptions& options,
                                ExportLevel niveau = ExportLevel::AsIs);
    /// D20.5 : le rendu du projet dans `file`, WAV, FLAC ou OGG selon
    /// l'extension -- le même rendu que `vsm-render`, puis JUCE transcode.
    /// Rend faux avec le message d'erreur dans `message`, vrai avec le compte
    /// rendu (durée, fréquence, profondeur, crête, avertissements).
    bool exportProjectToFile(const juce::File& file, const vsm::interchange::RenderOptions& options,
                             juce::String& message, ExportLevel niveau = ExportLevel::AsIs);
    /// Réécrit le WAV rendu dans `sortie` (WAV, FLAC ou OGG selon l'extension),
    /// en appliquant `gain` (1 = tel quel).
    bool transcodeRenderedWav(const juce::File& wav, const juce::File& sortie,
                              const vsm::interchange::RenderOptions& options, double gain,
                              juce::String& erreur);
    /// La sonie intégrée (LUFS) d'un WAV, par le mesureur du moteur.
    double measureLufsOf(const juce::File& wav);
    /// D50 : le rendu par stems dans `dossier`, et son compte rendu -- un
    /// fichier par piste (ou par groupe), la crête de chacun, et ce que le
    /// format demandé rabote. Partagé par le menu et par la capture, pour
    /// qu'il n'y ait qu'un seul texte à vérifier.
    bool exportStemsToFolder(const juce::File& dossier,
                             const vsm::interchange::RenderOptions& options,
                             vsm::interchange::StemGranularity granularite,
                             juce::String& message);
    /// Republie tout ce qui dépend du projet. `stopPlayback` est faux après un
    /// annuler/rétablir : l'utilisateur qui corrige une note pendant que ça
    /// joue n'a aucune raison de voir la lecture s'arrêter.
    void rebuildFromProject(bool stopPlayback = true);
    /// Prend l'instantané d'annulation du projet, avec le nom du geste.
    void beginProjectEdit(const juce::String& label);
    /// LES REPÈRES (D16.4) : posés, renommés, retirés depuis les DEUX règles
    /// (piano roll et arrangement) par les mêmes trois fonctions, et les deux
    /// vues rafraîchies ensemble.
    /// CRÉER UN CLIP (D16.1), d'une mesure, sur `trackIndex` à `tick`. Le seul
    /// endroit qui fabrique un clip à la demande : le double-clic de
    /// l'arrangement et l'article du menu Piste y passent tous les deux.
    void createClipOnTrack(size_t trackIndex, vsm::midi::Tick tick);
    /// D17.3 : pose sur la piste choisie les notes du tampon rétrospectif, à
    /// leur place réelle sur la ligne de temps. Annulable.
    void recoverRetrospectiveTake();
    /// D16.5 : bascule le cadenas de la piste choisie. Annulable, comme tout
    /// ce qui change le projet.
    void toggleLockSelectedTrack();
    /// D17.4 : masquer la piste choisie (les vues seulement, jamais le son),
    /// et réafficher toutes les pistes. Annulables.
    void hideSelectedTrack();
    /// D21.2 : cette piste seule en solo ; si elle l'était déjà seule, plus aucun solo.
    void soloTrackExclusively(size_t index);
    /// D21.4 : poser (ou retirer, numerator = 0) un changement de signature
    /// au début de la mesure qui contient la tête de lecture.
    void setTimeSignatureAtPlayhead(int numerator, int denominator);
    int derniereSignatureNum_ = 0, derniereSignatureDen_ = 0;
    void showAllTracks();
    /// D17.6 : rogne le clip à ce qui sonne, en relisant les échantillons du
    /// fichier. Annulable.
    void trimClipToSound(size_t trackIndex, uint64_t clipId);
    /// D20.3 : les clips audio choisis, coupés à chaque attaque trouvée.
    void sliceSelectedClipsAtOnsets();
    /// D21.3 : la coupe demandée à `tick` sur la piste, déplacée au passage
    /// par zéro le plus proche (±2 ms) lu dans la source que le moteur joue ;
    /// telle quelle sur une piste MIDI, un clip étiré, ou sans zéro à portée.
    /// `deplacementMs`, s'il est donné, reçoit de combien la coupe a bougé.
    vsm::midi::Tick snapCutToZeroCrossing(size_t trackIndex, vsm::midi::Tick tick,
                                          double* deplacementMs = nullptr);
    /// D20.4 : le premier clip audio choisi, transcrit en notes sur une piste neuve.
    void transcribeSelectedClip();
    /// D18.1 : rend hors ligne les clips choisis et les pose sur une piste
    /// audio neuve, à leur place. La piste d'origine n'est pas touchée --
    /// c'est ce qui distingue « reporter la sélection » de « reporter la
    /// piste », qui, lui, remplace le matériau.
    void bounceSelectionToNewTracks();
    /// D18.7b : une piste par sortie de la machine de la piste choisie.
    void publishInstrumentOutputsOfSelectedTrack();
    /// D19.3 : une piste par hauteur présente dans la piste choisie.
    void explodeSelectedTrackByPitch();
    /// D19.4 : crée un dossier au-dessus de la piste choisie et l'y range.
    void newFolderAboveSelectedTrack();
    /// D19.4 : fait entrer (+1) ou sortir (-1) la piste choisie d'un dossier.
    void changeSelectedTrackFolderDepth(int delta);
    /// D18.6 : ouvre le bloc-notes du projet.
    void showProjectNotes();
    /// D18.4 : ouvre l'ordre de jeu, sections relues depuis les repères.
    void showPlayOrder();
    /// D18.2 : ouvre l'assemblage des prises pour la piste choisie.
    void showTakeComp();
    std::unique_ptr<PanelWindow> takeCompWindow_;
    vsm::app::ui::TakeCompComponent takeCompPanel_;
    std::unique_ptr<PanelWindow> playOrderWindow_;
    vsm::app::ui::PlayOrderComponent playOrderPanel_;
    std::unique_ptr<PanelWindow> projectNotesWindow_;
    /// LE PANNEAU NE DÉTIENT RIEN : le texte vit dans `project_`, et l'éditeur
    /// l'y écrit à chaque frappe. Dupliquer l'état créerait une seconde
    /// vérité, et c'est toujours la seconde qui finit par mentir.
    juce::TextEditor projectNotesEditor_;
    /// D17.8 : les quatre gestes du groove. Le groove COURANT vit dans
    /// l'application et non dans le projet : c'est un outil qu'on porte d'un
    /// morceau à l'autre, comme un preset, pas une propriété du morceau.
    void extractGrooveFromSelectedTrack();
    void applyGrooveToSelection();
    void saveCurrentGroove();
    void loadGrooveFromLibrary();
    /// D22.2 : ALLER À UNE MESURE. La boîte de saisie, et la lecture d'un
    /// texte (« 17 », « 17.3 ») qui rend faux -- et le dit -- s'il n'est pas
    /// une position ; la tête et toutes les vues y vont.
    void promptGoToBar();
    bool goToBarText(const juce::String& texte);
    /// D22.5 : LES PRESETS DE PISTE. Le dossier `pistes` de la bibliothèque
    /// (sinon du projet, sinon des données de l'application, comme les
    /// grooves) ; la piste choisie écrite sous un nom ; un fichier appliqué à
    /// la piste choisie -- ses notes et ses clips restent, le reste est
    /// remplacé, la machine reçoit son état si le preset en a un.
    juce::File trackPresetFolder() const;
    juce::Array<juce::File> trackPresetFiles() const;
    void promptSaveTrackPreset();
    bool saveSelectedTrackAsPreset(const juce::String& nom);
    void applyTrackPresetFile(const juce::File& fichier);
    /// D22.4 : les derniers compteurs lus, pour n'allumer un voyant que sur
    /// ce qui vient d'arriver ou de partir.
    uint64_t midiInSeen_ = 0, notesOutSeen_ = 0;
    /// D23.2 : LE MODE D'ÉCOUTE DE L'ENTRÉE. 0 = manuel (l'interrupteur du
    /// menu), 1 = automatique (une piste audio armée, et le transport arrêté
    /// ou en enregistrement -- pas pendant une simple lecture), 2 = armée (une
    /// piste audio armée, toujours). Réglage d'application, retenu.
    int monitoringMode_ = 0;
    void applyMonitoringMode();
    /// D23.3 : la piste choisie seule, en MIDI -- la boîte, et l'écriture.
    void exportSelectedTrackMidi();
    bool writeSelectedTrackMidi(const juce::File& fichier);
    /// D24.2 : les contrôleurs captés, vidés avec les notes.
    std::vector<vsm::sequencer::RecordedControlEvent> recordControlDrain_;
    /// D24.5 : un fichier audio posé sur une piste neuve, à la mesure 1 -- la
    /// boîte, et le geste (la même fonction que le lâcher sur une piste).
    void importAudioFilePrompt();
    /// D25.2 : muet et solo de la piste choisie, par la même republication que
    /// les boutons ; la piste voisine, visible.
    void toggleMuteSelectedTrack();
    void toggleSoloSelectedTrack();
    void selectNeighbourTrack(int delta);
    /// D27.4 : le port de la piste choisie ; et la publication des ports de
    /// toutes les pistes au moteur, à chaque republication du projet.
    void setSelectedTrackMidiOutput(const std::string& port);
    void syncMidiOutputs();
    /// D28.2 / D28.3 : le programme et la banque de la piste choisie (boîte,
    /// et pose directe), le canal d'entrée.
    void promptMidiProgram();
    void setSelectedTrackMidiProgram(int programme, int banque);
    void setSelectedTrackInputChannel(int canal);
    /// D29.1 / D29.2 : les locateurs à la tête ; la tête déplacée de N temps ou N mesures.
    void setLoopBoundaryAtPlayhead(bool debut);
    void seekByBeats(int temps);
    void seekByBars(int mesures);
    bool importAudioFileOnNewTrack(const juce::File& fichier);
    vsm::sequencer::Groove grooveCourant_;
    /// Les trois vues qui dessinent des pistes, rafraîchies ensemble (D17.4).
    void refreshTrackViews();
    /// D30.1 : protège la piste choisie du solo des autres, ou lève la
    /// protection. Rien n'est republié au moteur au-delà du projet lui-même :
    /// c'est `trackAudible` qui lit le champ, à chaque bloc.
    void toggleSoloSafeSelectedTrack();
    /// D30.2 : retire la piste choisie du morceau, ou l'y remet. C'est le seul
    /// des trois qui REPUBLIE : l'instrument et les inserts sont défaits, puis
    /// refaits tels quels au retour.
    void toggleDisableSelectedTrack();
    /// D30.3 : LA CHAÎNE D'INSERTS COPIÉE D'UNE PISTE À L'AUTRE.
    ///
    /// Le presse-papier tient des DESCRIPTIONS (`TrackEffect`), pas des
    /// instances : identité, paramètres, état natif et contournement. C'est ce
    /// qui rend le collage sur la piste d'origine inoffensif -- on n'y duplique
    /// rien de vivant --, et ce qui fait qu'une chaîne collée sonne comme celle
    /// qu'on a copiée, effets tiers compris.
    void copySelectedTrackChain();
    /// `remplace` : la chaîne prend la place de celle qui est là. Sinon elle
    /// s'ajoute À LA SUITE, ce qui est l'autre geste qu'on fait vraiment
    /// (monter la même paire d'inserts derrière ce qui existe déjà).
    void pasteChainIntoSelectedTrack(bool remplace);
    /// La chaîne copiée, vide tant qu'on n'a rien copié.
    std::vector<vsm::sequencer::TrackEffect> chainClipboard_;
    /// D30.5 : réduit les points de TOUTES les courbes de la piste choisie.
    ///
    /// De la piste et non d'une courbe : une passe en W écrit le volume et le
    /// panoramique ensemble, et l'on veut nettoyer ce qu'on vient d'enregistrer,
    /// pas le nettoyer courbe par courbe. La tolérance est UN CENTIÈME de
    /// l'amplitude du paramètre, que l'application est seule à connaître --
    /// `core/` reçoit un nombre en unités du paramètre, comme partout.
    void thinAutomationOfSelectedTrack();
    /// D31.4 : ajoute un effet MIDI à la piste choisie, à ses valeurs par
    /// défaut. Les régler se fait dans le volet.
    void addMidiEffectToSelectedTrack(const std::string& type);
    /// D31.4 : vide la chaîne MIDI de la piste choisie.
    void clearMidiEffectsOfSelectedTrack();
    /// D31.5 : ÉCRIT dans les notes ce que la chaîne joue, puis vide la
    /// chaîne. C'est le geste qui rend un arpège définitif -- l'inverse exact
    /// de ce qu'est un effet, et c'est pourquoi il est nommé et annulable
    /// plutôt qu'implicite.
    void bakeMidiEffectsOfSelectedTrack();
    /// D31.5 : les pistes dont l'export `.mid` ne portera pas ce qu'on entend
    /// -- chaîne MIDI ou transposition de piste (D17.5). Vide : rien à dire.
    juce::StringArray tracksWhoseMidiExportWillDiffer() const;
    /// D32.1 : décode un fichier et le lance dans la pré-écoute du graphe.
    /// Ne touche pas au projet et n'ouvre aucune entrée d'historique --
    /// écouter n'est pas une modification.
    void auditionSample(const juce::File& fichier);
    /// D32.4 : renomme les pistes VISIBLES d'après un motif où `#` est
    /// remplacé par le numéro d'ordre. Rend le nombre de pistes renommées.
    /// Un motif vide ou sans `#` ne renomme RIEN plutôt qu'à moitié : quarante
    /// pistes qui porteraient toutes le même nom seraient pires qu'avant.
    size_t renameTracksInSeries(const juce::String& motif);
    /// D32.4 : demande le motif, puis applique.
    void promptRenameTracksInSeries();
    /// D32.5 : le compte rendu chiffré du projet, en texte.
    juce::String projectStatisticsText() const;
    void showProjectStatistics();
    /// D32.3 : écrire au journal le masque des notes sonnantes, à la demande.
    bool journalClavier_ = false;
    /// D32.3 : une note ÉPINGLÉE au clavier pour la capture. Tant qu'elle
    /// l'est, le rafraîchissement périodique ne l'efface pas -- sans quoi la
    /// touche s'allumerait puis s'éteindrait un vingtième de seconde plus
    /// tard, et l'autoportrait ne montrerait jamais rien.
    bool epingleClavier_ = false;
    /// D33.2 : LE FONDU DE SÉCURITÉ aux bords des clips audio, en
    /// millisecondes. Deux millisecondes par défaut -- assez pour supprimer un
    /// clic, trop court pour s'entendre comme un fondu. Zéro le désactive, et
    /// la lecture reprend exactement le chemin d'avant la phase. Conservé
    /// d'une exécution à l'autre.
    double safetyFadeMs_ = vsm::audio::engine::kDefaultSafetyFadeMs;
    /// D33.3 : un scrub est en cours, et le transport jouait-il avant lui ?
    /// C'est ce qui permet de le remettre dans l'état où on l'a trouvé.
    bool scrubEnCours_ = false;
    bool scrubJouaitDeja_ = false;
    /// D33.1 : une piste neuve PAR FICHIER. Ce qui échoue est nommé et
    /// n'arrête pas le reste.
    void importAudioFiles(const juce::Array<juce::File>& fichiers);
    /// D33.5 : ouvre un `.mid` SANS sélecteur de fichier, par le même chemin
    /// que « Ouvrir MIDI... ». Un écran qu'on ne peut atteindre qu'à la souris
    /// est un écran qu'on ne peut pas déclarer vérifié.
    void openMidiFileDirect(const juce::File& fichier);
    /// LA FENÊTRE IMPLICITE SE MATÉRIALISE (D16.1) : toute piste qui porte du
    /// matériau et aucun clip en reçoit un, « tout à zéro » -- exactement le
    /// passage que l'ordonnanceur fabriquait déjà pour elle, à l'échantillon
    /// près. Appelée à l'ouverture d'un projet ET après chaque écriture de
    /// notes : sans ce second appel, des notes posées au piano roll sur une
    /// piste neuve n'apparaissaient dans l'arrangement qu'après avoir
    /// sauvegardé et rouvert. Rend vrai si elle a créé quelque chose.
    bool materializeImplicitClips();
    void requestMarker(vsm::midi::Tick tick);
    void renameMarker(size_t index);
    void removeMarker(size_t index);
    void refreshMarkerViews();
    void refreshTransportSchedule();
    void updateSynthRackForSelection();
    void togglePanel(PanelWindow& window);

    // --- FENÊTRE UNIQUE (défaut). Les cinq panneaux s'ancrent DANS ce
    // composant au lieu de flotter chacun dans sa fenêtre : ce qu'on regarde
    // ensemble doit vivre ensemble. Les PanelWindow restent construites --
    // « Fenêtres flottantes » du menu Affichage rend l'ancienne disposition,
    // et le choix survit au redémarrage (fichier de préférences).
    // Le CENTRE montre l'arrangement OU le piano roll : les deux montrent le
    // même morceau à deux échelles, on passe de l'un à l'autre (déjà la règle
    // du mode flottant, où ils partagent le même emplacement d'écran).
    /// Poignée entre deux volets ancrés : on la tire, le volet se
    /// redimensionne, la taille survit au redémarrage. C'est ce qui rend la
    /// fenêtre unique HABITABLE -- une disposition qu'on ne peut pas ajuster
    /// est une disposition qu'on subit.
    class SeparateurDock : public juce::Component {
    public:
        explicit SeparateurDock(bool vertical) : vertical_(vertical) {
            setMouseCursor(vertical ? juce::MouseCursor::LeftRightResizeCursor
                                     : juce::MouseCursor::UpDownResizeCursor);
        }
        std::function<void()> onDebut;
        std::function<void(int)> onGlisse;  ///< delta depuis le début du geste
        std::function<void()> onFin;        ///< moment d'écrire la préférence
        void mouseDown(const juce::MouseEvent&) override { if (onDebut) onDebut(); }
        void mouseDrag(const juce::MouseEvent& e) override {
            if (onGlisse) onGlisse(vertical_ ? e.getDistanceFromDragStartX()
                                              : e.getDistanceFromDragStartY());
        }
        void mouseUp(const juce::MouseEvent&) override { if (onFin) onFin(); }
        void paint(juce::Graphics& g) override {
            g.fillAll(juce::Colour(0xff232327));
            g.setColour(juce::Colour(0xff3d3d44));
            const auto c = getLocalBounds().toFloat();
            if (vertical_) g.fillRect(c.getCentreX() - 1.0f, c.getY() + 4.0f, 2.0f, c.getHeight() - 8.0f);
            else g.fillRect(c.getX() + 4.0f, c.getCentreY() - 1.0f, c.getWidth() - 8.0f, 2.0f);
        }
    private:
        bool vertical_;
    };

    void dockPanels();
    void undockPanels();
    void layoutDockedPanels(juce::Rectangle<int> area);
    bool singleWindow_ = true;
    /// D58 : vrai quand `VSM_TAILLE` a fixé la taille de la fenêtre socle ;
    /// la disposition en fenêtre unique ne la recouvre alors plus.
    bool tailleImposee_ = false;
    bool centerShowsArrangement_ = false;
    // Tailles des volets ancrés, en points, conservées d'une session à l'autre.
    int dockGauche_ = 300;
    int dockDroite_ = 380;
    /// D16.8 : 282 et non 260 -- la tranche de console a gagné la rangée du
    /// bouton W, et à 260 le fader perdait sa poignée. La hauteur reste
    /// réglable et retenue ; c'est le DÉFAUT qui suit ce que la tranche
    /// demande, plutôt que la tranche qui se serre.
    int dockBas_ = 282;
    int dockBase_ = 0;  // taille au début du geste en cours
    SeparateurDock sepGauche_ { true };
    SeparateurDock sepDroite_ { true };
    SeparateurDock sepBas_ { false };
    void showAboutDialog();
    void showAudioSettings();
    /// Écrit le choix du périphérique audio dans les préférences.
    void saveAudioDeviceState();
    /// Change la taille de toute l'interface, et l'enregistre.
    void setUiScale(float factor);

    VsmLookAndFeel lookAndFeel_;

    vsm::sequencer::Project project_;
    /// L'annulation du DAW : elle porte sur le PROJET, donc sur tout ce que
    /// l'utilisateur peut modifier -- notes, mixage, effets, pistes, repères,
    /// clips -- et non sur les seules notes de la piste affichée.
    vsm::sequencer::ProjectHistory history_;
    size_t maxAssignedTracks_ = 0; // plus haut nombre de pistes déjà assignées au ProcessGraph (pour nettoyer les slots après suppression)
    AudioEngine audioEngine_;
    // DÉCLARÉ APRÈS `audioEngine_`, ET CE N'EST PAS UN DÉTAIL DE STYLE : il
    // garde une référence sur le graphe que le moteur possède, donc il doit
    // être construit après lui et détruit avant.
    vsm::audio::engine::Transport transport_;

    /// D17.5 : combien de notes la transposition fait sortir de la plage MIDI,
    /// pour ne le dire qu'au franchissement.
    size_t notesPerduesParTransposition_ = 0;
    vsm::sequencer::MidiRecorder recorder_;
    /// D17.3 : LE TAMPON RÉTROSPECTIF, alimenté dès que l'application tourne
    /// et non seulement pendant l'enregistrement -- un tampon qui ne se
    /// remplirait qu'une fois l'enregistrement lancé ne servirait à rien.
    /// Quatre mille événements : environ mille notes, de quoi rattraper une
    /// improvisation entière, pour deux cents kilo-octets.
    vsm::sequencer::RetrospectiveBuffer retrospectif_{4096};
    RecordPhase recordPhase_ = RecordPhase::Off;
    /// Le POINT D'ENTRÉE de la prise en cours, en secondes et en ticks : la
    /// position à laquelle on a appuyé sur Rec, et non celle où le décompte a
    /// commencé.
    double punchSeconds_ = 0.0;
    vsm::midi::Tick punchTick_ = 0;
    /// Nombre de MESURES de décompte (0, 1 ou 2), conservé d'une exécution à
    /// l'autre comme l'échelle d'interface.
    int countInBars_ = 1;
    vsm::sequencer::RecordMode recordMode_ = vsm::sequencer::RecordMode::Overdub;
    /// Les notes de la dernière prise, par piste : ce sur quoi porte
    /// « Quantifier la prise ».
    std::vector<std::pair<size_t, vsm::sequencer::NoteSelection>> lastTake_;
    /// Tampon de drainage, réutilisé pour ne pas allouer à chaque tour du timer.
    std::vector<vsm::sequencer::RecordedNoteEvent> recordDrain_;
    /// La passe de boucle en cours, et le nombre de rebouclages déjà traités :
    /// c'est en comparant au compteur du moteur qu'on sait qu'une passe vient
    /// de se terminer.
    uint64_t loopPassesClosed_ = 0;
    /// Le fichier d'une prise audio en boucle est UNIQUE pour toute la session
    /// d'enregistrement ; chaque passe en est une fenêtre. On garde donc la
    /// position du début de la session dans le fichier pour la calculer.
    double audioTakeSessionStartSeconds_ = 0.0;
    /// Le débordement de la file de capture ne se dit qu'UNE fois par prise :
    /// une alerte par tour de timer serait un mur de fenêtres.
    bool recordDropReported_ = false;
    /// L'action annulable de la prise en cours est-elle déjà ouverte.
    bool recordEditOpened_ = false;
    /// Dernier état connu de la carte son, pour ne rafraîchir le bouton Rec
    /// que lorsqu'il change et non à chaque tour du timer.
    bool recordDeviceWasOpen_ = false;
    /// La piste audio qui reçoit la prise en cours, et le fichier qu'on lui
    /// écrit. `npos` quand la prise est purement MIDI.
    size_t audioTakeTrack_ = static_cast<size_t>(-1);
    juce::File audioTakeFile_;
    juce::String audioTakeRelativePath_;

    std::vector<vsm::audio::engine::AutomationLane> currentAutomation_;
    bool mixDirty_ = false; // vol/pan/mute/solo modifiés -> republier le snapshot au timer

    TransportBarComponent transportBar_;

    // Contenu des panneaux flottants (déclarés AVANT les PanelWindow qui les
    // référencent -- l'ordre d'initialisation des membres suit l'ordre de
    // déclaration en C++, pas l'ordre de la liste d'initialisation).
    TrackListComponent trackList_;
    PianoRollComponent pianoRoll_;
    VelocityLaneComponent velocityLane_;
    PianoRollPanel pianoRollPanel_;
    SynthRackComponent synthRack_;

    /// LA VUE D'ARRANGEMENT (D5.1). Une fenêtre flottante comme les autres :
    /// on arrange en la regardant à côté du piano roll, pas à sa place.
    ArrangementComponent arrangement_;
    MixerComponent mixer_;
    AutomationComponent automation_;
    EffectChainComponent effectChain_;
    juce::TabbedComponent bottomTabs_ { juce::TabbedButtonBar::TabsAtTop };
    /// Ce qui est chargé en référence, en clair : nom du fichier, décodeur,
    /// fréquence, canaux, durée. Rempli au chargement, affiché dans le menu
    /// Fichier -- un MP3 décodé sonne comme un WAV, et il faut bien un endroit
    /// où lire lequel des deux on écoute.
    juce::String referenceDescription_;

    /// Dossier du projet ouvert (vide = jamais enregistré). Ce que « Ctrl+S »
    /// réécrit sans rien demander.
    juce::File currentProjectFolder_;
    /// LE CACHE D'APERÇU DES FORMES D'ONDE (D5.7), par index de piste.
    ///
    /// CONSTRUIT LÀ OÙ LE FICHIER EST DÉJÀ DÉCODÉ -- dans `loadAudioTracks` --
    /// et non sur un thread de fond, parce qu'il n'y a rien à gagner à en
    /// lancer un : mesuré, neuf minutes de stéréo coûtent 21 ms de calcul de
    /// cache par-dessus un décodage qui en coûte cent fois plus, et que ce
    /// projet a déjà choisi de faire sur le thread de l'interface (voir
    /// `loadAudioTracks`). Un thread de plus n'aurait déplacé que 21 ms, en
    /// échange d'une synchronisation à tenir juste.
    ///
    /// CE QUI COMPTE VRAIMENT est ailleurs : le DESSIN ne parcourt jamais le
    /// fichier. Mesuré à 0,08 ms par rafraîchissement pour neuf minutes, quelle
    /// que soit leur longueur -- c'est cela, « s'affichent sans bloquer
    /// l'interface ».
    std::map<size_t, std::shared_ptr<const std::vector<vsm::audio::io::PeakBin>>> waveformCache_;

#if VSM_WITH_CLAP || VSM_WITH_VST3
    /// LE BALAYAGE EN COURS (D7.5), ou nullptr. Gardé par l'application parce
    /// qu'il doit survivre au menu qui l'a lancé : c'est ce que « en tâche de
    /// fond » veut dire.
    std::unique_ptr<vsm::app::plugins::PluginScanner> pluginScanner_;
    /// Ce que le dernier balayage a trouvé. Relu au démarrage : refaire un
    /// balayage de deux cents fichiers à chaque lancement serait absurde.
    vsm::interchange::PluginCatalogue pluginCatalogue_;
#endif

#if VSM_WITH_VST3
    /// LA FENÊTRE DE LA FAÇADE NATIVE D'UN PLUGIN (D7.4), une par piste.
    ///
    /// GARDÉE PAR L'APPLICATION ET NON PAR LA PISTE : c'est un objet
    /// d'interface, pas une propriété du morceau. La refermer ne perd rien --
    /// l'état vit dans le plugin, la fenêtre n'en montre qu'un dessin -- et
    /// c'est ce qui rend « fermable sans perte d'état » vrai par construction
    /// plutôt que par précaution.
    std::map<size_t, std::unique_ptr<juce::DocumentWindow>> pluginEditorWindows_;
#endif

    /// Dernière configuration audio appliquée aux effets, pour ne refabriquer
    /// que lorsqu'elle change réellement.
    double appliedSampleRate_ = 0.0;
    /// D51.2 : LA FRÉQUENCE À LAQUELLE LES PISTES AUDIO SONT DÉJÀ CHARGÉES.
    /// `applyAudioConfig()` rechargeait tout au premier passage du minuteur --
    /// `appliedSampleRate_` part de zéro -- même quand l'ouverture du projet
    /// venait de le faire à la MÊME fréquence. Mesuré : douze chargements pour
    /// six pistes. Recharger n'a de sens que si la fréquence a changé.
    double audioTracksLoadedAtRate_ = 0.0;

    MidiCcComponent midiCc_;
    /// D32.2 : la liste des événements de la piste choisie -- la seule vue qui
    /// montre les programmes, les plis et les pressions.
    vsm::app::ui::EventListComponent eventList_;
    TempoLaneComponent tempoLane_;

    PanelWindow trackListWindow_;
    PanelWindow pianoRollWindow_;
    PanelWindow synthRackWindow_;
    PanelWindow mixerWindow_;
    PanelWindow arrangementWindow_;

#if !JUCE_MAC
    // Sur macOS, le menu s'affiche dans la barre système (setMacMainMenu) ;
    // sur Windows/Linux, il faut un composant de barre de menu explicite.
    juce::MenuBarComponent menuBarComponent_;
#endif
};
