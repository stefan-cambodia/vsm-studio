#pragma once
#include <JuceHeader.h>
#include "vsm/sequencer/Project.h"
#include "vsm/sequencer/MidiRecorder.h"
#include "vsm/sequencer/ProjectHistory.h"
#include "vsm/sequencer/RealtimeTransport.h"
#include "audio/AudioEngine.h"
#include "ui/TransportBarComponent.h"
#include "ui/TrackListComponent.h"
#include "ui/PianoRollComponent.h"
#include "ui/VelocityLaneComponent.h"
#include "ui/PianoRollPanel.h"
#include "ui/SynthRackComponent.h"
#include "ui/MixerComponent.h"
#include "ui/AutomationComponent.h"
#include "ui/EffectChainComponent.h"
#include "ui/PanelWindow.h"
#include "ui/LookAndFeel/VsmLookAndFeel.h"
#include "vsm/audio/engine/ReferenceTrack.h"

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
// Deux transports coexistent délibérément (voir ARCHITECTURE.md section 6) :
// RealtimeTransport (Phase 1, thread MIDI dédié) reste la référence pour la
// position affichée dans le piano roll ; AudioEngine::processGraph() est
// synchronisé dessus à chaque changement d'état Play/Stop (voir
// timerCallback()) et c'est LUI qui produit réellement le son.
class MainComponent : public juce::Component,
                       public vsm::sequencer::IMidiEventSink,
                       public juce::MenuBarModel,
                       private juce::KeyListener,
                       private juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // IMidiEventSink : appelé depuis le thread du RealtimeTransport, JAMAIS
    // le thread UI. Toujours no-op : c'est AudioEngine::processGraph() qui
    // déclenche réellement les notes des synthés -- ce hook reste
    // disponible pour un futur MIDI-thru vers du matériel externe.
    void onMidiEvent(size_t trackIndex, const vsm::midi::MidiEventData& data) override;

    /// À appeler par MainWindow (Main.cpp) UNE FOIS que la fenêtre socle a
    /// une position d'écran réelle (après centreWithSize/setVisible) --
    /// sinon les fenêtres flottantes se positionneraient par rapport à des
    /// coordonnées d'écran non définies (voir le .cpp pour le pourquoi).
    void showFloatingPanels();

    // juce::MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

private:
    enum MenuItemId {
        kMenuFileNewProject = 1,
        kMenuFileOpen,
        kMenuFileOpenBundle,
        kMenuFileSave,
        kMenuFileSaveAs,
        kMenuFileLoadReference,
        kMenuFileReferenceOff,
        kMenuFileReferenceMix,
        kMenuFileReferenceSolo,
        kMenuFileReferenceCycle,
        kMenuFileExport,
        kMenuFileExportWav,
        kMenuFileAudioSettings,
        kMenuFileQuit,
        kMenuTrackAdd,
        kMenuTrackRemove,
        kMenuRecordCountInNone,
        kMenuRecordCountInOne,
        kMenuRecordCountInTwo,
        kMenuRecordOverdub,
        kMenuRecordReplace,
        kMenuRecordQuantizeTake,
        kMenuViewTracks,
        kMenuViewPianoRoll,
        kMenuViewSynthRack,
        kMenuViewMixer,
        // Un identifiant par palier d'échelle, attribué à la suite :
        // kMenuViewScaleFirst + index dans UiScale::steps().
        kMenuViewScaleFirst,
        kMenuViewScaleLast = kMenuViewScaleFirst + 15,
        kMenuHelpAbout,
    };

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
    /// Ouvre un DOSSIER de projet complet (project.json + MIDI + presets +
    /// échantillons) -- typiquement celui qu'écrit la chaîne d'analyse.
    void openProjectBundle();
    /// Charge l'enregistrement d'origine comme piste de référence, pour
    /// l'écoute A/B (étape 11.2).
    void loadReferenceAudio();
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
    using juce::Component::keyPressed;   // la surcharge du composant reste visible (sinon -Woverloaded-virtual)
    void newProject();
    void addTrack();
    void removeSelectedTrack();
    void exportMidiFile();
    void exportAudioFile();
    /// Republie tout ce qui dépend du projet. `stopPlayback` est faux après un
    /// annuler/rétablir : l'utilisateur qui corrige une note pendant que ça
    /// joue n'a aucune raison de voir la lecture s'arrêter.
    void rebuildFromProject(bool stopPlayback = true);
    /// Prend l'instantané d'annulation du projet, avec le nom du geste.
    void beginProjectEdit(const juce::String& label);
    void refreshTransportSchedule();
    void updateSynthRackForSelection();
    void togglePanel(PanelWindow& window);
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
    vsm::sequencer::RealtimeTransport transport_;
    AudioEngine audioEngine_;
    bool audioWasPlaying_ = false;

    vsm::sequencer::MidiRecorder recorder_;
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
    /// Le débordement de la file de capture ne se dit qu'UNE fois par prise :
    /// une alerte par tour de timer serait un mur de fenêtres.
    bool recordDropReported_ = false;
    /// Dernier état connu de la carte son, pour ne rafraîchir le bouton Rec
    /// que lorsqu'il change et non à chaque tour du timer.
    bool recordDeviceWasOpen_ = false;

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
    /// Dernière configuration audio appliquée aux effets, pour ne refabriquer
    /// que lorsqu'elle change réellement.
    double appliedSampleRate_ = 0.0;

    juce::Label mixerPlaceholder_;
    juce::Label automationPlaceholder_;
    juce::Label midiCcPlaceholder_;

    PanelWindow trackListWindow_;
    PanelWindow pianoRollWindow_;
    PanelWindow synthRackWindow_;
    PanelWindow mixerWindow_;

#if !JUCE_MAC
    // Sur macOS, le menu s'affiche dans la barre système (setMacMainMenu) ;
    // sur Windows/Linux, il faut un composant de barre de menu explicite.
    juce::MenuBarComponent menuBarComponent_;
#endif
};
