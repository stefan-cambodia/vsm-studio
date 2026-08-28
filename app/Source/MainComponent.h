#pragma once
#include <JuceHeader.h>
#include "vsm/sequencer/Project.h"
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

    void openMidiFile();
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
    void rebuildFromProject();
    void refreshTransportSchedule();
    void updateSynthRackForSelection();
    void togglePanel(PanelWindow& window);
    void showAboutDialog();
    void showAudioSettings();
    /// Change la taille de toute l'interface, et l'enregistre.
    void setUiScale(float factor);

    VsmLookAndFeel lookAndFeel_;

    vsm::sequencer::Project project_;
    size_t maxAssignedTracks_ = 0; // plus haut nombre de pistes déjà assignées au ProcessGraph (pour nettoyer les slots après suppression)
    vsm::sequencer::RealtimeTransport transport_;
    AudioEngine audioEngine_;
    bool audioWasPlaying_ = false;
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
