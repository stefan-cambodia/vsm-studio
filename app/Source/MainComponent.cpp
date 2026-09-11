#include <filesystem>
#include "vsm/audio/plugin/ISampleLoader.h"
#include "MainComponent.h"
#include "vsm/sequencer/ClipEdit.h"
#include "vsm/sequencer/NoteEdit.h"
#include "vsm/sequencer/PlayOrder.h"
#include "vsm/interchange/GroovePreset.h"
#include "vsm/audio/io/OnsetDetection.h"
#include "vsm/audio/io/ZeroCrossing.h"
#include "vsm/audio/dsp/LufsMeter.h"
#include "vsm/audio/io/SilenceDetection.h"
#include "vsm/sequencer/TimeEdit.h"
#include "vsm/sequencer/ProjectImport.h"
#include "vsm/interchange/DawImport.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include "vsm/midi/MidiFileParser.h"
#include "vsm/midi/MidiFileWriter.h"
#include "vsm/sequencer/MidiEffects.h"
#include "vsm/sequencer/EventList.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/audio/io/WavFileWriter.h"
#include "vsm/audio/effect/Reverb.h"
#include "vsm/audio/effect/Delay.h"
#include <algorithm>
#include <chrono>
#include <thread>
#include <cstddef>
#include <cstdint>
#include <limits>
#include "vsm/audio/effect/EffectFactory.h"
#include "vsm/interchange/ParameterDescriptor.h"
#include "vsm/interchange/EffectDescription.h"
#include "vsm/interchange/EffectPreset.h"
#include "vsm/interchange/OfflineReconstruction.h"
#include "ui/DrumVoiceNames.h"
#if VSM_WITH_CLAP
#include "ClapPluginHost.h"
#include "ClapPluginWindow.h"
#endif
#if VSM_WITH_VST3
#include "Vst3PluginHost.h"
#include "Vst3PluginWindow.h"
#endif
#include "vsm/interchange/ProjectBundle.h"
#include "vsm/interchange/ReconstructionReport.h"
#include "vsm/interchange/SynthPreset.h"
#include "vsm/interchange/TrackPreset.h"
#include <cstring>
#include "audio/ReferenceAudioLoader.h"
#include "vsm/audio/io/AudioTrackLoader.h"
#include "vsm/audio/io/WavFileReader.h"
#include "ui/UiScale.h"
#include "ui/Langue.h"
#include "vsm/interchange/Json.h"
#include "ui/Shortcuts.h"

using namespace vsm::sequencer;
using namespace vsm::midi;
using vsm::audio::engine::TransportState;
// D73 : `tr()` sans qualification dans tout ce fichier -- il porte la barre
// de menus entière, et « vsm::app::ui::tr » devant chaque libellé rendrait
// illisible ce qu'on lit justement pour vérifier un libellé.
using vsm::app::ui::tr;

namespace {
/// D95 : UNE BOÎTE, ET SA PHRASE AU TERMINAL. Une boîte modale demandée au
/// démarrage d'un banc n'est plus là au moment de la photo : c'est la course
/// que D72 a mesurée (une photo sur sept), revenue en entier sous un écran
/// verrouillé -- 0 boîte sur 7, six fenêtres de premier niveau et aucun
/// composant modal deux secondes après le geste. Ce que la boîte dit s'écrit
/// donc AUSSI sur la sortie d'erreur, au moment où elle est demandée et dans la
/// langue affichée : `VSM_BOITE : titre : message`, retours à la ligne rendus
/// par « / ». Même raison que les réserves d'effet (D71) : un banc sans souris
/// doit pouvoir relire la phrase.
void montrerBoite(juce::MessageBoxIconType icone, const juce::String& titre, const juce::String& message) {
    std::fputs(("VSM_BOITE : " + titre + " : " + message.replace("\n", " / ") + "\n").toRawUTF8(), stderr);
    juce::AlertWindow::showMessageBoxAsync(icone, titre, message);
}

/// D104 : UNE QUESTION À DEUX BOUTONS, lue quand elle est demandée -- comme
/// `montrerBoite` (D95), avec ses deux réponses. Sous un écran verrouillé, JUCE
/// ne montre pas la boîte ; sa ligne `VSM_BOITE` reste.
void demanderOuiNon(juce::MessageBoxIconType icone, const juce::String& titre, const juce::String& message,
                    const juce::String& oui, const juce::String& non, juce::Component* parent,
                    juce::ModalComponentManager::Callback* suite) {
    std::fputs(("VSM_BOITE : " + titre + " : " + message.replace("\n", " / ") + " : [" + oui + " | " + non
                + "]\n").toRawUTF8(), stderr);
    juce::AlertWindow::showOkCancelBox(icone, titre, message, oui, non, parent, suite);
}
/// D121 : UNE BOÎTE DONT LE TEXTE NE DÉCIDE PLUS LA LARGEUR. `AlertWindow` met
/// son message en page à une largeur tirée du texte lui-même (`300 + 2·√(h ×
/// largeur)`), AVANT de s'élargir pour ses boutons et ses composants : à 150 %,
/// « « 17.3 » » et « Batterie 2 » s'y coupaient (D119, D120). Ses BLOCS de texte,
/// eux, sont mis en page APRÈS, à 0,8 fois la largeur finale : le texte passe
/// donc dans un bloc, et une CALE vide impose la largeur de la ligne la plus
/// longue, mesurée dans la police des messages. Chaque paragraphe tient alors
/// sur une ligne. Le bloc est aligné à gauche -- c'est ce que cela change à l'œil.
///
/// PAS D'ICÔNE (second essai) : JUCE centre un bloc à 10 % du bord, sans l'espace
/// qu'il réserve à l'icône pour un message -- le « ? » du renommage passait sous
/// le texte. L'icône demandée par l'appelant est donc ignorée, et c'est dit ici.
class BoiteLisible final : public juce::AlertWindow {
public:
    BoiteLisible(const juce::String& titre, const juce::String& texte, juce::MessageBoxIconType /*icone*/)
        : juce::AlertWindow(titre, juce::String(), juce::MessageBoxIconType::NoIcon) {
        const auto police = getLookAndFeel().getAlertWindowMessageFont();
        float plusLongue = 0.0f;
        juce::StringArray lignes;
        lignes.addLines(texte);
        for (const auto& ligne : lignes)
            plusLongue = std::max(plusLongue, juce::GlyphArrangement::getStringWidth(police, ligne));
        // + 8 : le bloc se met en page à « largeur − 8 » ; + 6 : le TextEditor coupe à
        // sa largeur moins son retrait (4 à gauche, 2 à droite, `getMaximumTextWidth`)
        // ; + 18 de marge. Le premier essai (+16) laissait « 1. » seul sur sa ligne.
        cale_.setSize(static_cast<int>(std::ceil(plusLongue)) + 32, 1);
        addCustomComponent(&cale_);
        addTextBlock(texte);
    }

private:
    // Détruite AVANT la boîte (membre d'une classe dérivée) : un composant se
    // retire de son parent en mourant, et `AlertWindow` ne possède pas ses
    // composants ajoutés.
    juce::Component cale_;
};

/// D126 : `montrerBoite`, PAR `BoiteLisible`. Pour une boîte dont la coupure
/// dépend d'un nombre écrit dans la phrase : la boîte statique de JUCE garde,
/// faute de mieux, la largeur où les deux dernières lignes sont les PLUS
/// déséquilibrées, et « it. » restait seul pour 3 blocs perdus (D125) --
/// retoucher le texte pour 3 l'aurait laissé céder pour un autre compte. Même
/// ligne `VSM_BOITE` que `montrerBoite` ; l'icône tombe, comme en D121.
void montrerBoiteLisible(juce::MessageBoxIconType icone, const juce::String& titre, const juce::String& message) {
    std::fputs(("VSM_BOITE : " + titre + " : " + message.replace("\n", " / ") + "\n").toRawUTF8(), stderr);
    auto* fenetre = new BoiteLisible(titre, message, icone);
    fenetre->addButton("OK", 0, juce::KeyPress(juce::KeyPress::returnKey),
                       juce::KeyPress(juce::KeyPress::escapeKey));
    fenetre->enterModalState(true, nullptr, true);
}
} // namespace

MainComponent::MainComponent()
    : transport_(audioEngine_.processGraph()),
      transportBar_(transport_),
      velocityLane_(pianoRoll_),
      pianoRollPanel_(pianoRoll_, velocityLane_),
      trackListWindow_("Pistes", trackList_),
      pianoRollWindow_("Piano Roll", pianoRollPanel_),
      synthRackWindow_("Synth Rack", synthRack_),
      mixerWindow_("Mixer", bottomTabs_),
      arrangementWindow_("Arrangement", arrangement_) {
#if VSM_WITH_CLAP
    // D7.1 : LES IDENTIFIANTS `clap:` DEVIENNENT CHARGEABLES, ici et une seule
    // fois. Tout le reste -- le graphe, le format de projet, le rendu hors
    // ligne -- continue de ne parler que d'identifiants d'instrument, sans
    // savoir que certains désignent des machines qu'on n'a pas écrites.
    vsm::clap::installClapResolver();
#endif
#if VSM_WITH_VST3
    // D7.2 : ET LES INSTRUMENTS VST3. Les deux résolveurs s'ENCHAÎNENT au lieu
    // de s'écraser, si bien que l'ordre de ces deux appels n'a aucune
    // importance -- une règle d'ordre serait exactement ce qu'on oublierait.
    vsm::vst3::installVst3Resolver();
#endif

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

    // L'ONGLET MIDI CC ÉDITE POUR DE VRAI : il n'était qu'un libellé qui
    // renvoyait aux lanes du piano roll, lesquelles n'éditent pas les CC.
    midiCc_.setHistory(&history_);
    midiCc_.onCcEdited = [this] { refreshTransportSchedule(); };
    // LA PISTE DE TEMPO (D3.2, enfin dessinée) : chaque geste republie le
    // projet au moteur, qui suit la carte à chaque bloc, et la barre de
    // transport montre le tempo de départ.
    tempoLane_.setHistory(&history_);
    tempoLane_.onTempoEdited = [this] {
        refreshTransportSchedule();
        audioEngine_.processGraph().setProject(project_);
        transportBar_.setBpm(project_.tempoMap.bpmAt(0));
        pianoRollPanel_.refresh();
    };
    // D73 : LES NOMS FRANÇAIS SONT GARDÉS À PART. Un onglet posé porte son
    // libellé TRADUIT ; pour le re-traduire en cours de séance, il faut la clé,
    // c'est-à-dire le français. Le relire dans l'onglet ne marcherait qu'une
    // fois -- au second changement de langue, on chercherait « Effects » dans
    // une table indexée par « Effets ».
    nomsDesOnglets_ = { "Mixer", "Automation", "Effets", "MIDI CC", "Liste", "Tempo" };
    // D122 : les boutons des zones de la fenêtre unique (placés par la disposition).
    for (int i = 0; i < 4; ++i) {
        addChildComponent(boutonsDeZone_[i]);
        boutonsDeZone_[i].onClick = [this, i] { basculerZoneAgrandie(i); };
    }
    bottomTabs_.addTab(tr("Mixer"), vsm::ui::Palette::panel, &mixer_, false);
    bottomTabs_.addTab(tr("Automation"), vsm::ui::Palette::panel, &automation_, false);
    bottomTabs_.addTab(tr("Effets"), vsm::ui::Palette::panel, &effectChain_, false);
    bottomTabs_.addTab(tr("MIDI CC"), vsm::ui::Palette::panel, &midiCc_, false);
    // D32.2 : APRÈS « MIDI CC » et avant « Tempo ». Sa voisine de gauche
    // montre les contrôleurs en courbe ; celle-ci montre TOUT en nombres, y
    // compris les quatre familles que rien ne montrait.
    bottomTabs_.addTab(tr(u8"Liste"), vsm::ui::Palette::panel, &eventList_, false);
    bottomTabs_.addTab(tr("Tempo"), vsm::ui::Palette::panel, &tempoLane_, false);

    // D11 : l'historique visible. Un clic sur un pas y revient par autant
    // d'annulations (ou de rétablissements) qu'il faut, par le MÊME chemin
    // que Ctrl+Z — le piano roll, qui republie le projet restauré.
    spectrumPanel_.setTap(&audioEngine_.processGraph().spectrumTap());
    spectrumPanel_.sampleRateProvider = [this] { return audioEngine_.currentSampleRate(); };
    historyPanel_.onUndoSteps = [this](size_t pas) {
        for (size_t i = 0; i < pas; ++i) pianoRoll_.undo();
        refreshHistoryList();
    };
    historyPanel_.onRedoSteps = [this](size_t pas) {
        for (size_t i = 0; i < pas; ++i) pianoRoll_.redo();
        refreshHistoryList();
    };
    transportBar_.onOpenMidiFile = [this] { openMidiFile(); };
    transportBar_.onExportMidiFile = [this] { exportMidiFile(); };
    transportBar_.onCycleListening = [this] { cycleReferenceMode(); };
    refreshListeningIndicator();

    trackList_.onTrackSelected = [this](size_t idx) {
        pianoRoll_.setActiveTrackIndex(idx);
        updateSynthRackForSelection();
        effectChain_.setActiveTrack(static_cast<int>(idx));
        midiCc_.setActiveTrackIndex(idx);
        eventList_.setActiveTrack(static_cast<int>(idx));   // D32.2
        audioEngine_.setLiveInputTrack(idx); // un clavier MIDI joue la piste sélectionnée
    };
    // D36.1 : LA LISTE DES PISTES REJOINT L'HISTORIQUE. Neuf de ses gestes
    // écrivaient dans la piste sans empiler de pas et sans marquer le projet à
    // photographier -- `beginProjectEdit` fait les deux, et c'est pour cela
    // qu'ils passent par lui plutôt que par `history_` directement.
    trackList_.onEditStarted = [this](const juce::String& libelle) { beginProjectEdit(libelle); };
    trackList_.onRenamed = [this] { refreshTrackNamesEverywhere(); };
    trackList_.onTracksChanged = [this] {
        refreshTransportSchedule();
        // D36.7 : LES DEUX PANNEAUX SE DISENT LA MÊME CHOSE. Le muet et le solo
        // vivent ici ET dans la tranche du mélangeur ; chacun posait son bouton
        // à sa construction et ne le relisait jamais. Rendre une piste muette
        // dans la liste laissait donc le M du mélangeur éteint -- et l'inverse
        // aussi. Deux affichages d'une même valeur qui se contredisent, c'est
        // la panne muette sous sa forme la plus ordinaire.
        mixer_.refreshMuteSolo();
        mixer_.refreshFromTracks();   // D37.2 : le fader suit le curseur de la ligne
    };
    trackList_.onInstrumentChanged = [this](size_t idx, const std::string& pluginId) {
        // D76 : la piste a reçu une machine (ou « aucune ») par un choix : la
        // demande d'une machine absente de ce build ne s'écrira plus.
        if (idx < project_.tracks.size()) project_.tracks[idx].requestedInstrumentId.clear();
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
    mixer_.onExclusiveSoloRequested = [this](size_t index) { soloTrackExclusively(index); };
    // D16.8 : la console écrit l'automation en jouant, et il lui faut la
    // position du transport et son état -- deux choses qu'elle ne peut pas
    // connaître seule.
    mixer_.playheadTickProvider = [this] { return transport_.currentTick(); };
    mixer_.transportPlayingProvider = [this] {
        return transport_.state() == TransportState::Playing;
    };
    mixer_.onAutomationWritten = [this] {
        applyAutomationFromProject();
        arrangement_.repaint();
        markProjectDirty();
    };
    mixer_.onMixChanged = [this] {
        mixDirty_ = true;
        markProjectDirty();
        trackList_.refreshMuteSolo();   // D36.7 : l'autre sens du même accord
        trackList_.refreshMix();        // D37.2 : et le curseur suit le fader
        // D17.5 : une note poussée hors de 0..127 par la transposition ne
        // sonne pas, et cela se DIT -- une fois par franchissement, pas à
        // chaque cran du curseur, sinon régler le chiffre serait impossible.
        const size_t perdues = vsm::sequencer::PlaybackScheduler::transposeDroppedNotes(project_);
        // DIT UNE FOIS, au franchissement, et pas à chaque cran du curseur :
        // une alerte par demi-ton rendrait le réglage inutilisable, et une
        // alerte qui ne vient jamais laisserait chercher la note manquante.
        if (perdues > 0 && notesPerduesParTransposition_ == 0)
            montrerBoite(
                juce::AlertWindow::InfoIcon, tr(u8"Transposition"),
                tr(perdues > 1 ? u8"%1 notes ne sonneront pas" : u8"%1 note ne sonnera pas")
                        .replace("%1", juce::String(static_cast<int>(perdues)))
                    + tr(u8" : la transposition les pousse hors de la plage MIDI (0 à 127). "
                         u8"Elles ne sont pas repliées à l'octave — les faire sonner à une "
                         u8"hauteur que personne n'a demandée serait pire. Le matériau, lui, "
                         u8"n'a pas bougé : remettez la transposition à zéro et tout revient."));
        notesPerduesParTransposition_ = perdues;
    };
    mixer_.onMasterParam = [this](vsm::audio::plugin::ParamId id, float v) {
        audioEngine_.processGraph().masterBus().setParameter(id, v);
    };
    // D23.5 : l'écoute en mono, du bouton MONO comme du menu Mixage.
    mixer_.onMonoListen = [this](bool on) { audioEngine_.processGraph().masterBus().setMonoListen(on); };
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
    // D71 : les réserves des inserts remontent par le MÊME canal que celles des
    // bus de départ, pour qu'un projet n'ait qu'un seul endroit où se plaindre.
    effectChain_.onEffectReserve = [this](size_t piste, const juce::String& reserve) {
        noterReserveDEffet(juce::String::fromUTF8(vsm::interchange::libellePiste(piste).c_str())
                           + " : " + reserve);
    };
    effectChain_.onChainChanged =
        [this](size_t track, std::shared_ptr<const EffectChainComponent::Chain> chain) {
            audioEngine_.processGraph().setTrackEffectChain(track, chain);
        };
    // D31.4 : une chaîne MIDI changée change ce qui est JOUÉ. Le planning est
    // donc refait -- sans quoi le réglage ne s'entendrait qu'à la prochaine
    // relecture, ce qui est intenable pour un arpège qu'on règle à l'oreille.
    // D32.2 : la liste retire un événement -- même grammaire que partout
    // ailleurs : un instantané avant, une republication après.
    // D32.3 : une note jouée au clavier de l'écran part par le MÊME chemin que
    // le clavier d'ordinateur (D11.7). Deux chemins pour une seule idée
    // finiraient par ne plus jouer pareil -- et c'est celui-là qui sait déjà
    // trouver la piste armée.
    // D33.3 : LE SCRUB. On lance le transport à la vitesse du geste et on le
    // remet où on l'a trouvé au relâchement -- si le morceau jouait déjà, il
    // continue ; s'il était à l'arrêt, il s'arrête.
    pianoRollPanel_.onScrub = [this](vsm::midi::Tick tick, double vitesse) {
        if (vitesse <= 0.0) {
            audioEngine_.processGraph().setPlaybackSpeed(1.0);
            if (!scrubJouaitDeja_) transport_.stop();
            scrubEnCours_ = false;
            return;
        }
        if (!scrubEnCours_) {
            scrubEnCours_ = true;
            scrubJouaitDeja_ = audioEngine_.processGraph().isPlaying();
        }
        transport_.seekToTick(tick);
        audioEngine_.processGraph().seekSeconds(project_.ticksToSeconds(tick));
        arrangement_.setPlayheadTick(tick);
        audioEngine_.processGraph().setPlaybackSpeed(vitesse);
        if (!audioEngine_.processGraph().isPlaying()) transport_.play();
    };
    pianoRollPanel_.onKeyboardNote = [this](int note, float velo, bool on) {
        audioEngine_.playComputerKey(static_cast<uint8_t>(juce::jlimit(0, 127, note)),
                                      static_cast<uint8_t>(juce::jlimit(1, 127,
                                          static_cast<int>(velo * 127.0f))), on);
    };
    eventList_.onEditStarted = [this](const juce::String& libelle) { beginProjectEdit(libelle); };
    eventList_.onEventsChanged = [this] {
        refreshTransportSchedule();
        refreshTrackViews();
        pianoRollPanel_.refresh();
    };
    eventList_.onSeekRequested = [this](vsm::midi::Tick tick) {
        transport_.seekToTick(tick);
        audioEngine_.processGraph().seekSeconds(project_.ticksToSeconds(tick));
        arrangement_.setPlayheadTick(tick);
    };
    effectChain_.onMidiChainChanged = [this] {
        refreshTransportSchedule();
        refreshTrackViews();
    };

    // Le projet est donné APRÈS le rappel : la toute première publication des
    // chaînes part alors vers le moteur au lieu de tomber dans le vide.
    effectChain_.setProject(&project_);
    // D15.4 : où les presets d'effet se lisent (bibliothèque et projet) et
    // où ils s'écrivent (la bibliothèque si elle est réglée, sinon le projet,
    // sinon le dossier des préférences) ; le navigateur les voit aussitôt.
    effectChain_.presetFoldersProvider = [this] {
        std::vector<juce::File> dossiers;
        const juce::String bibliotheque =
            vsm::app::ui::UiScale::properties().getValue("dossierBibliotheque", "");
        if (bibliotheque.isNotEmpty()) dossiers.emplace_back(bibliotheque);
        if (currentProjectFolder_ != juce::File()) dossiers.push_back(currentProjectFolder_);
        return dossiers;
    };
    effectChain_.presetSaveFolderProvider = [this] {
        const juce::String bibliotheque =
            vsm::app::ui::UiScale::properties().getValue("dossierBibliotheque", "");
        if (bibliotheque.isNotEmpty()) return juce::File(bibliotheque).getChildFile("effets");
        if (currentProjectFolder_ != juce::File()) return currentProjectFolder_.getChildFile("effets");
        return vsm::app::ui::UiScale::properties().getFile().getParentDirectory().getChildFile("effets");
    };
    effectChain_.onPresetsChanged = [this] { refreshBrowser(); };

#if VSM_WITH_CLAP || VSM_WITH_VST3
    // D7.5 : LE CATALOGUE EST RELU, PAS REFAIT. Rouvrir deux cents fichiers à
    // chaque lancement coûterait des secondes pour un résultat identique --
    // et ferait payer à chaque fois la chute d'un plugin fautif.
    pluginCatalogue_ = vsm::app::plugins::loadCatalogue();
#endif

#if VSM_WITH_CLAP || VSM_WITH_VST3
    // D7.3 : LA VUE DEMANDE « UN IDENTIFIANT D'EFFET », L'APPLICATION SAIT OÙ
    // LES TROUVER. `EffectChainComponent` ne connaît ni CLAP ni VST3 -- elle
    // sait seulement que la fabrique acceptera ce qu'on lui rendra.
    effectChain_.setPluginEffectChooser([this](std::function<void(std::string)> quandChoisi) {
        chooseThirdPartyEffect(std::move(quandChoisi));
    });
#endif

    // Les bus de départ viennent désormais DU PROJET (D4.2) : voir
    // `applySendBuses`, appelée par `rebuildFromProject`. Le projet vide de
    // démarrage reçoit les deux qu'on veut neuf fois sur dix -- un mixeur sans
    // aucun départ donnerait l'impression que la fonction a disparu.
    project_.sends = defaultSendBuses();

    // REPÈRES : posés sur la règle, nommés tout de suite. Un repère sans nom
    // ne repère rien, et c'est pourquoi l'interface demande le nom au moment de
    // la pose plutôt que d'en créer un « Repère 3 » à renommer plus tard.
    pianoRollPanel_.onMarkerRequested = [this](vsm::midi::Tick tick) { requestMarker(tick); };
    pianoRollPanel_.onMarkerRenameRequested = [this](size_t index) { renameMarker(index); };
    pianoRollPanel_.onMarkerRemoved = [this](size_t index) { removeMarker(index); };
    // D16.4 : la règle de l'arrangement fait les mêmes trois gestes.
    arrangement_.onMarkerRequested = [this](vsm::midi::Tick tick) { requestMarker(tick); };
    arrangement_.onMarkerRenameRequested = [this](size_t index) { renameMarker(index); };
    arrangement_.onMarkerRemoved = [this](size_t index) { removeMarker(index); };

    pianoRoll_.setHistory(&history_);
    pianoRoll_.onProjectRestored = [this] { rebuildFromProject(false); refreshHistoryList(); };
    pianoRoll_.setProject(&project_);
    // D16.1 : LES NOTES ÉCRITES SE MATÉRIALISENT TOUT DE SUITE. Avant, une
    // piste neuve où l'on venait d'écrire ne montrait aucun clip dans
    // l'arrangement tant qu'on n'avait pas sauvegardé et rouvert le projet.
    pianoRoll_.onNotesEdited = [this] {
        if (materializeImplicitClips()) arrangement_.repaint();
        refreshTransportSchedule();
    };
    arrangement_.onClipCreationRequested = [this](size_t piste, vsm::midi::Tick tick) {
        createClipOnTrack(piste, tick);
    };
    arrangement_.onClipSliceAtOnsetsRequested = [this] { sliceSelectedClipsAtOnsets(); };
    arrangement_.onClipTranscribeRequested = [this] { transcribeSelectedClip(); };
    arrangement_.cutSnapProvider = [this](size_t piste, vsm::midi::Tick tick) { return snapCutToZeroCrossing(piste, tick); };
    arrangement_.onClipTrimToSoundRequested = [this](size_t piste, uint64_t clipId) {
        trimClipToSound(piste, clipId);
    };
    // D16.3 : ce qui n'a pas pu être joint est DIT, avec la raison. Un
    // Ctrl+J qui ne fait rien et se tait laisse chercher pourquoi.
    // D16.5 : le cadenas se DIT quand il refuse. Un clip qui ne bouge pas et
    // ne dit rien laisse chercher la panne ailleurs.
    arrangement_.onLockRefused = [](size_t refuses) {
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Piste verrouillée"),
            tr(refuses > 1 ? u8"%1 clips appartiennent à une piste verrouillée"
                           : u8"%1 clip appartient à une piste verrouillée")
                    .replace("%1", juce::String(static_cast<int>(refuses)))
                + tr(u8" et n'ont pas bougé. Piste ▸ Déverrouiller la piste pour "
                     u8"reprendre le montage. Une piste verrouillée continue de "
                     u8"sonner et de se mixer : seul le montage est refusé."));
    };
    pianoRoll_.onLockRefused = [] {
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Piste verrouillée"),
            tr(u8"Les notes de cette piste ne s'éditent pas tant qu'elle est "
               u8"verrouillée. Piste ▸ Déverrouiller la piste pour reprendre."));
    };
    arrangement_.onJoinRefused = [](size_t refuses) {
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Joindre des clips"),
            tr(refuses > 1 ? u8"%1 jonctions n'ont pas pu se faire" : u8"%1 jonction n'a pas pu se faire")
                    .replace("%1", juce::String(static_cast<int>(refuses)))
                + tr(u8" : deux clips ne se joignent que s'ils se touchent sur la ligne "
                     u8"de temps ET que leur fenêtre se prolonge — c'est-à-dire si le "
                     u8"second est exactement ce qu'une coupe aurait produit du premier. "
                     u8"Un clip bouclé, un clip qui suit le tempo, ou deux réglages de "
                     u8"gain, de phase ou de sens différents ne se joignent pas."));
    };
    // LA SAISIE PAS À PAS (D13.5) : le piano roll arme le moteur, le moteur
    // poste la note, le piano roll l'écrit. Un seul chemin pour le clavier
    // MIDI et le clavier d'ordinateur, puisque le second passe par le premier.
    pianoRoll_.onStepInputChanged = [this](bool armee) { audioEngine_.setStepInputArmed(armee); };
    audioEngine_.onStepInputNote = [this](uint8_t note, uint8_t velocity) {
        pianoRoll_.stepInputNote(note, velocity);
    };
    // D36.3 : basculer un pas RÉÉCRIT les notes de la piste. Il lui fallait
    // donc un pas d'historique -- il n'en avait aucun.
    synthRack_.onEditStarted = [this](const juce::String& libelle) { beginProjectEdit(libelle); };
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
    transportBar_.onMetronomeToggled = [this](bool actif) {
        audioEngine_.processGraph().setMetronomeEnabled(actif);
    };
    transportBar_.onRecordToggled = [this](bool demarrer) {
        if (demarrer) startRecording(); else stopRecording();
    };
    // L'ARRÊT CLÔT LA PRISE. Sans ce fil, appuyer sur Stop laisserait
    // l'enregistrement ouvert : on aurait joué, et rien ne serait écrit.
    transportBar_.onStopPressed = [this] { stopRecording(); };
    // D18.5 : LA VITESSE DE LECTURE. Elle va au graphe et à lui seul -- ni le
    // projet ni le tempo ne bougent, et rien n'en est écrit dans le fichier :
    // c'est un réglage de séance, comme l'armement d'une piste.
    transportBar_.onPlaybackSpeedChanged = [this](double facteur) {
        audioEngine_.processGraph().setPlaybackSpeed(facteur);
    };
    trackList_.onArmChanged = [this] { refreshArmedTracks(); };

    // LA VUE D'ARRANGEMENT (D5.1). Elle ne connaît que le projet et ses propres
    // gestes ; c'est l'application qui sait ce qu'un geste coûte -- un pas
    // d'annulation, une republication au moteur.
    arrangement_.onEditStarted = [this](const juce::String& nom) { beginProjectEdit(nom); };
    arrangement_.onClipsChanged = [this] {
        // Les clips changent CE QUI EST JOUÉ : le planning du moteur et le
        // matériau audio découpé doivent suivre, sans interrompre la lecture.
        audioEngine_.processGraph().setProject(project_);
        loadAudioTracks();
        // Une courbe dessinée sur l'arrangement doit S'ENTENDRE tout de suite
        // (D5.4) : sans cette republication, elle serait sauvegardée et muette
        // jusqu'à la prochaine ouverture du projet.
        applyAutomationFromProject();
        pianoRollPanel_.refresh();
    };
    // D11.3 : la position se lit aussi en MESURE · TEMPS, à côté du temps.
    // La barre de transport ne connaît pas le projet ; elle demande.
    transportBar_.positionInBarsProvider = [this](vsm::midi::Tick tick) {
        const auto bb = project_.timeSignatureMap.barBeatAt(tick, project_.ticksPerQuarterNote);
        return tr(u8"mes. ") + juce::String(static_cast<long long>(bb.bar + 1))   // D78
               + juce::String(u8" \u00b7 ") + juce::String(static_cast<long long>(bb.beat + 1));
    };
    transportBar_.onPositionDoubleClicked = [this] { promptGoToBar(); };
    arrangement_.onPlayheadRequested = [this](vsm::midi::Tick tick) {
        transport_.seekToTick(tick);
        audioEngine_.processGraph().seekSeconds(project_.ticksToSeconds(tick));
    };
    arrangement_.onTrackSelected = [this](size_t index) { trackList_.selectTrackIndex(index); };
    // D39.3 : UN SEUL ENDROIT TIENT LA SÉLECTION, et c'est la liste. Le clic
    // d'en-tête de l'arrangement lui passe les modificateurs plutôt que de
    // calculer sa propre sélection : deux endroits qui la calculeraient
    // finiraient par ne pas être d'accord, et l'on ne saurait pas lequel croire.
    arrangement_.onTrackSelectedWithMods = [this](size_t index, juce::ModifierKeys mods) {
        trackList_.cliquerSurLaPiste(index, mods);
    };
    // D39.3 / D39.4 : la sélection va aux deux autres panneaux qui dessinent des
    // pistes. Ils la reçoivent, ils ne la tiennent pas.
    trackList_.onSelectionChanged = [this] {
        arrangement_.setSelectedTracks(trackList_.selectedTracks());
        mixer_.setSelectedTracks(trackList_.selectedTracks());
        // D40.3 : et la console défile jusqu'à la tranche de la piste ACTIVE.
        // À 64 pistes elle en montre treize : sans cela, la marque de sélection
        // se dessine sur une tranche que personne ne voit.
        mixer_.faireVoirLaTranche(trackList_.selectedTrackIndex());
    };
    // D11.1 : ce qu'un changement de piste a refusé se DIT — un clip audio
    // vers une piste qui porte un autre fichier, un groupe, un genre qui ne
    // correspond pas. Le geste a fait le reste ; ceci n'est pas une erreur.
    arrangement_.onClipsRefused = [](size_t refuses) {
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Changement de piste"),
            tr(refuses > 1 ? u8"%1 clips n'ont pas changé de piste" : u8"%1 clip n'a pas changé de piste")
                    .replace("%1", juce::String(static_cast<int>(refuses)))
                + tr(u8" : un clip audio ne va que vers une piste audio qui porte le même fichier "
                     u8"(ou aucun), un clip MIDI vers une piste MIDI, et un groupe ne reçoit rien. "
                     u8"Les autres clips de la sélection ont été déplacés."));
    };
    // La grille fine de l'arrangement EST celle du piano roll, lue à l'usage :
    // deux réglages de grille dans deux vues du même morceau finiraient par se
    // contredire.
    arrangement_.gridProvider = [this] { return pianoRoll_.gridResolution(); };
    arrangement_.waveformProvider = [this](size_t index)
        -> std::shared_ptr<const std::vector<vsm::audio::io::PeakBin>> {
        const auto it = waveformCache_.find(index);
        return it == waveformCache_.end() ? nullptr : it->second;
    };
    arrangement_.sampleRateProvider = [this] {
        return audioEngine_.currentSampleRate() > 0.0 ? audioEngine_.currentSampleRate() : 48000.0;
    };
    // LES BORNES D'UN PARAMÈTRE AUTOMATISÉ (D5.4). Elles viennent des listes de
    // paramètres des machines et des effets, que la vue d'arrangement n'a pas à
    // connaître -- elle demande, l'application répond.
    arrangement_.automationRange = [this](size_t index, const std::string& parametre,
                                           float& mini, float& maxi) {
        if (parametre == "mix.volume") { mini = 0.0f; maxi = 1.5f; return true; }
        if (parametre == "mix.pan")    { mini = -1.0f; maxi = 1.0f; return true; }
        if (parametre == "mix.trim")   { mini = -24.0f; maxi = 24.0f; return true; }  // D30.4, en dB
        if (parametre.rfind("mix.send.", 0) == 0) { mini = 0.0f; maxi = 1.0f; return true; }
        if (parametre.rfind("master.", 0) == 0) {
            const std::string nom = parametre.substr(7);
            for (const auto& info : audioEngine_.processGraph().masterBus().parameterList())
                if (info.name == nom) { mini = info.minValue; maxi = info.maxValue; return true; }
            return false;
        }
        if (index >= project_.tracks.size()) return false;
        const auto& track = project_.tracks[index];
        if (parametre.rfind("insert.", 0) == 0) {
            const size_t point = parametre.find('.', 7);
            if (point == std::string::npos) return false;
            const int numero = std::atoi(parametre.substr(7, point - 7).c_str());
            const std::string semantique = parametre.substr(point + 1);
            const size_t slot = numero >= 1 ? static_cast<size_t>(numero - 1) : 0;
            if (numero < 1 || slot >= track.effects.size()) return false;
            auto fx = vsm::audio::effect::EffectFactory::create(track.effects[slot].type);
            if (!fx) return false;
            const auto profil = vsm::interchange::buildSemanticProfile(
                vsm::interchange::effectSemanticPluginId(track.effects[slot].type));
            const auto* d = profil.findBySemanticId(semantique);
            if (d == nullptr) return false;
            for (const auto& info : fx->parameterList())
                if (info.id == d->paramId) { mini = info.minValue; maxi = info.maxValue; return true; }
            return false;
        }
        if (track.instrumentId.empty()) return false;
        const auto profil = vsm::interchange::buildSemanticProfile(track.instrumentId);
        const auto* d = profil.findBySemanticId(parametre);
        if (d == nullptr) return false;
        auto* machine = audioEngine_.processGraph().trackInstrument(index);
        if (machine == nullptr) return false;
        for (const auto& info : machine->parameterList())
            if (info.id == d->paramId) { mini = info.minValue; maxi = info.maxValue; return true; }
        return false;
    };
    // D11.4 : RENOMMER ET COLORER UN CLIP. `Clip::name` et `Clip::colorRgba`
    // étaient dans le modèle et dans le fichier depuis D1, et aucune vue ne
    // les éditait : la couleur était toujours celle de la piste.
    arrangement_.onClipRenameRequested = [this](size_t piste, uint64_t clipId) {
        auto* clip = findClip(piste, clipId);
        if (clip == nullptr) return;
        auto* fenetre = new juce::AlertWindow(
            tr(u8"Renommer le clip"), tr(u8"Le nom s'affiche sur le clip et se sauvegarde avec le projet."),
            juce::MessageBoxIconType::NoIcon);
        fenetre->addTextEditor("nom", juce::String(clip->name), tr(u8"Nom :"));
        fenetre->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
        fenetre->addButton(vsm::app::ui::trSelon("bouton", u8"Annuler"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
        fenetre->enterModalState(true, juce::ModalCallbackFunction::create(
            [this, piste, clipId, fenetre](int resultat) {
                if (resultat != 1) return;
                if (auto* c = findClip(piste, clipId)) {
                    beginProjectEdit(u8"Renommer un clip");
                    c->name = fenetre->getTextEditorContents("nom").toStdString();
                    arrangement_.repaint();
                }
            }), true);
    };
    // « LE CLIP FAIT N MESURES » (D12.6, § 6 du CDC d'étirement). C'est la
    // première commande du suivi de tempo, et la plus utile : un musicien sait
    // combien de mesures fait sa boucle, il ne sait pas son tempo au centième.
    // On pose les deux marqueurs extrêmes et on DIT le tempo déduit, pour
    // qu'il se vérifie.
    arrangement_.onClipBarsRequested = [this](size_t piste, uint64_t clipId) {
        auto* clip = findClip(piste, clipId);
        if (clip == nullptr || piste >= project_.tracks.size()) return;
        auto* fenetre = new juce::AlertWindow(
            tr(u8"Le clip fait N mesures"),
            // D117 : UN SEUL PARAGRAPHE. Le retour forcé laissait « pitch. » seul sur
            // sa ligne à 150 % : la mise en page équilibre mieux sans lui.
            tr(u8"Le clip s'étirera pour durer ce nombre de mesures, sans changer de hauteur. "
               u8"Le tempo d'origine du matériau sera déduit et affiché."),
            juce::MessageBoxIconType::NoIcon);
        fenetre->addTextEditor("mesures", "4", tr(u8"Mesures :"));
        fenetre->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
        fenetre->addButton(vsm::app::ui::trSelon("bouton", u8"Annuler"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
        fenetre->enterModalState(true, juce::ModalCallbackFunction::create(
            [this, piste, clipId, fenetre](int resultat) {
                if (resultat != 1 || piste >= project_.tracks.size()) return;
                const int mesures = fenetre->getTextEditorContents("mesures").getIntValue();
                if (mesures <= 0) return;
                auto& track = project_.tracks[piste];
                const auto parMesure =
                    project_.timeSignatureMap.ticksPerBar(0, project_.ticksPerQuarterNote);
                // LA FIN DU MATÉRIAU d'une piste audio est celle de son
                // fichier, en ticks -- la même règle que dans la vue.
                const vsm::midi::Tick finMateriau =
                    track.audio.sampleRate > 0.0
                        ? project_.secondsToTicks(track.audio.durationSeconds()) : 0;
                beginProjectEdit(u8"Le clip fait N mesures");
                const double bpm = vsm::sequencer::setClipBars(
                    track.clips, clipId, mesures, parMesure, finMateriau,
                    [this](vsm::midi::Tick t) { return project_.ticksToSeconds(t); });
                loadAudioTracks();
                arrangement_.repaint();
                if (bpm <= 0.0) return;
                // LE TEMPO DÉDUIT SE DIT, parce qu'il se vérifie : un nombre de
                // mesures faux donne un tempo absurde, et c'est le seul moment
                // où on peut s'en apercevoir sans écouter.
                // Chaque littéral passe par `juce::String` : concaténer une
                // `juce::String` et un `u8"..."` est AMBIGU depuis C++20
                // (`char8_t`), et l'erreur ne se voit qu'à la compilation de
                // l'application -- le piège qui avait fait annoncer une
                // capture faite « avec ce code » à D11.1.
                // ET LE GESTE INVERSE (D13.7) : caler le PROJET sur la boucle.
                // Le changement de tempo au tick 0 prend la valeur déduite, les
                // autres restent, et la boucle joue alors au rapport un -- le
                // court-circuit de l'étireur, pas un bit de différence.
                auto* choix = new juce::AlertWindow(
                    tr(u8"Tempo du clip"),
                    tr(u8"Le matériau de ce clip a été enregistré à environ %1 BPM.\n"
                       u8"Il joue désormais à %2 BPM, sans changer de hauteur.\n\n"
                       u8"Adopter ce tempo pour le projet le cale sur la boucle, qui joue alors telle quelle.")
                        .replace("%1", juce::String(bpm, 1))
                        .replace("%2", juce::String(project_.tempoMap.bpmAt(0), 1)),
                    juce::MessageBoxIconType::InfoIcon);
                choix->addButton(tr(u8"Garder le tempo du projet"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
                choix->addButton(tr(u8"Adopter ce tempo pour le projet"), 1, juce::KeyPress(juce::KeyPress::returnKey));
                choix->enterModalState(true, juce::ModalCallbackFunction::create(
                    [this, bpm](int resultat) {
                        if (resultat != 1 || bpm <= 0.0) return;
                        beginProjectEdit(u8"Adopter le tempo du clip");
                        project_.tempoMap.addTempoChange(
                            0, static_cast<uint32_t>(std::lround(60'000'000.0 / bpm)));
                        refreshTransportSchedule();
                        loadAudioTracks();
                        arrangement_.repaint();
                        tempoLane_.repaint();
                    }), true);
            }), true);
    };
    arrangement_.onClipColourRequested = [this](size_t piste, uint64_t clipId) {
        auto* clip = findClip(piste, clipId);
        if (clip == nullptr) return;
        colourEditOpen_ = false;
        auto* selecteur = new juce::ColourSelector(
            juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders
                | juce::ColourSelector::showColourspace);
        selecteur->setName(tr(u8"Couleur du clip"));
        selecteur->setCurrentColour(juce::Colour(clip->colorRgba));
        selecteur->setSize(280, 320);
        selecteur->addChangeListener(new ClipColourApplier(*this, piste, clipId));
        juce::CallOutBox::launchAsynchronously(std::unique_ptr<juce::Component>(selecteur),
                                               arrangement_.getScreenBounds().withSize(1, 1)
                                                   .translated(arrangement_.getWidth() / 2, arrangement_.getHeight() / 3),
                                               nullptr);
    };
    arrangement_.onColourRequested = [this](size_t index) {
        if (index >= project_.tracks.size()) return;
        // OUVRIR LE SÉLECTEUR COMMENCE UN NOUVEAU PAS D'ANNULATION : sans cette
        // remise à zéro, tous les changements de couleur de la session
        // n'en feraient qu'un seul, et annuler les défairait tous.
        colourEditOpen_ = false;
        // LE SÉLECTEUR DE COULEUR EST DE JUCE, donc il est ici : le composant
        // d'arrangement ne connaît de JUCE que le dessin, et lui faire ouvrir
        // une fenêtre le lierait à l'application.
        auto* selecteur = new juce::ColourSelector(
            juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders
                | juce::ColourSelector::showColourspace);
        selecteur->setName("Couleur de la piste");
        selecteur->setCurrentColour(juce::Colour(project_.tracks[index].colorRgba));
        selecteur->setSize(280, 320);
        // La couleur suit le sélecteur EN DIRECT : on choisit une couleur en la
        // voyant sur la piste, pas en la devinant dans un carré.
        selecteur->addChangeListener(new ColourApplier(*this, index));
        juce::CallOutBox::launchAsynchronously(
            std::unique_ptr<juce::Component>(selecteur),
            arrangement_.getScreenBounds().withSize(1, 1).translated(80, 60 + 20 * static_cast<int>(index)),
            nullptr);
    };
    trackList_.onOutputChanged = [this] {
        // Le routage est une donnée de mixage : il se republie sans interrompre
        // la lecture, comme un fader.
        mixDirty_ = true;
    };
    transportBar_.onTempoChanged = [this](double bpm) {
        // LE TEMPO EST UNE DONNÉE DU PROJET, et le changer est une action
        // annulable comme les autres.
        beginProjectEdit("Tempo");
        // SEUL LE TEMPO DE DÉPART CHANGE : la carte dessinée dans l'onglet
        // Tempo reste. La première version effaçait toute la carte, ce qui
        // n'avait pas de conséquence tant que personne ne pouvait en dessiner.
        project_.tempoMap.addTempoChange(0, static_cast<uint32_t>(std::llround(60000000.0 / bpm)));
        tempoLane_.refresh();
        refreshTransportSchedule();
        audioEngine_.processGraph().setProject(project_);
        pianoRollPanel_.refresh();
    };
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
    pianoRoll_.onPunchRegionChanged = [this](vsm::midi::Tick start, vsm::midi::Tick end, bool active) {
        // La région de punch est une DONNÉE DE MORCEAU : on refait le même
        // passage vingt fois, et la redéfinir à chaque ouverture reviendrait à
        // perdre l'endroit qu'on a mis dix minutes à cerner.
        project_.punchStartTick = start;
        project_.punchEndTick = end;
        project_.punchEnabled = active && end > start;
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

    // Décompte et mode d'enregistrement : des PRÉFÉRENCES de session, pas des
    // données de morceau. Elles sont donc conservées comme l'échelle
    // d'interface, et non écrites dans `project.json` -- un projet rouvert ne
    // doit pas imposer le mode de travail de la dernière fois.
    {
        auto& reglages = vsm::app::ui::UiScale::properties();
        countInBars_ = juce::jlimit(0, 2, reglages.getIntValue("recordCountInBars", 1));
        monitoringMode_ = juce::jlimit(0, 2, reglages.getIntValue("monitoringMode", 0));   // D23.2
        // La latence mesurée est CONSERVÉE : elle décrit la machine et sa carte,
        // pas le morceau, et la remesurer à chaque lancement serait absurde.
        audioEngine_.setMeasuredRoundTripSeconds(
            juce::jlimit(0.0, 1.0, reglages.getDoubleValue("latenceAllerRetour", 0.0)));
        recordMode_ = static_cast<vsm::sequencer::RecordMode>(
            juce::jlimit(0, 2, reglages.getIntValue("recordMode", 0)));
    }

    // LES THREADS DE RENDU, AVANT QUE LE PÉRIPHÉRIQUE NE DÉMARRE (D8.1) : le
    // graphe sait certes en changer en marche, mais les créer pendant qu'il ne
    // tourne pas évite au tout premier bloc d'être celui qui les attend.
    audioEngine_.processGraph().setRenderThreadCount(effectiveRenderThreadCount());

    rebuildFromProject();
    // Le périphérique retenu au dernier lancement, s'il y en a un.
    {
        auto etat = std::unique_ptr<juce::XmlElement>(
            vsm::app::ui::UiScale::properties().getXmlValue("audioDeviceState"));
        audioEngine_.start(etat.get()); // échec silencieux et non bloquant
    }
    // L'HORLOGE DE SECOURS, TOUT DE SUITE SI LA CARTE N'EST PAS LÀ (D8.3).
    // L'attendre du prochain changement d'état ne marcherait pas : sur une
    // machine sans audio, il n'y a jamais de changement, et le temps ne
    // partirait donc jamais.
    midiLearnPanel_.onRemove = [this](int cc) {
        audioEngine_.clearMidiLearnController(static_cast<uint8_t>(cc));
        midiLearnSeenCount_ = audioEngine_.midiLearnMappingCount();
        saveMidiLearnMappings();
        refreshMidiLearnList();
        menuItemsChanged();
    };
    midiLearnPanel_.onLearn = [this](juce::Component* origine) {
        // CE QUI EST PROPOSÉ DÉPEND DE CE QUI EXISTE : le transport toujours,
        // les réglages de piste seulement s'il y a une piste choisie, et un
        // départ seulement s'il est déclaré par le projet. Proposer une cible
        // qui n'existe pas serait promettre une association qui ne ferait rien.
        using Kind = vsm::audio::engine::MidiLearnKind;
        struct Choix { Kind kind; const char* libelle; uint8_t slot; };
        auto choix = std::make_shared<std::vector<Choix>>();
        juce::PopupMenu menu;
        auto ajouter = [&](Kind kind, const juce::String& libelle, uint8_t slot = 0) {
            choix->push_back({kind, "", slot});
            menu.addItem(static_cast<int>(choix->size()), libelle);
        };
        menu.addSectionHeader(tr("Transport"));
        ajouter(Kind::TransportPlay, tr(u8"Lecture / arrêt"));
        ajouter(Kind::TransportStop, tr(u8"Arrêt"));
        ajouter(Kind::TransportRecord, tr("Enregistrement"));
        ajouter(Kind::TransportLoop, tr("Boucle"));

        const size_t piste = trackList_.selectedTrackIndex();
        if (piste < project_.tracks.size()) {
            menu.addSectionHeader(tr(u8"Piste %1 — %2")
                                      .replace("%1", juce::String(static_cast<int>(piste) + 1))
                                      .replace("%2", juce::String::fromUTF8(project_.tracks[piste].name.c_str())));
            ajouter(Kind::TrackVolume, tr("Volume"));
            ajouter(Kind::TrackPan, tr("Panoramique"));
            ajouter(Kind::TrackMute, tr("Muet"));
            ajouter(Kind::TrackSolo, tr("Solo"));
            for (size_t bus = 0; bus < project_.sends.size()
                                 && bus < vsm::audio::engine::ProcessGraph::kMaxSends; ++bus)
                ajouter(Kind::TrackSend,
                         tr(u8"Départ %1").replace("%1", juce::String(static_cast<char>('A' + bus))),
                         static_cast<uint8_t>(bus));
        }

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(origine),
                            [this, choix, piste](int resultat) {
            if (resultat <= 0 || resultat > static_cast<int>(choix->size())) return;
            const auto& retenu = (*choix)[static_cast<size_t>(resultat) - 1];
            vsm::audio::engine::MidiLearnTarget cible;
            cible.kind = retenu.kind;
            cible.trackIndex = piste;
            cible.slot = retenu.slot;
            // LA PLAGE EST CELLE DU RÉGLAGE RÉEL, et le panoramique est le seul
            // qui ne parte pas de zéro : l'enregistrer de 0 à 1 le bloquerait à
            // droite de l'axe.
            cible.min = retenu.kind == vsm::audio::engine::MidiLearnKind::TrackPan ? -1.0f : 0.0f;
            cible.max = 1.0f;
            cible.valid = true;
            audioEngine_.armMidiLearn(cible);
            midiLearnPanel_.setWaiting(juce::String::fromUTF8(
                vsm::interchange::describeMidiLearnTarget(cible).c_str()));
        });
    };
    midiLearnPanel_.onRemoveAll = [this] {
        audioEngine_.clearMidiLearn();
        midiLearnSeenCount_ = 0;
        saveMidiLearnMappings();
        refreshMidiLearnList();
        menuItemsChanged();
    };
    loadMidiLearnMappings();

    // D10.3 : LA TABLE DES RACCOURCIS, prêtée au piano roll. Les deux
    // gestionnaires de touches consultent la MÊME.
    loadShortcuts();
    browserPanel_.onApply = [this](const vsm::interchange::BrowserItem& entree) {
        applyBrowserItem(entree, trackList_.selectedTrackIndex());
    };
    // D32.1 : LA PRÉ-ÉCOUTE. Le fichier est décodé par le MÊME chemin que la
    // piste de référence -- WAV par le lecteur du moteur, le reste par JUCE --
    // pour qu'un format qui s'importe s'écoute, et qu'un format qui ne
    // s'importe pas le dise avec les mêmes mots.
    browserPanel_.onAudition = [this](const vsm::interchange::BrowserItem& entree) {
        auditionSample(juce::File(juce::String::fromUTF8(entree.reference.c_str())));
    };
    trackList_.onBrowserItemDropped = [this](size_t piste, const juce::String& description) {
        applyBrowserDrop(piste, description);
    };
    arrangement_.onBrowserItemDropped = [this](size_t piste, vsm::midi::Tick tick,
                                                const juce::String& description) {
        applyBrowserDropAt(piste, tick, description);
    };
    preferencesPanel_.onUiScaleChanged = [this](float facteur) { setUiScale(facteur); };
    preferencesPanel_.onRenderThreadsChanged = [this](int choix) {
        setRenderThreadChoice(choix);
        refreshPreferences();
    };
    preferencesPanel_.onChooseChainFolder = [this] { chooseChainFolder(); };
    preferencesPanel_.onChooseLibraryFolder = [this] {
        auto chooser = std::make_shared<juce::FileChooser>(
            tr(u8"Dossier de la bibliothèque (presets, profils, échantillons)"),
            juce::File(), "");
        chooser->launchAsync(juce::FileBrowserComponent::openMode
                                  | juce::FileBrowserComponent::canSelectDirectories,
                              [this, chooser](const juce::FileChooser& fc) {
            const juce::File dossier = fc.getResult();
            if (dossier == juce::File()) return;
            vsm::app::ui::UiScale::properties().setValue("dossierBibliotheque",
                                                          dossier.getFullPathName());
            vsm::app::ui::UiScale::properties().saveIfNeeded();
            refreshPreferences();
            refreshBrowser();
        });
    };
    preferencesPanel_.onOpenShortcuts = [this] { menuItemSelected(kMenuViewShortcuts, 0); };
    retourAuDepart_ = vsm::app::ui::UiScale::properties().getBoolValue("retourAuDepartALArret", false);
    preferencesPanel_.onReturnToStartChanged = [this](bool actif) {
        retourAuDepart_ = actif;
        vsm::app::ui::UiScale::properties().setValue("retourAuDepartALArret", actif);
        vsm::app::ui::UiScale::properties().saveIfNeeded();
    };
    // D16.6 : LE MÉTRONOME RÉGLABLE. Le niveau et les deux restrictions sont
    // relus au démarrage et poussés au graphe -- sans cela, `setMetronomeLevel`
    // existait et n'était appelé de nulle part, et le réglage se perdait à
    // chaque fermeture.
    {
        auto& reglages = vsm::app::ui::UiScale::properties();
        const auto niveau = static_cast<float>(reglages.getDoubleValue("niveauMetronome", 0.35));
        audioEngine_.processGraph().setMetronomeLevel(juce::jlimit(0.0f, 1.0f, niveau));
        audioEngine_.processGraph().setMetronomeCountInOnly(
            reglages.getBoolValue("metronomeDecompteSeul", false));
        audioEngine_.processGraph().setMetronomeRecordOnly(
            reglages.getBoolValue("metronomeEnregistrementSeul", false));
    }
    // D33.2 : LE FONDU DE SÉCURITÉ, relu au démarrage. Deux millisecondes par
    // défaut : assez pour supprimer un clic, trop court pour s'entendre comme
    // un fondu. Zéro le désactive, et la lecture reprend le chemin d'avant.
    safetyFadeMs_ = juce::jlimit(0.0, 50.0,
        vsm::app::ui::UiScale::properties().getDoubleValue("fonduDeSecuriteMs", 2.0));
    // D34.4 : la règle retrouve le mode qu'on lui avait laissé.
    arrangement_.setRulerInTime(
        vsm::app::ui::UiScale::properties().getBoolValue("regleEnTemps", false));

    // D17.2 : « l'automation suit les clips », active par défaut comme chez
    // Cubase, et retenue d'une exécution à l'autre.
    arrangement_.setAutomationFollowsClips(
        vsm::app::ui::UiScale::properties().getBoolValue("automationSuitLesClips", true));
    preferencesPanel_.onAutomationFollowsClipsChanged = [this](bool suit) {
        arrangement_.setAutomationFollowsClips(suit);
        vsm::app::ui::UiScale::properties().setValue("automationSuitLesClips", suit);
        vsm::app::ui::UiScale::properties().saveIfNeeded();
    };
    preferencesPanel_.onMetronomeLevelChanged = [this](float niveau) {
        audioEngine_.processGraph().setMetronomeLevel(niveau);
        vsm::app::ui::UiScale::properties().setValue("niveauMetronome", niveau);
        vsm::app::ui::UiScale::properties().saveIfNeeded();
    };
    preferencesPanel_.onMetronomeCountInOnlyChanged = [this](bool actif) {
        audioEngine_.processGraph().setMetronomeCountInOnly(actif);
        vsm::app::ui::UiScale::properties().setValue("metronomeDecompteSeul", actif);
        vsm::app::ui::UiScale::properties().saveIfNeeded();
    };
    preferencesPanel_.onMetronomeRecordOnlyChanged = [this](bool actif) {
        audioEngine_.processGraph().setMetronomeRecordOnly(actif);
        vsm::app::ui::UiScale::properties().setValue("metronomeEnregistrementSeul", actif);
        vsm::app::ui::UiScale::properties().saveIfNeeded();
    };
    preferencesPanel_.onOpenMidiLearn = [this] { menuItemSelected(kMenuViewMidiLearn, 0); };
    shortcutsPanel_.onRebind = [this](vsm::interchange::ShortcutId id) {
        rebindPending_ = true;
        rebindTarget_ = id;
        const auto* commande = vsm::interchange::findShortcutCommand(id);
        shortcutsPanel_.setCapturing(commande ? juce::String::fromUTF8(commande->label)
                                               : juce::String());
    };
    shortcutsPanel_.onReset = [this](vsm::interchange::ShortcutId id) {
        shortcuts_.reset(id);
        saveShortcuts();
        refreshShortcutList();
    };
    shortcutsPanel_.onResetAll = [this] {
        shortcuts_.resetAll();
        saveShortcuts();
        refreshShortcutList();
    };
    shortcutsPanel_.onKeyCaptured = [this](const juce::KeyPress& touche) {
        if (!rebindPending_) return false;
        // ÉCHAP ANNULE, et n'est donc jamais assignable depuis ici. C'est le
        // prix d'avoir une sortie de secours, et il est petit : Échap veut dire
        // « annuler » partout ailleurs dans le logiciel.
        if (touche == juce::KeyPress::escapeKey) {
            rebindPending_ = false;
            shortcutsPanel_.setCapturing({});
            return true;
        }
        const juce::String description = vsm::app::ui::normalizedKeyDescription(touche);
        // UN CONFLIT SE DIT AVANT D'ÊTRE CRÉÉ. Deux commandes sur la même
        // touche, c'est une seule qui répond, et rien qui dise laquelle.
        const auto conflits = shortcuts_.conflictsFor(description.toStdString(), rebindTarget_);
        rebindPending_ = false;
        shortcutsPanel_.setCapturing({});
        if (!conflits.empty()) {
            juce::String qui;
            for (auto autre : conflits)
                if (const auto* c = vsm::interchange::findShortcutCommand(autre))
                    qui += juce::String("\n  · ") + tr(juce::String::fromUTF8(c->label));
            montrerBoite(
                juce::AlertWindow::WarningIcon,
                tr(u8"Touche déjà prise"),
                tr(u8"%1 est déjà associée à :%2\n\nLibérez-la d'abord, ou choisissez-en une autre.")
                    .replace("%2", qui)
                    .replace("%1", description));
            return true;
        }
        shortcuts_.setKey(rebindTarget_, description.toStdString());
        saveShortcuts();
        refreshShortcutList();
        return true;
    };
    shortcutsPanel_.onExport = [this] {
        auto chooser = std::make_shared<juce::FileChooser>(
            tr(u8"Enregistrer la table des raccourcis..."),
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                .getChildFile("raccourcis-vsm.txt"),
            "*.txt");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode
                                  | juce::FileBrowserComponent::canSelectFiles,
                              [this, chooser](const juce::FileChooser& fc) {
            const juce::File fichier = fc.getResult();
            if (fichier == juce::File()) return;
            fichier.replaceWithText(juce::String::fromUTF8(
                vsm::interchange::shortcutTableToPrintableText(shortcuts_).c_str()));
        });
    };

    // D10.4 : ON CHERCHE UNE SESSION INTERROMPUE AVANT D'OUVRIR LA NÔTRE. Une
    // fois notre dossier créé, il faudrait l'exclure de la recherche -- une
    // condition de plus, donc une occasion de plus de se tromper.
    autosave_ = std::make_unique<vsm::app::AutosaveService>(
        vsm::app::ui::UiScale::properties().getFile().getParentDirectory()
            .getChildFile("recuperation"));
    offerCrashRecovery();
    autosave_->begin();

    refreshReconstructionChain();
    transport_.setAudioDeviceOpen(audioEngine_.isDeviceOpen(),
                                   audioEngine_.currentSampleRate(),
                                   audioEngine_.currentBlockSize());

#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(this);
    setSize(1000, 56);
#else
    addAndMakeVisible(menuBarComponent_);
    menuBarComponent_.setModel(this);
    setSize(1000, 56 + 26);
#endif

    // Le rapport d'import : ajouté ici, INVISIBLE, et rendu visible par
    // `applyDawImport`. Il est enfant du composant de contenu -- donc de ce que
    // photographie l'autoportrait -- parce qu'un rapport qu'aucune capture ne
    // montre est un écran qu'on ne peut pas juger. Il se gère seul (bornes par
    // `parentSizeChanged`, premier plan en devenant visible) : rien ici ni
    // dans `resized()` n'a à le connaître.
    addChildComponent(importReport_);

    startTimerHz(30);
}

MainComponent::~MainComponent() {
    // FERMETURE NORMALE : c'est l'ABSENCE du dossier de récupération qui, au
    // prochain lancement, signalera un plantage. L'effacer ici est donc la
    // seule chose qui distingue « on a quitté » de « on est mort ».
    if (autosave_) autosave_->endCleanly();
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

/// D59 : la barre d'onglets et les marges du dock du bas, MESURÉES plutôt
/// qu'estimées : le mélangeur reçoit 221 pixels quand le dock en fait 282.
static constexpr int kSurcoutDockBas = 61;

void MainComponent::resized() {
    auto area = getLocalBounds();
#if !JUCE_MAC
    menuBarComponent_.setBounds(area.removeFromTop(26));
#endif
    if (singleWindow_ && trackList_.getParentComponent() == this) {
        // D68 : la barre DEMANDE sa hauteur. Elle se replie sur une seconde
        // rangée quand une seule ne suffit pas ; les 56 px fixes d'avant
        // coupaient sa moitié gauche (signature rythmique à zéro pixel à
        // 900x660) et empêchaient toute sa moitié droite de se poser.
        transportBar_.setBounds(area.removeFromTop(transportBar_.hauteurUtile(area.getWidth())));
        layoutDockedPanels(area);
        return;
    }
    transportBar_.setBounds(area);
    placerLesBoutonsDeZone();   // D122 : hors de la fenêtre unique, ils se cachent
}

void MainComponent::layoutDockedPanels(juce::Rectangle<int> area) {
    // D122 : UNE ZONE AGRANDIE PREND TOUTE L'AIRE sous la barre de transport ; les
    // autres ont été cachées par `basculerZoneAgrandie`, et leurs poignées aussi.
    if (zoneAgrandie_ >= 0) {
        sepBas_.setVisible(false);
        sepGauche_.setVisible(false);
        sepDroite_.setVisible(false);
        if (zoneAgrandie_ == 0) trackList_.setBounds(area);
        else if (zoneAgrandie_ == 1) synthRack_.setBounds(area);
        else if (zoneAgrandie_ == 2) bottomTabs_.setBounds(area);
        else { arrangement_.setBounds(area); pianoRollPanel_.setBounds(area); }
        placerLesBoutonsDeZone();
        return;
    }
    // La géométrie de l'ancienne disposition flottante, repliée dans une seule
    // fenêtre : pistes à gauche, console en bas, rack à droite, le morceau au
    // centre. Chaque volet ne prend sa place que s'il est VISIBLE -- le menu
    // Affichage cache un volet, l'espace revient au centre -- et chaque
    // frontière porte une POIGNÉE : les tailles se tirent à la souris et
    // survivent au redémarrage. Les bornes gardent toujours un centre lisible.
    constexpr int poignee = 7;
    // D59 : LE DOCK DU BAS NE DESCEND PLUS SOUS CE QUE LA CONSOLE RÉCLAME.
    // Il pouvait descendre à 120 px, et la tranche master perdait alors sa
    // septième commande -- le PLAFOND DU LIMITEUR, celui-là même que D49 a
    // rendu honnête -- hors de l'écran, sans libellé et à moitié coupée. Le
    // surcoût (barre d'onglets et marges du dock) est mesuré : le mélangeur
    // reçoit 221 px quand le dock en fait 282.
    const int plancherDuBas = juce::jmax(120, mixer_.hauteurMinimale() + kSurcoutDockBas);
    dockBas_ = juce::jlimit(plancherDuBas, juce::jmax(plancherDuBas + 1, area.getHeight() - 220),
                             dockBas_);
    dockGauche_ = juce::jlimit(180, juce::jmax(181, area.getWidth() / 2), dockGauche_);
    // D64 : LE PLANCHER DU RACK RESTE 220, ET C'EST UNE DÉCISION. Le prendre
    // sur ce que la façade réclame — comme le dock du bas prend celui de la
    // tranche master depuis D59 — a été écrit, puis RETIRÉ par la mesure : les
    // 54 façades décrites demandent de 428 à 1372 px, contre 426 que le rack
    // reçoit par défaut. Le plancher aurait donc élargi le rack de force sur
    // presque toutes les machines, en écrasant une largeur que l'utilisateur a
    // choisie et que l'application retient (`dock.droite`). Une disposition
    // réglable ne se reprend pas à son propriétaire ; c'est la façade qui
    // défile quand elle ne tient pas.
    dockDroite_ = juce::jlimit(220, juce::jmax(221, area.getWidth() / 2), dockDroite_);

    sepBas_.setVisible(bottomTabs_.isVisible());
    if (bottomTabs_.isVisible()) {
        bottomTabs_.setBounds(area.removeFromBottom(dockBas_));
        sepBas_.setBounds(area.removeFromBottom(poignee));
    }
    sepGauche_.setVisible(trackList_.isVisible());
    if (trackList_.isVisible()) {
        trackList_.setBounds(area.removeFromLeft(dockGauche_));
        sepGauche_.setBounds(area.removeFromLeft(poignee));
    }
    sepDroite_.setVisible(synthRack_.isVisible());
    if (synthRack_.isVisible()) {
        synthRack_.setBounds(area.removeFromRight(dockDroite_));
        sepDroite_.setBounds(area.removeFromRight(poignee));
    }
    arrangement_.setBounds(area);
    pianoRollPanel_.setBounds(area);
    placerLesBoutonsDeZone();
}

void MainComponent::BoutonDeZone::paintButton(juce::Graphics& g, bool survol, bool enfonce) {
    // DESSINÉ, PAS UN GLYPHE (D122) : D117 a vu ce qu'un caractère inhabituel peut
    // devenir à l'écran. Quatre flèches vers les coins pour agrandir, vers le
    // centre pour rendre.
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(vsm::ui::Palette::panel.brighter(enfonce ? 0.35f : (survol ? 0.22f : 0.12f)));
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(vsm::ui::Palette::textSecondary.brighter(survol ? 0.4f : 0.0f));
    const auto c = r.reduced(r.getWidth() * 0.22f);
    const float l = c.getWidth() * 0.36f;   // longueur d'un bras de coin
    juce::Path p;
    auto coin = [&](juce::Point<float> sommet, float dx, float dy) {
        // un « L » dont l'angle est au sommet donné ; dx, dy orientent ses bras
        p.startNewSubPath(sommet.translated(dx * l, 0.0f));
        p.lineTo(sommet);
        p.lineTo(sommet.translated(0.0f, dy * l));
    };
    if (!agrandie) {
        coin(c.getTopLeft(), 1.0f, 1.0f);
        coin(c.getTopRight(), -1.0f, 1.0f);
        coin(c.getBottomLeft(), 1.0f, -1.0f);
        coin(c.getBottomRight(), -1.0f, -1.0f);
    } else {
        // les angles tournés vers le centre : « revenir »
        const auto m = c.getCentre();
        const float e = c.getWidth() * 0.12f;
        coin({m.x - e, m.y - e}, -1.0f, -1.0f);
        coin({m.x + e, m.y - e}, 1.0f, -1.0f);
        coin({m.x - e, m.y + e}, -1.0f, 1.0f);
        coin({m.x + e, m.y + e}, 1.0f, 1.0f);
    }
    g.strokePath(p, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void MainComponent::placerLesBoutonsDeZone() {
    // D122 : un bouton dans le coin haut droit de chaque zone VISIBLE de la
    // fenêtre unique. En mode flottant, aucun : chaque fenêtre y a le sien.
    juce::Component* zones[4] = { &trackList_, &synthRack_, &bottomTabs_,
                                  centerShowsArrangement_ ? static_cast<juce::Component*>(&arrangement_)
                                                          : static_cast<juce::Component*>(&pianoRollPanel_) };
    const bool dansLaFenetreUnique = singleWindow_ && trackList_.getParentComponent() == this;
    for (int i = 0; i < 4; ++i) {
        auto& b = boutonsDeZone_[i];
        const bool montre = dansLaFenetreUnique && zones[i]->isVisible()
                            && (zoneAgrandie_ < 0 || zoneAgrandie_ == i);
        b.setVisible(montre);
        if (!montre) continue;
        b.agrandie = zoneAgrandie_ == i;
        b.setTooltip(b.agrandie ? tr(u8"Revenir à la disposition d'avant")
                                : tr(u8"Agrandir ce volet : il prend toute la place (un clic pour revenir)"));
        const auto z = zones[i]->getBounds();
        if (i == 0)   // la liste des pistes : toujours au bout de la ligne du filtre
            b.setBounds(trackList_.placeDuBoutonDeZone(kTailleBoutonDeZone) + z.getPosition());
        else
            b.setBounds(z.getRight() - kTailleBoutonDeZone - 4, z.getY() + 4, kTailleBoutonDeZone, kTailleBoutonDeZone);
        b.toFront(false);
        b.repaint();
    }
    // Là où le coin est occupé, la zone réserve la place du bouton.
    trackList_.setReserveDroite(boutonsDeZone_[0].isVisible() ? kTailleBoutonDeZone + 6 : 0);
    pianoRollPanel_.setReserveDroite(boutonsDeZone_[3].isVisible() && !centerShowsArrangement_
                                         ? kTailleBoutonDeZone + 6 : 0);
}

void MainComponent::basculerZoneAgrandie(int zone) {
    if (zone < 0 || zone > 3 || !singleWindow_) return;
    if (zoneAgrandie_ == zone) { rendreLesZones(); return; }
    if (zoneAgrandie_ >= 0) rendreLesZones();
    avantAgrandir_ = { trackList_.isVisible(), synthRack_.isVisible(), bottomTabs_.isVisible() };
    zoneAgrandie_ = zone;
    trackList_.setVisible(zone == 0);
    synthRack_.setVisible(zone == 1);
    bottomTabs_.setVisible(zone == 2);
    // le centre : caché si une autre zone prend la place, rendu tel qu'il était sinon
    arrangement_.setVisible(zone == 3 && centerShowsArrangement_);
    pianoRollPanel_.setVisible(zone == 3 && !centerShowsArrangement_);
    resized();
    menuItemsChanged();
}

void MainComponent::rendreLesZones() {
    if (zoneAgrandie_ < 0) return;
    zoneAgrandie_ = -1;
    trackList_.setVisible(avantAgrandir_.pistes);
    synthRack_.setVisible(avantAgrandir_.rack);
    bottomTabs_.setVisible(avantAgrandir_.bas);
    arrangement_.setVisible(centerShowsArrangement_);
    pianoRollPanel_.setVisible(!centerShowsArrangement_);
    resized();
    menuItemsChanged();
}

bool MainComponent::runMenuEntryForCapture(const juce::String& libelle) {
    const juce::String voulu = libelle.trim();
    if (voulu.isEmpty()) return false;
    const juce::StringArray noms = getMenuBarNames();
    // LE LIBELLÉ EXACT D'ABORD, LE PRÉFIXE ENSUITE : « Tout sélectionner »
    // existe deux fois (les notes du piano roll, les clips de l'arrangement),
    // et le premier préfixe venu n'est pas forcément celui qu'on visait.
    for (int passe = 0; passe < 2; ++passe)
    for (int i = 0; i < noms.size(); ++i) {
        juce::PopupMenu menu = getMenuForIndex(i, noms[i]);
        for (juce::PopupMenu::MenuItemIterator it(menu, true); it.next();) {
            const juce::PopupMenu::Item& item = it.getItem();
            if (item.itemID == 0 || item.isSectionHeader || item.isSeparator) continue;
            const juce::String texte = item.text.upToFirstOccurrenceOf(" (", false, false).trim();
            if (passe == 0 ? !texte.equalsIgnoreCase(voulu) : !item.text.startsWithIgnoreCase(voulu)) continue;
            // PANNE MUETTE INTERDITE, y compris dans l'outil qui sert à
            // vérifier : une entrée grisée n'est pas exécutée, et c'est dit.
            if (!item.isEnabled) {
                std::fputs(("VSM_MENU : \u00ab " + item.text.toStdString()
                            + " \u00bb est gris\u00e9e, rien n'a \u00e9t\u00e9 fait\n").c_str(), stderr);
                return false;
            }
            menuItemSelected(item.itemID, i);
            // D34.1 : ON DIT CE QU'ON A EXÉCUTÉ. Le silence en cas de succès
            // rendait la vérification bancale : « aucune erreur » ne distingue
            // pas « l'entrée a été jouée » de « la commande n'a pas tourné du
            // tout ». Le libellé retenu est écrit en entier, parce qu'un
            // préfixe peut avoir attrapé une AUTRE entrée que celle qu'on
            // visait -- c'est exactement la panne du 06/09 (« Automatique »).
            std::fputs(("VSM_MENU : \u00ab " + item.text.toStdString()
                        + " \u00bb ex\u00e9cut\u00e9e (menu " + noms[i].toStdString() + ")\n").c_str(),
                       stderr);
            return true;
        }
    }
    std::fputs(("VSM_MENU : aucune entr\u00e9e de menu ne commence par \u00ab "
                + voulu.toStdString() + " \u00bb\n").c_str(), stderr);
    return false;
}

void MainComponent::listMenusForCapture() {
    // D80 : LE MENU DIT LUI-MÊME CE QU'IL AFFICHE. Le « 227 / 227 » de D73 était
    // un compte fait dans le code, qui ne voyait pas le menu Édition construit
    // par le piano roll : 64 entrées, aucune traduite. Une ligne par entrée,
    // dans la langue courante, avec son chemin de sous-menus ; une entrée
    // grisée et un titre de section sont marqués, pour que deux listes prises
    // dans deux langues se comparent ligne à ligne.
    const juce::StringArray noms = getMenuBarNames();
    std::function<void(const juce::PopupMenu&, const juce::String&)> parcourir =
        [&parcourir](const juce::PopupMenu& menu, const juce::String& chemin) {
            for (juce::PopupMenu::MenuItemIterator it(menu, false); it.next();) {
                const auto& item = it.getItem();
                if (item.isSeparator) continue;
                const juce::String ligne = chemin + " > " + item.text
                    + (item.isSectionHeader ? juce::String(" [titre]")
                                            : !item.isEnabled ? juce::String(u8" [grisée]") : juce::String());
                std::fputs(("VSM_MENU_LISTE : " + ligne + "\n").toRawUTF8(), stderr);
                if (item.subMenu != nullptr) parcourir(*item.subMenu, chemin + " > " + item.text);
            }
        };
    for (int i = 0; i < noms.size(); ++i) parcourir(getMenuForIndex(i, noms[i]), noms[i]);
    // D83 : et les menus du clic droit de l'arrangement.
    for (const auto& [nom, menu] : arrangement_.menusPourCapture()) parcourir(menu, nom);
}

bool MainComponent::runContextMenuForCapture(const juce::String& entree) {
    const juce::String quel = entree.upToFirstOccurrenceOf(":", false, false).trim();
    const juce::String libelle = entree.fromFirstOccurrenceOf(":", false, false).trim();
    // « effets » : le premier effet de la chaîne affichée, celle de la piste choisie.
    // D102 : « ajout-effet » : une entrée de la liste « ajouter un effet », choisie.
    // D115 : « pianoroll » : le menu du clic droit du piano roll, sur la piste montrée.
    const bool fait = quel == "effets"        ? effectChain_.presetMenuPourCapture(0, libelle)
                    : quel == "ajout-effet"   ? effectChain_.ajouterPourCapture(libelle)
                    : quel == "pianoroll"     ? pianoRoll_.actionDeMenuPourCapture(libelle)
                                              : arrangement_.actionDeMenuPourCapture(quel, libelle);
    std::fputs((juce::String("VSM_MENU_CONTEXTE : ")
                + (fait ? juce::String(u8"« ") + libelle + juce::String(u8" » exécutée (") + quel + ")"
                        : juce::String(u8"aucune entrée « ") + libelle + juce::String(u8" » dans le menu ") + quel)
                + "\n").toRawUTF8(), stderr);
    return fait;
}

void MainComponent::dropFileForCapture(const juce::File& fichier) {
    std::fputs((juce::String("VSM_DEPOSER : ") + fichier.getFullPathName()
                + (fichier.existsAsFile() ? juce::String() : juce::String(u8" — introuvable")) + "\n").toRawUTF8(), stderr);
    filesDropped(juce::StringArray(fichier.getFullPathName()), 0, 0);
}

void MainComponent::listReportForCapture() {
    // D89 : `VSM_OUVERTURE` écrit les lignes BRUTES, les mêmes que `vsm-render` ;
    // celle-ci écrit ce que l'utilisateur LIT dans le volet, dans sa langue.
    if (!importReport_.hasReport()) return;
    juce::StringArray lignes;
    lignes.addLines(importReport_.reportText());
    for (const auto& ligne : lignes)
        std::fputs(("VSM_RAPPORT : " + ligne + "\n").toRawUTF8(), stderr);
}

namespace {
/// D94 / D95 : LA DESCENTE COMMUNE -- chaque texte visible de `racine` (bouton,
/// libellé, liste, infobulle), rendu à `ecrire(nature, texte)`, ses retours à
/// la ligne rendus par « / ». Une seule descente pour la fenêtre principale et
/// pour les boîtes : deux copies finiraient par ne plus lire la même chose.
int parcourirLesTextes(juce::Component& racine,
                       const std::function<void(const char*, const juce::String&)>& ecrire,
                       bool racineMemeCachee = false) {
    int nombre = 0;
    std::function<void(juce::Component&)> parcourir = [&](juce::Component& c) {
        if (!c.isVisible() && !(racineMemeCachee && &c == &racine)) return;
        auto noter = [&](const char* nature, const juce::String& texte) {
            if (texte.trim().isEmpty()) return;
            ++nombre;
            juce::StringArray lignes;
            lignes.addLines(texte);
            ecrire(nature, lignes.joinIntoString(" / "));
        };
        if (auto* bouton = dynamic_cast<juce::Button*>(&c)) noter("bouton", bouton->getButtonText());
        else if (auto* libelle = dynamic_cast<juce::Label*>(&c)) noter("libellé", libelle->getText());
        else if (auto* liste = dynamic_cast<juce::ComboBox*>(&c)) noter("liste", liste->getText());
        if (auto* bulle = dynamic_cast<juce::SettableTooltipClient*>(&c)) noter("infobulle", bulle->getTooltip());
        // Une liste déroulante porte son texte dans un libellé enfant : ne pas
        // le compter deux fois.
        if (dynamic_cast<juce::ComboBox*>(&c) != nullptr) return;
        for (auto* enfant : c.getChildren()) parcourir(*enfant);
    };
    parcourir(racine);
    return nombre;
}

/// D102 : UNE FENÊTRE DE CHOIX SE LIT QUAND ELLE EST DEMANDÉE, comme une boîte
/// (D95). Sous un écran verrouillé, JUCE ne montre aucun composant modal, et la
/// liste des fenêtres ne la voit pas : son titre et ses textes vont sur la
/// sortie d'erreur avant `enterModalState`.
void annoncerFenetre(juce::AlertWindow& fenetre) {
    const juce::String titre = fenetre.getName();
    std::fputs(("VSM_CHOIX : " + titre + "\n").toRawUTF8(), stderr);
    parcourirLesTextes(fenetre, [&titre](const char* nature, const juce::String& texte) {
        std::fputs(("VSM_CHOIX_TEXTE : " + titre + " : " + juce::String::fromUTF8(nature) + " : "
                    + texte.replace("\n", " / ") + "\n").toRawUTF8(), stderr);
    }, true);
}
} // namespace

void MainComponent::listTextsForCapture() {
    // D94 : CE QUE LA FENÊTRE MONTRE, composant visible par composant visible.
    // L'inventaire du code (tools/inventaire_langue.py) compte les chaînes
    // écrites hors tr() ; celle-ci lit ce qui s'AFFICHE -- la leçon de D80 : la
    // barre de menus comptée dans le code disait « 227 / 227 » quand l'écran en
    // montrait 35 en français. Une ligne par texte : sa nature, puis le texte,
    // ses retours à la ligne rendus par « / ». Ce que `paint()` dessine sans
    // composant (les messages d'état vides) n'y est pas : l'image le montre.
    //
    // `isVisible()` ET NON `isShowing()` : le second exige aussi que la fenêtre
    // ne soit pas minimisée, et un écran VERROUILLÉ la fait passer pour telle.
    // La liste rendait alors 0 texte sans un mot (11/09, D94), pendant que
    // l'autoportrait dessinait la fenêtre entière. La descente part de la racine
    // et ne suit que des enfants visibles : c'est « visible dans la fenêtre »,
    // la même liste que `isShowing()` quand la fenêtre est à l'écran.
    const int nombre = parcourirLesTextes(*this, [](const char* nature, const juce::String& texte) {
        std::fputs(("VSM_TEXTE : " + juce::String::fromUTF8(nature) + " : " + texte + "\n").toRawUTF8(),
                   stderr);
    });
    // LE COMPTE, ET CE QUI LE REND SUSPECT : un zéro doit se lire, pas se deviner.
    std::fputs(("VSM_TEXTES : " + juce::String(nombre) + juce::String(u8" texte(s) listé(s)")
                + (isShowing() ? juce::String()
                               : juce::String(u8" -- fenêtre non affichée (écran verrouillé ?)"))
                + "\n").toRawUTF8(), stderr);
}

void MainComponent::listWindowTextsForCapture() {
    // D95 : LES AUTRES FENÊTRES, et d'abord les BOÎTES. Une boîte n'est pas un
    // enfant de cette fenêtre : c'est une fenêtre à elle, ouverte après le geste
    // qui la demande -- d'où l'appel au moment de la photo, pas au démarrage.
    // JUCE ne rend pas le message d'une `AlertWindow` (`text` est privé), mais il
    // le pose dans un libellé ACCESSIBLE, enfant visible et transparent de la
    // boîte : « titre. message ». La même descente le lit donc. Préfixe à part
    // (`VSM_FENETRE_TEXTE`) : la liste de la fenêtre principale reste celle de D94.
    const juce::Component* principale = getTopLevelComponent();
    int fenetres = 0;
    for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i) {
        auto* fenetre = juce::TopLevelWindow::getTopLevelWindow(i);
        if (fenetre == nullptr || fenetre == principale) continue;
        // ET CE QUI N'EST PAS AFFICHÉ, DIT (D95) : sous un écran VERROUILLÉ, la
        // boîte « Aller à la mesure » de D91 n'était pas une fenêtre visible au
        // moment de la photo. Une boîte MODALE non affichée se lit quand même --
        // c'est elle que le geste a ouverte --, marquée comme telle ; une fenêtre
        // ni visible ni modale est seulement nommée.
        const bool visible = fenetre->isVisible();
        const bool modale = fenetre->isCurrentlyModal(false);
        const juce::String nom = fenetre->getName();
        if (!visible && !modale) {
            std::fputs(("VSM_FENETRE_CACHEE : " + nom + "\n").toRawUTF8(), stderr);
            continue;
        }
        ++fenetres;
        const int n = parcourirLesTextes(*fenetre, [&nom](const char* nature, const juce::String& texte) {
            std::fputs(("VSM_FENETRE_TEXTE : " + nom + " : " + juce::String::fromUTF8(nature) + " : "
                        + texte + "\n").toRawUTF8(), stderr);
        }, !visible);
        std::fputs(("VSM_FENETRE : " + nom + " -- " + juce::String(n) + juce::String(u8" texte(s)")
                    + " -- limites " + fenetre->getBounds().toString()   // D100
                    + (visible ? juce::String() : juce::String(u8" -- NON AFFICHÉE (modale)")) + "\n")
                       .toRawUTF8(), stderr);
    }
    std::fputs(("VSM_FENETRES : " + juce::String(fenetres) + juce::String(u8" autre(s) fenêtre(s) lue(s) sur ")
                + juce::String(juce::TopLevelWindow::getNumTopLevelWindows())
                + juce::String(u8" ; composants modaux : ")
                + juce::String(juce::ModalComponentManager::getInstance()->getNumModalComponents())
                + "\n").toRawUTF8(), stderr);
}

void MainComponent::applyViewCommand(const juce::String& nom) {
    // Les MÊMES identifiants que le menu : tester autre chose que ce que
    // l'utilisateur clique ne testerait rien.
    if (nom == "arrangement")      menuItemSelected(kMenuViewArrangement, 5);
    else if (nom == "pianoroll")   menuItemSelected(kMenuViewPianoRoll, 5);
    else if (nom == "sans-pistes") menuItemSelected(kMenuViewTracks, 5);
    else if (nom == "sans-rack")   menuItemSelected(kMenuViewSynthRack, 5);
    else if (nom == "sans-mixer")  menuItemSelected(kMenuViewMixer, 5);
    else if (nom == "flottant")    menuItemSelected(kMenuViewSingleWindow, 5);
    else if (nom == "historique")  menuItemSelected(kMenuViewHistory, 5);   // D11 : la fenêtre d'historique, pour la photographier
    else if (nom == "spectre")     menuItemSelected(kMenuViewSpectrum, 5);  // D15.3 : l'analyseur, pour le photographier
    else if (nom == "notes")       menuItemSelected(kMenuViewProjectNotes, 5);  // D18.6
    // D122 : plein:<fenêtre> -- setFullScreen sur un des cinq panneaux flottants,
    // ce que fait le bouton agrandir ; deux fois, il rend la fenêtre.
    else if (nom.startsWith("plein:")) {
        const juce::String cle = nom.fromFirstOccurrenceOf(":", false, false);
        PanelWindow* fenetres[] = { &trackListWindow_, &pianoRollWindow_, &synthRackWindow_,
                                    &mixerWindow_, &arrangementWindow_ };
        bool trouvee = false;
        for (auto* w : fenetres)
            if (w->getName() == tr(cle) || w->getName() == cle) {
                w->setFullScreen(!w->isFullScreen());
                trouvee = true;
            }
        if (!trouvee) std::fputs("VSM_VUE : fenêtre flottante inconnue\n", stderr);
    }
    // D122 : agrandir:pistes|rack|bas|centre -- la MÊME fonction que le bouton de la zone.
    else if (nom.startsWith("agrandir:")) {
        const juce::String z = nom.fromFirstOccurrenceOf(":", false, false);
        const int zone = z == "pistes" ? 0 : z == "rack" ? 1 : z == "bas" ? 2 : z == "centre" ? 3 : -1;
        if (zone >= 0) basculerZoneAgrandie(zone);
        else std::fputs("VSM_VUE : zone inconnue (pistes, rack, bas, centre)\n", stderr);
    }
    else if (nom == "ordre")       menuItemSelected(kMenuViewPlayOrder, 5);     // D18.4
    else if (nom == "prises")      menuItemSelected(kMenuRecordCompTakes, 3);  // D18.2
    // D55.2 : poser un tronçon, composer, fermer, et surtout COMPTER ce que le
    // panneau montre quand on le rouvre. C'est ce nombre qui dit si la recette
    // a survécu -- une liste de trois lignes ne se juge pas sur une capture.
    // `troncon:PRISE:DE:A` passe par le `onClick` du bouton « Ajouter ».
    else if (nom.startsWith("troncon:")) {
        if (!takeCompWindow_) showTakeComp();
        const auto morceaux = juce::StringArray::fromTokens(nom.substring(8), ":", "");
        const bool pose = morceaux.size() == 3
                        && takeCompPanel_.addSegmentForCapture(morceaux[0].getIntValue(),
                                                                morceaux[1].getIntValue(),
                                                                morceaux[2].getIntValue());
        std::fputs((juce::String::fromUTF8(u8"Tronçon ") + nom.substring(8)
                     + (pose ? juce::String::fromUTF8(u8" : posé, ")
                             : juce::String::fromUTF8(u8" : REFUSÉ (prise inconnue ou bornes à "
                                                       u8"l'envers), "))
                     + juce::String(takeCompPanel_.segmentCount())
                     + juce::String::fromUTF8(u8" au total\n")).toRawUTF8(), stderr);
    }
    // D57 : retirer la prise n° N de la piste choisie, par la MÊME fonction que
    // l'entrée de menu -- un sous-menu ne se clique pas sans souris.
    else if (nom.startsWith("retirer-prise:"))
        removeTakeFromSelectedTrack(nom.substring(14).getIntValue());
    else if (nom == "composer") {
        if (!takeCompWindow_) showTakeComp();
        const bool fait = takeCompPanel_.composeForCapture();
        std::fputs((juce::String::fromUTF8(u8"Composer : ")
                     + (fait ? juce::String::fromUTF8(u8"demandé")
                             : juce::String::fromUTF8(u8"rien à composer"))
                     + "\n").toRawUTF8(), stderr);
    }
    // D55.2 : enregistrer le projet ouvert, par l'entrée de menu même
    // (« Fichier ▸ Enregistrer ») -- sans quoi la survie de la recette au
    // disque ne se vérifierait qu'en écrivant le fichier autrement que
    // l'application ne l'écrit.
    else if (nom == "enregistrer") {
        menuItemSelected(kMenuFileSave, 0);
        std::fputs("Projet : enregistr\u00e9\n", stderr);
    }
    else if (nom == "fermer-prises") {
        if (takeCompWindow_) takeCompWindow_->setVisible(false);
        std::fputs("Panneau d'assemblage : ferm\u00e9\n", stderr);
    }
    else if (nom == "troncons") {
        const size_t piste = trackList_.selectedTrackIndex();
        const int surLaPiste = piste < project_.tracks.size()
                             ? static_cast<int>(project_.tracks[piste].compSegments.size()) : -1;
        std::fputs((juce::String::fromUTF8(u8"Tronçons : ")
                     + juce::String(takeCompPanel_.segmentCount())
                     + juce::String::fromUTF8(u8" au panneau, ") + juce::String(surLaPiste)
                     + juce::String::fromUTF8(u8" sur la piste\n")).toRawUTF8(), stderr);
    }
    // D18.4 : `aplatir:0:0:1` pose l'ordre et l'aplatit tout de suite. Le
    // panneau se pilote à la souris, et le RÉSULTAT est ce qu'il faut
    // regarder : c'est l'arrangement qui doit avoir changé.
    else if (nom.startsWith("aplatir:")) {
        std::vector<int> ordre;
        auto morceaux = juce::StringArray::fromTokens(nom.substring(8), ":", "");
        for (const auto& m : morceaux) ordre.push_back(std::max(0, m.getIntValue()));
        if (!ordre.empty()) {
            beginProjectEdit(u8"Aplatir l'ordre de jeu");
            if (vsm::sequencer::flattenPlayOrder(project_, ordre)) {
                rebuildFromProject(false);
                refreshTransportSchedule();
                arrangement_.repaint();
            }
        }
    }
    // D16.6 : la fenêtre des préférences, pour photographier le réglage du
    // métronome — elle ne s'ouvre autrement qu'au menu Fichier, à la souris.
    else if (nom == "preferences") menuItemSelected(kMenuFilePreferences, 0);
    // Ferme l'écran de rapport (import ou reconstruction) : VSM_IMPORT le
    // montre, et sans ce jeton l'arrangement d'un projet importé ne serait
    // photographiable qu'à travers lui. Pas dans le menu Affichage — le
    // rapport a son bouton Fermer et Échap — mais l'autoportrait n'a ni
    // souris ni clavier.
    else if (nom == "sans-rapport") importReport_.setVisible(false);
    // L'ONGLET DU BAS, pour photographier les effets d'une piste ou son
    // automation : un projet reconstruit avec --reverb-melange porte un
    // insert que personne n'a posé, et il doit se voir là où on le règle.
    else if (nom == "mixer")       bottomTabs_.setCurrentTabIndex(0);
    else if (nom == "automation")  bottomTabs_.setCurrentTabIndex(1);
    else if (nom == "effets")      bottomTabs_.setCurrentTabIndex(2);
    else if (nom == "midi-cc")     bottomTabs_.setCurrentTabIndex(3);
    // D32.2 : LES INDEX SONT NOMMÉS PAR LEUR ONGLET, et non écrits en clair.
    // Insérer « Liste » avant « Tempo » a décalé ce dernier d'un rang, et
    // `VSM_VUE=tempo` ouvrait la liste : un numéro en dur est un piège qui se
    // referme au premier onglet ajouté. `getTabNames().indexOf` ne se trompe
    // pas d'un rang, et rend -1 si l'onglet n'existe pas, ce qui se dit.
    else if (nom == "liste" || nom == "tempo") {
        const juce::String voulu = (nom == "liste") ? juce::String::fromUTF8(u8"Liste") : "Tempo";
        const int rang = bottomTabs_.getTabNames().indexOf(voulu);
        if (rang < 0) std::fputs(("VSM_VUE : onglet introuvable — " + voulu.toStdString() + "\n").c_str(),
                                  stderr);
        else bottomTabs_.setCurrentTabIndex(rang);
    }
    // CHOISIR UNE PISTE (piste:N, à partir de 0) : le piano roll, le rack et
    // l'onglet Effets suivent la piste choisie, et sans souris seule la
    // première se laissait photographier.
    // CHOISIR LE PREMIER CLIP D'UNE PISTE (premier-clip:N) : pour photographier
    // un geste qui porte sur UN clip -- transcrire, par exemple.
    else if (nom.startsWith("premier-clip:"))
        arrangement_.selectFirstClipOf(static_cast<size_t>(
            std::max(0, nom.fromFirstOccurrenceOf(":", false, false).getIntValue())));
    else if (nom.startsWith("piste:"))
        trackList_.selectTrackIndex(static_cast<size_t>(std::max(0, nom.substring(6).getIntValue())));
    // LANCER LA LECTURE pour l'autoportrait : le compteur de CPU de la barre
    // de transport ne dit rien tant que rien ne joue, et c'est justement lui
    // qu'il faut regarder pour juger un projet à soixante-quatre machines.
    // Sans ce jeton, la charge du fil audio ne se vérifie qu'à la souris.
    else if (nom == "jouer")       transport_.play();
    // CRÉER UN CLIP (D16.1) : `clip:piste:mesure`, à partir de 0 pour les deux.
    // Le geste est un DOUBLE-CLIC sur le vide d'une piste, c'est-à-dire
    // invisible à un autoportrait sans souris -- or c'est précisément le
    // résultat qu'il faut regarder. Passe par la MÊME fonction que le
    // double-clic et que l'article du menu : photographier autre chose que ce
    // que l'utilisateur déclenche ne photographierait rien.
    // D16.3 : couper à la tête et joindre, sur la sélection courante. Même
    // raison d'être que `clip:` -- un raccourci clavier ne se photographie pas.
    // D16.5 : verrouiller la piste N (à partir de 0), pour photographier le
    // cadenas et ce qu'il refuse -- l'article de menu ne s'atteint qu'à la
    // souris. Passe par la MÊME fonction que le menu.
    // D17.4 : masquer la piste N, pour photographier ce qu'il en reste.
    else if (nom.startsWith("masquer:")) {
        trackList_.selectTrackIndex(static_cast<size_t>(std::max(0, nom.substring(8).getIntValue())));
        hideSelectedTrack();
    }
    else if (nom.startsWith("verrouiller:")) {
        trackList_.selectTrackIndex(static_cast<size_t>(std::max(0, nom.substring(12).getIntValue())));
        toggleLockSelectedTrack();
    }
    else if (nom == "deplacer-clips") arrangement_.nudgeSelection(
        project_.timeSignatureMap.ticksPerBar(0, project_.ticksPerQuarterNote));
    // D17.7 : les courbes d'automation par-dessus les clips (la touche `A` de
    // l'arrangement), pour les photographier -- une touche ne se capture pas.
    else if (nom == "courbes") arrangement_.toggleAutomation();
    else if (nom.startsWith("choisir-clip:"))
        arrangement_.selectFirstClipOf(
            static_cast<size_t>(std::max(0, nom.substring(13).getIntValue())));
    else if (nom == "tout-choisir") arrangement_.selectAll();
    // D22.1 : le gain et la phase du clip choisi (gain-clip:+3, gain-clip:-6,
    // gain-clip:0, phase-clip), par la MÊME fonction que le menu contextuel.
    else if (nom.startsWith("gain-clip:")) {
        static const int kPas[] = {-6, -3, -1, 1, 3, 6};
        const int voulu = nom.substring(10).getIntValue();
        int choix = voulu == 0 ? 56 : -1;
        for (int i = 0; i < 6; ++i) if (kPas[i] == voulu) choix = 50 + i;
        if (choix < 0 || !arrangement_.runClipMenuActionForCapture(choix))
            std::fputs("VSM_VUE gain-clip : pas parmi -6, -3, -1, +1, +3, +6, 0, ou aucun clip choisi\n", stderr);
    }
    // D54 : la hauteur du clip choisi (hauteur-clip:+12, hauteur-clip:-5,
    // hauteur-clip:0), par la MÊME fonction que le menu contextuel. Et l'on
    // DIT la hauteur obtenue : deux demi-tons ne se lisent pas sur une capture
    // d'un clip de vingt pixels, alors qu'un nombre, si.
    else if (nom.startsWith("hauteur-clip:")) {
        static const int kDemiTons[] = {-12, -5, -1, 1, 5, 12};
        const int voulu = nom.substring(13).getIntValue();
        int choix = voulu == 0 ? 66 : -1;
        for (int i = 0; i < 6; ++i) if (kDemiTons[i] == voulu) choix = 60 + i;
        const bool fait = choix >= 0 && arrangement_.runClipMenuActionForCapture(choix);
        if (!fait) {
            std::fputs("VSM_VUE hauteur-clip : pas parmi -12, -5, -1, +1, +5, +12, 0, "
                        "ou aucun clip choisi\n", stderr);
        } else {
            juce::String dit;
            for (const auto& piste : project_.tracks)
                for (const auto& clip : piste.clips)
                    if (clip.pitchSemitones != 0.0)
                        dit << (dit.isEmpty() ? "" : ", ") << juce::String(clip.name)
                            << " " << (clip.pitchSemitones > 0 ? "+" : "")
                            << juce::String(clip.pitchSemitones, 2);
            std::fputs((juce::String::fromUTF8(u8"VSM_VUE hauteur-clip : ")
                         + (dit.isEmpty() ? juce::String::fromUTF8(u8"aucun clip transposé")
                                          : dit + juce::String::fromUTF8(u8" demi-ton(s)"))
                         + "\n").toRawUTF8(), stderr);
        }
    }
    // D34.2 : délier le clip choisi, par la MÊME fonction que le menu
    // contextuel. Et DIRE ce qui a changé : un marqueur de lien sur un clip de
    // vingt pixels ne se juge pas sur une capture, et le nombre de clips liés
    // avant et après, si.
    else if (nom == "delier-clip") {
        const int avant = linkedMidiClipCount();
        const bool fait = arrangement_.runClipMenuActionForCapture(24);
        std::fputs((juce::String::fromUTF8(u8"Copies liées : ") + juce::String(avant)
                     + juce::String::fromUTF8(u8" avant, ") + juce::String(linkedMidiClipCount())
                     + juce::String::fromUTF8(u8" après")
                     + (fait ? "" : juce::String::fromUTF8(u8" (rien fait : aucun clip choisi, "
                                                            u8"ou ce clip n'était lié à rien)"))
                     + "\n").toRawUTF8(), stderr);
    }
    // D34.3 : « deposer-audio:a.wav;b.wav » emprunte le MÊME point d'entrée que
    // le glisser-déposer (`filesDropped`) : un fichier lâché sur la fenêtre ne
    // se simule pas sans souris, et la boîte qui s'ouvre est justement ce que
    // l'étape change. Les chemins sont séparés par « ; », la virgule servant
    // déjà à séparer les commandes de VSM_VUE.
    else if (nom.startsWith("deposer-audio:")) {
        juce::StringArray chemins;
        chemins.addTokens(nom.substring(14), ";", "");
        juce::StringArray absolus;
        for (const auto& c : chemins)
            absolus.add(juce::File::getCurrentWorkingDirectory().getChildFile(c.trim())
                            .getFullPathName());
        std::fputs((juce::String::fromUTF8(u8"Dépôt simulé de ") + juce::String(absolus.size())
                     + juce::String::fromUTF8(u8" fichier(s).\n")).toRawUTF8(), stderr);
        const size_t avant = audioTrackCount();
        filesDropped(absolus, 0, 0);
        // LA BOÎTE S'OUVRE POUR UN HUMAIN ; ON PREND SA PLACE. Un clic ne se
        // pilote pas sans souris, et JUCE ne pose ici aucune `AlertWindow`
        // qu'on puisse retrouver dans l'arbre pour la presser. On appelle donc
        // EXACTEMENT la méthode que le bouton « Poser » appelle, et l'on compte
        // les pistes : ce qui est vérifié est tout le chemin sauf le clic.
        placeDroppedAudioOnTracks();
        std::fputs((juce::String::fromUTF8(u8"Pistes audio : ") + juce::String(int(avant))
                     + juce::String::fromUTF8(u8" avant, ") + juce::String(int(audioTrackCount()))
                     + juce::String::fromUTF8(u8" après le dépôt posé.\n")).toRawUTF8(), stderr);
        pendingDroppedAudio_ = juce::File();
        pendingDroppedAudios_.clear();
    }
    // D34.4 : « regle:temps » et « regle:mesures », par la MÊME méthode que le
    // menu. La règle DIT ce qu'elle affiche, parce qu'une capture d'écran d'une
    // règle graduée « 0 · 5 · 10 » et d'une autre graduée « 1 · 2 · 3 » se
    // distinguent mal quand on ne sait pas laquelle on regarde.
    else if (nom.startsWith("regle:")) {
        const juce::String demande = nom.substring(6);
        if (demande != "temps" && demande != "mesures") {
            std::fputs("VSM_VUE regle : attendu temps ou mesures\n", stderr);
            return;
        }
        setRulerInTime(demande == "temps");
        std::fputs((juce::String::fromUTF8(u8"Règle : ")
                     + (arrangement_.rulerInTime()
                            ? juce::String::fromUTF8(u8"minutes:secondes")
                            : juce::String::fromUTF8(u8"mesures"))
                     + juce::String::fromUTF8(u8", écart mini ")
                     + juce::String(arrangement_.rulerSmallestGapForCapture(), 1)
                     + juce::String::fromUTF8(u8" px, graduations : ")
                     + arrangement_.rulerLabelsForCapture() + "\n").toRawUTF8(), stderr);
    }
    // D35 : « pistes » écrit l'arbre des pistes -- nom, profondeur, muet, solo
    // --, et « monter-piste » / « descendre-piste » empruntent la MÊME méthode
    // que le menu. Un ordre de pistes ne se juge pas sur une capture d'écran de
    // douze lignes ; écrit sur une ligne, il se compare d'un coup d'œil.
    else if (nom == "pistes") std::fputs((trackTreeForCapture() + "\n").toRawUTF8(), stderr);
    else if (nom == "monter-piste" || nom == "descendre-piste") {
        const bool fait = moveSelectedTrack(nom == "monter-piste" ? -1 : +1);
        std::fputs(((fait ? juce::String() : juce::String::fromUTF8(u8"(refusé) "))
                     + trackTreeForCapture() + "\n").toRawUTF8(), stderr);
    }
    // D34.5 : « auto-forme:sinus » et ses variantes, par la MÊME méthode que
    // le menu. Le tracé DIT combien de points il pose et combien il remplace :
    // une courbe de six pixels de haut sur une capture ne se juge pas, et le
    // critère de l'étape est justement un nombre de points.
    else if (nom.startsWith("auto-forme:")) {
        using vsm::sequencer::AutomationShape;
        const juce::String demande = nom.substring(11);
        AutomationShape forme = AutomationShape::Line;
        bool descendante = false;
        if (demande == "rampe") forme = AutomationShape::Line;
        else if (demande == "rampe-bas") { forme = AutomationShape::Line; descendante = true; }
        else if (demande == "sinus") forme = AutomationShape::Sine;
        else if (demande == "triangle") forme = AutomationShape::Triangle;
        else if (demande == "carre") forme = AutomationShape::Square;
        else {
            std::fputs("VSM_VUE auto-forme : attendu rampe, rampe-bas, sinus, triangle ou carre\n",
                        stderr);
            return;
        }
        arrangement_.drawAutomationShapeOnSelection(trackList_.selectedTrackIndex(),
                                                     forme, descendante);
    }
    // D34.4 : zoomer l'arrangement, pour vérifier la règle À PLUSIEURS ZOOMS.
    // Le pas des graduations n'a de sens qu'ainsi : une règle correcte à un
    // seul zoom ne prouve rien de la règle qui la choisit.
    else if (nom.startsWith("zoom-arrangement:"))
        arrangement_.zoomHorizontally(nom.substring(17).getFloatValue());
    else if (nom == "copies-liees")
        std::fputs((juce::String::fromUTF8(u8"Copies liées : ") + juce::String(linkedMidiClipCount())
                     + juce::String::fromUTF8(u8" clip(s) MIDI partagent leur fenêtre.\n")).toRawUTF8(),
                    stderr);
    // D22.4 : une note par le chemin du clavier d'ordinateur, pour
    // photographier les voyants IN (elle arrive) et OUT (elle part).
    else if (nom.startsWith("note:"))
        playNoteForCapture(static_cast<uint8_t>(juce::jlimit(0, 127, nom.substring(5).getIntValue())), 15);
    // D23 : armer une piste (armer:N), inverser sa polarité (polarite:N), les
    // hauteurs (pistes-a-la-fenetre, hauteur-pistes:N) -- par les MÊMES
    // fonctions que les boutons et les menus.
    else if (nom.startsWith("armer:")) {
        const auto p = static_cast<size_t>(std::max(0, nom.substring(6).getIntValue()));
        if (p < project_.tracks.size()) {
            project_.tracks[p].armed = true;
            trackList_.refreshTrackRow(p);
            refreshArmedTracks();
        }
    }
    else if (nom.startsWith("polarite:")) {
        const auto p = static_cast<size_t>(std::max(0, nom.substring(9).getIntValue()));
        if (p < project_.tracks.size()) {
            beginProjectEdit("Mixage");
            project_.tracks[p].invertPhase = !project_.tracks[p].invertPhase;
            mixer_.setProject(&project_);
            if (mixer_.onMixChanged) mixer_.onMixChanged();
        }
    }
    // D25.2 : muet et solo de la piste N, par les mêmes fonctions que Maj+M / Maj+S.
    else if (nom.startsWith("muet-piste:")) {
        trackList_.selectTrackIndex(static_cast<size_t>(std::max(0, nom.substring(11).getIntValue())));
        toggleMuteSelectedTrack();
    }
    else if (nom.startsWith("solo-piste:")) {
        trackList_.selectTrackIndex(static_cast<size_t>(std::max(0, nom.substring(11).getIntValue())));
        toggleSoloSelectedTrack();
    }
    // D30 : les quatre gestes de la phase, par les MÊMES fonctions que les
    // menus -- un chemin de capture qui doublerait le code photographierait
    // autre chose que ce qu'on livre.
    else if (nom.startsWith("solo-protege:")) {              // D30.1
        trackList_.selectTrackIndex(static_cast<size_t>(std::max(0, nom.substring(13).getIntValue())));
        toggleSoloSafeSelectedTrack();
    }
    else if (nom.startsWith("piste-eteinte:")) {             // D30.2
        trackList_.selectTrackIndex(static_cast<size_t>(std::max(0, nom.substring(14).getIntValue())));
        toggleDisableSelectedTrack();
    }
    // trim-piste:N:dB -- « trim-piste:2:-6 » pose -6 dB sur la piste 2.
    else if (nom.startsWith("trim-piste:")) {                // D30.4
        const juce::String reste = nom.substring(11);
        const int deuxPoints = reste.indexOfChar(':');
        const size_t piste = static_cast<size_t>(std::max(0,
            (deuxPoints >= 0 ? reste.substring(0, deuxPoints) : reste).getIntValue()));
        if (deuxPoints < 0 || piste >= project_.tracks.size()) {
            std::fputs("VSM_VUE trim-piste : attendu trim-piste:PISTE:DECIBELS\n", stderr);
        } else {
            beginProjectEdit(u8"Trim d'entrée");
            project_.tracks[piste].inputTrimDb =
                juce::jlimit(-24.0f, 24.0f, reste.substring(deuxPoints + 1).getFloatValue());
            trackList_.selectTrackIndex(piste);
            mixer_.setProject(&project_);
            refreshTrackViews();
        }
    }
    // D30.3 : la chaîne d'une piste à l'autre. EN COMMANDES DE VUE et pas
    // seulement en entrées de menu, parce que le geste en demande TROIS à la
    // suite -- copier, changer de piste, coller -- et que `VSM_MENU` s'exécute
    // en bloc APRÈS `VSM_VUE` : on ne peut pas s'y intercaler un changement de
    // piste. Les trois appellent les mêmes fonctions que le menu.
    // D31.4 : les effets MIDI, par les MÊMES fonctions que le sous-menu.
    // « fx-midi:type » ajoute, « fx-midi-vider » retire, « fx-midi-reporter »
    // écrit dans les notes.
    else if (nom.startsWith("fx-midi:")) addMidiEffectToSelectedTrack(nom.substring(8).toStdString());
    else if (nom.startsWith("defiler-effets:")) effectChain_.scrollBy(nom.substring(15).getIntValue());
    // D32.4 / D32.5 : le renommage en série (motif après les deux points) et
    // les statistiques, par les MÊMES fonctions que les menus -- la boîte de
    // saisie et la boîte de message ne se pilotent pas sans souris.
    else if (nom.startsWith("renommer-serie:")) renameTracksInSeries(nom.substring(15));
    else if (nom == "statistiques") showProjectStatistics();
    // D33.3 : « scrub:TICK:VITESSE » emprunte le MÊME rappel que le geste de
    // la règle -- un glissé de souris ne se pilote pas sans souris, mais ce
    // qu'il déclenche, si. « scrub:0:0 » le termine.
    else if (nom.startsWith("scrub:")) {
        const auto m = juce::StringArray::fromTokens(nom.substring(6), ":", "");
        if (m.size() >= 2 && pianoRollPanel_.onScrub) {
            pianoRollPanel_.onScrub(static_cast<vsm::midi::Tick>(m[0].getLargeIntValue()),
                                     m[1].getDoubleValue());
            std::fputs((juce::String::fromUTF8(u8"Scrub : tick ") + m[0]
                         + juce::String::fromUTF8(u8", vitesse ") + m[1]
                         + juce::String::fromUTF8(u8" -> lecture ")
                         + (audioEngine_.processGraph().isPlaying() ? "en marche" : "arrêtée")
                         + juce::String::fromUTF8(u8", vitesse du moteur ")
                         + juce::String(audioEngine_.processGraph().playbackSpeed(), 2)
                         + "\n").toRawUTF8(), stderr);
        } else {
            std::fputs("VSM_VUE scrub : attendu scrub:TICK:VITESSE\n", stderr);
        }
    }
    else if (nom.startsWith("ouvrir-midi:"))
        openMidiFileDirect(juce::File::getCurrentWorkingDirectory().getChildFile(nom.substring(12)));
    // D33.2 : « fondu-securite:0 » l'éteint, « fondu-securite:5 » l'allonge --
    // et les clips sont RECHARGÉS, sans quoi le réglage ne prendrait qu'au
    // prochain chargement de projet.
    // D34.1 : « fondu-croise:puissance|lineaire|lente|rapide ». Elle emprunte
    // la MÊME méthode que l'entrée de menu, et DIT la forme obtenue -- une
    // capture d'écran ne montre pas une courbe de fondu, et vérifier l'absence
    // d'erreur ne vérifie rien.
    else if (nom.startsWith("fondu-croise:")) {
        const juce::String demande = nom.substring(13);
        const auto forme = demande == "lineaire" ? vsm::sequencer::FadeShape::Linear
                         : demande == "lente"    ? vsm::sequencer::FadeShape::Slow
                         : demande == "rapide"   ? vsm::sequencer::FadeShape::Fast
                                                 : vsm::sequencer::FadeShape::EqualPower;
        if (demande != "puissance" && demande != "lineaire" && demande != "lente"
            && demande != "rapide") {
            std::fputs("VSM_VUE fondu-croise : attendu puissance, lineaire, lente ou rapide\n", stderr);
            return;
        }
        setCrossfadeShape(forme);
        std::fputs((juce::String::fromUTF8(u8"Forme des fondus croisés : ") + demande
                     + juce::String::fromUTF8(u8" — appliquée à ")
                     + juce::String(audioSpansWithCrossfade())
                     + juce::String::fromUTF8(u8" jonction(s) de clips audio.\n")).toRawUTF8(),
                    stderr);
    }
    else if (nom.startsWith("fondu-securite:")) {
        safetyFadeMs_ = juce::jlimit(0.0, 50.0, nom.substring(15).getDoubleValue());
        vsm::app::ui::UiScale::properties().setValue("fonduDeSecuriteMs", safetyFadeMs_);
        loadAudioTracks();
        std::fputs((juce::String::fromUTF8(u8"Fondu de sécurité : ")
                     + juce::String(safetyFadeMs_, 2)
                     + juce::String::fromUTF8(u8" ms aux bords des clips audio.\n")).toRawUTF8(),
                    stderr);
    }
    // D32.3 : montrer le clavier, et y poser une note pour la photographier
    // (« clavier-note:60 »). Une touche enfoncée à la souris ne se capture pas.
    else if (nom == "clavier") pianoRollPanel_.setKeyboardVisible(true);
    else if (nom == "clavier-journal") { pianoRollPanel_.setKeyboardVisible(true); journalClavier_ = true; }
    else if (nom.startsWith("clavier-note:")) {
        const int note = juce::jlimit(0, 127, nom.substring(13).getIntValue());
        pianoRollPanel_.setKeyboardVisible(true);
        epingleClavier_ = true;
        pianoRollPanel_.setSoundingNotes(note < 64 ? (uint64_t{1} << note) : 0,
                                          note >= 64 ? (uint64_t{1} << (note - 64)) : 0);
    }
    else if (nom == "fx-midi-vider") clearMidiEffectsOfSelectedTrack();
    else if (nom == "fx-midi-reporter") bakeMidiEffectsOfSelectedTrack();
    // D31.5 : ce que l'export MIDI ne portera pas, SANS ouvrir le sélecteur de
    // fichier -- un avertissement qu'on ne peut pas déclencher sans souris est
    // un avertissement qu'on ne peut pas vérifier.
    else if (nom == "diagnostic-export-midi") {
        const juce::StringArray divergentes = tracksWhoseMidiExportWillDiffer();
        std::fputs((divergentes.isEmpty()
                        ? juce::String::fromUTF8(u8"Export MIDI : le .mid portera tout ce qui est joué.\n")
                        : juce::String::fromUTF8(u8"Export MIDI — ne seront pas portées : ")
                              + divergentes.joinIntoString(" ; ") + "\n").toRawUTF8(), stderr);
    }
    else if (nom == "copier-chaine") copySelectedTrackChain();
    else if (nom == "coller-chaine") pasteChainIntoSelectedTrack(true);
    else if (nom == "ajouter-chaine") pasteChainIntoSelectedTrack(false);
    else if (nom.startsWith("reduire-automation:")) {        // D30.5
        trackList_.selectTrackIndex(static_cast<size_t>(std::max(0, nom.substring(19).getIntValue())));
        thinAutomationOfSelectedTrack();
    }
    else if (nom == "pistes-a-la-fenetre") arrangement_.fitTracksToWindow();
    // D27.4 : sortie-midi:N:port -- le port de la piste N, par la même fonction que le menu.
    else if (nom.startsWith("sortie-midi:")) {
        const auto morceaux = juce::StringArray::fromTokens(nom.substring(12), ":", "");
        if (morceaux.size() >= 2) {
            trackList_.selectTrackIndex(static_cast<size_t>(std::max(0, morceaux[0].getIntValue())));
            setSelectedTrackMidiOutput(morceaux[1].toStdString());
        }
    }
    // D28 : programme:N:P:B (P de 1 à 128, B vide ou -1 = aucune), canal-entree:N:C.
    else if (nom.startsWith("programme:")) {
        const auto m = juce::StringArray::fromTokens(nom.substring(10), ":", "");
        if (m.size() >= 2) {
            trackList_.selectTrackIndex(static_cast<size_t>(std::max(0, m[0].getIntValue())));
            setSelectedTrackMidiProgram(m[1].getIntValue() - 1, m.size() >= 3 ? m[2].getIntValue() : -1);
        }
    }
    else if (nom.startsWith("canal-entree:")) {
        const auto m = juce::StringArray::fromTokens(nom.substring(13), ":", "");
        if (m.size() >= 2) {
            trackList_.selectTrackIndex(static_cast<size_t>(std::max(0, m[0].getIntValue())));
            setSelectedTrackInputChannel(m[1].getIntValue());
        }
    }
    // D29.1 / D29.2 : les locateurs à la tête, la tête déplacée.
    // D29.5 : la grille adaptative et le zoom du piano roll, pour la photographier à deux zooms.
    else if (nom == "grille-auto") pianoRoll_.setAdaptiveGrid(true);
    else if (nom.startsWith("zoom-piano:")) pianoRoll_.zoomHorizontally(nom.substring(11).getFloatValue());
    else if (nom == "locateur-debut") setLoopBoundaryAtPlayhead(true);
    else if (nom == "locateur-fin") setLoopBoundaryAtPlayhead(false);
    else if (nom.startsWith("tete-temps:")) seekByBeats(nom.substring(11).getIntValue());
    else if (nom.startsWith("tete-mesure:")) seekByBars(nom.substring(12).getIntValue());
    else if (nom.startsWith("hauteur-pistes:"))
        arrangement_.setAllTrackHeights(nom.substring(15).getIntValue());
    else if (nom == "phase-clip") {
        if (!arrangement_.runClipMenuActionForCapture(57))
            std::fputs("VSM_VUE phase-clip : aucun clip choisi\n", stderr);
    }
    else if (nom == "couper-clips") arrangement_.splitSelectionAtPlayhead();
    else if (nom == "joindre-clips") arrangement_.joinSelection();
    else if (nom.startsWith("tete:")) {
        const int mesure = std::max(0, nom.substring(5).getIntValue());
        const vsm::midi::Tick parMesure =
            project_.timeSignatureMap.ticksPerBar(0, project_.ticksPerQuarterNote);
        const auto tick = static_cast<vsm::midi::Tick>(mesure) * parMesure;
        transport_.seekToTick(tick);
        audioEngine_.processGraph().seekSeconds(project_.ticksToSeconds(tick));
        arrangement_.setPlayheadTick(tick);
    }
    else if (nom.startsWith("clip:")) {
        auto morceaux = juce::StringArray::fromTokens(nom.substring(5), ":", "");
        const size_t piste = static_cast<size_t>(std::max(0, morceaux[0].getIntValue()));
        const int mesure = morceaux.size() > 1 ? std::max(0, morceaux[1].getIntValue()) : 0;
        const vsm::midi::Tick parMesure =
            project_.timeSignatureMap.ticksPerBar(0, project_.ticksPerQuarterNote);
        createClipOnTrack(piste, static_cast<vsm::midi::Tick>(mesure) * parMesure);
    }
    // D30 : UNE COMMANDE INCONNUE SE DIT, et c'est la correction la plus utile
    // de cette phase. `VSM_VUE=melangeur` (pour « mixer ») a été avalé sans un
    // mot pendant la vérification de D30.1 : la capture est sortie, elle
    // montrait l'écran d'accueil, et rien ne distinguait « la commande n'existe
    // pas » de « le réglage n'a rien changé ». C'est exactement la panne muette
    // que le projet s'interdit -- et elle vivait dans l'outil qui sert à
    // prouver que les interfaces marchent, donc à l'endroit où elle coûte le
    // plus cher.
    else {
        std::fputs(("VSM_VUE : commande inconnue « " + nom.toStdString()
                    + " » — rien n'a été fait.\n").c_str(), stderr);
    }
}

void MainComponent::dockPanels() {
    for (auto* fenetre : { &trackListWindow_, &pianoRollWindow_, &synthRackWindow_,
                            &mixerWindow_, &arrangementWindow_ }) {
        fenetre->setVisible(false);
        // DÉTACHER AVANT D'ANCRER, et c'est le point qui a coûté une capture :
        // une ResizableWindow qui garde son pointeur de contenu REPLAQUE ce
        // contenu à sa propre taille à chaque resized(), même re-parenté --
        // le piano roll se retrouvait plein cadre par-dessus tous les volets.
        fenetre->clearContentComponent();
    }
    // Re-parentage : addAndMakeVisible RETIRE le composant de sa fenêtre --
    // c'est le même objet qui vit ici ou là, jamais deux états.
    for (auto* volet : std::initializer_list<juce::Component*>{
             &trackList_, &synthRack_, &bottomTabs_, &arrangement_, &pianoRollPanel_ })
        addAndMakeVisible(volet);

    auto& prefs = vsm::app::ui::UiScale::properties();
    dockGauche_ = prefs.getIntValue("dock.gauche", dockGauche_);
    dockDroite_ = prefs.getIntValue("dock.droite", dockDroite_);
    dockBas_ = prefs.getIntValue("dock.bas", dockBas_);
    auto cabler = [this](SeparateurDock& sep, int& taille, const char* cle, int signe) {
        sep.onDebut = [this, &taille] { dockBase_ = taille; };
        sep.onGlisse = [this, &taille, signe](int delta) {
            taille = dockBase_ + signe * delta;
            resized();
        };
        sep.onFin = [&taille, cle] {
            vsm::app::ui::UiScale::properties().setValue(cle, taille);
            vsm::app::ui::UiScale::properties().saveIfNeeded();
        };
    };
    // Le signe dit dans quel sens « tirer vers la droite / le bas » AGRANDIT :
    // +1 pour le volet de gauche, -1 pour ceux qui touchent le bord opposé.
    cabler(sepGauche_, dockGauche_, "dock.gauche", +1);
    cabler(sepDroite_, dockDroite_, "dock.droite", -1);
    cabler(sepBas_, dockBas_, "dock.bas", -1);
    for (auto* sep : { &sepGauche_, &sepDroite_, &sepBas_ })
        addAndMakeVisible(sep);
    arrangement_.setVisible(centerShowsArrangement_);
    pianoRollPanel_.setVisible(!centerShowsArrangement_);
    resized();
}

void MainComponent::undockPanels() {
    rendreLesZones();   // D122 : on quitte la fenêtre unique dans sa disposition normale
    trackListWindow_.setContentNonOwned(&trackList_, false);
    pianoRollWindow_.setContentNonOwned(&pianoRollPanel_, false);
    synthRackWindow_.setContentNonOwned(&synthRack_, false);
    mixerWindow_.setContentNonOwned(&bottomTabs_, false);
    arrangementWindow_.setContentNonOwned(&arrangement_, false);
    for (auto* sep : { &sepGauche_, &sepDroite_, &sepBas_ })
        sep->setVisible(false);
    pianoRollPanel_.setVisible(true);
    arrangement_.setVisible(true);
    resized();
}

void MainComponent::showFloatingPanels() {
    // Raccourcis globaux : chaque fenêtre flottante remonte ses touches non
    // consommées ici (voir keyPressed). Idempotent : JUCE ignore un écouteur
    // déjà inscrit.
    for (auto* fenetre : { &trackListWindow_, &pianoRollWindow_, &synthRackWindow_, &mixerWindow_,
                            &arrangementWindow_ })
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

    singleWindow_ = vsm::app::ui::UiScale::properties()
                        .getBoolValue("fenetreUnique", true);
    if (singleWindow_) {
        // UNE fenêtre, la taille de l'écran de travail : c'est elle le studio.
        //
        // SAUF SI `VSM_TAILLE` L'A FIXÉE (D58). Cette ligne recouvrait la
        // taille demandée juste après que `Main.cpp` l'avait posée : quatre
        // valeurs différentes rendaient la MÊME image de 1264x742, soit la
        // largeur de l'écran divisée par l'échelle d'interface. Un réglage de
        // vérification qui promet et ne fait rien est pire qu'un réglage
        // absent, et il rendait invérifiable toute disposition qui ne tient
        // qu'à une certaine largeur.
        if (!tailleImposee_)
            if (auto* socle = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent()))
                socle->setBounds(screenArea.reduced(8));
        dockPanels();
        return;
    }

    int topY = getScreenBounds().getBottom() + 10;
    int leftW = 320, rightW = 360, mixerH = 300;
    int centerW = juce::jmax(500, screenArea.getWidth() - leftW - rightW - 40);
    int centerH = juce::jmax(360, screenArea.getBottom() - topY - mixerH - 20);

    trackListWindow_.setBounds(screenArea.getX() + 10, topY, leftW, centerH);
    pianoRollWindow_.setBounds(screenArea.getX() + leftW + 20, topY, centerW, centerH);
    synthRackWindow_.setBounds(screenArea.getX() + leftW + centerW + 30, topY, rightW, centerH);
    mixerWindow_.setBounds(screenArea.getX() + 10, topY + centerH + 10, screenArea.getWidth() - 20, mixerH);
    // L'ARRANGEMENT reprend la place du piano roll : les deux montrent le même
    // morceau à deux échelles, et on passe de l'un à l'autre plutôt que de les
    // regarder ensemble sur un écran qui n'en a pas la place. Masqué au
    // démarrage -- le menu Affichage l'ouvre.
    arrangementWindow_.setBounds(screenArea.getX() + leftW + 20, topY, centerW, centerH);

    trackListWindow_.setVisible(true);
    pianoRollWindow_.setVisible(true);
    synthRackWindow_.setVisible(true);
    mixerWindow_.setVisible(true);
}

void MainComponent::timerCallback() {
    // UNE SEULE HORLOGE, ET PLUS DE « SELON LES CAS » (D8.3). Elle compte les
    // échantillons réellement sortis de la carte son : il n'existe pas de
    // mesure plus exacte de « où en est la lecture », puisque c'est
    // littéralement ce qu'on entend.
    //
    // SANS CARTE SON, C'EST LA MÊME HORLOGE, simplement alimentée autrement :
    // un thread de secours appelle `processBlock` dans un tampon qu'on jette,
    // au rythme du temps réel (voir `Transport`). L'application reste
    // utilisable pour éditer, faire défiler et exporter sur une machine sans
    // audio, et la position affichée vient du même endroit qu'ailleurs.
    // La carte son peut ouvrir à une autre fréquence que celle qu'on croit, et
    // en changer en cours de route (réglages audio). Les effets suivent.
    applyAudioConfig();

    // RETOUR AU DÉBUT À L'ARRÊT (D14.5). La transition se voit ICI, sur
    // l'horloge unique, quel que soit le chemin qui a arrêté le transport --
    // le bouton, la barre d'espace, une commande MIDI apprise : un seul
    // endroit, pas quatre.
    {
        const bool lecture = transport_.state() == TransportState::Playing;
        if (lecture && !etaitEnLecture_) departLecture_ = transport_.currentTick();
        // D16.8 : L'ARRÊT CLÔT LES PASSES EN LATCH. Le front descendant est
        // détecté ICI, et pas dans les six endroits qui appellent
        // `transport_.stop()` : un seul de ces six oublié laisserait une passe
        // ouverte pour toujours, et elle se déposerait au prochain arrêt --
        // longtemps après le geste, et sur la mauvaise plage. Fermé AVANT le
        // retour au départ, pour que la fin de la passe soit là où la lecture
        // s'est arrêtée et non là où elle était partie.
        if (!lecture && etaitEnLecture_) mixer_.closeLatchedPasses();
        if (!lecture && etaitEnLecture_ && retourAuDepart_) seekAllViews(departLecture_);
        etaitEnLecture_ = lecture;
    }

    const bool audioClockAvailable = audioEngine_.isDeviceOpen();
    // La carte son peut apparaître ou disparaître en cours de route (réglages
    // audio, périphérique débranché) : le bouton Rec doit suivre, et dire
    // laquelle des deux conditions manque -- et c'est aussi le moment où
    // l'horloge de secours prend ou rend la main.
    if (audioClockAvailable != recordDeviceWasOpen_) {
        refreshArmedTracks();
        transport_.setAudioDeviceOpen(audioClockAvailable,
                                       audioEngine_.currentSampleRate(),
                                       audioEngine_.currentBlockSize());
    }
    const double horlogeAudio = audioEngine_.processGraph().currentSeconds();
    // La tête de lecture ne recule jamais AVANT le début du morceau à
    // l'affichage : pendant un décompte, la position du moteur est négative
    // (voir ProcessGraph::seekSeconds), et un tick négatif ne veut rien dire
    // pour le piano roll. C'est le compteur de la barre de transport qui dit
    // alors où l'on en est.
    const vsm::midi::Tick playhead = transport_.currentTick();

    // ENREGISTREMENT : vider la file de capture à chaque tour, décompte
    // compris. `MidiRecorder` écarte lui-même ce qui précède le point d'entrée,
    // donc rien ne se perd et rien n'entre par erreur.
    // D17.3 : LE TAMPON RÉTROSPECTIF SE REMPLIT TOUJOURS. Hors
    // enregistrement, la file de capture n'était vidée par personne : ce qu'on
    // jouait au clavier était perdu à mesure, et « récupérer ce qui vient
    // d'être joué » n'aurait rien trouvé. Elle est donc vidée ici, et ce qui
    // en sort part au tampon.
    if (recordPhase_ == RecordPhase::Off) {
        recordDrain_.clear();
        audioEngine_.drainRecordedEvents(recordDrain_);
        for (const auto& evenement : recordDrain_) retrospectif_.push(evenement);
        // D25.4 : les contrôleurs vont au tampon rétrospectif avec les notes.
        recordControlDrain_.clear();
        audioEngine_.drainRecordedControls(recordControlDrain_);
        for (const auto& c : recordControlDrain_) retrospectif_.pushControl(c);
    }

    bool priseEmpilee = false;
    if (recordPhase_ != RecordPhase::Off) {
        drainRecording();
        if (recordPhase_ == RecordPhase::CountIn) {
            if (horlogeAudio >= punchSeconds_ - 1.0e-9) {
                // Le décompte est fini. Le transport joue déjà -- le décompte
                // EST de la lecture, simplement située avant le point d'entrée
                // -- il n'y a donc plus rien à synchroniser : seulement à
                // changer de phase.
                recordPhase_ = RecordPhase::Recording;
                transportBar_.setCountIn(0);
            } else {
                const double restant = punchSeconds_ - horlogeAudio;
                const double parTemps =
                    60.0 / std::max(1.0, project_.tempoMap.bpmAt(punchTick_));
                transportBar_.setCountIn(std::max(1, static_cast<int>(std::ceil(restant / parTemps))));
            }
        }
        // ENREGISTREMENT EN BOUCLE : chaque rebouclage clôt une passe. On
        // compare au compteur du MOTEUR plutôt qu'à la position affichée : la
        // position revient en arrière, mais elle le fait entre deux tours de
        // ce timer, et deux boucles courtes pourraient passer inaperçues.
        if (recordPhase_ == RecordPhase::Recording
            && recordMode_ == vsm::sequencer::RecordMode::Stack
            && audioEngine_.processGraph().isLoopActive()) {
            const uint64_t tours = audioEngine_.processGraph().loopWrapCount();
            while (loopPassesClosed_ < tours) {
                closePass(static_cast<uint32_t>(loopPassesClosed_),
                           audioEngine_.processGraph().loopStartSeconds(),
                           audioEngine_.processGraph().loopEndSeconds());
                ++loopPassesClosed_;
                priseEmpilee = true;
            }
        }

        // Une note jouée et perdue faute de place dans la file serait une
        // prise incomplète, et il n'est pas permis que ça arrive en silence.
        if (audioEngine_.droppedRecordedEvents() > 0 && !recordDropReported_) {
            recordDropReported_ = true;
            boiteNotesPerdues();
        }
    }
    // Une passe empilée a changé le matériau des pistes armées : il faut le
    // republier, sans quoi la boucle suivante rejouerait la passe précédente.
    if (priseEmpilee) {
        audioEngine_.processGraph().setProject(project_);
        pianoRollPanel_.refresh();
        pianoRoll_.repaint();
    }

    transportBar_.setInputLevel(audioEngine_.readInputPeak(),
                                 audioEngine_.currentInputChannels());
    // D22.4 : LES VOYANTS MIDI, sur ce qui a bougé depuis le dernier tour.
    {
        const uint64_t in = audioEngine_.midiInCount();
        const uint64_t out = audioEngine_.processGraph().notesSentToInstruments()
                             + audioEngine_.midiOutSentCount();   // D27.3 : le port compte aussi
        transportBar_.setMidiActivity(in != midiInSeen_, out != notesOutSeen_);
        midiInSeen_ = in;
        notesOutSeen_ = out;
    }
    // D23.2 : l'écoute automatique suit l'armement et le transport ; le
    // témoin dit l'état réel, quel que soit le mode.
    applyMonitoringMode();
    transportBar_.setInputMonitoring(audioEngine_.inputMonitoring());
    pianoRoll_.setPlayheadTick(playhead);
    synthRack_.setPlayheadTick(playhead); // éclaire le pas en cours sur les grilles
    arrangement_.setPlayheadTick(playhead);
    // D21.4 : LA SIGNATURE SOUS LA TÊTE, pas celle du tick zéro -- mise à
    // jour seulement quand elle change, la barre n'a pas à se redessiner
    // trente fois par seconde.
    {
        const int num = project_.timeSignatureMap.numeratorAt(std::max<vsm::midi::Tick>(0, playhead));
        const int den = static_cast<int>(project_.timeSignatureMap.denominatorAt(std::max<vsm::midi::Tick>(0, playhead)));
        if (num != derniereSignatureNum_ || den != derniereSignatureDen_) {
            derniereSignatureNum_ = num;
            derniereSignatureDen_ = den;
            transportBar_.setTimeSignature(num, den);
        }
    }
    pianoRollPanel_.refresh(); // règle + barre d'outils suivent la tête de lecture et l'historique

    autosaveIfNeeded();

    // CE QUE LE THREAD MIDI A DÉPOSÉ (D10.2) : mixage et transport, qu'il n'a
    // pas le droit de toucher lui-même.
    applyLearnedControls();
    // ET UN APPRENTISSAGE PEUT AVOIR EU LIEU SANS QUE PERSONNE LE DISE : il
    // arrive du thread MIDI, au moment où l'utilisateur tourne un bouton.
    // Comparer le compte est la seule façon de s'en apercevoir sans faire
    // signer un contrat au thread MIDI.
    if (const size_t associations = audioEngine_.midiLearnMappingCount();
        associations != midiLearnSeenCount_) {
        midiLearnSeenCount_ = associations;
        midiLearnPanel_.setWaiting({});
        saveMidiLearnMappings();
        refreshMidiLearnList();
        menuItemsChanged();
    }

    // LA FIN DU MORCEAU EST LA SEULE CHOSE QUE LE GRAPHE NE PEUT PAS DÉCIDER :
    // il sait rendre, pas ce qu'est « la fin ». Le transport la connaît, et
    // c'est ici qu'on lui demande de regarder.
    transport_.poll();
    const bool playing = (transport_.state() == TransportState::Playing);
    // LE TRANSPORT PEUT S'ARRÊTER TOUT SEUL, à la fin du morceau : une prise
    // laissée ouverte serait une prise perdue, puisque rien ne l'écrirait.
    if (!playing && recordPhase_ == RecordPhase::Recording) stopRecording();

    transportBar_.setCpuUsage(audioEngine_.currentCpuUsagePercent());
    transportBar_.setXrunCount(audioEngine_.xrunCount());   // D41.3
    // D43.1 : `lastError()` existait, était publique, et n'était appelée NULLE
    // PART. Le moteur notait pourquoi il n'y aurait pas de son, et personne ne
    // le lisait : l'application s'ouvrait, tous ses boutons répondaient, et
    // elle ne faisait aucun bruit sans un mot. Le repli lui-même est juste --
    // éditer, mixer et exporter n'ont pas besoin de carte son ; le défaut
    // n'était pas de continuer, il était de continuer EN SILENCE.
    transportBar_.setAudioUnavailable(audioEngine_.isDeviceOpen() ? juce::String()
                                                                  : audioEngine_.lastError());
    // D42.3 : QUELLE PISTE COÛTE. Le total dit qu'il faut alléger ; celui-ci
    // dit quoi. Publié au même rythme que les vumètres, dont il partage la
    // banque.
    mixer_.publishRenderCosts(
        [this](size_t piste) { return audioEngine_.processGraph().readTrackRenderMicros(piste); },
        transport_.state() == vsm::audio::engine::TransportState::Playing);
    // VSM_TRACE_COUTS=1 : écrire les coûts par piste sur la sortie d'erreur, une
    // fois par seconde. Ce n'est pas une trace de mise au point laissée là : le
    // marquage ambre de D42.3 ne se photographie que si une piste dépasse ses
    // voisines, et une capture qui ne le montre pas laisse deux explications
    // ouvertes -- « la règle est fausse » et « la capture n'a rien attrapé ».
    // Cette trace les départage, et c'est le seul moyen de le faire sans écran.
    if (const char* t = std::getenv("VSM_TRACE_COUTS"); t != nullptr && *t) {
        static int tours = 0;
        if (++tours % 30 == 0) {
            std::fprintf(stderr, "[couts] lecture=%d ",
                         transport_.state() == vsm::audio::engine::TransportState::Playing ? 1 : 0);
            for (size_t i = 0; i < project_.tracks.size() && i < 8; ++i)
                std::fprintf(stderr, "%zu=%.1f ", i,
                             audioEngine_.processGraph().readTrackRenderMicros(i));
            std::fprintf(stderr, "us\n");
        }
    }
    transportBar_.setSampleRate(audioEngine_.currentSampleRate());

    // Republication coalescée des changements de mix (fader/pan/mute/solo)
    // sans interrompre la lecture (contrairement à refreshTransportSchedule).
    if (mixDirty_) {
        audioEngine_.processGraph().setProject(project_);
        mixDirty_ = false;
    }

    auto& mb = audioEngine_.processGraph().masterBus();
    // D32.3 : LE CLAVIER À L'ÉCRAN suit ce que la piste choisie joue. Lu par
    // masque atomique -- voir `soundingNotesOf` : un voyant peut être en
    // retard d'un bloc, il ne doit pas être une course.
    if (pianoRollPanel_.keyboardVisible() && !epingleClavier_) {
        uint64_t basses = 0, hautes = 0;
        audioEngine_.processGraph().soundingNotesOf(trackList_.selectedTrackIndex(), basses, hautes);
        pianoRollPanel_.setSoundingNotes(basses, hautes);
        // D32.3 : le masque au journal quand on le demande. Un voyant qui ne
        // s'allume pas peut mentir de trois façons -- rien ne sonne, le masque
        // ne suit pas, la touche ne se peint pas -- et seule celle-ci les
        // distingue.
        if (journalClavier_ && (basses != 0 || hautes != 0))
            std::fputs((juce::String::fromUTF8(u8"Clavier : notes sonnantes 0x")
                         + juce::String::toHexString(static_cast<juce::int64>(hautes))
                         + ":" + juce::String::toHexString(static_cast<juce::int64>(basses))
                         + "\n").toRawUTF8(), stderr);
    }
    mixer_.updateMeters(
        [this](size_t i) {
            vsm::audio::engine::TrackMeasurement m;
            m.peak = audioEngine_.processGraph().readMeterPeak(i);
            m.rms = audioEngine_.processGraph().readMeterRms(i);
            m.correlation = audioEngine_.processGraph().readMeterCorrelation(i);
            return m;
        },
        mb.integratedLufs(), mb.outputPeak(), mb.outputRms(), mb.outputCorrelation());

    // Le bouton MIDI Learn se désarme tout seul une fois un CC lié côté moteur.
    synthRack_.setLearnArmed(audioEngine_.isMidiLearnArmed());
}

// --- Threads de rendu (D8.1) -----------------------------------------------

int MainComponent::savedRenderThreadChoice() const {
    const int enregistre = vsm::app::ui::UiScale::properties().getIntValue(
        "renderThreads", kRenderThreadsAutomatic);
    if (enregistre < 0) return kRenderThreadsAutomatic;
    return std::min<int>(enregistre,
                          static_cast<int>(vsm::audio::engine::RenderThreadPool::kMaxWorkers));
}

size_t MainComponent::effectiveRenderThreadCount() const {
    const int choix = savedRenderThreadChoice();
    return choix == kRenderThreadsAutomatic
               ? vsm::audio::engine::ProcessGraph::recommendedRenderThreadCount()
               : static_cast<size_t>(choix);
}

void MainComponent::setRenderThreadChoice(int choice) {
    vsm::app::ui::UiScale::properties().setValue("renderThreads", choice);
    // Écrit tout de suite, comme l'échelle d'interface : une application qui se
    // termine mal ne doit pas faire perdre le réglage.
    vsm::app::ui::UiScale::properties().saveIfNeeded();
    audioEngine_.processGraph().setRenderThreadCount(effectiveRenderThreadCount());
}

// --- Menu ------------------------------------------------------------------

juce::StringArray MainComponent::getMenuBarNames() {
    return { tr("Fichier"), tr(u8"Édition"), tr("Piste"), tr("Enregistrement"), tr("Mixage"), tr("Affichage"), tr("Aide") };
}

juce::PopupMenu MainComponent::getMenuForIndex(int topLevelMenuIndex, const juce::String&) {
    juce::PopupMenu menu;
    switch (topLevelMenuIndex) {
        case 0:
            menu.addItem(kMenuFileNewProject, tr("Nouveau projet"));
            menu.addItem(kMenuFileOpen, tr("Ouvrir MIDI..."));
            menu.addItem(kMenuFileImportMidiIntoProject, tr(u8"Importer un MIDI dans le projet..."));
            menu.addItem(kMenuFileOpenBundle, tr("Ouvrir un projet VSM..."));
            {
                // D11.6 : LES PROJETS RÉCENTS, dix au plus, le dernier ouvert
                // en tête. Un dossier disparu reste listé barré de sa raison :
                // le retirer en silence ferait chercher où il est passé.
                juce::PopupMenu recents;
                const auto liste = recentProjects();
                for (int i = 0; i < liste.size(); ++i) {
                    const juce::File dossier(liste[i]);
                    const bool existe = dossier.isDirectory();
                    recents.addItem(kMenuFileRecentFirst + i,
                                    dossier.getFileName() + juce::String(tr(u8"  \u2014  ")) + dossier.getParentDirectory().getFullPathName()
                                        + (existe ? juce::String() : juce::String(tr(u8"  (introuvable)"))),
                                    existe);
                }
                if (liste.isEmpty()) recents.addItem(kMenuFileRecentFirst, tr("(aucun)"), false);
                menu.addSubMenu(tr(u8"Projets récents"), recents);
            }
            menu.addItem(kMenuFileImportDaw,
                         tr(u8"Importer un projet (Ableton, FL Studio, Cubase)..."));
            // GRISÉE tant qu'aucun import n'a eu lieu, plutôt qu'absente : une
            // entrée qui apparaît puis disparaît ne s'apprend pas. Là, on voit
            // qu'un rapport EXISTE et où le retrouver.
            menu.addItem(kMenuFileImportReport, tr(u8"Voir le dernier rapport d'import"),
                         importReport_.hasReport(), false);
            // Le rapport de RECONSTRUCTION du projet ouvert (§ 4.3 du CDC
            // multipiste) : grisé quand le projet n'en a pas — un projet
            // ouvert à la main n'en a pas, et c'est normal.
            menu.addItem(kMenuFileReconstructionReport,
                         tr(u8"Voir le rapport de reconstruction"),
                         rapportReconstruction_ != juce::File(), false);
            // LA PARITÉ, COCHÉE PAR DÉFAUT : autant de pistes que le morceau a
            // de parties. C'est un choix de travail — il vaut pour toutes les
            // reconstructions — et il se voit, coché, plutôt que de vivre
            // dans un fichier de préférences que personne n'ouvre.
            menu.addItem(kMenuFileParite,
                         tr(u8"Reconstruire en visant la parité des pistes (le défaut de la chaîne)"), true,
                         vsm::app::ui::UiScale::properties()
                             .getBoolValue(tr("reconstruireEnParite"), true));
            menu.addItem(kMenuFileSave, tr("Enregistrer") +
                          juce::String(currentProjectFolder_ == juce::File() ? "..." : "")
                          + tr(" (Ctrl+S)"));
            menu.addItem(kMenuFileSaveAs, tr("Enregistrer sous..."));
            menu.addSeparator();
            // D11.6 : LE MODÈLE. Un seul, dans le dossier des préférences : le
            // projet qu'on ouvre pour commencer (pistes, machines, routage,
            // tempo). « Nouveau depuis le modèle » rend un projet SANS chemin :
            // Ctrl+S demandera où, et le modèle ne s'écrase pas par mégarde.
            menu.addItem(kMenuFileSaveTemplate, tr(u8"Enregistrer comme modèle de projet"));
            // D32.5 : LES CHIFFRES DU PROJET. Au menu Fichier parce qu'ils
            // parlent du fichier entier, et non d'une piste.
            menu.addItem(kMenuFileStatistics,
                          tr(u8"Statistiques du projet..."));
            menu.addItem(kMenuFileNewFromTemplate, tr(u8"Nouveau depuis le modèle"),
                         templateFolder().getChildFile("project.json").existsAsFile());
            menu.addSeparator();
            // Écoute A/B : l'enregistrement d'origine en regard de la
            // reconstruction. Les trois modes sont dans le même menu, cochés,
            // pour qu'on voie d'un coup d'œil ce qu'on est en train d'écouter.
            menu.addItem(kMenuFileLoadReference, tr(u8"Charger l'original (référence A/B)..."));
            {
                const bool aUneReference = audioEngine_.processGraph().referenceTrack().hasAudio();
                const auto mode = audioEngine_.processGraph().referenceTrack().mode();
                using Mode = vsm::audio::engine::ReferenceTrack::Mode;
                if (aUneReference && referenceDescription_.isNotEmpty()) {
                    menu.addSectionHeader(referenceDescription_);
                }
                menu.addItem(kMenuFileReferenceOff, tr(u8"Écoute : reconstruction"), aUneReference,
                              mode == Mode::Off);
                menu.addItem(kMenuFileReferenceMix, tr(u8"Écoute : les deux"), aUneReference,
                              mode == Mode::Mix);
                menu.addItem(kMenuFileReferenceSolo, tr(u8"Écoute : original"), aUneReference,
                              mode == Mode::Solo);
                menu.addItem(kMenuFileReferenceCycle, tr(u8"Basculer l'écoute A/B (touche R)"), aUneReference);
            }
            menu.addItem(kMenuFileExport, tr("Exporter MIDI..."));
            // D24.5 : un fichier audio sur une piste neuve, sans passer par le
            // lâcher -- qui, lui, propose la reconstruction.
            menu.addItem(kMenuFileImportAudio, tr(u8"Importer un fichier audio sur une piste neuve..."),
                         currentProjectFolder_ != juce::File());
            // D23.3 : la piste choisie seule -- pour donner une partie, pas le morceau.
            {
                const size_t p = trackList_.selectedTrackIndex();
                const bool midi = p < project_.tracks.size() && project_.tracks[p].kind == Track::Kind::Midi;
                menu.addItem(kMenuFileExportTrackMidi,
                             midi ? juce::String(tr(u8"Exporter la piste choisie en MIDI (\u00ab "))
                                        + juce::String(project_.tracks[p].name) + juce::String(tr(u8" \u00bb)..."))
                                  : juce::String(tr(u8"Exporter la piste choisie en MIDI (choisir une piste MIDI)...")),
                             midi);
            }
            menu.addItem(kMenuFileExportWav, tr("Exporter audio (WAV)..."));
            menu.addItem(kMenuFileExportStems, tr(u8"Exporter les stems (un WAV par piste)..."));
            menu.addSeparator();
            menu.addSeparator();
            {
                // D9.1 : « FONCTION GRISÉE AVEC SA RAISON, JAMAIS UNE ERREUR ».
                // Un menu inerte sans explication et un message d'échec sont
                // deux façons de laisser l'utilisateur devant un mur. Quand la
                // chaîne manque, l'entrée reste VISIBLE, elle est grisée, et la
                // ligne juste en dessous dit pourquoi et ce qu'il faut faire.
                const bool dispo = reconstructionChain_.available
                                   && !reconstructionRunner_.isRunning();
                menu.addItem(kMenuFileReconstruct,
                              tr(u8"Reconstruire un morceau..."), dispo);
                if (!reconstructionChain_.available) {
                    // D126 : raison et remède traduits à l'affichage, comme dans la boîte (D114)
                    menu.addItem(-1, tr(u8"    ↳ ")
                                          + vsm::app::ui::trPhrase(juce::String::fromUTF8(reconstructionChain_.reason.c_str())),
                                  false, false);
                    if (!reconstructionChain_.remedy.empty())
                        menu.addItem(-1, tr(u8"    ↳ ")
                                              + vsm::app::ui::trPhrase(juce::String::fromUTF8(reconstructionChain_.remedy.c_str())),
                                      false, false);
                    menu.addItem(kMenuFileChainFolder,
                                  tr(u8"Indiquer le dossier de la chaîne..."));
                } else if (reconstructionRunner_.isRunning()) {
                    menu.addItem(-1, tr(u8"    ↳ une reconstruction est déjà en cours"),
                                  false, false);
                }
            }
            menu.addSeparator();
            menu.addItem(kMenuFileAudioSettings, tr(u8"Réglages audio..."));
            menu.addItem(kMenuFilePreferences, tr(u8"Préférences..."));
            {
                // THREADS DE RENDU (D8.1). Le multicœur ne change pas un seul
                // échantillon du résultat -- un test le vérifie -- donc ce
                // réglage ne décide de RIEN d'autre que de la marge avant le
                // décrochage. C'est pour cela qu'il vit dans un sous-menu et
                // non dans une fenêtre : on le règle une fois, et on l'oublie.
                juce::PopupMenu threads;
                const int choix = savedRenderThreadChoice();
                const size_t recommande =
                    vsm::audio::engine::ProcessGraph::recommendedRenderThreadCount();
                threads.addItem(kMenuAudioThreadsFirst,
                                 tr("Automatique (")
                                     + juce::String(static_cast<int>(recommande))
                                     + tr(" threads auxiliaires ici)"),
                                 true, choix == kRenderThreadsAutomatic);
                threads.addSeparator();
                const int maximum = std::min<int>(
                    static_cast<int>(vsm::audio::engine::RenderThreadPool::kMaxWorkers),
                    std::max(1, static_cast<int>(std::thread::hardware_concurrency())) - 1);
                for (int n = 0; n <= maximum; ++n) {
                    const juce::String pluriel = n > 1 ? juce::String(tr("s")) : juce::String();
                    const juce::String libelle =
                        n == 0 ? tr("Mono-cœur (aucun thread auxiliaire)")
                               : juce::String(n) + tr(" thread") + pluriel + tr(" auxiliaire") + pluriel;
                    threads.addItem(kMenuAudioThreadsFirst + 1 + n, libelle, true, choix == n);
                }
                menu.addSubMenu(tr("Threads de rendu"), threads);
            }
            menu.addSeparator();
            menu.addItem(kMenuFileQuit, tr("Quitter"));
            break;
        case 1:
            // Le menu Édition EST le menu contextuel du piano roll : une seule
            // définition, donc aucun risque qu'une opération existe à un
            // endroit et pas à l'autre, ou que les deux divergent.
            menu = pianoRoll_.buildContextMenu();
            // LA PLAGE ENTRE LES LOCATEURS (D13.3) : deux opérations sur TOUT
            // le morceau, qui n'ont pas leur place dans le piano roll -- elles
            // déplacent aussi les clips, les repères et le tempo.
            menu.addSeparator();
            menu.addItem(kMenuEditInsertTimeAtLocators,
                         tr(u8"Insérer du silence entre les locateurs (Ctrl+Maj+I)"),
                         project_.loopEndTick > project_.loopStartTick);
            menu.addItem(kMenuEditDeleteTimeAtLocators,
                         tr(u8"Supprimer le temps entre les locateurs (Ctrl+Maj+K)"),
                         project_.loopEndTick > project_.loopStartTick);
            menu.addItem(kMenuEditLocatorsFromSelection, tr(u8"Locateurs sur la s\u00e9lection (P)"),
                         arrangement_.hasSelection() || pianoRoll_.hasSelection());
            // D22.2 : ALLER À UNE MESURE. La position se lisait (D11.3) et ne
            // se saisissait pas : rejoindre la mesure 57 se faisait à la
            // souris, en zoomant.
            menu.addItem(kMenuEditGoToBar, tr(u8"Aller \u00e0 la mesure\u2026 (Maj+P, double-clic sur la position)"));
            // D20.1 : RÉPÉTER LA SÉLECTION de l'arrangement, jumeau du menu
            // contextuel du clip -- ici pour qu'il s'atteigne sans souris.
            menu.addSeparator();
            menu.addItem(kMenuEditSelectAllClips, tr(u8"Tout s\u00e9lectionner dans l'arrangement (Ctrl+A)"),
                         !project_.tracks.empty());
            // D34.5 : DESSINER UNE AUTOMATION PAR UNE FORME. Un balayage de
            // filtre sur seize mesures se posait point par point, et un
            // trémolo régulier ne se posait pas du tout.
            {
                juce::PopupMenu formes;
                const bool possible = !project_.tracks.empty()
                                   && (arrangement_.hasSelection() || project_.loopEnabled);
                formes.addItem(kMenuEditDrawAutomationRampUp,
                                tr(u8"Rampe montante"), possible);
                formes.addItem(kMenuEditDrawAutomationRampDown,
                                tr(u8"Rampe descendante"), possible);
                formes.addItem(kMenuEditDrawAutomationSine,
                                tr(u8"Sinus (une période par mesure)"), possible);
                formes.addItem(kMenuEditDrawAutomationTriangle,
                                tr(u8"Triangle (une période par mesure)"), possible);
                formes.addItem(kMenuEditDrawAutomationSquare,
                                tr(u8"Carré (une période par mesure)"), possible);
                menu.addSubMenu(tr(u8"Dessiner l'automation sur la sélection"),
                                 formes, possible);
            }
            {
                juce::PopupMenu repeter;
                static const int kNombres[] = {2, 3, 4, 8, 16};
                for (int i = 0; i < 5; ++i)
                    repeter.addItem(kMenuEditRepeatFirst + i, juce::String(kNombres[i]) + tr(" fois"),
                                    arrangement_.hasSelection());
                const int jusquALaBoucle = arrangement_.repeatsUntilLoopEnd();
                repeter.addSeparator();
                repeter.addItem(kMenuEditRepeatToLoopEnd,
                                jusquALaBoucle > 0
                                    ? juce::String(tr(u8"Jusqu'\u00e0 la fin de la boucle ("))
                                          + juce::String(jusquALaBoucle) + tr(" fois)")
                                    : juce::String(tr(u8"Jusqu'\u00e0 la fin de la boucle (rien n'y tient, ou pas de boucle)")),
                                jusquALaBoucle > 0);
                menu.addSubMenu(tr(u8"R\u00e9p\u00e9ter la s\u00e9lection (\u00e0 la suite)"), repeter,
                                arrangement_.hasSelection());
            }
            menu.addItem(kMenuEditSliceAtOnsets,
                         tr(u8"D\u00e9couper la s\u00e9lection aux transitoires (clips audio)"),
                         arrangement_.hasSelection());
            // D20.4 : TRANSCRIRE. Grisée AVEC sa raison quand la chaîne
            // d'analyse manque : une entrée grisée sans raison est une entrée
            // qu'on croit cassée.
            // D21.4 : LA SIGNATURE. Des valeurs fixes, à la mesure qui contient
            // la tête ; l'entrée de retrait dit s'il y a quelque chose à retirer.
            {
                static const int kSignatures[][2] = {{2, 4}, {3, 4}, {4, 4}, {5, 4}, {6, 8}, {7, 8}};
                juce::PopupMenu signature;
                const vsm::midi::Tick ici = std::max<vsm::midi::Tick>(0, transport_.currentTick());
                const int actuelNum = project_.timeSignatureMap.numeratorAt(ici);
                const int actuelDen = static_cast<int>(project_.timeSignatureMap.denominatorAt(ici));
                for (int i = 0; i < 6; ++i)
                    signature.addItem(kMenuEditSignatureFirst + i,
                                      juce::String(kSignatures[i][0]) + tr("/") + juce::String(kSignatures[i][1]),
                                      true, kSignatures[i][0] == actuelNum && kSignatures[i][1] == actuelDen);
                bool changementIci = false;
                for (const auto& c : project_.timeSignatureMap.changes())
                    if (c.tick != 0 && c.tick <= ici && ici < c.tick + project_.timeSignatureMap.ticksPerBar(c.tick, project_.ticksPerQuarterNote))
                        changementIci = true;
                signature.addSeparator();
                signature.addItem(kMenuEditSignatureRemove,
                                  changementIci ? juce::String(tr(u8"Retirer le changement de cette mesure"))
                                                : juce::String(tr(u8"Retirer le changement de cette mesure (aucun ici)")),
                                  changementIci);
                menu.addSubMenu(tr(u8"Signature \u00e0 la t\u00eate de lecture"), signature);
            }
            menu.addItem(kMenuEditTranscribeClip,
                         reconstructionChain_.available
                             ? juce::String(tr(u8"Transcrire le clip audio choisi en MIDI (Basic Pitch)"))
                             : juce::String(tr(u8"Transcrire le clip audio choisi en MIDI ("))
                                   + juce::String::fromUTF8(reconstructionChain_.reason.c_str()) + ")",
                         reconstructionChain_.available && arrangement_.hasSelection()
                             && !clipTranscriber_.isRunning());
            // D17.8 : LE GROOVE. Le nom du groove courant est DIT dans
            // l'article qui l'applique : appliquer « quelque chose » qu'on ne
            // nomme pas, c'est appliquer on ne sait quoi.
            menu.addSeparator();
            menu.addItem(kMenuEditExtractGroove,
                          tr(u8"Extraire le groove de la piste choisie"),
                          !project_.tracks.empty());
            menu.addItem(kMenuEditApplyGroove,
                          grooveCourant_.empty()
                              ? tr(u8"Appliquer le groove (aucun en mémoire)")
                              // D80 : UN MODÈLE ENTIER, et non trois morceaux : traduits
                              // un par un, ils laissaient des guillemets français autour
                              // d'un nom dans une phrase anglaise.
                              : tr(u8"Appliquer le groove \u00ab %1 \u00bb")
                                    .replace("%1", juce::String::fromUTF8(grooveCourant_.name.c_str())),
                          !grooveCourant_.empty() && pianoRoll_.hasSelection());
            menu.addItem(kMenuEditSaveGroove, tr(u8"Enregistrer le groove\u2026"), !grooveCourant_.empty());
            menu.addItem(kMenuEditLoadGroove, tr(u8"Charger un groove\u2026"));
            break;
        case 2:
            menu.addItem(kMenuTrackAdd, tr("Ajouter une piste MIDI"));
            menu.addItem(kMenuTrackAddAudio, tr("Ajouter une piste audio"));
            menu.addItem(kMenuTrackAddGroup, tr("Ajouter un groupe"));
            menu.addItem(kMenuTrackRemove, tr(u8"Supprimer la piste sélectionnée"),
                         !project_.tracks.empty());
            menu.addItem(kMenuTrackDuplicate, tr(u8"Dupliquer la piste sélectionnée"),
                         !project_.tracks.empty());
            menu.addItem(kMenuTrackCreateClip, tr(u8"Créer un clip d'une mesure à la tête de lecture"),
                         !project_.tracks.empty());
            // D35.1 : MONTER ET DESCENDRE. `moveTrack` répare les routages
            // depuis D5.3, est couverte de tests, et AUCUN geste ne l'appelait :
            // l'ordre des pistes était celui du fichier MIDI, définitivement.
            menu.addSeparator();
            menu.addItem(kMenuTrackMoveUp, tr(u8"Monter la piste"),
                         trackList_.selectedTrackIndex() > 0);
            menu.addItem(kMenuTrackMoveDown, tr(u8"Descendre la piste"),
                         !project_.tracks.empty()
                             && trackList_.selectedTrackIndex() + 1 < project_.tracks.size());
            {
                const size_t choisie = trackList_.selectedTrackIndex();
                const bool verrouillee = choisie < project_.tracks.size()
                                         && project_.tracks[choisie].locked;
                size_t masquees = 0;
                for (const auto& t : project_.tracks) if (t.hidden) ++masquees;
                // D21.2 : LE SOLO EXCLUSIF, aussi au menu -- pour le clavier, et pour que
                // VSM_MENU puisse le photographier.
                {
                    const size_t p = trackList_.selectedTrackIndex();
                    const bool seule = p < project_.tracks.size() && project_.tracks[p].solo
                                       && std::count_if(project_.tracks.begin(), project_.tracks.end(),
                                                        [](const Track& t) { return t.solo; }) == 1;
                    menu.addItem(kMenuTrackSoloExclusive,
                                 seule ? tr(u8"Plus aucun solo (Ctrl+clic sur Solo)")
                                       : tr(u8"Solo exclusif de la piste choisie (Ctrl+clic sur Solo)"),
                                 p < project_.tracks.size());
                }
                // D30.1 : LE SOLO PROTÉGÉ, au menu comme au bouton -- pour
                // le clavier, et pour que VSM_MENU puisse le photographier.
                {
                    const size_t p = trackList_.selectedTrackIndex();
                    const bool protegee = p < project_.tracks.size() && project_.tracks[p].soloSafe;
                    menu.addItem(kMenuTrackSoloSafe,
                                 protegee ? tr(u8"Ne plus protéger cette piste du solo des autres (Alt+clic sur Solo)")
                                          : tr(u8"Protéger cette piste du solo des autres (Alt+clic sur Solo)"),
                                 p < project_.tracks.size());
                }
                // D32.4 : LE RENOMMAGE EN SÉRIE. Il dit sur COMBIEN de pistes
                // il portera : « les pistes visibles » n'est pas un nombre, et
                // c'est le nombre qu'on veut connaître avant de cliquer.
                {
                    size_t visibles = 0;
                    for (const auto& t : project_.tracks) if (!t.hidden) ++visibles;
                    menu.addItem(kMenuTrackRenameSeries,
                                  tr(u8"Renommer les pistes en série (%1 visibles)...").replace("%1", juce::String(static_cast<int>(visibles))),
                                  visibles > 0);
                }
                menu.addItem(kMenuTrackHide, tr(u8"Masquer la piste (elle continue de sonner)"),
                              !project_.tracks.empty());
                menu.addItem(kMenuTrackShowAll,
                              masquees == 0
                                  ? tr(u8"Afficher toutes les pistes (aucune masquée)")
                                  : tr(u8"Afficher toutes les pistes (%1 masquées)").replace("%1", juce::String(static_cast<int>(masquees))),
                              masquees > 0);
                // D22.5 : LES PRESETS DE PISTE. Le sous-menu LISTE le dossier,
                // et dit lequel quand il est vide : un sous-menu vide sans
                // raison est un sous-menu qu'on croit cassé.
                menu.addSeparator();
                // D27.4 : LE PORT MIDI DE SORTIE. Les ports par leur nom, le
                // port virtuel en tête ; « (aucune) » remet la machine seule.
                {
                    juce::PopupMenu ports;
                    const std::string actuel = choisie < project_.tracks.size()
                                                   ? project_.tracks[choisie].midiOutputDevice : std::string();
                    ports.addItem(kMenuTrackMidiOutNone, tr(u8"(aucune : la machine interne seulement)"), true, actuel.empty());
                    const auto noms = audioEngine_.availableMidiOutputs();
                    for (size_t i = 0; i < noms.size() && i <= static_cast<size_t>(kMenuTrackMidiOutLast - kMenuTrackMidiOutFirst); ++i)
                        ports.addItem(kMenuTrackMidiOutFirst + static_cast<int>(i), juce::String(noms[i]), true, noms[i] == actuel);
                    menu.addSubMenu(actuel.empty() ? juce::String(tr(u8"Sortie MIDI mat\u00e9rielle"))
                                                   : juce::String(tr(u8"Sortie MIDI mat\u00e9rielle (\u2192 ")) + juce::String(actuel) + tr(")"),
                                    ports, choisie < project_.tracks.size() && project_.tracks[choisie].kind == Track::Kind::Midi);
                    // D28.2 : le programme, dit dans l'entrée ; D28.3 : le canal d'entrée.
                    const bool midi = choisie < project_.tracks.size() && project_.tracks[choisie].kind == Track::Kind::Midi;
                    const int prog = midi ? project_.tracks[choisie].midiProgram : -1;
                    menu.addItem(kMenuTrackMidiProgram,
                                 prog >= 0 ? juce::String(tr(u8"Programme MIDI (")) + juce::String(prog + 1)
                                                 + (project_.tracks[choisie].midiBank >= 0
                                                        ? juce::String(tr(u8", banque ")) + juce::String(project_.tracks[choisie].midiBank) : juce::String())
                                                 + tr(")...")
                                           : juce::String(tr(u8"Programme MIDI (aucun)...")),
                                 midi && !actuel.empty());
                    {
                        juce::PopupMenu canaux;
                        const int actuelCanal = midi ? project_.tracks[choisie].midiInputChannel : 0;
                        canaux.addItem(kMenuTrackInputChannelFirst, tr(u8"Tous les canaux"), true, actuelCanal == 0);
                        for (int c = 1; c <= 16; ++c)
                            canaux.addItem(kMenuTrackInputChannelFirst + c, juce::String(tr(u8"Canal ")) + juce::String(c), true, actuelCanal == c);
                        menu.addSubMenu(actuelCanal > 0 ? juce::String(tr(u8"Canal d'entr\u00e9e MIDI (")) + juce::String(actuelCanal) + tr(")")
                                                        : juce::String(tr(u8"Canal d'entr\u00e9e MIDI (tous)")),
                                        canaux, midi);
                    }
                }
                menu.addItem(kMenuTrackSavePreset,
                              tr(u8"Enregistrer la piste comme preset\u2026"),
                              choisie < project_.tracks.size()
                                  && !project_.tracks[choisie].isFolder());
                {
                    juce::PopupMenu presets;
                    const auto fichiers = trackPresetFiles();
                    for (int i = 0; i < fichiers.size() && i <= kMenuTrackPresetLast - kMenuTrackPresetFirst; ++i)
                        presets.addItem(kMenuTrackPresetFirst + i,
                                         fichiers[i].getFileName().dropLastCharacters(
                                             static_cast<int>(std::strlen(vsm::interchange::kTrackPresetExtension))),
                                         choisie < project_.tracks.size());
                    if (fichiers.isEmpty())
                        presets.addItem(999999,   // jamais choisi : grisé
                                         juce::String(tr(u8"(aucun preset dans "))
                                             + trackPresetFolder().getFullPathName() + tr(")"),
                                         false);
                    menu.addSubMenu(tr(u8"Appliquer un preset de piste (les notes et les clips restent)"),
                                     presets, choisie < project_.tracks.size());
                }
                menu.addSeparator();
                {
                    // D18.3 : LE GROUPE D'ÉDITION. Huit suffisent -- au-delà,
                    // on ne s'y retrouve plus, et le besoin réel est « les
                    // micros de la batterie » et « les doublages de la voix ».
                    const size_t choisie = trackList_.selectedTrackIndex();
                    const int actuel = choisie < project_.tracks.size()
                                           ? project_.tracks[choisie].editGroup : 0;
                    juce::PopupMenu groupes;
                    groupes.addItem(kMenuTrackEditGroupNone, tr(u8"Aucun"), true, actuel == 0);
                    for (int g = 1; g <= 8; ++g)
                        groupes.addItem(kMenuTrackEditGroupNone + g,
                                         juce::String(tr(u8"Groupe ")) + juce::String(g),
                                         true, actuel == g);
                    menu.addSubMenu(tr(u8"Groupe d'édition (couper et déplacer ensemble)"), groupes,
                                     !project_.tracks.empty());
                }
                menu.addItem(kMenuTrackLock,
                              verrouillee ? tr(u8"Déverrouiller la piste (le montage reprend)")
                                          : tr(u8"Verrouiller la piste (le montage s'arrête)"),
                              !project_.tracks.empty());
            }
            menu.addSeparator();
            {
                const size_t piste = trackList_.selectedTrackIndex();
                // D33.4 : LE GEL ACCEPTE AUSSI UNE PISTE AUDIO.
                // `renderTrackForFreeze` isole une piste QUELCONQUE ; la
                // restriction ne vivait que dans ce test. Une piste audio
                // portant une réverbération à convolution et un pitch-shift
                // coûte à chaque bloc ce qu'un fichier coûterait une fois.
                //
                // LE GROUPE RESTE REFUSÉ, et c'est écrit dans la feuille de
                // route : geler un bus voudrait dire figer la SOMME de ses
                // membres, donc décider ce qu'il advient d'eux -- les taire,
                // et les laisser muets quand on dégèle ? Cubase ne gèle pas
                // ses groupes non plus. Rien ne l'a demandé, et l'inventer
                // coûterait un état de plus dans le modèle.
                const bool gelable = piste < project_.tracks.size()
                                     && (project_.tracks[piste].kind == Track::Kind::Midi
                                         || project_.tracks[piste].kind == Track::Kind::Audio);
                const bool gelee = gelable && project_.tracks[piste].frozen;
                menu.addItem(kMenuTrackFreeze,
                              gelee ? tr(u8"Dégeler la piste (l'instrument reprend)")
                                    : tr(u8"Geler la piste (l'instrument s'arrête)"),
                              gelable);
                menu.addItem(kMenuTrackBounce, tr(u8"Reporter la piste en audio (définitif)"),
                              gelable);
                // D30.2 : DÉSACTIVER, à côté du gel parce que c'est à lui
                // qu'on la compare -- et le libellé dit la différence, sans
                // quoi on aurait deux commandes qu'on croit jumelles.
                {
                    const bool eteinte = piste < project_.tracks.size()
                                         && project_.tracks[piste].disabled;
                    menu.addItem(kMenuTrackDisable,
                                  eteinte ? tr(u8"Réactiver la piste (sa machine revient)")
                                          : tr(u8"Désactiver la piste (sa machine et ses inserts sont libérés)"),
                                  piste < project_.tracks.size());
                }
                // D30.3 : LA CHAÎNE D'INSERTS, D'UNE PISTE À L'AUTRE. Les
                // libellés DISENT COMBIEN : une commande grisée sans raison est
                // une commande qu'on croit cassée, et « coller 4 inserts » dit
                // aussi qu'on a bien copié ce qu'on croyait.
                {
                    menu.addSeparator();
                    const size_t combien = piste < project_.tracks.size()
                                               ? project_.tracks[piste].effects.size() : 0;
                    menu.addItem(kMenuTrackCopyChain,
                                  tr(u8"Copier la chaîne d'inserts (%1)").replace("%1", juce::String(static_cast<int>(combien))),
                                  combien > 0);
                    const int presse = static_cast<int>(chainClipboard_.size());
                    menu.addItem(kMenuTrackPasteChain,
                                  presse == 0
                                      ? tr(u8"Coller la chaîne (rien de copié)")
                                      : tr(u8"Coller la chaîne (%1 inserts, remplace)").replace("%1", juce::String(presse)),
                                  presse > 0 && piste < project_.tracks.size());
                    menu.addItem(kMenuTrackAppendChain,
                                  presse == 0
                                      ? tr(u8"Ajouter la chaîne (rien de copié)")
                                      : tr(u8"Ajouter la chaîne à la suite (%1 inserts)").replace("%1", juce::String(presse)),
                                  presse > 0 && piste < project_.tracks.size());
                }
                // D31.4 : LA CHAÎNE D'EFFETS MIDI. Au menu Piste comme la
                // chaîne d'inserts (D30.3), et pour la même raison : c'est le
                // seul endroit qu'on atteint sans souris, donc le seul qui se
                // vérifie à l'écran.
                {
                    menu.addSeparator();
                    juce::PopupMenu midiFx;
                    const auto types = vsm::sequencer::midiEffectTypes();
                    for (size_t i = 0; i < types.size() && i < 8; ++i)
                        // `u8"..."` est un `char8_t[]` en C++20 : le concaténer
                        // à un `std::string` ne compile pas (piège de CLAUDE.md,
                        // payé une fois de plus ici). On assemble en juce::String.
                        midiFx.addItem(kMenuTrackMidiFxFirst + static_cast<int>(i),
                                        tr(u8"Ajouter : %1").replace("%1", tr(juce::String::fromUTF8(
                                            vsm::sequencer::midiEffectDisplayName(types[i]).c_str()))),
                                        piste < project_.tracks.size());
                    const size_t combien = piste < project_.tracks.size()
                                               ? project_.tracks[piste].midiEffects.size() : 0;
                    midiFx.addSeparator();
                    midiFx.addItem(kMenuTrackMidiFxBake,
                                    tr(u8"Reporter les effets MIDI dans les notes (définitif)"),
                                    combien > 0);
                    midiFx.addItem(kMenuTrackMidiFxClear,
                                    tr(u8"Retirer tous les effets MIDI"),
                                    combien > 0);
                    // LE NOMBRE DANS LE TITRE : un sous-menu qui ne dit pas ce
                    // qu'il contient déjà se visite pour rien.
                    menu.addSubMenu(tr(u8"Effets MIDI de la piste (%1)").replace("%1", juce::String(static_cast<int>(combien))),
                                     midiFx, !project_.tracks.empty());
                }
                // D30.5 : RÉDUIRE LES POINTS. Le libellé dit COMBIEN il y en
                // a : c'est ce nombre qui fait comprendre pourquoi la commande
                // existe, et c'est lui qu'on compare à celui d'après.
                {
                    size_t points = 0;
                    if (piste < project_.tracks.size())
                        for (const auto& c : project_.tracks[piste].automation)
                            points += c.points.size();
                    menu.addItem(kMenuTrackThinAutomation,
                                  tr(u8"Réduire les points d'automation (%1 points)").replace("%1", juce::String(static_cast<int>(points))),
                                  points > 2);
                }
                menu.addItem(kMenuTrackBounceSelection,
                              tr(u8"Reporter la sélection en audio (sur une piste neuve)"),
                              arrangement_.hasSelection());
                // D18.7b : PUBLIER LES SORTIES. L'entrée dit COMBIEN, parce
                // qu'une commande grisée sans raison est une commande qu'on
                // croit cassée -- et le nombre vient de la machine elle-même.
                {
                    int sorties = 1;
                    if (piste < project_.tracks.size())
                        if (auto* machine = audioEngine_.processGraph().trackInstrument(piste))
                            sorties = machine->outputCount();
                    // D19.3 : ÉCLATER PAR HAUTEUR. L'entrée dit combien de
                    // hauteurs elle trouverait : une commande grisée sans
                    // raison est une commande qu'on croit cassée.
                    size_t hauteurs = 0;
                    if (piste < project_.tracks.size()) {
                        std::set<uint8_t> vues;
                        for (const auto& n : project_.tracks[piste].notes) vues.insert(n.number);
                        hauteurs = vues.size();
                    }
                    // D19.4 : LES DOSSIERS. Trois gestes qui se composent —
                    // créer un tiroir, y entrer, en sortir — plutôt qu'une
                    // grande commande qui devinerait ce qu'on veut ranger.
                    menu.addItem(kMenuTrackNewFolder,
                                  tr(u8"Ranger cette piste dans un dossier neuf"),
                                  piste < project_.tracks.size());
                    {
                        const bool peutEntrer =
                            piste > 0 && piste < project_.tracks.size()
                            && project_.tracks[piste - 1].isFolder()
                            && project_.tracks[piste].folderDepth
                                   <= project_.tracks[piste - 1].folderDepth;
                        menu.addItem(kMenuTrackFolderIn, tr(u8"Entrer dans le dossier du dessus"),
                                      peutEntrer);
                        menu.addItem(kMenuTrackFolderOut, tr(u8"Sortir du dossier"),
                                      piste < project_.tracks.size()
                                          && project_.tracks[piste].folderDepth > 0);
                    }
                    menu.addSeparator();
                    menu.addItem(kMenuTrackExplodeByPitch,
                                  hauteurs >= 2
                                      ? juce::String(tr(u8"Éclater par hauteur ("))
                                            + juce::String(static_cast<int>(hauteurs))
                                            + juce::String(tr(u8" pistes)"))
                                      : juce::String(tr(u8"Éclater par hauteur (une seule hauteur ici)")),
                                  hauteurs >= 2);
                    menu.addItem(kMenuTrackPublishOutputs,
                                  sorties > 1
                                      ? juce::String(tr(u8"Publier les ")) + juce::String(sorties - 1)
                                            + juce::String(tr(u8" autres sorties sur des pistes"))
                                      : juce::String(tr(u8"Publier les sorties de l'instrument (cette machine n'en a qu'une)")),
                                  sorties > 1);
                }
#if VSM_WITH_CLAP || VSM_WITH_VST3
                menu.addSeparator();
#endif
#if VSM_WITH_CLAP
                menu.addItem(kMenuTrackClapPlugin, tr(u8"Charger un plugin CLAP sur la piste..."),
                              piste < project_.tracks.size()
                                  && project_.tracks[piste].kind == Track::Kind::Midi);
#endif
#if VSM_WITH_CLAP || VSM_WITH_VST3
                menu.addItem(kMenuTrackScanPlugins,
                              pluginScanner_ != nullptr
                                  ? juce::String(tr(u8"Balayage des plugins en cours..."))
                                  : juce::String(tr(u8"Rechercher les plugins installés...")),
                              pluginScanner_ == nullptr);
                menu.addItem(kMenuTrackPluginFromCatalogue,
                              tr(u8"Instrument parmi les plugins trouvés..."),
                              !pluginCatalogue_.instruments().empty()
                                  && piste < project_.tracks.size()
                                  && project_.tracks[piste].kind == Track::Kind::Midi);
#endif
#if VSM_WITH_VST3
                menu.addItem(kMenuTrackVst3Plugin, tr(u8"Charger un instrument VST3 sur la piste..."),
                              piste < project_.tracks.size()
                                  && project_.tracks[piste].kind == Track::Kind::Midi);
#endif
#if VSM_WITH_CLAP || VSM_WITH_VST3
                // D7.4 : GRISÉ QUAND LA MACHINE N'A PAS DE FAÇADE NATIVE. Les
                // machines du parc ont la leur, montrée par le Synth Rack ;
                // proposer « ouvrir l'interface » pour elles ferait deux
                // chemins vers la même chose, dont l'un ne mènerait nulle part.
                //
                // ON DEMANDE AUX DEUX FORMATS, et pas seulement à VST3 : la
                // façade CLAP existe depuis que son report a été levé, et
                // n'interroger qu'un des deux hôtes grisait l'entrée pour un
                // plugin qui a bel et bien une interface -- une commande morte
                // dans l'autre sens, ce que l'invariant n° 5 du § 6 interdit
                // tout autant.
                {
                    bool aFacade = false;
                    if (piste < project_.tracks.size())
                        if (auto* machine = audioEngine_.processGraph().trackInstrument(piste)) {
#if VSM_WITH_VST3
                            aFacade = vsm::vst3::hasNativeEditor(*machine);
#endif
#if VSM_WITH_CLAP
                            if (!aFacade) aFacade = vsm::clap::hasNativeEditor(*machine);
#endif
                        }
                    menu.addItem(kMenuTrackPluginEditor,
                                  tr(u8"Ouvrir l'interface du plugin de la piste"), aFacade);
                }
#endif
            }
            break;
        case 3:
            // ENREGISTREMENT. Les deux réglages qui changent ce qu'une prise
            // fait -- combien de temps on compte avant, et ce qu'elle fait de ce
            // qui était déjà là -- plus la quantification de la dernière prise.
            {
                const int mesures = countInBars_;
                menu.addSectionHeader(tr(u8"Décompte"));
                menu.addItem(kMenuRecordCountInNone, tr("Aucun"), true, mesures == 0);
                menu.addItem(kMenuRecordCountInOne, tr("1 mesure"), true, mesures == 1);
                menu.addItem(kMenuRecordCountInTwo, tr("2 mesures"), true, mesures == 2);
                menu.addSectionHeader(tr(u8"Ce que fait la prise MIDI"));
                menu.addItem(kMenuRecordOverdub, tr("Superposer"),
                              true, recordMode_ == vsm::sequencer::RecordMode::Overdub);
                menu.addItem(kMenuRecordReplace, tr("Remplacer"),
                              true, recordMode_ == vsm::sequencer::RecordMode::Replace);
                menu.addItem(kMenuRecordStack, tr(u8"Empiler les prises"),
                              true, recordMode_ == vsm::sequencer::RecordMode::Stack);
                // HORS DU MODE EMPILÉ, une prise audio remplace toujours le
                // matériau de sa piste -- une piste audio porte un seul fichier.
                // Le menu le dit plutôt que de laisser croire que
                // « superposer » la concerne.
                menu.addItem(-1, tr(u8"(en boucle et en mode empilé, chaque passage devient une prise)"), false, false);
                menu.addSeparator();

                // LA RÉGION DE PUNCH : entre ces deux points, et seulement là,
                // l'enregistrement capte. Elle se dessine à la souris sur la
                // règle du piano roll avec Alt -- comme la boucle avec Maj --
                // et le menu offre les deux gestes qu'on fait le plus souvent.
                const bool punchPose = project_.punchEndTick > project_.punchStartTick;
                menu.addSectionHeader(tr(u8"Région de punch (Alt sur la règle)"));
                menu.addItem(kMenuRecordPunchToggle, tr(u8"Active"), punchPose, project_.punchEnabled);
                menu.addItem(kMenuRecordPunchFromLoop, tr(u8"La prendre sur la boucle"),
                              project_.loopEndTick > project_.loopStartTick);
                menu.addItem(kMenuRecordPunchClear, tr(u8"L'effacer"), punchPose);
                menu.addSeparator();

                // LA LATENCE, PUBLIÉE. Le critère de D3.6 dit « le chiffre est
                // publié » : il ne suffit pas de corriger, il faut pouvoir lire
                // de combien -- sans quoi on ne saurait pas si la correction a
                // seulement eu lieu.
                {
                    const double r = audioEngine_.measuredRoundTripSeconds();
                    const double sr = audioEngine_.currentSampleRate();
                    menu.addSectionHeader(tr(u8"Latence d'entrée"));
                    menu.addItem(-2,
                                  r > 0.0
                                      ? juce::String(tr(u8"Mesurée : ")) + juce::String(r * 1000.0, 2)
                                            + tr(" ms (") + juce::String(juce::roundToInt(r * sr))
                                            + juce::String(tr(u8" échantillons)"))
                                      : juce::String(tr(u8"Jamais mesurée — les prises audio ne sont pas compensées")),
                                  false, false);
                    // D11.7 : S'ENTENDRE. L'entrée recopiée vers la sortie, en
                    // direct — à la latence du périphérique, que la commande
                    // suivante mesure. Coché quand c'est actif ; jamais par défaut.
                    menu.addItem(kMenuRecordMonitorInput,
                                 tr(u8"\u00c9couter l'entr\u00e9e en direct (latence du p\u00e9riph\u00e9rique)"),
                                 audioEngine_.isDeviceOpen() && monitoringMode_ == 0, audioEngine_.inputMonitoring());
                    // D23.2 : LE MODE. Chaque entrée dit QUAND elle écoute :
                    // « automatique » sans sa règle serait une magie qu'on ne
                    // peut ni prévoir ni vérifier.
                    {
                        juce::PopupMenu modes;
                        modes.addItem(kMenuRecordMonitorManual,
                                      tr(u8"\u00c9coute manuelle (l'interrupteur ci-dessus)"), true, monitoringMode_ == 0);
                        modes.addItem(kMenuRecordMonitorAuto,
                                      tr(u8"\u00c9coute automatique (une piste audio arm\u00e9e, transport arr\u00eat\u00e9 ou en enregistrement)"),
                                      true, monitoringMode_ == 1);
                        modes.addItem(kMenuRecordMonitorArmed,
                                      tr(u8"\u00c9coute quand une piste est arm\u00e9e (une piste audio arm\u00e9e, toujours)"), true, monitoringMode_ == 2);
                        menu.addSubMenu(tr(u8"\u00c9coute de l'entr\u00e9e"), modes);
                    }
                    // D24.4 : LE PANIC. Toujours actif : une note bloquée ne
                    // prévient pas.
                    menu.addItem(kMenuRecordPanic, tr(u8"Couper toutes les notes (panic)"));
                    menu.addItem(kMenuRecordMeasureLatency,
                                  tr(u8"Mesurer (brancher la sortie sur l'entrée)..."));
                    menu.addItem(kMenuRecordClearLatency, tr(u8"Oublier la mesure"), r > 0.0);
                }
                menu.addSeparator();

                // LES PRISES DE LA PISTE SÉLECTIONNÉE. C'est le « se
                // choisissent » du critère de D3.5 : sans ce menu, les prises
                // seraient conservées et inatteignables.
                const size_t pisteChoisie = trackList_.selectedTrackIndex();
                if (pisteChoisie < project_.tracks.size()
                    && !project_.tracks[pisteChoisie].takes.empty()) {
                    const auto& prises = project_.tracks[pisteChoisie].takes;
                    menu.addSectionHeader(juce::String(tr(u8"Prises de « "))
                                           + juce::String(project_.tracks[pisteChoisie].name)
                                           + juce::String(tr(u8" »")));
                    for (size_t i = 0; i < prises.size() && i <= 63; ++i)
                        menu.addItem(kMenuRecordTakeFirst + static_cast<int>(i),
                                      juce::String(prises[i].name.empty()
                                                       ? (tr("Prise ") + std::to_string(i + 1))
                                                       : prises[i].name),
                                      true,
                                      static_cast<int>(i) == project_.tracks[pisteChoisie].activeTake);
                    // D57 : LES RETIRER. `pushTake` empilait depuis D3.5 et
                    // rien ne dépilait : les passes ratées restaient pour
                    // toujours. Dans un sous-menu, parce que supprimer et
                    // choisir ne se confondent pas d'un clic distrait.
                    juce::PopupMenu retirer;
                    for (size_t i = 0; i < prises.size() && i <= 63; ++i)
                        retirer.addItem(kMenuRecordDeleteTakeFirst + static_cast<int>(i),
                                         juce::String(prises[i].name.empty()
                                                          ? (tr("Prise ") + std::to_string(i + 1))
                                                          : prises[i].name)
                                         + (static_cast<int>(i) == project_.tracks[pisteChoisie].activeTake
                                                ? tr(u8"  (celle qu'on entend)")
                                                : juce::String()));
                    menu.addSubMenu(tr(u8"Retirer une prise du tiroir"),
                                     std::move(retirer));
                    menu.addSeparator();
                }
                menu.addItem(kMenuRecordQuantizeTake,
                              tr(u8"Quantifier la dernière prise (grille du piano roll)"),
                              !lastTake_.empty());
                // D17.3 : ce qu'on vient de jouer sans avoir armé. Le nombre
                // d'événements gardés est DIT : « récupérer » sur un tampon
                // vide ne doit pas se découvrir en cliquant.
                // D18.2 : le nombre de prises est DIT. Sans prises il n'y a rien
                // à assembler, et l'apprendre en ouvrant la fenêtre serait un
                // aller-retour pour rien.
                {
                    const size_t p = trackList_.selectedTrackIndex();
                    const size_t prises = p < project_.tracks.size()
                                              ? project_.tracks[p].takes.size() : 0;
                    menu.addItem(kMenuRecordCompTakes,
                                  prises == 0
                                      ? tr(u8"Assembler les prises... (aucune)")
                                      : tr(u8"Assembler les prises... (%1 prises)").replace("%1", juce::String(static_cast<int>(prises))),
                                  prises > 1);
                }
                menu.addItem(kMenuRecordRetrospective,
                              retrospectif_.empty()
                                  ? tr(u8"Récupérer ce qui vient d'être joué (rien en mémoire)")
                                  : tr(u8"Récupérer ce qui vient d'être joué (%1 événements)").replace("%1", juce::String(static_cast<int>(retrospectif_.size()))),
                              !retrospectif_.empty() && !project_.tracks.empty());
            }
            break;
        case 4:
            // LE MIXAGE. Les bus de départ y sont NOMMÉS et leur effet s'y
            // choisit : ils étaient deux, figés dans le code sur une
            // réverbération et un delay, et rien -- ni le projet, ni
            // l'interface -- ne disait ce que les boutons alimentaient.
            {
                menu.addSectionHeader(tr(u8"Bus de départ"));
                const auto& effets = vsm::audio::effect::EffectFactory::available();
                for (size_t bus = 0; bus < project_.sends.size() && bus < 8; ++bus) {
                    const auto& decrit = project_.sends[bus];
                    juce::PopupMenu sousMenu;
                    for (size_t e = 0; e < effets.size() && e < 20; ++e)
                        sousMenu.addItem(kMenuMixSendEffectFirst + static_cast<int>(bus * 20 + e),
                                          effets[e].displayName, true,
                                          effets[e].id == decrit.effectType);
                    sousMenu.addSeparator();
                    // PRÉ / POST-FADER (D4.3). Post-fader était codé en dur ;
                    // l'infobulle du menu dit ce que chacun change, parce que
                    // « pré-fader » n'apprend rien à qui ne le sait pas déjà.
                    // LE RETOUR S'ÉTEINT, et ce n'est pas un raffinement : un bus
                    // qui ne sert qu'à faire ÉCOUTER une piste à un compresseur
                    // (chaîne latérale, D4.4) ne doit pas s'entendre. Sans ce
                    // commutateur, il faudrait choisir entre une réverbération
                    // parasite et pas de chaîne latérale du tout.
                    sousMenu.addItem(kMenuMixSendReturnFirst + static_cast<int>(bus),
                                      tr(u8"Retour audible"), true, decrit.returnGain > 0.0f);
                    sousMenu.addItem(kMenuMixSendPreFaderFirst + static_cast<int>(bus),
                                      decrit.preFader
                                          ? tr(u8"Pré-fader (le fader ne l'affecte pas)")
                                          : tr(u8"Post-fader (le fader l'emporte avec lui)"),
                                      true, decrit.preFader);
                    sousMenu.addSeparator();
                    sousMenu.addItem(kMenuMixRemoveSendFirst + static_cast<int>(bus),
                                      tr(u8"Retirer ce bus"));
                    menu.addSubMenu(juce::String(decrit.name.empty() ? tr("Bus") : decrit.name)
                                         + tr("  (") + juce::String(decrit.effectType) + tr(")"),
                                     sousMenu);
                }
                if (project_.sends.empty())
                    menu.addItem(-1, tr(u8"(aucun — les tranches n'ont pas de bouton de départ)"),
                                  false, false);
                menu.addSeparator();
                menu.addItem(kMenuMixAddSend, tr(u8"Ajouter un bus de départ"),
                              project_.sends.size() < vsm::audio::engine::ProcessGraph::kMaxSends);
            }
            // D23.5 : L'ÉCOUTE EN MONO, aussi au menu -- pour le clavier, et pour
            // que VSM_MENU puisse la photographier.
            menu.addSeparator();
            menu.addItem(kMenuMixMonoListen, tr(u8"\u00c9coute en mono (jamais dans un export)"), true,
                         audioEngine_.processGraph().masterBus().monoListen());
            // D34.1 : LA FORME DES FONDUS CROISÉS. Une donnée du MORCEAU, et
            // non une préférence : elle change ce que l'export contient.
            {
                juce::PopupMenu formes;
                const auto coche = [&](vsm::sequencer::FadeShape f) {
                    return project_.crossfadeShape == f;
                };
                formes.addItem(kMenuMixCrossfadeEqualPower,
                                tr(u8"Puissance constante (deux prises différentes)"),
                                true, coche(vsm::sequencer::FadeShape::EqualPower));
                formes.addItem(kMenuMixCrossfadeLinear,
                                tr(u8"Linéaire (deux copies du même son)"),
                                true, coche(vsm::sequencer::FadeShape::Linear));
                formes.addItem(kMenuMixCrossfadeSlow, tr(u8"Lente"),
                                true, coche(vsm::sequencer::FadeShape::Slow));
                formes.addItem(kMenuMixCrossfadeFast, tr(u8"Rapide"),
                                true, coche(vsm::sequencer::FadeShape::Fast));
                menu.addSubMenu(tr(u8"Forme des fondus croisés"), formes);
            }
            break;
        case 5:
            menu.addItem(kMenuViewSingleWindow, tr(u8"Fenêtre unique"),
                          true, singleWindow_);
            menu.addItem(kMenuViewComputerKeyboard,
                         tr(u8"Clavier d'ordinateur (A S D F… jouent la piste choisie, Z/X : octave)"),
                         true, computerKeyboard_);
            {
                auto* fenetre = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent());
                menu.addItem(kMenuViewFullScreen, tr(u8"Plein \u00e9cran (F11)"), fenetre != nullptr,
                              fenetre != nullptr && fenetre->isFullScreen());
            }
            menu.addSeparator();
            menu.addItem(kMenuViewTracks, tr("Pistes"), true,
                          singleWindow_ ? trackList_.isVisible() : trackListWindow_.isVisible());
            menu.addItem(kMenuViewPianoRoll, tr("Piano Roll"), true,
                          singleWindow_ ? pianoRollPanel_.isVisible() : pianoRollWindow_.isVisible());
            menu.addItem(kMenuViewSynthRack, tr("Synth Rack"), true,
                          singleWindow_ ? synthRack_.isVisible() : synthRackWindow_.isVisible());
            menu.addItem(kMenuViewMixer, tr("Mixer"), true,
                          singleWindow_ ? bottomTabs_.isVisible() : mixerWindow_.isVisible());
            menu.addItem(kMenuViewArrangement, tr("Arrangement"), true,
                          singleWindow_ ? arrangement_.isVisible() : arrangementWindow_.isVisible());
            menu.addItem(kMenuViewBrowser, tr(u8"Navigateur"),
                          true, browserWindow_ && browserWindow_->isVisible());
            menu.addItem(kMenuViewShortcuts,
                          tr(u8"Raccourcis clavier..."),
                          true, shortcutsWindow_ && shortcutsWindow_->isVisible());
            menu.addItem(kMenuViewHistory,
                          tr(u8"Historique des modifications..."),
                          true, historyWindow_ && historyWindow_->isVisible());
            menu.addItem(kMenuViewSpectrum,
                          tr(u8"Analyseur de spectre..."),
                          true, spectrumWindow_ && spectrumWindow_->isVisible());
            // D18.6 : LE NOMBRE DE CARACTÈRES EST DIT. Un bloc-notes vide et un
            // bloc-notes plein s'ouvrent pareil ; savoir qu'il y a quelque
            // chose dedans est la moitié de son intérêt.
            // D18.4 : le nombre de sections est DIT. Sans repère il n'y a rien
            // à ordonner, et l'apprendre en ouvrant la fenêtre serait un
            // aller-retour pour rien.
            {
                const auto sections = vsm::sequencer::sectionsFromMarkers(project_);
                menu.addItem(kMenuViewPlayOrder,
                              sections.empty()
                                  ? tr(u8"Ordre de jeu... (aucune section)")
                                  : tr(u8"Ordre de jeu... (%1 sections)").replace("%1", juce::String(static_cast<int>(sections.size()))),
                              !sections.empty(),
                              playOrderWindow_ && playOrderWindow_->isVisible());
            }
            menu.addItem(kMenuViewProjectNotes,
                          project_.notes.empty()
                              ? tr(u8"Notes du projet... (vides)")
                              : tr(u8"Notes du projet... (%1 caractères)").replace("%1", juce::String(static_cast<int>(project_.notes.size()))),
                          true, projectNotesWindow_ && projectNotesWindow_->isVisible());
            menu.addItem(kMenuViewMidiLearn,
                          tr(u8"Associations MIDI (%1)").replace("%1", juce::String(static_cast<int>(audioEngine_.midiLearnMappingCount()))),
                          true, midiLearnWindow_ && midiLearnWindow_->isVisible());
            // D23.4 : TOUTES LES PISTES À LA FENÊTRE, et trois hauteurs fixes.
            menu.addSeparator();
            menu.addItem(kMenuViewFitTracks, tr(u8"Toutes les pistes \u00e0 la fen\u00eatre (arrangement)"),
                         !project_.tracks.empty());
            {
                juce::PopupMenu hauteurs;
                hauteurs.addItem(kMenuViewTrackHeightSmall, tr(u8"Petite (24 px)"), !project_.tracks.empty());
                hauteurs.addItem(kMenuViewTrackHeightNormal, tr(u8"Normale (56 px)"), !project_.tracks.empty());
                hauteurs.addItem(kMenuViewTrackHeightLarge, tr(u8"Grande (112 px)"), !project_.tracks.empty());
                menu.addSubMenu(tr(u8"Hauteur des pistes"), hauteurs);
            }
            // D34.4 : LA RÈGLE, EN MESURES OU EN TEMPS. La barre de transport
            // affiche les deux positions depuis D11.3 ; la règle n'en montrait
            // qu'une, alors que ce logiciel compare une reconstruction à un
            // enregistrement, qui se mesure en secondes.
            {
                juce::PopupMenu regle;
                regle.addItem(kMenuViewRulerBars, tr(u8"Mesures"),
                               true, !arrangement_.rulerInTime());
                regle.addItem(kMenuViewRulerTime, tr(u8"Minutes:secondes"),
                               true, arrangement_.rulerInTime());
                menu.addSubMenu(tr(u8"Règle"), regle);
            }
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
                menu.addSubMenu(tr("Taille de l'interface"), tailles);

                // D73 : LA LANGUE, à côté de la taille -- ce sont les deux
                // réglages qui décident de ce qu'on LIT, et ils se cherchent au
                // même endroit. Chaque langue est écrite DANS SA PROPRE LANGUE
                // (« Français », « English ») : c'est la convention de tous les
                // sélecteurs de langue, et la seule qui permette de retrouver
                // la sienne quand l'interface est posée dans une autre.
                //
                // « Langue », « Français » et « English » sont UNIQUES dans
                // toute la barre, et ce n'est pas un hasard : `VSM_MENU` prend
                // le PREMIER libellé exact tous menus confondus, et un doublon
                // ferait piloter autre chose que ce qu'on croit (piège payé le
                // 06/09 avec « Automatique »).
                juce::PopupMenu langues;
                const vsm::app::ui::Langue::Choix choix[] = {
                    vsm::app::ui::Langue::Choix::Francais,
                    vsm::app::ui::Langue::Choix::Anglais,
                };
                for (int i = 0; i < static_cast<int>(std::size(choix)); ++i)
                    langues.addItem(kMenuViewLangueFirst + i,
                                    vsm::app::ui::Langue::libelle(choix[i]), true,
                                    vsm::app::ui::Langue::courante() == choix[i]);
                menu.addSubMenu(tr("Langue"), langues);
            }
            break;
        case 6:
            menu.addItem(kMenuHelpAbout, tr(u8"À propos de Vintage Synth MIDI Studio"));
            break;
        default:
            break;
    }
    return menu;
}

void MainComponent::menuItemSelected(int menuItemID, int /*topLevelMenuIndex*/) {
    if (menuItemID == kMenuEditInsertTimeAtLocators) { editTimeAtLocators(true); return; }
    if (menuItemID == kMenuEditDeleteTimeAtLocators) { editTimeAtLocators(false); return; }
    if (menuItemID == kMenuEditExtractGroove) { extractGrooveFromSelectedTrack(); return; }
    if (menuItemID == kMenuEditApplyGroove)   { applyGrooveToSelection(); return; }
    if (menuItemID == kMenuEditSaveGroove)    { saveCurrentGroove(); return; }
    if (menuItemID == kMenuEditLoadGroove)    { loadGrooveFromLibrary(); return; }
    if (menuItemID == kMenuEditLocatorsFromSelection) { locatorsFromSelection(); return; }
    if (menuItemID == kMenuEditGoToBar) { promptGoToBar(); return; }
    // D23 : l'écoute de l'entrée, la piste en MIDI, le mono, les hauteurs.
    if (menuItemID >= kMenuRecordMonitorManual && menuItemID <= kMenuRecordMonitorArmed) {
        monitoringMode_ = menuItemID - kMenuRecordMonitorManual;
        vsm::app::ui::UiScale::properties().setValue("monitoringMode", monitoringMode_);
        applyMonitoringMode();
        return;
    }
    if (menuItemID == kMenuFileExportTrackMidi) { exportSelectedTrackMidi(); return; }
    if (menuItemID == kMenuFileImportAudio) { importAudioFilePrompt(); return; }
    if (menuItemID == kMenuRecordPanic) {
        // Les touches d'ordinateur enfoncées sont relâchées aussi : sinon leur
        // relâchement enverrait un NoteOff à une note déjà coupée, sans mal,
        // mais leur maintien la relancerait à la répétition de la touche.
        for (const auto& [code, note] : computerKeysDown_) audioEngine_.playComputerKey(note, 0, false);
        computerKeysDown_.clear();
        audioEngine_.processGraph().requestPanic();
        return;
    }
    if (menuItemID >= kMenuMixCrossfadeLinear && menuItemID <= kMenuMixCrossfadeFast) {
        // LES QUATRE ENTRÉES SONT CONTIGUES ET DANS L'ORDRE DE `FadeShape` :
        // la soustraction suffit, et une cinquième forme n'exigerait rien
        // d'autre qu'une ligne de menu.
        setCrossfadeShape(static_cast<vsm::sequencer::FadeShape>(
            static_cast<uint8_t>(menuItemID - kMenuMixCrossfadeLinear)));
        return;
    }
    if (menuItemID == kMenuMixMonoListen) {
        const bool on = !audioEngine_.processGraph().masterBus().monoListen();
        audioEngine_.processGraph().masterBus().setMonoListen(on);
        mixer_.setMonoListen(on);
        return;
    }
    if (menuItemID == kMenuViewFitTracks) { arrangement_.fitTracksToWindow(); return; }
    if (menuItemID == kMenuViewTrackHeightSmall)  { arrangement_.setAllTrackHeights(24); return; }
    if (menuItemID == kMenuViewTrackHeightNormal) { arrangement_.setAllTrackHeights(56); return; }
    if (menuItemID == kMenuViewTrackHeightLarge)  { arrangement_.setAllTrackHeights(112); return; }
    if (menuItemID == kMenuTrackMoveUp)   { moveSelectedTrack(-1); return; }
    if (menuItemID == kMenuTrackMoveDown) { moveSelectedTrack(+1); return; }
    if (menuItemID >= kMenuEditDrawAutomationRampUp
        && menuItemID <= kMenuEditDrawAutomationSquare) {
        using vsm::sequencer::AutomationShape;
        static const struct { AutomationShape forme; bool descendante; } kFormes[] = {
            {AutomationShape::Line, false}, {AutomationShape::Line, true},
            {AutomationShape::Sine, false}, {AutomationShape::Triangle, false},
            {AutomationShape::Square, false},
        };
        const auto& choix = kFormes[menuItemID - kMenuEditDrawAutomationRampUp];
        arrangement_.drawAutomationShapeOnSelection(trackList_.selectedTrackIndex(),
                                                     choix.forme, choix.descendante);
        return;
    }
    if (menuItemID == kMenuViewRulerBars || menuItemID == kMenuViewRulerTime) {
        setRulerInTime(menuItemID == kMenuViewRulerTime);
        return;
    }
    if (menuItemID == kMenuEditSelectAllClips) { arrangement_.selectAll(); return; }
    if (menuItemID == kMenuEditRepeatToLoopEnd) { arrangement_.repeatSelectionUntilLoopEnd(); return; }
    if (menuItemID == kMenuEditSliceAtOnsets) { sliceSelectedClipsAtOnsets(); return; }
    if (menuItemID == kMenuEditTranscribeClip) { transcribeSelectedClip(); return; }
    if (menuItemID == kMenuEditSignatureRemove) { setTimeSignatureAtPlayhead(0, 0); return; }
    if (menuItemID >= kMenuEditSignatureFirst && menuItemID <= kMenuEditSignatureLast) {
        static const int kSignatures[][2] = {{2, 4}, {3, 4}, {4, 4}, {5, 4}, {6, 8}, {7, 8}};
        const int i = menuItemID - kMenuEditSignatureFirst;
        setTimeSignatureAtPlayhead(kSignatures[i][0], kSignatures[i][1]);
        return;
    }
    if (menuItemID >= kMenuEditRepeatFirst && menuItemID <= kMenuEditRepeatLast) {
        static const int kNombres[] = {2, 3, 4, 8, 16};
        arrangement_.repeatSelection(kNombres[menuItemID - kMenuEditRepeatFirst]);
        return;
    }
    if (menuItemID == kMenuFileImportMidiIntoProject) { chooseMidiToImport(); return; }
    if (menuItemID >= kMenuFileRecentFirst && menuItemID <= kMenuFileRecentLast) {
        const auto liste = recentProjects();
        const int i = menuItemID - kMenuFileRecentFirst;
        if (i < liste.size()) loadProjectBundleFromFolder(juce::File(liste[i]));
        return;
    }
    // Les entrées du menu Édition proviennent du piano roll et utilisent sa
    // propre numérotation (>= 100 000, voir PianoRollComponent.cpp) : elles
    // lui sont renvoyées telles quelles. LA BASE VALAIT 100, et l'énumération
    // ci-dessous l'a dépassée en grandissant : tout le menu Affichage partait
    // au piano roll et mourait en silence -- « Arrangement ne s'affiche pas »,
    // dit par l'utilisateur, vérifié par l'autoportrait, corrigé en montant
    // la base hors d'atteinte.
    if (menuItemID >= 100000) {
        pianoRoll_.performContextMenuAction(menuItemID);
        pianoRollPanel_.refresh();
        return;
    }

    switch (menuItemID) {
        case kMenuFileNewProject: newProject(); break;
        case kMenuFileOpen:      openMidiFile(); break;
        case kMenuFileOpenBundle: openProjectBundle(); break;
        case kMenuFileImportDaw: importDawProject(); break;
        case kMenuFileImportReport: showLastImportReport(); break;
        case kMenuFileReconstructionReport: showReconstructionReport(); break;
        case kMenuFileParite: {
            auto& reglages = vsm::app::ui::UiScale::properties();
            const bool actif = !reglages.getBoolValue("reconstruireEnParite", true);
            reglages.setValue("reconstruireEnParite", actif);
            reglages.saveIfNeeded();
            menuItemsChanged();
            break;
        }
        case kMenuFileStatistics: showProjectStatistics(); break;                // D32.5
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
        case kMenuFileExportStems: exportStems(); break;
        case kMenuFileAudioSettings: showAudioSettings(); break;
        case kMenuFileReconstruct: {
            auto chooser = std::make_shared<juce::FileChooser>(
                tr(u8"Reconstruire un morceau (wav, mp3, flac...)"),
                juce::File(), "*.wav;*.mp3;*.flac;*.ogg;*.m4a;*.aiff;*.aif");
            chooser->launchAsync(juce::FileBrowserComponent::openMode
                                      | juce::FileBrowserComponent::canSelectFiles,
                                  [this, chooser](const juce::FileChooser& fc) {
                                      const juce::File f = fc.getResult();
                                      if (f != juce::File()) startReconstruction(f);
                                  });
            break;
        }
        case kMenuFileChainFolder: chooseChainFolder(); break;
        case kMenuFilePreferences: showPreferences(); break;
        case kMenuViewBrowser: {
            if (!browserWindow_) {
                browserWindow_ = std::make_unique<PanelWindow>("Navigateur", browserPanel_);
                browserWindow_->setDefaultSize(620, 560);
            }
            const bool visible = browserWindow_->isVisible();
            // L'INVENTAIRE EST REFAIT À L'OUVERTURE, jamais en continu : un
            // dossier d'échantillons se parcourt en quelques dizaines de
            // millisecondes, et le refaire à chaque tour de minuterie ferait
            // travailler le disque pour rien pendant qu'on compose.
            if (!visible) refreshBrowser();
            browserWindow_->setVisible(!visible);
            if (!visible) browserWindow_->toFront(true);
            break;
        }
        case kMenuViewHistory: {
            if (!historyWindow_) {
                historyWindow_ = std::make_unique<PanelWindow>(
                    juce::String::fromUTF8(u8"Historique des modifications"), historyPanel_);
                historyWindow_->setDefaultSize(420, 520);
            }
            const bool visible = historyWindow_->isVisible();
            if (!visible) refreshHistoryList();
            historyWindow_->setVisible(!visible);
            break;
        }
        case kMenuViewProjectNotes: showProjectNotes(); break;
        case kMenuViewPlayOrder: showPlayOrder(); break;
        case kMenuViewSpectrum: {
            if (!spectrumWindow_) {
                spectrumWindow_ = std::make_unique<PanelWindow>(
                    juce::String::fromUTF8(u8"Analyseur de spectre"), spectrumPanel_);
                spectrumWindow_->setDefaultSize(720, 420);
                // La prise ne coûte au fil audio que fenêtre ouverte.
                spectrumWindow_->onVisibilityChanged = [this](bool visible) {
                    audioEngine_.processGraph().spectrumTap().setEnabled(visible);
                };
            }
            spectrumWindow_->setVisible(!spectrumWindow_->isVisible());
            break;
        }
        case kMenuViewShortcuts: {
            if (!shortcutsWindow_) {
                shortcutsWindow_ = std::make_unique<PanelWindow>(
                    juce::String::fromUTF8(u8"Raccourcis clavier"), shortcutsPanel_);
                shortcutsWindow_->setDefaultSize(640, 620);
            }
            const bool visible = shortcutsWindow_->isVisible();
            if (!visible) refreshShortcutList();
            shortcutsWindow_->setVisible(!visible);
            if (!visible) shortcutsWindow_->toFront(true);
            break;
        }
        case kMenuViewMidiLearn: {
            if (!midiLearnWindow_) {
                midiLearnWindow_ = std::make_unique<PanelWindow>(
                    juce::String::fromUTF8(u8"Associations MIDI"), midiLearnPanel_);
                midiLearnWindow_->setDefaultSize(560, 420);
            }
            const bool visible = midiLearnWindow_->isVisible();
            if (!visible) refreshMidiLearnList();
            midiLearnWindow_->setVisible(!visible);
            if (!visible) midiLearnWindow_->toFront(true);
            break;
        }
        case kMenuRecordCountInNone:
        case kMenuRecordCountInOne:
        case kMenuRecordCountInTwo:
            countInBars_ = menuItemID - kMenuRecordCountInNone;
            vsm::app::ui::UiScale::properties().setValue("recordCountInBars", countInBars_);
            break;
        case kMenuRecordOverdub:
        case kMenuRecordReplace:
        case kMenuRecordStack:
            recordMode_ = menuItemID == kMenuRecordReplace ? vsm::sequencer::RecordMode::Replace
                        : menuItemID == kMenuRecordStack   ? vsm::sequencer::RecordMode::Stack
                                                            : vsm::sequencer::RecordMode::Overdub;
            vsm::app::ui::UiScale::properties().setValue("recordMode",
                                                          static_cast<int>(recordMode_));
            break;
        case kMenuRecordPunchToggle:
            project_.punchEnabled = !project_.punchEnabled;
            pianoRollPanel_.setPunchRegion(project_.punchStartTick, project_.punchEndTick,
                                       project_.punchEnabled);
            pianoRollPanel_.refresh();
            break;
        case kMenuRecordPunchFromLoop:
            beginProjectEdit(u8"Région de punch");
            project_.punchStartTick = project_.loopStartTick;
            project_.punchEndTick = project_.loopEndTick;
            project_.punchEnabled = project_.punchEndTick > project_.punchStartTick;
            pianoRollPanel_.setPunchRegion(project_.punchStartTick, project_.punchEndTick,
                                       project_.punchEnabled);
            pianoRollPanel_.refresh();
            break;
        case kMenuRecordPunchClear:
            beginProjectEdit(u8"Région de punch");
            project_.punchEnabled = false;
            project_.punchStartTick = project_.punchEndTick = 0;
            pianoRollPanel_.setPunchRegion(0, 0, false);
            pianoRollPanel_.refresh();
            break;
        case kMenuRecordMeasureLatency: measureInputLatency(); break;
        case kMenuRecordClearLatency:
            audioEngine_.setMeasuredRoundTripSeconds(0.0);
            vsm::app::ui::UiScale::properties().setValue("latenceAllerRetour", 0.0);
            break;
        case kMenuRecordQuantizeTake: quantizeLastTake(); break;
        case kMenuRecordRetrospective: recoverRetrospectiveTake(); break;
        case kMenuRecordCompTakes: showTakeComp(); break;
        case kMenuMixAddSend: {
            if (project_.sends.size() >= vsm::audio::engine::ProcessGraph::kMaxSends) break;
            beginProjectEdit(u8"Ajouter un bus de départ");
            vsm::sequencer::SendBusDescription bus;
            bus.name = "Bus " + std::to_string(project_.sends.size() + 1);
            bus.effectType = "reverb";
            project_.sends.push_back(std::move(bus));
            sendBusesChanged();
            break;
        }
        case kMenuFileQuit:      juce::JUCEApplication::getInstance()->systemRequestedQuit(); break;
        case kMenuFileSaveTemplate:    saveAsTemplate(); break;
        case kMenuFileNewFromTemplate: newFromTemplate(); break;
        case kMenuViewFullScreen:      toggleFullScreen(); break;
        case kMenuViewComputerKeyboard:
            computerKeyboard_ = !computerKeyboard_;
            // Éteindre ce qui sonne encore : une note tenue par une touche qu'on
            // ne surveille plus ne s'éteindrait jamais.
            for (const auto& [code, note] : computerKeysDown_) audioEngine_.playComputerKey(note, 0, false);
            computerKeysDown_.clear();
            break;
        case kMenuRecordMonitorInput:
            audioEngine_.setInputMonitoring(!audioEngine_.inputMonitoring());
            break;
        case kMenuTrackAdd:      addTrack(Track::Kind::Midi); break;
        case kMenuTrackAddAudio: addTrack(Track::Kind::Audio); break;
        case kMenuTrackAddGroup: addTrack(Track::Kind::Group); break;
        case kMenuTrackRemove:   removeSelectedTrack(); break;
        case kMenuTrackDuplicate: duplicateSelectedTrack(); break;
        case kMenuTrackCreateClip: {
            // À LA TÊTE DE LECTURE, ramenée sur la mesure : l'article du menu
            // vise le même endroit que le double-clic, à la souris près.
            const size_t piste = trackList_.selectedTrackIndex();
            if (piste >= project_.tracks.size()) break;
            const vsm::midi::Tick ici = std::max<vsm::midi::Tick>(0, transport_.currentTick());
            const vsm::midi::Tick mesure = std::max<vsm::midi::Tick>(
                1, project_.timeSignatureMap.ticksPerBar(ici, project_.ticksPerQuarterNote));
            createClipOnTrack(piste, (ici / mesure) * mesure);
            break;
        }
        case kMenuTrackFreeze:   toggleFreezeSelectedTrack(); break;
        case kMenuTrackLock:     toggleLockSelectedTrack(); break;
        case kMenuTrackSoloSafe: toggleSoloSafeSelectedTrack(); break;   // D30.1
        case kMenuTrackDisable:  toggleDisableSelectedTrack(); break;    // D30.2
        case kMenuTrackCopyChain:   copySelectedTrackChain(); break;      // D30.3
        case kMenuTrackPasteChain:  pasteChainIntoSelectedTrack(true); break;
        case kMenuTrackAppendChain: pasteChainIntoSelectedTrack(false); break;
        case kMenuTrackThinAutomation: thinAutomationOfSelectedTrack(); break;   // D30.5
        case kMenuTrackMidiFxClear: clearMidiEffectsOfSelectedTrack(); break;    // D31.4
        case kMenuTrackMidiFxBake:  bakeMidiEffectsOfSelectedTrack(); break;     // D31.5
        case kMenuTrackRenameSeries: promptRenameTracksInSeries(); break;        // D32.4
        case kMenuTrackHide:     hideSelectedTrack(); break;
        case kMenuTrackSoloExclusive: soloTrackExclusively(trackList_.selectedTrackIndex()); break;
        case kMenuTrackShowAll:  showAllTracks(); break;
        case kMenuTrackSavePreset: promptSaveTrackPreset(); break;
        default: break;
    }
    if (menuItemID >= kMenuTrackMidiFxFirst && menuItemID <= kMenuTrackMidiFxLast) {   // D31.4
        const auto types = vsm::sequencer::midiEffectTypes();
        const size_t i = static_cast<size_t>(menuItemID - kMenuTrackMidiFxFirst);
        if (i < types.size()) addMidiEffectToSelectedTrack(types[i]);
        return;
    }
    if (menuItemID == kMenuTrackMidiOutNone) { setSelectedTrackMidiOutput(""); return; }
    if (menuItemID == kMenuTrackMidiProgram) { promptMidiProgram(); return; }
    if (menuItemID >= kMenuTrackInputChannelFirst && menuItemID <= kMenuTrackInputChannelLast) {
        setSelectedTrackInputChannel(menuItemID - kMenuTrackInputChannelFirst);
        return;
    }
    if (menuItemID >= kMenuTrackMidiOutFirst && menuItemID <= kMenuTrackMidiOutLast) {
        const auto noms = audioEngine_.availableMidiOutputs();
        const size_t i = static_cast<size_t>(menuItemID - kMenuTrackMidiOutFirst);
        if (i < noms.size()) setSelectedTrackMidiOutput(noms[i]);
        return;
    }
    if (menuItemID >= kMenuTrackPresetFirst && menuItemID <= kMenuTrackPresetLast) {
        const auto fichiers = trackPresetFiles();
        const int i = menuItemID - kMenuTrackPresetFirst;
        if (i < fichiers.size()) applyTrackPresetFile(fichiers[i]);
        return;
    }
    if (menuItemID >= kMenuTrackEditGroupNone && menuItemID <= kMenuTrackEditGroupLast) {
        const size_t piste = trackList_.selectedTrackIndex();
        if (piste < project_.tracks.size()) {
            const int groupe = menuItemID - kMenuTrackEditGroupNone;
            if (project_.tracks[piste].editGroup != groupe) {
                beginProjectEdit(u8"Groupe d'édition");
                project_.tracks[piste].editGroup = groupe;
                // AUCUN SIGNAL NE CHANGE : un groupe d'édition lie des gestes,
                // pas des bus. Rien n'est republié au moteur.
                arrangement_.repaint();
                trackList_.repaint();
            }
        }
        return;
    }
    switch (menuItemID) {
#if VSM_WITH_CLAP
        case kMenuTrackClapPlugin: loadClapPluginOnSelectedTrack(); break;
#endif
#if VSM_WITH_CLAP || VSM_WITH_VST3
        case kMenuTrackScanPlugins: scanInstalledPlugins(); break;
        case kMenuTrackPluginFromCatalogue: chooseInstrumentFromCatalogue(); break;
#endif
#if VSM_WITH_VST3
        case kMenuTrackVst3Plugin: loadVst3PluginOnSelectedTrack(); break;
        case kMenuTrackPluginEditor: openPluginEditorForSelectedTrack(); break;
#endif
        case kMenuTrackBounce:   bounceSelectedTrack(); break;
        case kMenuTrackBounceSelection: bounceSelectionToNewTracks(); break;
        case kMenuTrackPublishOutputs: publishInstrumentOutputsOfSelectedTrack(); break;
        case kMenuTrackExplodeByPitch: explodeSelectedTrackByPitch(); break;
        case kMenuTrackNewFolder: newFolderAboveSelectedTrack(); break;
        case kMenuTrackFolderIn:  changeSelectedTrackFolderDepth(+1); break;
        case kMenuTrackFolderOut: changeSelectedTrackFolderDepth(-1); break;
        case kMenuViewSingleWindow:
            singleWindow_ = !singleWindow_;
            // Écrit DÈS le choix, comme les associations MIDI : une disposition
            // qu'on refait à chaque lancement n'est pas une disposition.
            vsm::app::ui::UiScale::properties().setValue("fenetreUnique", singleWindow_);
            vsm::app::ui::UiScale::properties().saveIfNeeded();
            if (singleWindow_) {
                if (auto* socle = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent()))
                    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                        socle->setBounds(display->userArea.reduced(8));
                dockPanels();
            } else {
                undockPanels();
                if (auto* socle = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent()))
                    socle->setSize(1000, 56 + 26);
                showFloatingPanels();
            }
            break;
        case kMenuViewTracks:
            rendreLesZones();   // D122 : le menu agit sur la disposition normale
            if (singleWindow_) { trackList_.setVisible(!trackList_.isVisible()); resized(); }
            else togglePanel(trackListWindow_);
            break;
        case kMenuViewPianoRoll:
            rendreLesZones();
            if (singleWindow_) {
                // Le centre montre l'arrangement OU le piano roll ; demander
                // l'un affiche l'un et range l'autre, comme en mode flottant
                // où ils partagent le même emplacement.
                centerShowsArrangement_ = false;
                pianoRollPanel_.setVisible(true);
                arrangement_.setVisible(false);
                resized();
            } else togglePanel(pianoRollWindow_);
            break;
        case kMenuViewSynthRack:
            rendreLesZones();
            if (singleWindow_) { synthRack_.setVisible(!synthRack_.isVisible()); resized(); }
            else togglePanel(synthRackWindow_);
            break;
        case kMenuViewMixer:
            rendreLesZones();
            if (singleWindow_) { bottomTabs_.setVisible(!bottomTabs_.isVisible()); resized(); }
            else togglePanel(mixerWindow_);
            break;
        case kMenuViewArrangement:
            rendreLesZones();
            if (singleWindow_) {
                centerShowsArrangement_ = true;
                arrangement_.setVisible(true);
                pianoRollPanel_.setVisible(false);
                resized();
            } else togglePanel(arrangementWindow_);
            break;
        case kMenuHelpAbout:     showAboutDialog(); break;
        default:
            if (menuItemID >= kMenuMixRemoveSendFirst && menuItemID <= kMenuMixRemoveSendLast) {
                const size_t bus = static_cast<size_t>(menuItemID - kMenuMixRemoveSendFirst);
                if (bus >= project_.sends.size()) break;
                beginProjectEdit(u8"Retirer un bus de départ");
                project_.sends.erase(project_.sends.begin() + static_cast<std::ptrdiff_t>(bus));
                // LES NIVEAUX DES PISTES SUIVENT LE BUS RETIRÉ. Sans cela, le
                // départ qui visait le bus 2 viserait le bus 1 après la
                // suppression du 0 : la piste enverrait dans le mauvais effet
                // sans qu'aucun bouton n'ait bougé.
                for (auto& piste : project_.tracks)
                    if (bus < piste.sendLevels.size())
                        piste.sendLevels.erase(piste.sendLevels.begin()
                                                + static_cast<std::ptrdiff_t>(bus));
                sendBusesChanged();
                break;
            }
            if (menuItemID >= kMenuMixSendReturnFirst && menuItemID <= kMenuMixSendReturnLast) {
                const size_t bus = static_cast<size_t>(menuItemID - kMenuMixSendReturnFirst);
                if (bus >= project_.sends.size()) break;
                beginProjectEdit(u8"Retour d'un départ");
                project_.sends[bus].returnGain = project_.sends[bus].returnGain > 0.0f ? 0.0f : 1.0f;
                sendBusesChanged();
                break;
            }
            if (menuItemID >= kMenuMixSendPreFaderFirst && menuItemID <= kMenuMixSendPreFaderLast) {
                const size_t bus = static_cast<size_t>(menuItemID - kMenuMixSendPreFaderFirst);
                if (bus >= project_.sends.size()) break;
                beginProjectEdit(u8"Pré/post-fader d'un départ");
                project_.sends[bus].preFader = !project_.sends[bus].preFader;
                sendBusesChanged();
                break;
            }
            if (menuItemID >= kMenuMixSendEffectFirst && menuItemID <= kMenuMixSendEffectLast) {
                const int offset = menuItemID - kMenuMixSendEffectFirst;
                const size_t bus = static_cast<size_t>(offset / 20);
                const size_t choix = static_cast<size_t>(offset % 20);
                const auto& effets = vsm::audio::effect::EffectFactory::available();
                if (bus >= project_.sends.size() || choix >= effets.size()) break;
                beginProjectEdit(u8"Effet d'un bus de départ");
                project_.sends[bus].effectType = effets[choix].id;
                // Les réglages appartenaient à l'effet précédent : les garder
                // reposerait des valeurs nommées pour un autre effet, qui les
                // signalerait toutes comme inconnues.
                project_.sends[bus].parameters.clear();
                project_.sends[bus].name = effets[choix].displayName;
                sendBusesChanged();
                break;
            }
            if (menuItemID >= kMenuAudioThreadsFirst && menuItemID <= kMenuAudioThreadsLast) {
                setRenderThreadChoice(menuItemID == kMenuAudioThreadsFirst
                                           ? kRenderThreadsAutomatic
                                           : menuItemID - kMenuAudioThreadsFirst - 1);
                break;
            }
            if (menuItemID >= kMenuViewLangueFirst && menuItemID <= kMenuViewLangueLast) {
                const vsm::app::ui::Langue::Choix choix[] = {
                    vsm::app::ui::Langue::Choix::Francais,
                    vsm::app::ui::Langue::Choix::Anglais,
                };
                const int index = menuItemID - kMenuViewLangueFirst;
                if (index >= 0 && index < static_cast<int>(std::size(choix)))
                    if (vsm::app::ui::Langue::appliquer(choix[index])) retraduire();
                break;
            }
            if (menuItemID >= kMenuViewScaleFirst && menuItemID <= kMenuViewScaleLast) {
                const auto& paliers = vsm::app::ui::UiScale::steps();
                const int index = menuItemID - kMenuViewScaleFirst;
                if (index < paliers.size()) setUiScale(paliers[index]);
                break;
            }
            // CHOISIR UNE PRISE. C'est le « se choisissent » du critère de
            // D3.5 : la piste range son matériau courant dans la prise à
            // laquelle il appartient, et sort celui de la prise demandée.
            if (menuItemID >= kMenuRecordTakeFirst && menuItemID <= kMenuRecordTakeLast) {
                const size_t piste = trackList_.selectedTrackIndex();
                if (piste >= project_.tracks.size()) break;
                beginProjectEdit(u8"Choisir une prise");
                vsm::sequencer::selectTake(project_.tracks[piste],
                                            menuItemID - kMenuRecordTakeFirst);
                rebuildFromProject(false);
                refreshTakeCompPanel();   // D57 : « celle qu'on entend » a changé
            }
            // D57 : RETIRER UNE PRISE DU TIROIR, et dire ce que cela coûte.
            if (menuItemID >= kMenuRecordDeleteTakeFirst && menuItemID <= kMenuRecordDeleteTakeLast) {
                removeTakeFromSelectedTrack(menuItemID - kMenuRecordDeleteTakeFirst);
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
    montrerBoite(
        juce::AlertWindow::InfoIcon, "Vintage Synth MIDI Studio",
        tr(u8"Séquenceur MIDI + rack de synthétiseurs vintage virtuels.\n\n"
           u8"Version 0.1.0 -- Phases 3 et 4 faites (instruments de référence + extension)."));
}

bool MainComponent::prendreLeFichierDeBanc(const std::function<void(const juce::File&)>& suite) {
    // D102 : LE SÉLECTEUR, ET LUI SEUL, SAUTÉ PAR LE BANC. Un sélecteur de fichier
    // ne se pilote pas sans souris ; ce qu'il rend, si. Le reste du chemin -- le
    // menu qui l'ouvre, ce qu'on fait du fichier -- est celui de l'utilisateur.
    if (fichierDeBanc_ == juce::File()) return false;
    const juce::File fichier = fichierDeBanc_;
    fichierDeBanc_ = juce::File();
    std::fputs((juce::String(u8"VSM_PLUGIN : sélecteur sauté, fichier ") + fichier.getFullPathName() + "\n")
                   .toRawUTF8(), stderr);
    suite(fichier);
    return true;
}

void MainComponent::loadClapPluginOnSelectedTrack() {
#if VSM_WITH_CLAP
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;

    auto suite = [this, piste](const juce::File& fichier) {
        if (fichier == juce::File()) return;

        // ON REGARDE CE QU'IL Y A DEDANS AVANT DE L'INSTANCIER. Un fichier
        // .clap peut contenir plusieurs plugins, et un fichier cassé ne doit
        // jamais faire tomber l'application qui l'ouvre -- c'est déjà la
        // promesse de `scanClapFile`.
        std::string erreur;
        const auto trouves = vsm::clap::scanClapFile(fichier.getFullPathName().toStdString(),
                                                      erreur);
        if (trouves.empty()) {
            montrerBoite(
                juce::AlertWindow::WarningIcon, tr(u8"Plugin CLAP illisible"),
                tr(u8"Ce fichier n'a livré aucun plugin.\n\n%1")
                    .replace("%1", vsm::app::ui::trPhrase(juce::String(erreur))));
            return;
        }

        auto poser = [this, piste, fichier](const std::string& pluginId,
                                             const std::string& nomAffiche) {
            beginProjectEdit(u8"Charger un plugin CLAP");
            auto& cible = project_.tracks[piste];
            cible.requestedInstrumentId.clear();   // D76
            cible.instrumentId = vsm::clap::clapInstrumentId(
                fichier.getFullPathName().toStdString(), pluginId);
            // LE PRESET DE L'ANCIENNE MACHINE NE SUIT PAS : ses identifiants
            // sémantiques ne veulent rien dire pour celle-ci, et les appliquer
            // en silence donnerait un son que personne n'a réglé.
            rebuildFromProject();
            montrerBoite(
                juce::AlertWindow::InfoIcon, tr(u8"Plugin chargé"),
                tr(u8"%1 joue maintenant sur la piste %2.")
                    .replace("%2", juce::String(static_cast<int>(piste) + 1))
                    .replace("%1", juce::String(nomAffiche)));
        };

        if (trouves.size() == 1) {
            poser(trouves[0].id, trouves[0].name);
            return;
        }

        // PLUSIEURS PLUGINS DANS LE MÊME FICHIER : on demande lequel plutôt que
        // de prendre le premier. Prendre le premier chargerait une machine que
        // l'utilisateur n'a pas choisie, sans qu'il puisse s'en apercevoir
        // autrement qu'à l'oreille.
        auto fenetre = std::make_shared<juce::AlertWindow>(
            tr(u8"Plusieurs plugins dans ce fichier"), tr(u8"Lequel charger ?"),
            juce::AlertWindow::NoIcon);
        juce::StringArray noms;
        for (const auto& info : trouves)
            noms.add(juce::String(info.name) + " -- " + juce::String(info.vendor));
        fenetre->addComboBox("plugin", noms, u8"Plugin");
        fenetre->addButton(tr(u8"Charger"), 1, juce::KeyPress(juce::KeyPress::returnKey));
        fenetre->addButton(vsm::app::ui::trSelon("bouton", u8"Annuler"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
        annoncerFenetre(*fenetre);   // D102
        fenetre->enterModalState(true, juce::ModalCallbackFunction::create(
            [fenetre, trouves, poser](int resultat) {
                const int choix = fenetre->getComboBoxComponent("plugin")->getSelectedId();
                fenetre->exitModalState(resultat);
                fenetre->setVisible(false);
                if (resultat != 1 || choix < 1
                    || static_cast<size_t>(choix) > trouves.size()) return;
                poser(trouves[static_cast<size_t>(choix) - 1].id,
                       trouves[static_cast<size_t>(choix) - 1].name);
            }), false);
    };
    if (prendreLeFichierDeBanc(suite)) return;   // D102 : le banc
    auto chooser = std::make_shared<juce::FileChooser>(
        tr(u8"Choisir un plugin CLAP..."), juce::File("/usr/lib/clap"), "*.clap");
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::canSelectDirectories,
                          [chooser, suite](const juce::FileChooser& fc) { suite(fc.getResult()); });
#endif
}

void MainComponent::scanInstalledPlugins() {
#if VSM_WITH_CLAP || VSM_WITH_VST3
    if (pluginScanner_ != nullptr) return;   // un seul balayage à la fois

    pluginScanner_ = std::make_unique<vsm::app::plugins::PluginScanner>();
    // LES DEUX RAPPELS ARRIVENT SUR LE FIL DE FOND. Toucher à l'interface
    // depuis là ferait tomber l'application de façon irrégulière et
    // impossible à reproduire ; on repasse donc par le fil des messages.
    pluginScanner_->onProgress = [this](int fait, int total, const juce::String& courant) {
        juce::MessageManager::callAsync([this, fait, total, courant] {
            if (auto* fenetre = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent()))
                fenetre->setName(tr(u8"Vintage Synth MIDI Studio -- balayage %1/%2 : %3")
                                  .replace("%1", juce::String(fait))
                                  .replace("%2", juce::String(total))
                                  .replace("%3", courant));
        });
    };
    pluginScanner_->onFinished = [this](vsm::interchange::PluginCatalogue catalogue) {
        juce::MessageManager::callAsync([this, catalogue = std::move(catalogue)]() mutable {
            pluginCatalogue_ = std::move(catalogue);
            pluginScanner_.reset();
            if (auto* fenetre = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent()))
                fenetre->setName("Vintage Synth MIDI Studio");

            // `u8"..."` NE SE CONCATÈNE PAS DIRECTEMENT à une `juce::String`
            // (voir ARCHITECTURE.md § 6 bis bis) : chaque littéral accentué
            // passe par un `juce::String` explicite.
            juce::String message =
                tr(u8"%1 instrument(s), %2 effet(s) trouvés.")
                    .replace("%1", juce::String(pluginCatalogue_.instruments().size()))
                    .replace("%2", juce::String(pluginCatalogue_.effects().size()));
            // LES FAUTIFS SONT NOMMÉS. Un fichier qui disparaît du balayage
            // sans un mot laisse l'utilisateur chercher pourquoi son plugin
            // n'apparaît nulle part.
            if (!pluginCatalogue_.faulty.empty()) {
                message += juce::String("\n\n")
                           + tr(u8"%1 fichier(s) n'ont pas pu être lus. Ils sont isolés : ils n'ont pas fait "
                                 u8"tomber l'application, et ne seront pas rouverts.\n")
                                 .replace("%1", juce::String(pluginCatalogue_.faulty.size()));
                for (const auto& fautif : pluginCatalogue_.faulty)
                    message += "\n" + juce::String(fautif.path) + "\n   "
                               + vsm::app::ui::trPhrase(juce::String(fautif.reason));
            }
            montrerBoite(juce::AlertWindow::InfoIcon,
                                                     tr(u8"Balayage terminé"), message);
        });
    };
    pluginScanner_->start(false);

    montrerBoite(
        juce::AlertWindow::InfoIcon, tr(u8"Balayage lancé"),
        tr(u8"Les plugins installés sont ouverts un par un, dans un processus à part.\n\n"
           u8"Vous pouvez continuer à travailler : un plugin qui ferait tomber son processus "
           u8"de balayage sera signalé, pas fatal."));
#endif
}

void MainComponent::chooseInstrumentFromCatalogue() {
#if VSM_WITH_CLAP || VSM_WITH_VST3
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;
    const auto instruments = pluginCatalogue_.instruments();
    if (instruments.empty()) return;

    auto fenetre = std::make_shared<juce::AlertWindow>(
        tr(u8"Instruments trouvés sur cette machine"), tr(u8"Lequel poser sur la piste ?"),
        juce::AlertWindow::NoIcon);
    juce::StringArray noms;
    for (const auto& plugin : instruments)
        noms.add(juce::String(plugin.name) + "  --  " + juce::String(plugin.vendor)
                 + "  [" + juce::String(plugin.format) + "]");
    fenetre->addComboBox("plugin", noms, u8"Instrument");
    fenetre->addButton(tr(u8"Charger"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    fenetre->addButton(vsm::app::ui::trSelon("bouton", u8"Annuler"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    annoncerFenetre(*fenetre);   // D102
    fenetre->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, fenetre, instruments, piste](int resultat) {
            const int choix = fenetre->getComboBoxComponent("plugin")->getSelectedId();
            fenetre->exitModalState(resultat);
            fenetre->setVisible(false);
            if (resultat != 1 || choix < 1 || static_cast<size_t>(choix) > instruments.size()) return;

            beginProjectEdit(u8"Charger un instrument");
            auto& cible = project_.tracks[piste];
            // L'IDENTIFIANT VIENT DU CATALOGUE, et c'est exactement celui que
            // les fabriques savent lire : le balayage ne sert à rien s'il ne
            // débouche pas sur la même porte que le reste (D7.1 à D7.3).
            cible.requestedInstrumentId.clear();   // D76
            cible.instrumentId = instruments[static_cast<size_t>(choix) - 1].instrumentId();
            rebuildFromProject();
        }), false);
#endif
}

void MainComponent::openPluginEditorForSelectedTrack() {
#if VSM_WITH_VST3 || VSM_WITH_CLAP
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;

    // DÉJÀ OUVERTE : ON LA RAMÈNE DEVANT, on n'en ouvre pas une seconde. Deux
    // fenêtres sur le même plugin montreraient le même état à deux endroits, et
    // l'utilisateur ne saurait plus laquelle il vient de régler.
    if (const auto trouvee = pluginEditorWindows_.find(piste);
        trouvee != pluginEditorWindows_.end() && trouvee->second != nullptr) {
        trouvee->second->toFront(true);
        return;
    }

    auto* machine = audioEngine_.processGraph().trackInstrument(piste);
    if (machine == nullptr) return;

    // DEUX FORMATS, UNE SEULE FENÊTRE. Un plugin est VST3 ou CLAP, jamais les
    // deux : on demande sa façade à chacun, et le premier qui en a une gagne.
    // La fenêtre qui suit ne sait pas lequel a répondu, et n'a pas à le savoir.
    std::unique_ptr<juce::Component> facade;
    bool redimensionnable = false;
#if VSM_WITH_VST3
    if (auto editeurVst3 = vsm::vst3::createEditorFor(*machine)) {
        redimensionnable = editeurVst3->isResizable();
        facade = std::move(editeurVst3);
    }
#endif
#if VSM_WITH_CLAP
    if (facade == nullptr) {
        facade = vsm::clap::createEditorFor(*machine);
        // LA FENÊTRE SUIT LA TAILLE QUE LE PLUGIN DEMANDE, et le laisse la
        // changer s'il le permet : un éditeur redimensionnable enfermé dans
        // une fenêtre fixe se retrouve rogné, ce qui est pire que pas de
        // fenêtre du tout. Côté CLAP, c'est `can_resize` qui le dit, et la
        // façade l'a déjà lu.
        if (facade != nullptr) redimensionnable = true;
    }
#endif
    if (facade == nullptr) {
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Pas d'interface native"),
            tr(u8"Cette machine n'a pas de façade à elle. Ses réglages restent accessibles dans le "
               u8"Synth Rack."));
        return;
    }
    class FenetreFacade final : public juce::DocumentWindow {
    public:
        FenetreFacade(const juce::String& titre, std::function<void()> quandFermee, bool agrandissable)
            // D122 : agrandir seulement si elle se redimensionne -- une façade de
            // taille fixe, agrandie, ne serait qu'un grand fond noir.
            : juce::DocumentWindow(titre, juce::Colours::black,
                                    juce::DocumentWindow::closeButton
                                        | (agrandissable ? juce::DocumentWindow::maximiseButton : 0)),
              quandFermee_(std::move(quandFermee)) {}
        /// FERMER DÉTRUIT LE DESSIN, PAS LE SON. L'état vit dans le plugin ;
        /// la prochaine ouverture en refabrique la façade, qui le montre tel
        /// qu'il est resté.
        void closeButtonPressed() override { if (quandFermee_) quandFermee_(); }
    private:
        std::function<void()> quandFermee_;
    };

    auto fenetre = std::make_unique<FenetreFacade>(
        juce::String::fromUTF8(project_.tracks[piste].name.c_str()) + " -- "
            + juce::String::fromUTF8(machine->machineName()),
        [this, piste] { pluginEditorWindows_.erase(piste); }, redimensionnable);
    fenetre->setUsingNativeTitleBar(true);
    fenetre->setResizable(redimensionnable, false);
    fenetre->setContentOwned(facade.release(), true);
    fenetre->centreWithSize(fenetre->getWidth(), fenetre->getHeight());
    fenetre->setVisible(true);
    pluginEditorWindows_[piste] = std::move(fenetre);
#endif
}

void MainComponent::chooseThirdPartyEffect(std::function<void(std::string)> quandChoisi) {
#if VSM_WITH_CLAP || VSM_WITH_VST3
    // D7.5 : SI ON A DÉJÀ BALAYÉ, ON PROPOSE CE QU'ON A TROUVÉ. Faire chercher
    // un fichier à quelqu'un qui vient d'attendre un balayage complet serait
    // lui redemander ce qu'on sait déjà. Le sélecteur de fichier reste en
    // dernier choix, pour un plugin installé ailleurs.
    const auto effetsConnus = pluginCatalogue_.effects();
    if (!effetsConnus.empty()) {
        auto fenetre = std::make_shared<juce::AlertWindow>(
            tr(u8"Effets trouvés sur cette machine"), tr(u8"Lequel insérer ?"),
            juce::AlertWindow::NoIcon);
        juce::StringArray noms;
        for (const auto& plugin : effetsConnus)
            noms.add(juce::String(plugin.name) + "  --  " + juce::String(plugin.vendor)
                     + "  [" + juce::String(plugin.format) + "]");
        noms.add(tr(u8"Parcourir un fichier..."));
        fenetre->addComboBox("effet", noms, tr(u8"Effet"));
        fenetre->addButton(tr(u8"Insérer"), 1, juce::KeyPress(juce::KeyPress::returnKey));
        fenetre->addButton(vsm::app::ui::trSelon("bouton", u8"Annuler"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
        annoncerFenetre(*fenetre);   // D102
        fenetre->enterModalState(true, juce::ModalCallbackFunction::create(
            [this, fenetre, effetsConnus, quandChoisi](int resultat) {
                const int choix = fenetre->getComboBoxComponent("effet")->getSelectedId();
                fenetre->exitModalState(resultat);
                fenetre->setVisible(false);
                if (resultat != 1 || choix < 1) return;
                if (static_cast<size_t>(choix) <= effetsConnus.size()) {
                    quandChoisi(effetsConnus[static_cast<size_t>(choix) - 1].instrumentId());
                    return;
                }
                browseForThirdPartyEffect(quandChoisi);
            }), false);
        return;
    }
    browseForThirdPartyEffect(std::move(quandChoisi));
#else
    juce::ignoreUnused(quandChoisi);
#endif
}

void MainComponent::browseForThirdPartyEffect(std::function<void(std::string)> quandChoisi) {
#if VSM_WITH_CLAP || VSM_WITH_VST3
    // UN SEUL SÉLECTEUR POUR LES DEUX FORMATS. Demander d'abord « CLAP ou
    // VST3 ? » ferait choisir une technologie avant de choisir un son ; le
    // filtre du sélecteur accepte les deux extensions, et c'est le fichier
    // désigné qui décide.
    auto suite = [this, quandChoisi](const juce::File& fichier) {
        if (fichier == juce::File()) return;
        const std::string chemin = fichier.getFullPathName().toStdString();

        // CE QUI DÉCIDE EST L'EXTENSION DU FICHIER, pas une question posée à
        // l'utilisateur. Un `.vst3` est un VST3, un `.clap` est un CLAP, et
        // aucun des deux ne se déguise en l'autre.
        std::string erreur;
        std::string identifiant;
        juce::String nomAffiche;

#if VSM_WITH_VST3
        if (fichier.getFileName().endsWithIgnoreCase(".vst3")) {
            for (const auto& info : vsm::vst3::scanVst3File(chemin, erreur)) {
                if (info.isInstrument) continue;   // un instrument n'est pas un insert
                identifiant = vsm::vst3::vst3InstrumentId(chemin, info.id);
                nomAffiche = juce::String(info.name);
                break;
            }
            if (identifiant.empty() && erreur.empty())
                erreur = "ce fichier ne contient que des instruments : "
                         "posez-le sur une piste, pas en insert";
        }
#endif
#if VSM_WITH_CLAP
        if (identifiant.empty() && fichier.getFileName().endsWithIgnoreCase(".clap")) {
            for (const auto& info : vsm::clap::scanClapFile(chemin, erreur)) {
                if (info.isInstrument) continue;
                identifiant = vsm::clap::clapInstrumentId(chemin, info.id);
                nomAffiche = juce::String(info.name);
                break;
            }
            if (identifiant.empty() && erreur.empty())
                erreur = "ce fichier ne contient que des instruments : "
                         "posez-le sur une piste, pas en insert";
        }
#endif

        if (identifiant.empty()) {
            montrerBoite(
                juce::AlertWindow::WarningIcon, tr(u8"Effet illisible"),
                tr(u8"Aucun effet n'a pu être chargé depuis ce fichier.\n\n%1")
                    .replace("%1", vsm::app::ui::trPhrase(juce::String(erreur))));
            return;
        }
        juce::ignoreUnused(nomAffiche);
        quandChoisi(identifiant);
    };
    if (prendreLeFichierDeBanc(suite)) return;   // D102 : le banc
    auto chooser = std::make_shared<juce::FileChooser>(
        tr(u8"Choisir un effet (.clap ou .vst3)..."), juce::File(), "*.clap;*.vst3");
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::canSelectDirectories,
                          [chooser, suite](const juce::FileChooser& fc) { suite(fc.getResult()); });
#else
    juce::ignoreUnused(quandChoisi);
#endif
}

void MainComponent::loadVst3PluginOnSelectedTrack() {
#if VSM_WITH_VST3
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;

    // LE DOSSIER PAR DÉFAUT EST CELUI OÙ LES VST3 VIVENT sur cette plateforme.
    // Ouvrir sur la racine obligerait à retrouver un chemin que personne ne
    // connaît par coeur.
#if JUCE_LINUX
    const juce::File depart("/usr/lib/vst3");
#elif JUCE_MAC
    const juce::File depart("/Library/Audio/Plug-Ins/VST3");
#else
    const juce::File depart("C:\\Program Files\\Common Files\\VST3");
#endif

    auto suite = [this, piste](const juce::File& fichier) {
        if (fichier == juce::File()) return;

        std::string erreur;
        const auto trouves = vsm::vst3::scanVst3File(fichier.getFullPathName().toStdString(),
                                                      erreur);
        if (trouves.empty()) {
            montrerBoite(
                juce::AlertWindow::WarningIcon, tr(u8"Plugin VST3 illisible"),
                tr(u8"Ce fichier n'a livré aucun plugin.\n\n%1")
                    .replace("%1", vsm::app::ui::trPhrase(juce::String(erreur))));
            return;
        }

        // ON NE PROPOSE QUE LES INSTRUMENTS. Un effet posé là où la piste
        // attend un instrument donnerait du silence, et il faudrait le deviner
        // à l'oreille. Les effets viendront en D7.3.
        std::vector<vsm::vst3::Vst3PluginInfo> instruments;
        for (const auto& info : trouves)
            if (info.isInstrument) instruments.push_back(info);
        if (instruments.empty()) {
            montrerBoite(
                juce::AlertWindow::WarningIcon, tr(u8"Pas d'instrument dans ce fichier"),
                tr(u8"Ce fichier ne contient que des effets. Les héberger viendra avec l'étape D7.3."));
            return;
        }

        auto poser = [this, piste, fichier](const std::string& pluginId,
                                             const std::string& nomAffiche) {
            beginProjectEdit(u8"Charger un instrument VST3");
            auto& cible = project_.tracks[piste];
            cible.requestedInstrumentId.clear();   // D76
            cible.instrumentId = vsm::vst3::vst3InstrumentId(
                fichier.getFullPathName().toStdString(), pluginId);
            // LE PRESET DE L'ANCIENNE MACHINE NE SUIT PAS : ses identités
            // sémantiques ne veulent rien dire pour celle-ci.
            rebuildFromProject();
            montrerBoite(
                juce::AlertWindow::InfoIcon, tr(u8"Instrument chargé"),
                tr(u8"%1 joue maintenant sur la piste %2.")
                    .replace("%2", juce::String(static_cast<int>(piste) + 1))
                    .replace("%1", juce::String(nomAffiche)));
        };

        if (instruments.size() == 1) {
            poser(instruments[0].id, instruments[0].name);
            return;
        }

        auto fenetre = std::make_shared<juce::AlertWindow>(
            tr(u8"Plusieurs instruments dans ce fichier"), tr(u8"Lequel charger ?"),
            juce::AlertWindow::NoIcon);
        juce::StringArray noms;
        for (const auto& info : instruments)
            noms.add(juce::String(info.name) + " -- " + juce::String(info.vendor));
        fenetre->addComboBox("plugin", noms, u8"Instrument");
        fenetre->addButton(tr(u8"Charger"), 1, juce::KeyPress(juce::KeyPress::returnKey));
        fenetre->addButton(vsm::app::ui::trSelon("bouton", u8"Annuler"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
        annoncerFenetre(*fenetre);   // D102
        fenetre->enterModalState(true, juce::ModalCallbackFunction::create(
            [fenetre, instruments, poser](int resultat) {
                const int choix = fenetre->getComboBoxComponent("plugin")->getSelectedId();
                fenetre->exitModalState(resultat);
                fenetre->setVisible(false);
                if (resultat != 1 || choix < 1
                    || static_cast<size_t>(choix) > instruments.size()) return;
                poser(instruments[static_cast<size_t>(choix) - 1].id,
                       instruments[static_cast<size_t>(choix) - 1].name);
            }), false);
    };
    if (prendreLeFichierDeBanc(suite)) return;   // D102 : le banc
    auto chooser = std::make_shared<juce::FileChooser>(
        tr(u8"Choisir un instrument VST3..."), depart.isDirectory() ? depart : juce::File(),
        "*.vst3");
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::canSelectDirectories,
                          [chooser, suite](const juce::FileChooser& fc) { suite(fc.getResult()); });
#endif
}

void MainComponent::exportAudioFile() {
    // D6.1 : ON DEMANDE AVANT D'ÉCRIRE. La version précédente rendait toujours
    // le morceau entier en 48 kHz / 24 bits, sans jamais le dire ni permettre
    // d'en changer : un projet travaillé à 96 kHz s'exportait rééchantillonné
    // en silence, et exporter huit mesures obligeait à exporter tout puis à
    // couper ailleurs.
    auto fenetre = std::make_shared<juce::AlertWindow>(
        tr(u8"Exporter en audio"), tr(u8"Ce qui sera rendu :"), juce::AlertWindow::NoIcon);

    juce::StringArray plages;
    // D90 : TOUT PAR `tr()`, ET LES ACCENTS RENDUS -- ce français était écrit
    // sans (« La selection », « Crete a -1 dBFS »).
    plages.add(tr(u8"Le morceau entier"));
    plages.add(tr(u8"La boucle"));
    plages.add(tr(u8"La sélection"));
    fenetre->addComboBox("plage", plages, tr(u8"Plage"));
    // CE QUI N'EXISTE PAS NE SE PROPOSE PAS : une boucle absente ou une
    // sélection vide donneraient un fichier vide sans rien expliquer.
    vsm::midi::Tick selDebut = 0, selFin = 0;
    const bool aSelection = arrangement_.selectionTickRange(selDebut, selFin);
    const bool aBoucle = project_.loopEndTick > project_.loopStartTick;
    if (auto* box = fenetre->getComboBoxComponent("plage")) {
        box->setItemEnabled(2, aBoucle);
        box->setItemEnabled(3, aSelection);
        box->setSelectedId(aSelection ? 3 : (aBoucle ? 2 : 1), juce::dontSendNotification);
    }

    juce::StringArray frequences;
    frequences.add(u8"44100 Hz");
    frequences.add(u8"48000 Hz");
    frequences.add(u8"88200 Hz");
    frequences.add(u8"96000 Hz");
    frequences.add(u8"192000 Hz");
    fenetre->addComboBox("frequence", frequences, tr(u8"Fréquence"));
    // LE DÉFAUT EST CELLE DE LA SESSION, pas 48 kHz : exporter à une fréquence
    // autre que celle qu'on vient d'entendre est un choix, jamais un accident.
    const double sessionHz = audioEngine_.currentSampleRate() > 0.0
                                 ? audioEngine_.currentSampleRate() : 48000.0;
    static const double kFrequences[] = {44100.0, 48000.0, 88200.0, 96000.0, 192000.0};
    if (auto* box = fenetre->getComboBoxComponent("frequence")) {
        int choix = 2;
        for (int i = 0; i < 5; ++i)
            if (std::abs(kFrequences[i] - sessionHz) < 1.0) choix = i + 1;
        box->setSelectedId(choix, juce::dontSendNotification);
    }

    juce::StringArray profondeurs;
    profondeurs.add(tr(u8"16 bits entiers"));
    profondeurs.add(tr(u8"24 bits entiers"));
    profondeurs.add(tr(u8"32 bits flottants"));
    fenetre->addComboBox("profondeur", profondeurs, tr(u8"Profondeur"));
    if (auto* box = fenetre->getComboBoxComponent("profondeur"))
        box->setSelectedId(2, juce::dontSendNotification);

    // LA QUEUE EST EN SECONDES ET SE RÈGLE : deux secondes suffisent à une
    // pièce sèche et coupent net une grande réverbération, ce qui s'entend.
    fenetre->addTextEditor("queue", "2.0", tr(u8"Queue (secondes)"));

    // D6.5 : L'OPTION EST EXPLICITE ET JAMAIS COCHÉE D'AVANCE. Les machines de
    // ce projet sont déterministes : un rendu accéléré leur donne exactement
    // les mêmes échantillons, et neuf minutes rendues en dix secondes valent
    // mieux que neuf minutes rendues en neuf minutes. Un plugin qui EXIGE le
    // temps réel l'obtient de lui-même, sans que personne ait à cocher quoi que
    // ce soit -- et le rendu le dit alors dans ses avertissements.
    fenetre->addTextBlock(tr(u8"Rendu en temps réel : uniquement si un plugin l'exige "
                             u8"(il le demande alors lui-même). Cocher ci-dessous force "
                             u8"le rendu à la vitesse du morceau."));
    juce::StringArray vitesses;
    vitesses.add(tr(u8"Aussi vite que possible (identique au bit près)"));
    vitesses.add(tr(u8"Au pas du temps réel"));
    fenetre->addComboBox("vitesse", vitesses, tr(u8"Vitesse de rendu"));
    if (auto* box = fenetre->getComboBoxComponent("vitesse"))
        box->setSelectedId(1, juce::dontSendNotification);

    // D21.5 : LE NIVEAU. Tel quel par défaut -- normaliser est un choix, jamais
    // un accident -- ; la crête ou une sonie cible, mesurées sur le rendu.
    juce::StringArray niveaux;
    niveaux.add(tr(u8"Tel quel (le niveau du mixage)"));
    niveaux.add(tr(u8"Crête à -1 dBFS"));
    niveaux.add(tr(u8"-14 LUFS (diffusion en flux)"));
    niveaux.add(tr(u8"-23 LUFS (radiodiffusion)"));
    fenetre->addComboBox("niveau", niveaux, tr(u8"Niveau"));
    if (auto* box = fenetre->getComboBoxComponent("niveau"))
        box->setSelectedId(1, juce::dontSendNotification);

    fenetre->addButton(tr(u8"Exporter..."), 1, juce::KeyPress(juce::KeyPress::returnKey));
    fenetre->addButton(vsm::app::ui::trSelon("bouton", u8"Annuler"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    fenetre->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, fenetre, aBoucle, aSelection, selDebut, selFin](int resultat) {
        const int plage = fenetre->getComboBoxComponent("plage")->getSelectedId();
        const int frequence = fenetre->getComboBoxComponent("frequence")->getSelectedId();
        const int profondeur = fenetre->getComboBoxComponent("profondeur")->getSelectedId();
        const double queue = std::max(0.0, fenetre->getTextEditorContents("queue").getDoubleValue());
        const int vitesse = fenetre->getComboBoxComponent("vitesse")->getSelectedId();
        const int niveau = fenetre->getComboBoxComponent("niveau")->getSelectedId();
        fenetre->exitModalState(resultat);
        fenetre->setVisible(false);
        if (resultat != 1) return;

        vsm::interchange::RenderOptions options;
        options.blockSize = 512;
        options.tailSeconds = queue;
        options.sampleRate = kFrequences[juce::jlimit(0, 4, frequence - 1)];
        options.realTimeRender = vitesse == 2;
        options.format = profondeur == 1 ? vsm::audio::io::SampleFormat::Int16
                        : profondeur == 3 ? vsm::audio::io::SampleFormat::Float32
                                          : vsm::audio::io::SampleFormat::Int24;

        // LA PLAGE : le morceau laisse tout déduire ; la boucle et la
        // sélection donnent un début ET une longueur, à quoi la queue s'ajoute
        // pour ne pas couper la dernière résonance sur le dernier temps.
        if (plage == 2 && aBoucle) {
            options.startSeconds = project_.ticksToSeconds(project_.loopStartTick);
            options.durationSeconds =
                project_.ticksToSeconds(project_.loopEndTick) - options.startSeconds + queue;
        } else if (plage == 3 && aSelection) {
            options.startSeconds = project_.ticksToSeconds(selDebut);
            options.durationSeconds =
                project_.ticksToSeconds(selFin) - options.startSeconds + queue;
        }

        exportAudioWithOptions(options, niveau == 2 ? ExportLevel::PeakMinus1
                                        : niveau == 3 ? ExportLevel::Lufs14
                                        : niveau == 4 ? ExportLevel::Lufs23 : ExportLevel::AsIs);
    }), false);
}

vsm::interchange::LoadedBundle MainComponent::bundleFromSession() {
    // CE QUE LA SESSION CONTIENT, MIS EN FORME DE PROJET CHARGÉ -- la seule
    // porte d'entrée du rendu. Écrit une fois plutôt que recopié dans chaque
    // export : deux copies finiraient par diverger, et l'une des deux
    // exporterait alors autre chose que ce qu'on entend.
    vsm::interchange::LoadedBundle bundle;
    bundle.project = project_;
    bundle.document = vsm::interchange::documentFromProject(project_);
    bundle.folderPath = currentProjectFolder_ == juce::File()
                            ? std::string()
                            : currentProjectFolder_.getFullPathName().toStdString();
    // D76 : LES ÉCHANTILLONS AVEC. Sans eux, l'export rendait silencieuse la
    // voix de `sky-v4` -- que l'application faisait pourtant entendre, et que
    // `vsm-render` rendait à -6,41 dBFS depuis le même dossier.
    bundle.presetsByTrack = presetsDeLaSession();
    return bundle;
}

void MainComponent::exportStems() {
    auto fenetre = std::make_shared<juce::AlertWindow>(
        tr(u8"Exporter les stems"),
        // D90 : SANS RETOUR À LA LIGNE FORCÉ. Coupé à la main pour une largeur que
        // l'échelle de 150 % n'a pas, le texte laissait « qu'il » seul sur sa
        // ligne ; la fenêtre coupe elle-même, à sa largeur, dans les deux langues.
        tr(u8"Un fichier WAV par piste, dans un dossier. La tranche master n'y est PAS : "
           u8"leur somme redonne le mixage tel qu'il arrive au master. C'est ce qu'on "
           u8"attend de stems."),
        juce::AlertWindow::NoIcon);

    juce::StringArray granularites;
    granularites.add(tr(u8"Une piste par fichier"));
    granularites.add(tr(u8"Un groupe par fichier"));
    fenetre->addComboBox("granularite", granularites, tr(u8"Découpage"));
    if (auto* box = fenetre->getComboBoxComponent("granularite"))
        box->setSelectedId(1, juce::dontSendNotification);

    juce::StringArray profondeurs;
    profondeurs.add(tr(u8"16 bits entiers"));
    profondeurs.add(tr(u8"24 bits entiers"));
    profondeurs.add(tr(u8"32 bits flottants"));
    fenetre->addComboBox("profondeur", profondeurs, tr(u8"Profondeur"));
    // 24 BITS PAR DÉFAUT, comme pour le mixage : des stems destinés à être
    // ADDITIONNÉS ailleurs perdent à passer par 16 bits, où le bruit de
    // quantification de chaque fichier s'additionne aussi.
    if (auto* box = fenetre->getComboBoxComponent("profondeur"))
        box->setSelectedId(2, juce::dontSendNotification);

    fenetre->addTextEditor("queue", "2.0", tr(u8"Queue (secondes)"));
    fenetre->addButton(tr(u8"Choisir le dossier..."), 1, juce::KeyPress(juce::KeyPress::returnKey));
    fenetre->addButton(vsm::app::ui::trSelon("bouton", u8"Annuler"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    fenetre->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, fenetre](int resultat) {
        const int decoupage = fenetre->getComboBoxComponent("granularite")->getSelectedId();
        const int profondeur = fenetre->getComboBoxComponent("profondeur")->getSelectedId();
        const double queue = std::max(0.0, fenetre->getTextEditorContents("queue").getDoubleValue());
        fenetre->exitModalState(resultat);
        fenetre->setVisible(false);
        if (resultat != 1) return;

        vsm::interchange::RenderOptions options;
        options.blockSize = 512;
        options.tailSeconds = queue;
        options.sampleRate = audioEngine_.currentSampleRate() > 0.0
                                 ? audioEngine_.currentSampleRate() : 48000.0;
        options.format = profondeur == 1 ? vsm::audio::io::SampleFormat::Int16
                        : profondeur == 3 ? vsm::audio::io::SampleFormat::Float32
                                          : vsm::audio::io::SampleFormat::Int24;
        const auto granularite = decoupage == 2 ? vsm::interchange::StemGranularity::Groups
                                                 : vsm::interchange::StemGranularity::Tracks;

        auto chooser = std::make_shared<juce::FileChooser>(
            tr(u8"Dossier des stems..."), juce::File(), "");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode
                                 | juce::FileBrowserComponent::canSelectDirectories,
                              [this, chooser, options, granularite](const juce::FileChooser& fc) {
            const juce::File dossier = fc.getResult();
            if (dossier == juce::File()) return;

            juce::String message;
            const bool fait = exportStemsToFolder(dossier, options, granularite, message);
            juce::AlertWindow::showMessageBoxAsync(fait ? juce::AlertWindow::InfoIcon
                                                        : juce::AlertWindow::WarningIcon,
                                                     fait ? tr(u8"Export des stems terminé")
                                                          : tr(u8"Erreur d'export des stems"),
                                                     message);
        });
    }), false);
}

bool MainComponent::exportStemsToFolder(const juce::File& dossier,
                                         const vsm::interchange::RenderOptions& options,
                                         vsm::interchange::StemGranularity granularite,
                                         juce::String& message) {
    captureSessionIntoProject();
    const auto bundle = bundleFromSession();
    const auto sortie = vsm::interchange::renderStemsToFolder(
        bundle, dossier.getFullPathName().toStdString(), granularite, options);
    if (!sortie.success) {
        message = vsm::app::ui::trPhrase(juce::String(sortie.error));   // D90
        return false;
    }
    // CHAQUE STEM DIT SA CRÊTE (D50), comme le mixage exporté dit la sienne.
    // La liste ne portait que des noms : un stem raboté par le format entier
    // choisi juste au-dessus sortait sans un mot, et la somme des fichiers ne
    // redonnait plus le mixage -- ce qui est pourtant toute la raison d'être
    // d'un jeu de stems.
    const bool entier = options.format != vsm::audio::io::SampleFormat::Float32;
    // D90 : LES PHRASES PAR `tr()`, en modèles remplis APRÈS traduction -- le nom
    // du stem en dernier, pour qu'un « %2 » dans un nom de piste reste un nom ;
    // celles du moteur par `trPhrase`.
    message = tr(u8"%1 stems écrits dans :").replace("%1", juce::String(sortie.stems.size()))
            + "\n" + dossier.getFullPathName() + "\n";
    for (const auto& stem : sortie.stems) {
        juce::String ligne;
        if (stem.peakLevel > 1e-9) {
            // LE SIGNE EST ÉCRIT : « crête 0,36 dBFS » se lit comme un niveau
            // SOUS l'échelle pleine, alors que c'est exactement l'inverse qui
            // fait perdre des échantillons.
            const double dbfs = 20.0 * std::log10(static_cast<double>(stem.peakLevel));
            // ET LA LIGNE RESTE VRAIE DU FICHIER QU'ELLE NOMME. C'est la leçon
            // de D49 : annoncer la crête du RENDU en face d'un nom de fichier
            // que le format entier a borné à 0 dBFS, c'est répéter l'intention
            // au lieu de décrire ce qui a été écrit.
            ligne = (stem.peakLevel > 1.0f && entier)
                        ? tr(u8"%1.wav — crête %2 dBFS, bornée à 0 dBFS par ce format")
                        : tr(u8"%1.wav — crête %2 dBFS");
            ligne = ligne.replace("%2", juce::String(dbfs > 0.0 ? "+" : "") + juce::String(dbfs, 2));
        } else {
            ligne = tr(u8"%1.wav — silencieux");
        }
        message += "\n" + ligne.replace("%1", juce::String(stem.name));
    }
    for (const auto& warning : sortie.warnings)
        message += "\n\n" + vsm::app::ui::trPhrase(juce::String(warning));
    return true;
}

bool MainComponent::exportStemsForCapture(const juce::File& dossier,
                                           vsm::audio::io::SampleFormat format,
                                           vsm::interchange::StemGranularity granularite) {
    // L'EXPORT PAR STEMS SE VÉRIFIE SANS SOURIS (D50). Il vit derrière DEUX
    // modales -- une fenêtre d'options, puis un sélecteur de dossier --, que
    // nulle capture ne traverse : son compte rendu était donc invérifiable, et
    // c'est exactement ce que le § « Interface » interdit de laisser. Le
    // chemin est le MÊME que celui du menu ; seule la façon de désigner le
    // dossier change.
    vsm::interchange::RenderOptions options;
    options.blockSize = 512;
    options.tailSeconds = 2.0;
    options.sampleRate = audioEngine_.currentSampleRate() > 0.0 ? audioEngine_.currentSampleRate() : 48000.0;
    options.format = format;
    juce::String message;
    const bool fait = exportStemsToFolder(dossier, options, granularite, message);
    std::fputs((juce::String(fait ? u8"VSM_EXPORT_STEMS : " : u8"VSM_EXPORT_STEMS : ÉCHEC — ")
                + message.replace("\n", " ; ") + "\n").toRawUTF8(), stderr);
    return fait;
}

void MainComponent::exportAudioWithOptions(const vsm::interchange::RenderOptions& options,
                                           ExportLevel niveau) {
    // D20.5 : TROIS FORMATS, ET LE SÉLECTEUR LES DIT. WAV tel quel ; FLAC et
    // Ogg Vorbis par transcodage du même rendu. MP3 n'y est pas : l'encodeur
    // n'est pas dans JUCE, et la règle n° 2 du § 0 interdit une dépendance à
    // télécharger.
    auto chooser = std::make_shared<juce::FileChooser>(
        tr(u8"Exporter en audio (WAV, FLAC ou OGG)..."), juce::File(), "*.wav;*.flac;*.ogg");

    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser, options, niveau](const juce::FileChooser& fc) {
        juce::File file = fc.getResult();
        if (file == juce::File()) return;
        if (file.getFileExtension().isEmpty()) file = file.withFileExtension("wav");
        juce::String message;
        const bool fait = exportProjectToFile(file, options, message, niveau);
        juce::AlertWindow::showMessageBoxAsync(fait ? juce::AlertWindow::InfoIcon : juce::AlertWindow::WarningIcon,
                                                 fait ? tr(u8"Export audio terminé") : tr(u8"Erreur d'export audio"),
                                                 message);
    });
}

bool MainComponent::exportProjectToFile(const juce::File& file, const vsm::interchange::RenderOptions& options,
                                        juce::String& message, ExportLevel niveau) {
    // L'EXPORT PASSE PAR LE MÊME CODE QUE `vsm-render`, et c'est la seule
    // façon d'être sûr qu'il rende la même chose. La version précédente
    // montait son propre graphe : elle y posait les instruments, le projet,
    // l'automation et le master -- mais ni les inserts ni les départs. Le
    // fichier exporté n'avait donc ni la réverbération ni le delay qu'on
    // venait d'entendre, et rien ne le disait. Deux chemins de rendu, c'est
    // deux vérités ; il n'y en a qu'un -- FLAC, OGG et la normalisation ne
    // font que RÉÉCRIRE ce rendu-là, jamais en rendre un autre.
    captureSessionIntoProject();
    const vsm::interchange::LoadedBundle bundle = bundleFromSession();
    const juce::String extension = file.getFileExtension().toLowerCase();
    const bool flac = extension == ".flac";
    const bool ogg = extension == ".ogg";
    const bool reecrire = flac || ogg || niveau != ExportLevel::AsIs;
    const juce::File wav = reecrire ? file.getSiblingFile(file.getFileNameWithoutExtension() + ".vsm-rendu.wav") : file;

    // LE FICHIER INTERMÉDIAIRE EST EN 32 BITS FLOTTANTS, TOUJOURS.
    //
    // Il était écrit dans le FORMAT CIBLE, donc en 24 bits le plus souvent :
    // un rendu dont le pic vaut 1,405 y était borné à 1,0 **avant** que le gain
    // de normalisation ne s'applique. Le remède contre l'écrêtage produisait
    // donc un fichier écrêté, et il manquait sa cible : demandé à -1 dBFS, il
    // sortait à **-3,95 dBFS** — le gain (0,634) appliqué à 1,0 au lieu de
    // 1,405. Le message, lui, annonçait « crête 0,891 », c'est-à-dire ce que le
    // calcul PRÉVOYAIT et non ce que le fichier CONTENAIT.
    //
    // Un intermédiaire n'a aucune raison d'être dans le format cible : il est
    // effacé juste après. Le flottant le porte sans rien perdre, et c'est
    // exactement ce pour quoi il existe (D47).
    vsm::interchange::RenderOptions optionsRendu = options;
    if (reecrire) optionsRendu.format = vsm::audio::io::SampleFormat::Float32;

    const auto rendered = vsm::interchange::renderBundleToWav(
        bundle, wav.getFullPathName().toStdString(), optionsRendu);
    if (!rendered.success) {
        message = vsm::app::ui::trPhrase(juce::String(rendered.error));   // D90
        return false;
    }

    // D21.5 : LE GAIN, MESURÉ SUR LE RENDU ÉCRIT. La crête vient du rendu
    // lui-même ; la sonie, du mesureur du moteur relisant le fichier.
    double gain = 1.0;
    juce::String niveauDit;
    if (niveau == ExportLevel::PeakMinus1) {
        const double crete = static_cast<double>(rendered.peakLevel);
        if (crete > 1e-6) gain = std::pow(10.0, -1.0 / 20.0) / crete;
        niveauDit = tr(u8"crête ramenée à -1 dBFS (%1 dB)")
                        .replace("%1", juce::String(20.0 * std::log10(std::max(1e-9, gain)), 1));
    } else if (niveau == ExportLevel::Lufs14 || niveau == ExportLevel::Lufs23) {
        const double cible = niveau == ExportLevel::Lufs14 ? -14.0 : -23.0;
        const double mesuree = measureLufsOf(wav);
        if (mesuree > -100.0) {
            gain = std::pow(10.0, (cible - mesuree) / 20.0);
            niveauDit = tr(u8"sonie %1 LUFS (mesurée %2, %3 dB)")
                            .replace("%1", juce::String(cible, 0))
                            .replace("%2", juce::String(mesuree, 1))
                            .replace("%3", juce::String(cible - mesuree, 1));
        } else {
            niveauDit = tr(u8"sonie non mesurable (silence) : niveau laissé tel quel");
        }
    }
    if (reecrire) {
        juce::String erreur;
        const bool fait = transcodeRenderedWav(wav, file, options, gain, erreur);
        wav.deleteFile();
        if (!fait) {
            message = erreur;
            return false;
        }
    }

    // Les avertissements du rendu sont MONTRÉS. Un export qui laisse une
    // piste muette ou saute un effet doit le dire au moment où il le fait.
    const bool flottant = options.format == vsm::audio::io::SampleFormat::Float32;
    const juce::String profondeur =
        flac ? tr(options.format == vsm::audio::io::SampleFormat::Int16 ? u8"16 bits" : u8"24 bits")
             : ogg ? tr(u8"Ogg Vorbis, qualité maximale (compression avec perte)")
                   : tr(options.format == vsm::audio::io::SampleFormat::Int16 ? u8"16 bits"
                        : flottant ? u8"32 bits flottants" : u8"24 bits");
    // D90 : un modèle rempli après traduction ; la profondeur, déjà traduite, en dernier.
    message = tr(u8"Rendu écrit :") + "\n" + file.getFullPathName() + "\n\n"
              + tr(u8"%1 s, %2 kHz, %3, crête %4.")
                    .replace("%4", juce::String(static_cast<double>(rendered.peakLevel) * gain, 3))
                    .replace("%1", juce::String(rendered.renderedSeconds, 1))
                    .replace("%2", juce::String(options.sampleRate / 1000.0, 1))
                    .replace("%3", profondeur);
    if (niveauDit.isNotEmpty()) message += "\n" + niveauDit;
    // CE QUI SERA ÉCRÊTÉ EST DIT, ET SEULEMENT QUAND ÇA L'EST.
    //
    // Un rendu dont la crête dépasse 1 tient dans un fichier 32 bits flottants
    // -- c'est sa raison d'être -- mais PAS dans un entier 16 ou 24 bits, qui
    // le bornera. Le message affichait « crête 1,405 » et laissait
    // l'utilisateur en tirer la conséquence tout seul.
    //
    // Trouvé en mesurant qu'un vrai morceau (`children-dream-v7`) rend un pic
    // de 1,405 : dans un fichier entier, 602 échantillons sur 20,5 millions
    // sont rabotés. On le dit, et l'on nomme le remède, qui existe déjà dans
    // ce même menu.
    const double cretePubliee = static_cast<double>(rendered.peakLevel) * gain;
    if (cretePubliee > 1.0 && !flottant)
        message += "\n\n" + tr(u8"ATTENTION : la crête dépasse 0 dBFS (%1 dBFS). Ce format ne peut pas "
                               u8"la porter et l'a bornée. « Niveau : crête à -1 dBFS » à l'export "
                               u8"l'évite, ou un export en 32 bits flottants la conserve.")
                                .replace("%1", juce::String(20.0 * std::log10(cretePubliee), 1));
    if (flac && flottant)
        message += "\n" + tr(u8"FLAC ne porte pas de flottants : le rendu 32 bits a été écrit en 24 bits.");
    for (const auto& warning : rendered.warnings)
        message += "\n" + vsm::app::ui::trPhrase(juce::String(warning));   // D90
    return true;
}

double MainComponent::measureLufsOf(const juce::File& wav) {
    juce::WavAudioFormat formatWav;
    std::unique_ptr<juce::AudioFormatReader> lecteur(formatWav.createReaderFor(new juce::FileInputStream(wav), true));
    if (!lecteur || lecteur->numChannels == 0) return -200.0;
    vsm::audio::dsp::LufsMeter mesureur;
    mesureur.prepare(lecteur->sampleRate);
    juce::AudioBuffer<float> bloc(static_cast<int>(lecteur->numChannels), 8192);
    for (juce::int64 position = 0; position < lecteur->lengthInSamples; position += bloc.getNumSamples()) {
        const int n = static_cast<int>(std::min<juce::int64>(bloc.getNumSamples(), lecteur->lengthInSamples - position));
        lecteur->read(&bloc, 0, n, position, true, true);
        const float* g = bloc.getReadPointer(0);
        const float* d = lecteur->numChannels > 1 ? bloc.getReadPointer(1) : g;
        for (int i = 0; i < n; ++i) mesureur.processStereo(g[i], d[i]);
    }
    return mesureur.integratedLufs();
}

bool MainComponent::transcodeRenderedWav(const juce::File& wav, const juce::File& sortie,
                                         const vsm::interchange::RenderOptions& options, double gain,
                                         juce::String& erreur) {
    // LE WAV RENDU EST RELU PAR JUCE, PUIS RÉÉCRIT PAR SON ENCODEUR. Le
    // décodage des imports vit déjà dans cette couche (WAV, AIFF, FLAC, Ogg,
    // MP3) ; l'encodage y vit aussi, et le moteur garde ses zéro dépendance.
    const juce::String extension = sortie.getFileExtension().toLowerCase();
    const bool flac = extension == ".flac";
    const bool ogg = extension == ".ogg";
    juce::WavAudioFormat formatWav;
    std::unique_ptr<juce::AudioFormatReader> lecteur(
        formatWav.createReaderFor(new juce::FileInputStream(wav), true));
    if (!lecteur) {
        erreur = tr(u8"Le rendu n'a pas pu être relu : %1").replace("%1", wav.getFullPathName());
        return false;
    }
    // Vers un fichier PROVISOIRE quand la sortie est le rendu lui-même (un WAV
    // normalisé) : on ne lit pas un fichier pendant qu'on l'écrase.
    const bool surPlace = sortie == wav;
    const juce::File cible = surPlace ? sortie.getSiblingFile(sortie.getFileNameWithoutExtension() + ".vsm-niveau.wav") : sortie;
    cible.deleteFile();
    std::unique_ptr<juce::FileOutputStream> flux(cible.createOutputStream());
    if (!flux || !flux->openedOk()) {
        erreur = tr(u8"Impossible d'écrire %1").replace("%1", cible.getFullPathName());
        return false;
    }
    std::unique_ptr<juce::AudioFormatWriter> ecrivain;
    const int bitsEntiers = options.format == vsm::audio::io::SampleFormat::Int16 ? 16 : 24;
    if (flac) {
        // FLAC : 16 ou 24 bits, jamais de flottants -- un rendu 32 bits
        // flottants s'écrit en 24, et le compte rendu le dit.
        juce::FlacAudioFormat formatFlac;
        ecrivain.reset(formatFlac.createWriterFor(flux.get(), lecteur->sampleRate,
                                                   lecteur->numChannels, bitsEntiers, {}, 0));
    } else if (ogg) {
        // Ogg Vorbis : la qualité la plus haute que l'encodeur propose. Une
        // compression avec perte n'a pas de « profondeur » ; 16 bits est ce
        // que l'API demande, pas ce que le fichier porte.
        juce::OggVorbisAudioFormat formatOgg;
        const juce::StringArray qualites = formatOgg.getQualityOptions();
        ecrivain.reset(formatOgg.createWriterFor(flux.get(), lecteur->sampleRate,
                                                  lecteur->numChannels, 16, {},
                                                  std::max(0, qualites.size() - 1)));
    } else {
        const int bits = options.format == vsm::audio::io::SampleFormat::Float32 ? 32 : bitsEntiers;
        ecrivain.reset(formatWav.createWriterFor(flux.get(), lecteur->sampleRate,
                                                  lecteur->numChannels, bits, {}, 0));
    }
    if (!ecrivain) {
        erreur = tr(u8"L'encodeur %1 n'a pas pu être créé pour %2")
                     .replace("%1", flac ? "FLAC" : ogg ? "Ogg Vorbis" : "WAV")
                     .replace("%2", cible.getFileName());
        return false;
    }
    flux.release();   // l'écrivain possède le flux et le ferme
    // BLOC PAR BLOC, LE GAIN APPLIQUÉ EN CHEMIN : `writeFromAudioReader` ne
    // sait pas multiplier, et lire tout le fichier en mémoire pour un gain
    // serait un détour de neuf minutes de stéréo.
    juce::AudioBuffer<float> bloc(static_cast<int>(lecteur->numChannels), 8192);
    bool ecrit = true;
    for (juce::int64 position = 0; position < lecteur->lengthInSamples && ecrit; position += bloc.getNumSamples()) {
        const int n = static_cast<int>(std::min<juce::int64>(bloc.getNumSamples(), lecteur->lengthInSamples - position));
        lecteur->read(&bloc, 0, n, position, true, true);
        if (gain != 1.0) bloc.applyGain(0, n, static_cast<float>(gain));
        ecrit = ecrivain->writeFromAudioSampleBuffer(bloc, 0, n);
    }
    ecrivain.reset();
    lecteur.reset();
    if (!ecrit) {
        erreur = tr(u8"L'encodage %1 a échoué.").replace("%1", flac ? "FLAC" : ogg ? "Ogg Vorbis" : "WAV");
        cible.deleteFile();
        return false;
    }
    if (surPlace) {
        wav.deleteFile();
        if (!cible.moveFileTo(sortie)) {
            erreur = tr(u8"Impossible de remplacer %1").replace("%1", sortie.getFullPathName());
            return false;
        }
    }
    return true;
}

bool MainComponent::exportForCapture(const juce::File& file, ExportLevel niveau) {
    vsm::interchange::RenderOptions options;
    options.sampleRate = audioEngine_.currentSampleRate() > 0.0 ? audioEngine_.currentSampleRate() : 48000.0;
    options.format = vsm::audio::io::SampleFormat::Int24;
    juce::String message;
    const bool fait = exportProjectToFile(file, options, message, niveau);
    std::fputs((juce::String(fait ? u8"VSM_EXPORT : " : u8"VSM_EXPORT : ÉCHEC — ") + message.replace("\n", " ; ") + "\n").toRawUTF8(), stderr);
    return fait;
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
    options.dialogTitle = tr(u8"Réglages audio");
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
        tr("Importer un fichier MIDI..."), juce::File(), "*.mid;*.midi");
    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(chooserFlags, [this, chooser](const juce::FileChooser& fc) {
        juce::File file = fc.getResult();
        if (file == juce::File()) return;

        try {
            ParsedFile parsed = MidiFileParser::parseFile(file.getFullPathName().toStdString());
            clearHistory();
            project_ = Project::fromParsedFile(parsed);
            oublierLesMachines();   // D76
            project_.title = file.getFileNameWithoutExtension().toStdString();
            rebuildFromProject();
            pianoRoll_.cadrerSurLesNotes();  // un projet qui arrive se regarde là où sont ses notes
        } catch (const std::exception& e) {
            montrerBoite(juce::AlertWindow::WarningIcon,
                                                     tr("Erreur d'import MIDI"), e.what());
        }
    });
}

void MainComponent::openMidiFileDirect(const juce::File& fichier) {
    if (!fichier.existsAsFile()) {
        std::fputs((juce::String::fromUTF8(u8"Ouvrir MIDI : fichier introuvable — ")
                     + fichier.getFullPathName() + "\n").toRawUTF8(), stderr);
        return;
    }
    try {
        ParsedFile parsed = MidiFileParser::parseFile(fichier.getFullPathName().toStdString());
        clearHistory();
        project_ = Project::fromParsedFile(parsed);
        oublierLesMachines();   // D76
        project_.title = fichier.getFileNameWithoutExtension().toStdString();
        rebuildFromProject();
        pianoRoll_.cadrerSurLesNotes();
        std::fputs((juce::String::fromUTF8(u8"Ouvrir MIDI : ")
                     + juce::String(static_cast<int>(project_.tracks.size()))
                     + juce::String::fromUTF8(u8" piste(s) — ") + fichier.getFileName()
                     + "\n").toRawUTF8(), stderr);
    } catch (const std::exception& e) {
        std::fputs((juce::String::fromUTF8(u8"Ouvrir MIDI : ") + juce::String(e.what())
                     + "\n").toRawUTF8(), stderr);
    }
}

void MainComponent::openProjectBundle() {
    // On choisit un DOSSIER, pas un fichier : un projet VSM est un ensemble
    // (project.json, le MIDI, les presets, les échantillons) et pointer vers
    // l'un de ses fichiers laisserait croire qu'on peut l'ouvrir seul.
    auto chooser = std::make_shared<juce::FileChooser>(
        tr("Ouvrir un dossier de projet VSM..."), juce::File(), "");
    const auto chooserFlags = juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectDirectories;

    chooser->launchAsync(chooserFlags, [this, chooser](const juce::FileChooser& fc) {
        const juce::File folder = fc.getResult();
        if (folder == juce::File()) return;
        loadProjectBundleFromFolder(folder);
    });
}

/// IMPORTER UN PROJET FAIT AILLEURS (docs/CDC-import-daw.md).
///
/// Le sélecteur accepte les trois formats lisibles ET le `.cpr` — non pour le
/// lire, mais pour pouvoir EXPLIQUER. Un musicien qui vient de Cubase cherche
/// son `.cpr` : ne pas l'afficher du tout le laisserait croire que
/// l'application ne l'a pas vu, alors que le message a quelque chose d'utile à
/// lui dire (l'archive de pistes, l'export MIDI).
void MainComponent::importDawProject() {
    auto chooser = std::make_shared<juce::FileChooser>(
        tr(u8"Importer un projet d'un autre DAW..."), juce::File(),
        "*.als;*.flp;*.xml;*.cpr");
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles,
                         [this, chooser](const juce::FileChooser& fc) {
        const juce::File fichier = fc.getResult();
        if (fichier == juce::File()) return;
        applyDawImport(fichier);
    });
}

/// APPLIQUE UN IMPORT, ET MONTRE SON RAPPORT DANS TOUS LES CAS.
///
/// Le rapport n'est pas un journal de mise au point : c'est une partie du
/// résultat (§ 0 du CDC). Un import réussi qui ne dirait pas « ces pistes
/// n'ont aucun instrument » ferait chercher pendant des heures pourquoi le
/// projet est muet.
bool MainComponent::applyDawImport(const juce::File& fichier) {
    vsm::interchange::DawImportResult resultat;
    try {
        resultat = vsm::interchange::importDawProjectFile(fichier.getFullPathName().toStdString());
    } catch (const std::exception& erreur) {
        // LE MESSAGE DU LECTEUR EST MONTRÉ TEL QUEL, et c'est voulu : pour un
        // `.cpr` il nomme les deux chemins praticables, ce qu'aucun « échec de
        // l'import » générique ne ferait.
        // D93 : le message est gardé EN FRANÇAIS (la donnée), traduit à
        // l'affichage, et refait par la bascule de langue.
        clientDuRapport_ = ClientDuRapport::echecImport;
        dernierEchecImport_ = juce::String::fromUTF8(erreur.what());
        afficherEchecImport(true);
        std::fputs((std::string("Import : ") + erreur.what() + "\n").c_str(), stderr);
        return false;
    }

    clearHistory();
    project_ = resultat.project;
    oublierLesMachines();   // D76
    currentProjectFolder_ = juce::File();   // un import n'a pas de dossier à réécrire
    if (auto* window = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent()))
        window->setName(juce::String::fromUTF8("Vintage Synth MIDI Studio -- ")
                        + fichier.getFileNameWithoutExtension());
    rebuildFromProject();
    pianoRoll_.cadrerSurLesNotes();  // un projet qui arrive se regarde là où sont ses notes

    // LE RAPPORT DANS LA FENÊTRE, ET NON DANS UNE ALERTE. Une boîte de message
    // traite ce texte comme une nouvelle qu'on chasse d'un clic ; or il fait
    // partie du résultat et doit rester consultable (Fichier ▸ Voir le dernier
    // rapport d'import). Il est aussi la seule forme que l'autoportrait
    // photographie : VSM_CAPTURE rend le composant de contenu, où une alerte
    // asynchrone n'apparaît pas.
    clientDuRapport_ = ClientDuRapport::importDaw;   // D93
    dernierImport_ = resultat.report;
    importReport_.showReport(dernierImport_);
    // AU TERMINAL AUSSI : un import lancé par VSM_IMPORT se juge depuis le
    // terminal qui l'a lancé, et le rapport doit y être lisible sans image.
    // Le « ! » marque les lignes que le lecteur a étiquetées attention ou
    // perte -- la même gravité que les couleurs du panneau.
    std::fputs(("Import : " + resultat.report.sourceFormat + "\n").c_str(), stderr);
    for (const auto& ligne : resultat.report.lines) {
        const bool grave =
            ligne.gravite != vsm::interchange::DawImportReport::Gravite::info;
        std::fputs(((grave ? "! " : "  ") + ligne.texte + "\n").c_str(), stderr);
    }
    return true;
}

bool MainComponent::importDawProjectForCapture(const juce::File& fichier) {
    return applyDawImport(fichier);
}

void MainComponent::afficherEchecImport(bool montrerLeVolet) {
    importReport_.showFailure(tr(u8"Import impossible"),
                              vsm::app::ui::trPhrase(dernierEchecImport_), montrerLeVolet);
}

void MainComponent::showLastImportReport() {
    importReport_.reopen();
}

/// LE RAPPORT DE RECONSTRUCTION À L'ÉCRAN (§ 4.3 de
/// docs/CDC-detection-multipiste.md). Les densités, le partage d'énergie et
/// les avertissements de fourre-tout existaient dans `rapport.json` et nulle
/// part dans l'application : le musicien ouvrait un projet dont une piste
/// porte 57 % du morceau sans que rien ne le lui dise.
///
/// La lecture se fait sur le JSON BRUT (vsm/interchange/Json.h) et non sur le
/// lecteur typé `loadReconstructionReport` : celui-ci sert l'appariement des
/// confiances note à note ; ici on AFFICHE ce que le rapport sait, champ
/// présent par champ présent — un rapport d'une version antérieure, sans
/// densités, montre simplement moins de lignes.
void MainComponent::showReconstructionReport(bool montrerLeVolet) {
    if (rapportReconstruction_ == juce::File()) return;
    const auto lu = vsm::interchange::parseJson(
        rapportReconstruction_.loadFileAsString().toStdString());
    if (!lu.success) {
        clientDuRapport_ = ClientDuRapport::reconstruction;   // D93
        importReport_.showFailure(tr(u8"Rapport illisible"),
                                  vsm::app::ui::trPhrase(juce::String::fromUTF8(lu.error.c_str())),
                                  montrerLeVolet);
        return;
    }
    const auto& racine = lu.value;
    using Ligne = vsm::app::ui::ImportReportComponent::LigneExterne;
    using Ton = vsm::app::ui::ImportReportComponent::Ton;
    juce::Array<Ligne> lignes;

    // Les BUS de groupe ne sont pas des pistes reconstruites : ce sont les
    // faders communs des pistes qui partagent un stem. On compte ce qui joue.
    int pistesJouees = 0, bus = 0;
    for (const auto& piste : project_.tracks)
        (piste.kind == vsm::sequencer::Track::Kind::Group ? bus : pistesJouees) += 1;
    // D92 : UNE LIGNE, UN MODÈLE -- la phrase entière, remplie après traduction ;
    // l'ordre des mots change d'une langue à l'autre, et un fragment traduit seul
    // ne fait pas une phrase. Les données (noms, machines) se posent en dernier.
    juce::String resume = (bus > 0 ? tr(u8"%1 piste(s) reconstruite(s) sous %2 bus de groupe")
                                   : tr(u8"%1 piste(s) reconstruite(s)"))
                              .replace("%1", juce::String(pistesJouees))
                              .replace("%2", juce::String(bus));
    const double distance = racine["globalDistance"].asNumber(-1.0);
    if (distance >= 0.0)
        resume << tr(u8" · distance globale %1 (0 = identique, 1 = silence)")
                      .replace("%1", juce::String(distance, 4));
    lignes.add({resume, Ton::resume});

    // D53 : CE QUI REND UNE DISTANCE COMPARABLE EST ÉCRIT AVEC ELLE.
    //
    // La règle du projet dit : « deux distances ne se comparent que si
    // métrique, budget, gate et stems sont identiques » -- et l'en-tête de
    // `ReconstructionReport` répète que « les distances v1 et v2 ne se
    // comparent pas ». Cet écran publiait pourtant « 0.2325 » tout nu. Un
    // nombre sans sa métrique invite exactement la comparaison que le projet
    // interdit, et il l'invite d'autant plus qu'il a l'air simple.
    if (distance >= 0.0) {
        const std::string metrique = racine["metric"].asString("");
        const int budget = static_cast<int>(racine["iterations"].asNumber(-1.0));
        juce::String condition;
        if (!metrique.empty() && budget >= 0)
            condition = tr(u8"Ne se compare qu'à une distance de métrique %1 et de budget %2 itération(s)");
        else if (!metrique.empty())
            condition = tr(u8"Ne se compare qu'à une distance de métrique %1");
        else if (budget >= 0)
            condition = tr(u8"Ne se compare qu'à une distance de budget %2 itération(s)");
        if (condition.isNotEmpty())
            lignes.add({condition.replace("%2", juce::String(budget))
                                 .replace("%1", juce::String::fromUTF8(metrique.c_str())),
                        Ton::info});
    }

    // --- Le partage : qui porte le morceau -------------------------------
    const auto& partage = racine["partage"];
    if (partage.isArray() && partage.size() > 0) {
        lignes.add({{}, Ton::info});
        for (const auto& part : partage.elements()) {
            const double pourcent = part["partEnergie"].asNumber(0.0);
            // Le même seuil que le cri de la chaîne : au-delà de la moitié
            // sur un stem, « N pistes » est une description trompeuse.
            const juce::String texte =
                tr(pourcent >= 50.0
                       ? u8"%1 : %2 % de l'énergie du morceau — cette piste porte le morceau à elle seule"
                       : u8"%1 : %2 % de l'énergie du morceau")
                    .replace("%2", juce::String(pourcent, 1))
                    .replace("%1", juce::String::fromUTF8(part["stem"].asString("?").c_str()));
            lignes.add({texte, pourcent >= 50.0 ? Ton::attention : Ton::info});
        }
    }

    // --- Les pistes mélodiques : machine et densité -----------------------
    const auto& stems = racine["stems"];
    if (stems.isArray() && stems.size() > 0) {
        lignes.add({{}, Ton::info});
        // D53 : LA PIRE PISTE EST NOMMÉE, parce que c'est la seule information
        // sur laquelle on agit. `StemReport::distance` existait dans le type,
        // était lue par le lecteur typé, et n'était affichée nulle part : le
        // musicien voyait « distance globale 0,2325 » sans savoir laquelle de
        // ses six pistes la tirait vers le haut. Mesuré sur children-dream-v7 :
        // de 0,1755 (guitar) à 0,2224 (piano), soit 27 % d'écart entre la
        // meilleure et la pire.
        double pire = -1.0;
        for (const auto& stem : stems.elements())
            pire = std::max(pire, stem["distance"].asNumber(-1.0));
        for (const auto& stem : stems.elements()) {
            juce::String texte;
            texte << juce::String::fromUTF8(stem["name"].asString("?").c_str())
                  << juce::String::fromUTF8(" → ")
                  << juce::String::fromUTF8(stem["machine"].asString("?").c_str());
            const double d = stem["distance"].asNumber(-1.0);
            if (d >= 0.0) {
                texte << juce::String::fromUTF8(" · distance ") << juce::String(d, 4);
                // LE GATE SEULEMENT QUAND IL N'EST PAS À 1. Il conditionne la
                // distance au même titre que la métrique (le faire passer de
                // 0,95 à 0,24 sur un violoncelle change la distance d'un
                // facteur 1,6 et INVERSE le classement des machines), mais
                // « gate 1.00 » sur chaque ligne deviendrait un meuble.
                const double gate = stem["gate"].asNumber(-1.0);
                if (gate >= 0.0 && std::abs(gate - 1.0) > 1e-6)
                    texte << juce::String::fromUTF8(" · gate ") << juce::String(gate, 2);
            }
            const std::string profil = stem["profile"].asString("");
            if (!profil.empty())
                texte << juce::String::fromUTF8(" [") << juce::String::fromUTF8(profil.c_str())
                      << juce::String::fromUTF8("]");
            const double poly = stem["polyphonieMoyenne"].asNumber(-1.0);
            const double ambitus = stem["ambitusDemiTons"].asNumber(-1.0);
            bool fourreTout = false;
            if (poly >= 0.0 && ambitus >= 0.0) {
                texte << tr(u8" · polyphonie %1 (max %2) · ambitus %3 demi-tons")
                             .replace("%1", juce::String(poly, 1))
                             .replace("%2", juce::String(static_cast<int>(stem["polyphonieMax"].asNumber(0.0))))
                             .replace("%3", juce::String(static_cast<int>(ambitus)));
                // LES SEUILS DU FOURRE-TOUT, les mêmes que ceux de la chaîne
                // (analyse/analyzer/vsm_reconstruct.py, `stem_fourre_tout`) :
                // au moins 3 notes simultanées en moyenne ET 3 octaves. Le
                // jour où la chaîne publiera le verdict dans le rapport, ce
                // recalcul disparaîtra — c'est noté au § 4.3 du CDC.
                fourreTout = poly >= 3.0 && ambitus >= 36.0;
                if (fourreTout)
                    texte << tr(u8" — PLUSIEURS parties sur une seule piste");
            }
            // La pire piste est marquée -- et seulement s'il y en a plusieurs :
            // sur un seul stem, « la plus loin » ne dit rien.
            const bool laPlusLoin = d >= 0.0 && stems.size() > 1 && std::abs(d - pire) < 1e-12;
            if (laPlusLoin)
                texte << tr(u8" — la plus loin de l'original");
            lignes.add({texte, (fourreTout || laPlusLoin) ? Ton::attention : Ton::info});
        }
    }

    // --- La BATTERIE, qui n'est pas un stem mélodique -------------------
    //
    // Elle manquait à cet écran comme elle a longtemps manqué au rapport :
    // c'est souvent la piste la plus lourde du morceau — 78 % sur *Sky and
    // Sand* — et la seule dont on sache EXACTEMENT combien de parties elle
    // porte, puisque les frappes sont classées par pièce.
    const auto& batterie = racine["drums"];
    if (batterie.isObject()) {
        lignes.add({{}, Ton::info});
        const auto& pieces = batterie["pieces"];
        juce::String tete = tr(u8"Batterie → %1 · %2 pièce(s), %3 frappe(s)")
                                .replace("%2", juce::String(static_cast<int>(pieces.size())))
                                .replace("%3", juce::String(static_cast<int>(batterie["hits"].asNumber(0.0))))
                                .replace("%1", juce::String::fromUTF8(batterie["machine"].asString("?").c_str()));
        // LE DÉCOUPAGE PAR PIÈCE, quand il a eu lieu : sans cette ligne, un
        // projet à huit pistes de batterie ne se distinguerait pas d'un
        // projet à une seule dans ce rapport.
        const auto& decoupe = batterie["splitByPiece"];
        const bool eclatee = decoupe.isArray() && decoupe.size() > 1;
        if (eclatee)
            tete << tr(u8", ÉCLATÉE en %1 pistes").replace("%1", juce::String(static_cast<int>(decoupe.size())));
        lignes.add({tete, Ton::info});
        for (const auto& piece : pieces.elements()) {
            const juce::String ligne =
                "    " + tr(u8"%1 : %2 frappe(s)")
                            .replace("%2", juce::String(static_cast<int>(piece["hits"].asNumber(0.0))))
                            .replace("%1", juce::String::fromUTF8(piece["family"].asString("?").c_str()));
            lignes.add({ligne, Ton::info});
        }
        // Ce que la machine a dû concéder — familles sans voix, toms rabattus
        // sur un clap : le rapport les portait, l'écran les taisait.
        for (const auto& avertissement : batterie["warnings"].elements())
            lignes.add({"    " + vsm::app::ui::trPhrase(juce::String::fromUTF8(avertissement.asString("").c_str())),
                        Ton::perte});
        if (!eclatee && pieces.size() >= 2)
            lignes.add({"    " + tr(u8"%1 parties sur une seule piste — la chaîne sait les séparer "
                                   u8"(--batterie-par-piece)")
                                    .replace("%1", juce::String(static_cast<int>(pieces.size()))),
                        Ton::perte});
    }

    // --- Le verdict du mélange : ce que la chaîne AVOUE -------------------
    // « Le morceau est MEILLEUR sans cette piste » n'existait que dans le
    // fichier. Or c'est la décision que la chaîne refuse de prendre -- couper
    // est humain -- et l'humain est devant cet écran. On dit aussi la
    // machine que le mélange a préférée à celle de l'arbitrage, quand il en
    // a changé : le projet joue cette machine-là, et le rapport doit le dire.
    const auto& verdict = racine["mixVerdict"];
    if (verdict.isArray() && verdict.size() > 0) {
        bool entete = false;
        for (const auto& decision : verdict.elements()) {
            const juce::String piste = juce::String::fromUTF8(decision["track"].asString("?").c_str());
            const double avec = decision["mixDistance"].asNumber(-1.0);
            const double sans = decision["mixDistanceMuted"].asNumber(-1.0);
            const juce::String gardee = juce::String::fromUTF8(decision["kept"].asString("").c_str());
            juce::Array<Ligne> nouvelles;
            if (avec >= 0.0 && sans >= 0.0 && sans < avec - 1e-6)
                nouvelles.add({tr(u8"%1 : le morceau mesuré est MEILLEUR sans cette piste (%2 contre %3) "
                                  u8"— conservée : couper est une décision humaine")
                                   .replace("%2", juce::String(sans, 4))
                                   .replace("%3", juce::String(avec, 4))
                                   .replace("%1", piste),
                               Ton::perte});
            if (gardee.isNotEmpty() && gardee != juce::String::fromUTF8("réglage"))
                // Le libellé gardé est celui de la CHAÎNE (« arbitrage », « machine
                // suivante (…) ») : un nom de choix, pas une donnée -- traduit à l'affichage.
                nouvelles.add({tr(u8"%1 : au mélange, gardé « %2 » plutôt que le réglage de piste")
                                   .replace("%2", vsm::app::ui::trPhrase(gardee))
                                   .replace("%1", piste),
                               Ton::info});
            if (!nouvelles.isEmpty() && !entete) {
                lignes.add({{}, Ton::info});
                lignes.add({tr(u8"Verdict du mélange"), Ton::info});
                entete = true;
            }
            lignes.addArray(nouvelles);
        }
    }

    // --- La réverbération cherchée au mélange (H24, option) ----------------
    // Retenue ou refusée, avec ses chiffres : un projet dont les pistes
    // portent un insert que personne n'a posé à la main doit dire d'où il
    // vient, et un refus chiffré vaut autant qu'un choix.
    const auto& reverb = racine["reverb"];
    if (reverb.isObject()) {
        lignes.add({{}, Ton::info});
        const double temoin = reverb["temoin"].asNumber(-1.0);
        const auto& retenu = reverb["retenu"];
        juce::String texte;
        if (retenu.isObject()) {
            const double taille = retenu["taille"].asNumber(0.0);
            const double dosage = retenu["dosage"].asNumber(0.0);
            double distanceRetenue = -1.0;
            for (const auto& point : reverb["grille"].elements())
                if (std::abs(point["taille"].asNumber(-1.0) - taille) < 1e-9
                    && std::abs(point["dosage"].asNumber(-1.0) - dosage) < 1e-9)
                    distanceRetenue = point["distance"].asNumber(-1.0);
            texte = tr(u8"Réverbération au mélange : RETENUE, pièce %1 à %2 % sur %3 piste(s) mélodique(s)")
                        .replace("%1", juce::String(taille, 1))
                        .replace("%2", juce::String(static_cast<int>(std::lround(dosage * 100.0))))
                        .replace("%3", juce::String(static_cast<int>(reverb["pistes"].size())));
            if (temoin > 0.0 && distanceRetenue >= 0.0)
                texte << juce::String::fromUTF8(" · ") << juce::String(temoin, 4)
                      << juce::String::fromUTF8(" → ") << juce::String(distanceRetenue, 4)
                      << juce::String::fromUTF8(" (")
                      << juce::String((distanceRetenue / temoin - 1.0) * 100.0, 2)
                      << juce::String::fromUTF8(" %)");
            lignes.add({texte, Ton::info});
        } else {
            texte = tr(u8"Réverbération au mélange : aucune — aucun point de la grille ne rapproche "
                       u8"de l'original");
            if (temoin > 0.0)
                texte << tr(u8" (témoin sec %1)").replace("%1", juce::String(temoin, 4));
            lignes.add({texte, Ton::info});
        }
    }

    clientDuRapport_ = ClientDuRapport::reconstruction;   // D93
    importReport_.showLines(
        tr(u8"Rapport de reconstruction"),
        currentProjectFolder_ != juce::File() ? currentProjectFolder_.getFileName()
                                              : juce::String(),
        lignes, montrerLeVolet);
}

/// OUVRIR UN DOSSIER DE PROJET, séparé du sélecteur de fichiers qui le
/// désigne. La séparation n'est pas cosmétique : depuis D9.3, un projet arrive
/// aussi SANS que personne l'ait choisi -- la chaîne de reconstruction vient
/// d'en écrire un, et il doit s'ouvrir exactement comme celui qu'on désigne à
/// la main, presets, échantillons et notes douteuses compris. Deux chemins
/// d'ouverture finiraient par ne plus charger tout à fait la même chose.
void MainComponent::loadProjectBundleFromFolder(const juce::File& folder,
                                                const juce::File& mediaFolder) {
    // LE PROJET ET SES MÉDIAS PEUVENT NE PAS ÊTRE AU MÊME ENDROIT (D10.4). Une
    // sauvegarde automatique ne copie pas les médias -- c'est ce qui la rend
    // écrivable toutes les trente secondes -- et leurs chemins sont restés
    // relatifs au dossier d'origine du projet. Partout ailleurs, les deux
    // coïncident, et c'est le cas par défaut.
    const juce::File medias = mediaFolder == juce::File() ? folder : mediaFolder;
    auto loaded = vsm::interchange::loadProjectBundle(folder.getFullPathName().toStdString());
    if (!loaded.success) {
        montrerBoite(
            juce::AlertWindow::WarningIcon, tr("Projet illisible"),
            vsm::app::ui::trPhrase(juce::String::fromUTF8(loaded.error.c_str())));
        return;
    }

    clearHistory();
    project_ = loaded.bundle.project;
    oublierLesMachines();   // D76
    if (project_.title.empty())
        project_.title = folder.getFileName().toStdString();
    // UNE COULEUR QUASI TRANSPARENTE N'EST PAS UNE COULEUR. La chaîne
    // d'analyse a longtemps écrit ses couleurs en RGBA là où ce fichier lit
    // de l'ARGB : « #06D6A0FF » donnait un alpha de 0x06, et les notes d'une
    // piste sur huit ne se voyaient pas dans le piano roll. La chaîne est
    // corrigée ; les projets déjà écrits, eux, restent -- on les rend
    // opaques à l'ouverture, en gardant leur teinte.
    for (auto& track : project_.tracks) {
        if ((track.colorRgba >> 24) < 0x40u) track.colorRgba |= 0xFF000000u;
        for (auto& clip : track.clips)
            if ((clip.colorRgba >> 24) < 0x40u) clip.colorRgba |= 0xFF000000u;
    }

    // UNE PISTE AVEC DU MATÉRIAU ET SANS CLIP JOUE — « pas de clip = tout le
    // matériau », c'est la sémantique du PlaybackScheduler — mais NE SE VOIT
    // PAS : la vue d'arrangement ne dessine que les clips. Tous les projets
    // écrits par la chaîne de reconstruction arrivaient ainsi : six pistes,
    // arrangement VIDE, et la capture de usandthem-h22b l'a montré. C'est le
    // bug de l'import DAW (poserUnClipSurLeMateriau), par une autre porte.
    // On matérialise la fenêtre implicite : un clip « tout à zéro » est
    // EXACTEMENT le passage que le scheduler fabriquait déjà pour une piste
    // sans clip — le rendu ne change pas d'un échantillon, mais le morceau
    // devient visible et saisissable dans l'arrangement.
    materializeImplicitClips();

    // Ctrl+S réécrira ICI, sans redemander où -- et « ici » est le dossier des
    // MÉDIAS, c'est-à-dire le vrai dossier du projet : réécrire une session
    // récupérée dans sa copie de travail la perdrait au prochain lancement.
    currentProjectFolder_ = medias;
    rememberRecentProject(medias);
    if (auto* window = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent()))
        window->setName("Vintage Synth MIDI Studio -- " + medias.getFileName());
    // rebuildFromProject() assigne les instruments d'après le projet : les
    // machines n'existent donc PAS avant cet appel, et appliquer les
    // presets plus tôt reviendrait à les appliquer à rien.
    //
    // D71 : LES RÉSERVES D'EFFET SE VIDENT JUSTE AVANT, pas après. C'est cet
    // appel qui construit les chaînes et les bus, donc qui les produit ; les
    // vider ensuite les effacerait toutes, et garder celles du projet
    // précédent ferait lire les manques d'un morceau sous le titre d'un autre
    // -- la faute que `rapportReconstruction_` évite dix lignes plus bas.
    reservesEffets_.clear();
    rebuildFromProject();
    pianoRoll_.cadrerSurLesNotes();  // un projet qui arrive se regarde là où sont ses notes
    chargerOriginalDuProjet(folder);

    // --- presets et échantillons, machine par machine --------------------
    juce::StringArray rapport;
    // D75 : LES PISTES QUI NE SONNERONT PAS, DITES AVEC LES MOTS DE
    // `vsm-render`. Le rendu hors ligne du même dossier écrit « aucun
    // instrument, elle restera silencieuse » et « instrument … indisponible » ;
    // l'application taisait la première, et ne disait la seconde que si la
    // piste avait un preset -- la ligne vivait dans la boucle des presets, et
    // une piste à machine inconnue SANS preset s'ouvrait muette sans un mot.
    // Les phrases sortent de la fonction qu'appelle le rendu : deux lecteurs
    // qui écrivent chacun la leur finissent par ne plus dire la même chose.
    for (size_t i = 0; i < project_.tracks.size(); ++i) {
        const auto& piste = project_.tracks[i];
        if (piste.instrumentId.empty()) {
            // Une machine ABSENTE de ce build n'est pas redite ici : le
            // chargement a vidé son identifiant et l'a déjà nommée par sa piste
            // (`loaded.warnings`, plus bas). La dire « aucun instrument » en
            // plus ferait deux lignes pour un manque, dont une fausse.
            if (vsm::interchange::pisteADireSansMachine(loaded.bundle, i))
                rapport.add(juce::String::fromUTF8(
                    vsm::interchange::avertissementSansMachine(i, piste).c_str()));
        } else if (!piste.disabled && audioEngine_.processGraph().trackInstrument(i) == nullptr) {
            rapport.add(juce::String::fromUTF8(
                vsm::interchange::avertissementMachineIndisponible(i, piste.instrumentId).c_str()));
        }
    }
    for (const auto& [index, preset] : loaded.bundle.presetsByTrack) {
        // Un `project.json` peut déclarer un preset pour une piste que le
        // MIDI ne contient pas : le fichier a pu être édité à la main, ou
        // produit par une version antérieure. On l'IGNORE en le disant,
        // plutôt que de lire hors des bornes.
        if (index >= project_.tracks.size()) {
            rapport.add(juce::String(u8"Preset pour une piste inexistante (%#1) : ignoré")
                            .replace("%#1", juce::String(static_cast<int>(index) + 1)));
            continue;
        }
        auto* instrument = audioEngine_.processGraph().trackInstrument(index);
        if (instrument == nullptr) {
            // La machine ABSENTE est déjà dite juste au-dessus, avec les mots du
            // rendu. D76 : ET SON PRESET EST GARDÉ, comme celui d'une piste
            // DÉSACTIVÉE (dont la machine n'est pas instanciée, D30.2) : il sera
            // reposé à la réactivation, et l'enregistrement l'écrira tel quel.
            // Il n'est donc plus une réserve -- D75 écrivait « désactivée,
            // preset non appliqué », ce qui était vrai tant que le preset se
            // perdait au premier Ctrl+S (mesuré : 4 réglages sur 9).
            reglagesGardes_[project_.tracks[index].uid] = preset;
            continue;
        }
        const auto applique = vsm::interchange::applyPreset(
            preset, *instrument, project_.tracks[index].instrumentId);
        if (applique.unsupportedCount() > 0 || applique.clampedCount() > 0)
            rapport.add(juce::String::fromUTF8(
                (vsm::interchange::libellePiste(index) + " : " + applique.summary()).c_str()));

        // Échantillons : chargés ICI, sur le thread de l'interface, et
        // jamais depuis le thread audio -- ce sont des lectures de
        // fichiers. La publication vers le thread audio est atomique,
        // c'est l'affaire de la machine.
        const auto echantillons = vsm::interchange::applyPresetSamples(
            preset, *instrument, loaded.bundle.folderPath);
        if (echantillons.aQuelqueChoseADire())
            rapport.add(juce::String::fromUTF8(
                (vsm::interchange::libellePiste(index) + " : " + echantillons.summary()).c_str()));
    }

    for (const auto& avertissement : loaded.warnings)
        rapport.add(juce::String::fromUTF8(avertissement.c_str()));
    // D71 : et ce que les EFFETS n'ont pas pu poser. Le rendu hors ligne du
    // même dossier le disait déjà ; l'ouverture le taisait.
    for (const auto& reserve : reservesEffets_) rapport.add(reserve);

    // --- rapport de reconstruction, s'il y en a un ------------------------
    //
    // Facultatif : un projet ouvert à la main n'en a pas, et c'est normal.
    // Quand il est là, il porte la confiance de la transcription note par
    // note, et le piano roll marque celles sur lesquelles elle a hésité.
    const juce::File fichierRapport = folder.getChildFile("rapport.json");
    // Retenu pour le menu « Voir le rapport de reconstruction » — et EFFACÉ
    // quand le projet n'en a pas : garder celui du projet précédent ferait
    // lire les densités d'un morceau sous le titre d'un autre.
    rapportReconstruction_ = fichierRapport.existsAsFile() ? fichierRapport : juce::File();
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
                rapport.add(juce::String(u8"%#1 note(s) signalée(s) comme douteuses sur %#2 transcrite(s) : elles sont "
                                         u8"marquées dans le piano roll, et la touche D y mène une par une")
                                .replace("%#1", juce::String(static_cast<int>(douteuses)))
                                .replace("%#2", juce::String(static_cast<int>(marquees))));
            // D53 : LA DISTANCE N'EST PAS AJOUTÉE ICI, ET LA RAISON EST
            // ÉCRITE PLUTÔT QUE TUE. Cette liste alimente la boîte « Projet
            // ouvert, avec des reserves » : une distance n'est pas une
            // réserve, et l'y mettre ferait s'ouvrir une boîte
            // d'avertissement sur CHAQUE reconstruction, y compris les
            // meilleures. Un avertissement qui s'allume toujours devient un
            // meuble qu'on ne lit plus -- le même raisonnement que le
            // compteur de décrochages de D41.3. La distance par stem est donc
            // publiée dans l'écran « Voir le rapport de reconstruction », où
            // on la cherche quand on la cherche.
            // Le projet a changé : le piano roll doit relire les notes.
            pianoRoll_.repaint();
        } else {
            // Un rapport illisible est DIT : le taire laisserait croire
            // que la transcription était sûre partout.
            // D115 : le littéral EST le modèle de D89 -- la donnée ne change pas.
            rapport.add(juce::String(u8"Rapport de reconstruction illisible : %1")
                            .replace("%1", juce::String::fromUTF8(lu.error.c_str())));
        }
    }

    updateSynthRackForSelection();

    // Un projet incomplet s'OUVRE et DIT ce qui lui manque. Le taire
    // donnerait un morceau amputé sans explication -- c'est précisément
    // le genre de panne que ce projet refuse.
    //
    // D72 : DANS LE VOLET DE RAPPORT, PLUS DANS UNE BOÎTE MODALE. Elle était
    // le seul des trois rapports de l'application à ouvrir une `AlertWindow`,
    // et cela coûtait trois choses : le banc ne la photographiait qu'une fois
    // sur sept (D71 -- une course, pas un délai), on ne pouvait ni la faire
    // défiler ni la copier, et « Voir le dernier rapport » ne la retrouvait
    // jamais. Le volet, lui, est dans l'autoportrait, garde ce qu'il a montré
    // et se rouvre.
    // D75 : LE RAPPORT PART AUSSI SUR LA SORTIE D'ERREUR, ligne par ligne,
    // comme `VSM_EFFET` en D71. Comparer ce que disent les deux lecteurs d'un
    // dossier -- l'application et `vsm-render` -- doit être un diff de texte ;
    // lire des phrases sur une capture, c'est comparer des impressions.
    for (const auto& ligne : rapport)
        std::fputs(("VSM_OUVERTURE : " + ligne + "\n").toRawUTF8(), stderr);
    rapportOuverture_ = rapport;                         // D84
    dossierRapportOuverture_ = folder.getFileName();
    if (!rapport.isEmpty()) {
        clientDuRapport_ = ClientDuRapport::ouverture;
        afficherRapportDOuverture(true);
    }
}

void MainComponent::afficherRapportDOuverture(bool montrerLeVolet) {
    if (rapportOuverture_.isEmpty()) return;
    using Rapport = vsm::app::ui::ImportReportComponent;
    using Ton = Rapport::Ton;
    juce::Array<Rapport::LigneExterne> lignes;
    lignes.add({juce::String(rapportOuverture_.size()) + (rapportOuverture_.size() > 1
                                                 ? tr(u8" réserves à l'ouverture")
                                                 : tr(u8" réserve à l'ouverture")),
                Ton::resume});
    for (const auto& ligne : rapportOuverture_) {
        // LE TON SUIT LA CONVENTION DÉJÀ ÉTABLIE PAR LES DEUX AUTRES
        // CLIENTS DU VOLET, et il a fallu la lire pour ne pas l'inverser :
        // `attention` est ROUGE et veut dire « regarde MAINTENANT »,
        // `perte` est AMBRE et veut dire « ceci a été perdu ». Un manque
        // est un fait, pas une alarme -- l'écran de reconstruction range
        // ainsi ses stems perdus en ambre et ses parts anormales en rouge.
        // Une première version peignait l'inverse : les effets non
        // appliqués en ambre discret et l'avertissement le plus anodin en
        // rouge vif. La capture l'a montrée, pas la lecture du code.
        const bool perte = ligne.contains(juce::String::fromUTF8("introuvable"))
                        || ligne.contains(juce::String::fromUTF8("illisible"))
                        || ligne.contains(juce::String::fromUTF8("non appliqué"))
                        || ligne.contains(juce::String::fromUTF8("inconnu"))
                        || ligne.contains(juce::String::fromUTF8("indisponible"))   // D75
                        || ligne.contains(juce::String::fromUTF8("silencieuse"));
        // D89 : le ton est lu sur le français (la donnée) ; ce qui s'affiche est traduit.
        lignes.add({vsm::app::ui::trPhrase(ligne), perte ? Ton::perte : Ton::info});
    }
    importReport_.showLines(tr(u8"Projet ouvert, avec des réserves"),
                             dossierRapportOuverture_, lignes, montrerLeVolet);
}

// --- D9 : reconstruire depuis l'application --------------------------------

void MainComponent::refreshReconstructionChain() {
    // LA DÉTECTION NE LANCE RIEN, et c'est délibéré (voir
    // `interchange/ReconstructionChain.h`) : elle coûte quelques `stat` et ne
    // peut ni échouer ni attendre. Elle peut donc être refaite à volonté.
    const juce::File binaire = juce::File::getSpecialLocation(
        juce::File::currentApplicationFile).getParentDirectory();
    const juce::String designe =
        vsm::app::ui::UiScale::properties().getValue("dossierChaineAnalyse", "");
    reconstructionChain_ = vsm::interchange::ReconstructionChain::locate(
        binaire.getFullPathName().toStdString(), designe.toStdString());
    menuItemsChanged();
}

void MainComponent::chooseChainFolder() {
    // D126 : CE QU'ON FAIT DU DOSSIER, DANS UNE FONCTION -- le sélecteur la
    // rappelle, et le banc aussi (`VSM_FICHIER`, par `prendreLeFichierDeBanc`,
    // D102) : un sélecteur ne se pilote pas sans souris ; ce qu'il rend, si.
    auto suite = [this](const juce::File& dossier) {
        if (dossier == juce::File()) return;
        vsm::app::ui::UiScale::properties().setValue(
            "dossierChaineAnalyse", dossier.getFullPathName());
        vsm::app::ui::UiScale::properties().saveIfNeeded();
        refreshReconstructionChain();
        refreshPreferences();
        // ON DIT TOUT DE SUITE SI ÇA A MARCHÉ. Enregistrer
        // un chemin faux sans rien dire ferait chercher le
        // problème ailleurs.
        // D126 : le titre par tr(), la raison et le remède -- des DONNÉES
        // d'interchange/ -- traduits à l'affichage (D114) ; et la ligne
        // `VSM_BOITE` de `montrerBoite`. Elle restait française en anglais.
        if (!reconstructionChain_.available)
            montrerBoite(
                juce::AlertWindow::InfoIcon, tr(u8"Chaîne d'analyse"),
                vsm::app::ui::trPhrase(juce::String::fromUTF8(reconstructionChain_.reason.c_str()))
                    + "\n\n"
                    + vsm::app::ui::trPhrase(juce::String::fromUTF8(reconstructionChain_.remedy.c_str())));
    };
    if (prendreLeFichierDeBanc(suite)) return;   // D126 : le banc (VSM_FICHIER)
    auto chooser = std::make_shared<juce::FileChooser>(
        tr(u8"Où se trouve le dossier analyse/ de la chaîne ?"),
        juce::File(), "");
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories,
                          [chooser, suite](const juce::FileChooser& fc) { suite(fc.getResult()); });
}

void MainComponent::startReconstruction(const juce::File& audioFile) {
    if (!reconstructionChain_.available) {
        // JAMAIS UNE ERREUR : une explication, et le moyen d'y remédier.
        boiteReconstructionIndisponible();
        return;
    }
    if (reconstructionRunner_.isRunning()) return;

    // LE DOSSIER DE SORTIE EST À CÔTÉ DU MORCEAU, et porte son nom. Le mettre
    // dans un dossier temporaire obligerait à le retrouver ; le mettre dans le
    // dossier du projet ouvert le mêlerait à un projet qui n'a rien à voir.
    juce::File sortie = audioFile.getParentDirectory()
                            .getChildFile(audioFile.getFileNameWithoutExtension()
                                           + "-reconstruction");
    int suffixe = 2;
    while (sortie.exists())
        sortie = audioFile.getParentDirectory().getChildFile(
            audioFile.getFileNameWithoutExtension() + "-reconstruction-" + juce::String(suffixe++));
    reconstructionOutput_ = sortie;
    reconstructionSource_ = audioFile;

    reconstructionPanel_.setSource(audioFile.getFileName());
    if (!reconstructionWindow_) {
        reconstructionWindow_ = std::make_unique<PanelWindow>(
            juce::String::fromUTF8(u8"Reconstruction"), reconstructionPanel_);
        reconstructionWindow_->setDefaultSize(720, 460);
    }
    reconstructionWindow_->setVisible(true);
    reconstructionWindow_->toFront(true);

    reconstructionPanel_.onCancel = [this] { reconstructionRunner_.cancel(); };
    reconstructionPanel_.onClose = [this] {
        if (reconstructionWindow_) reconstructionWindow_->setVisible(false);
    };
    reconstructionRunner_.onProgress = [this](const vsm::app::ReconstructionRunner::Progress& p) {
        reconstructionPanel_.setProgress(p);
    };
    reconstructionRunner_.onFinished = [this](bool succes, juce::File dossier, juce::String raison) {
        menuItemsChanged();
        if (!succes) {
            reconstructionPanel_.setFinished(false, raison);
            return;
        }
        // D9.3 : LE RÉSULTAT ARRIVE COMME UN PROJET OUVERT, pas comme un
        // dossier à retrouver. C'est le même chemin d'ouverture que celui d'un
        // projet désigné à la main -- presets, échantillons et notes douteuses
        // compris --, parce que deux chemins d'ouverture finiraient par ne plus
        // charger tout à fait la même chose.
        reconstructionPanel_.setFinished(
            true, juce::String::fromUTF8(u8"Terminé — le projet est ouvert, l'original en regard"));
        loadProjectBundleFromFolder(dossier);
        // D9.4 : L'ÉCOUTE A/B EST PRÊTE AVANT QU'ON LA DEMANDE. Le moment où la
        // comparaison compte le plus est celui-ci, et l'application sait de
        // quel fichier elle est partie : le lui faire redemander serait une
        // question dont elle a déjà la réponse. Silencieux en cas d'échec :
        // une fenêtre d'erreur par-dessus le projet qui vient de s'ouvrir
        // ferait passer une limite du décodeur pour un échec de la
        // reconstruction.
        setReferenceAudioFile(reconstructionSource_, /*silencieuxSiIllisible=*/true);
    };

    // LA PARITÉ EST UN CHOIX DE TRAVAIL, pas un paramètre d'appel : elle vit
    // dans les préférences et vaut pour toutes les reconstructions à venir.
    // Sans elle, l'application rendait quatre pistes là où la ligne de
    // commande en donnait treize — celui qui glisse son morceau dans la
    // fenêtre n'avait aucun moyen d'atteindre ce que la chaîne sait faire.
    const bool parite = vsm::app::ui::UiScale::properties()
                            .getBoolValue("reconstruireEnParite", true);
    reconstructionRunner_.start(reconstructionChain_, audioFile, sortie, parite);
    if (parite)
        reconstructionPanel_.setSource(tr(u8"%1 — parité des pistes").replace("%1", audioFile.getFileName()));
    menuItemsChanged();
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& f : files) {
        if (vsm::interchange::isReconstructableAudio(f.toStdString())) return true;
        if (f.endsWithIgnoreCase(".mid") || f.endsWithIgnoreCase(".midi")) return true;
    }
    return false;
}

void MainComponent::filesDropped(const juce::StringArray& files, int, int) {
    // UN FICHIER MIDI LÂCHÉ SUR LA FENÊTRE S'IMPORTE DANS LE PROJET (D14.3), à
    // la tête de lecture -- le geste le moins ambigu des deux qu'on peut
    // vouloir, et le seul qui ne perd rien.
    for (const auto& f : files)
        if (f.endsWithIgnoreCase(".mid") || f.endsWithIgnoreCase(".midi")) { importMidiIntoProject(juce::File(f)); return; }
    // D34.3 : TOUS LES FICHIERS AUDIO LÂCHÉS, ET NON LE PREMIER. Une
    // reconstruction rend des dizaines de stems ; n'en retenir qu'un
    // transformait un geste en autant de gestes qu'il y a de fichiers.
    juce::Array<juce::File> audios;
    for (const auto& f : files)
        if (vsm::interchange::isReconstructableAudio(f.toStdString()))
            audios.add(juce::File(f));
    if (audios.isEmpty()) return;

    // ON DEMANDE AVANT DE PARTIR POUR DIX MINUTES. Un fichier lâché sur une
    // fenêtre est un geste ambigu -- on peut vouloir l'écouter, le poser sur
    // une piste, ou le reconstruire --, et lancer d'autorité l'opération la
    // plus longue des trois serait le pire des choix par défaut.
    //
    // D34.3 : ET C'EST POURQUOI LES DEUX RÉPONSES SONT OFFERTES. Ce commentaire
    // nommait les trois choses qu'on peut vouloir depuis D14.3 et n'en
    // proposait qu'une -- la plus longue. « Poser sur une piste » est
    // l'opération qui prend une seconde, et c'est celle qu'on veut le plus
    // souvent quand on lâche douze stems d'un coup.
    pendingDroppedAudio_ = audios[0];
    pendingDroppedAudios_ = audios;

    const juce::String quoi = audios.size() == 1
        ? audios[0].getFileName()
        : tr(u8"%1 fichiers audio").replace("%1", juce::String(audios.size()));

    // LA RECONSTRUCTION N'EST PROPOSÉE QUE SI ELLE EST POSSIBLE, et d'un seul
    // fichier : la chaîne analyse UN morceau, et lui en donner douze ne veut
    // rien dire. Quand elle est hors service, on le dit dans la MÊME boîte au
    // lieu d'une boîte d'erreur qui remplacerait le choix par un refus.
    const bool reconstructible = reconstructionChain_.available && audios.size() == 1;
    juce::String detail = quoi + "\n\n";
    detail += reconstructible
        ? tr(u8"« Poser » crée une piste par fichier, tout de suite.\n"
             u8"« Reconstruire » sépare, transcrit et cherche les machines : "
             u8"plusieurs minutes.")
        : (audios.size() > 1
               ? tr(u8"« Poser » crée une piste par fichier, tout de suite.\n"
                    u8"La reconstruction n'analyse qu'un morceau à la fois : "
                    u8"elle n'est pas proposée pour un lot.")
               : tr(u8"« Poser » crée une piste, tout de suite.\n"
                    u8"Reconstruction indisponible — ")
                     // D124 : des DONNÉES d'interchange/, traduites à l'affichage (D114)
                     + vsm::app::ui::trPhrase(juce::String::fromUTF8(reconstructionChain_.reason.c_str())) + "\n"
                     + vsm::app::ui::trPhrase(juce::String::fromUTF8(reconstructionChain_.remedy.c_str())));

    // D124 : LES DEUX BOÎTES PAR `BoiteLisible` (D121) -- statiques, celles de JUCE
    // mettaient le texte en page avant la largeur, et « minutes. » restait seul
    // sur sa ligne (D119). Mêmes boutons, même ordre, mêmes valeurs rendues que
    // `showOkCancelBox` et `showYesNoCancelBox` (1, 2, 0) ; la ligne `VSM_BOITE`
    // au moment de la demande (D95), comme `montrerBoite`.
    if (!reconstructible) {
        auto* fenetre = new BoiteLisible(tr(u8"Poser sur une piste ?"), detail,
                                         juce::MessageBoxIconType::QuestionIcon);
        fenetre->addButton(tr(u8"Poser"), 1, juce::KeyPress(juce::KeyPress::returnKey));
        fenetre->addButton(vsm::app::ui::trSelon("bouton", u8"Annuler"), 0,
                           juce::KeyPress(juce::KeyPress::escapeKey));
        std::fputs(("VSM_BOITE : " + fenetre->getName() + " : " + detail.replace("\n", " / ")
                    + "\n").toRawUTF8(), stderr);
        fenetre->enterModalState(true, juce::ModalCallbackFunction::create([this](int resultat) {
            if (resultat == 1) placeDroppedAudioOnTracks();
            pendingDroppedAudio_ = juce::File();
            pendingDroppedAudios_.clear();
        }), true);
        return;
    }
    // TROIS BOUTONS quand les deux chemins sont ouverts : « Poser » en premier
    // parce que c'est le geste courant, « Reconstruire » ensuite, « Annuler »
    // au bout.
    auto* fenetre = new BoiteLisible(tr(u8"Que faire de ce fichier ?"), detail,
                                     juce::MessageBoxIconType::QuestionIcon);
    fenetre->addButton(tr(u8"Poser sur une piste"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    fenetre->addButton(tr(u8"Reconstruire"), 2);
    fenetre->addButton(vsm::app::ui::trSelon("bouton", u8"Annuler"), 0,
                       juce::KeyPress(juce::KeyPress::escapeKey));
    std::fputs(("VSM_BOITE : " + fenetre->getName() + " : " + detail.replace("\n", " / ")
                + "\n").toRawUTF8(), stderr);
    fenetre->enterModalState(true, juce::ModalCallbackFunction::create([this](int resultat) {
        if (resultat == 1) placeDroppedAudioOnTracks();
        else if (resultat == 2 && pendingDroppedAudio_ != juce::File())
            startReconstruction(pendingDroppedAudio_);
        pendingDroppedAudio_ = juce::File();
        pendingDroppedAudios_.clear();
    }), true);
}

// --- D10.2 : le MIDI learn se voit, se défait, et se souvient --------------

void MainComponent::loadMidiLearnMappings() {
    const juce::String texte =
        vsm::app::ui::UiScale::properties().getValue("midiLearnMappings", "");
    const auto lu = vsm::interchange::midiLearnFromJson(texte.toStdString());
    if (!lu.success) {
        // ON NE PERD PAS EN SILENCE. Des associations illisibles, c'est un
        // studio recâblé à la main sans savoir pourquoi.
        montrerBoite(
            juce::AlertWindow::WarningIcon,
            tr(u8"Associations MIDI illisibles"),
            vsm::app::ui::trPhrase(juce::String::fromUTF8(lu.error.c_str())));
        return;
    }
    audioEngine_.setMidiLearnMap(lu.map);
    midiLearnSeenCount_ = lu.map.size();
    if (lu.discarded > 0)
        montrerBoite(
            juce::AlertWindow::InfoIcon,
            tr(u8"Associations MIDI"),
            tr(u8"%1 association(s) enregistrée(s) n'ont pas été relues : elles désignent une cible "
               u8"que cette version ne connaît pas. Elles ont été écartées plutôt que devinées.")
                .replace("%1", juce::String(static_cast<int>(lu.discarded))));
    refreshMidiLearnList();
}

void MainComponent::saveMidiLearnMappings() {
    const std::string json = vsm::interchange::midiLearnToJson(audioEngine_.midiLearnMap());
    auto& reglages = vsm::app::ui::UiScale::properties();
    reglages.setValue("midiLearnMappings", juce::String::fromUTF8(json.c_str()));
    reglages.saveIfNeeded();
}

void MainComponent::refreshMidiLearnList() {
    std::vector<vsm::app::ui::MidiLearnWindow::Row> lignes;
    const auto carte = audioEngine_.midiLearnMap();
    for (const auto& entree : carte.entries()) {
        // LE NOM DU PARAMÈTRE VIENT DE LA MACHINE quand elle est là :
        // « paramètre 12 » n'aide personne à retrouver ce qu'il a réglé.
        std::string nomParametre;
        if (entree.target.kind == vsm::audio::engine::MidiLearnKind::InstrumentParam)
            if (auto* machine = audioEngine_.processGraph().trackInstrument(entree.target.trackIndex))
                for (const auto& info : machine->parameterList())
                    if (info.id == entree.target.paramId) { nomParametre = info.name; break; }
        lignes.push_back({static_cast<int>(entree.controller),
                          juce::String::fromUTF8(
                              vsm::interchange::describeMidiLearnTarget(entree.target, nomParametre)
                                  .c_str())});
    }
    midiLearnPanel_.setRows(std::move(lignes));
}

void MainComponent::applyLearnedControls() {
    learnedDrain_.clear();
    if (audioEngine_.drainLearnedControls(learnedDrain_) == 0) return;

    using Kind = vsm::audio::engine::MidiLearnKind;
    bool projetTouche = false;
    for (const auto& commande : learnedDrain_) {
        const auto& cible = commande.target;
        // UNE BASCULE S'APPUIE, UN FADER SE POSITIONNE. Traiter l'un comme
        // l'autre ferait démarrer la lecture au milieu d'une course de
        // potentiomètre. Le seuil est celui du MIDI : 64.
        const bool appui = commande.rawValue >= 64;
        const bool piste = cible.trackIndex < project_.tracks.size();
        switch (cible.kind) {
            case Kind::TrackVolume:
                // D29.3 : PAR LA TRANCHE quand elle existe -- le curseur suit, et la
                // passe d'automation s'ouvre si le W est armé, comme sous la souris.
                if (piste) {
                    if (!mixer_.applyExternalControl(cible.trackIndex, "mix.volume", commande.value))
                        project_.tracks[cible.trackIndex].volume = commande.value;
                    projetTouche = true;
                }
                break;
            case Kind::TrackPan:
                // La plage a été enregistrée AVEC l'association (-1 à +1) :
                // la valeur arrive donc déjà à l'échelle du réglage. La borner
                // reste utile pour un fichier de préférences édité à la main.
                if (piste) {
                    const float pan = juce::jlimit(-1.0f, 1.0f, commande.value);
                    if (!mixer_.applyExternalControl(cible.trackIndex, "mix.pan", pan))
                        project_.tracks[cible.trackIndex].pan = pan;
                    projetTouche = true;
                }
                break;
            case Kind::TrackMute:
                if (piste && appui) {
                    project_.tracks[cible.trackIndex].muted = !project_.tracks[cible.trackIndex].muted;
                    projetTouche = true;
                }
                break;
            case Kind::TrackSolo:
                if (piste && appui) {
                    project_.tracks[cible.trackIndex].solo = !project_.tracks[cible.trackIndex].solo;
                    projetTouche = true;
                }
                break;
            case Kind::TrackSend:
                if (piste && cible.slot < vsm::audio::engine::ProcessGraph::kMaxSends) {
                    project_.tracks[cible.trackIndex].setSendLevel(cible.slot, commande.value);
                    projetTouche = true;
                }
                break;
            case Kind::TransportPlay:
                if (appui) {
                    if (transport_.state() == TransportState::Playing) transport_.stop();
                    else transport_.play();
                }
                break;
            case Kind::TransportStop:  if (appui) transport_.stop(); break;
            case Kind::TransportRecord:
                if (appui) {
                    if (recordPhase_ == RecordPhase::Off) startRecording();
                    else stopRecording();
                }
                break;
            case Kind::TransportLoop:
                if (appui) {
                    const bool actif = !audioEngine_.processGraph().isLoopActive();
                    project_.loopEnabled = actif;
                    transport_.setLoopRegion(project_.loopStartTick, project_.loopEndTick, actif);
                    transportBar_.setLooping(actif);
                }
                break;
            case Kind::InstrumentParam:
                // Appliqué par le thread MIDI lui-même : il ne passe pas ici.
                break;
        }
    }
    if (projetTouche) {
        // REPUBLICATION COALESCÉE, comme pour un geste de souris sur le mixeur :
        // un potentiomètre physique envoie cent messages par seconde, et
        // republier le projet cent fois par seconde reviendrait à reconstruire
        // le planning cent fois pour un fader.
        mixDirty_ = true;
        markProjectDirty();
        // La console doit MONTRER ce qu'un potentiomètre physique vient de
        // faire : un fader qui bouge sans que le sien bouge à l'écran est
        // exactement ce qui fait douter du câblage.
        mixer_.setProject(&project_);
    }
}

// --- D10.4 : sauvegarde automatique et récupération -------------------------

void MainComponent::offerCrashRecovery() {
    if (!autosave_) return;
    auto sessions = autosave_->findInterruptedSessions();
    if (sessions.empty()) return;

    // ON NE DEMANDE PAS « RÉCUPÉRER UNE SESSION ? » : personne ne peut répondre
    // à cette question. On dit lequel, de quand, et ce qu'il contient.
    const auto& reprise = sessions.front();
    const auto maintenant = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const int minutes = static_cast<int>(
        (maintenant - reprise.record.savedAtEpochSeconds) / 60);
    const juce::String titre = reprise.record.title.empty()
        ? tr(u8"(projet sans titre)")
        : juce::String::fromUTF8(reprise.record.title.c_str());
    juce::String quand = minutes <= 0 ? tr(u8"il y a moins d'une minute")
                       : (minutes > 1 ? tr(u8"il y a %1 minutes") : tr(u8"il y a %1 minute"))
                             .replace("%1", juce::String(minutes));

    juce::String message = tr(u8"%1 — %2 piste(s), %3 note(s), enregistré automatiquement %4.")
        .replace("%2", juce::String(reprise.record.trackCount))
        .replace("%3", juce::String(reprise.record.noteCount))
        .replace("%4", quand)
        .replace("%1", titre);
    if (reprise.record.originalFolder.empty())
        message += tr(u8"\n\nCe projet n'avait JAMAIS été enregistré : sans cette copie, il serait perdu.");
    if (sessions.size() > 1)
        message += tr(u8"\n\n(%1 autre(s) session(s) interrompue(s) seront conservées et proposées "
                      u8"au prochain lancement.)")
                       .replace("%1", juce::String(static_cast<int>(sessions.size()) - 1));

    const juce::File dossier = reprise.folder;
    const juce::File origine = reprise.record.originalFolder.empty()
        ? juce::File()
        : juce::File(juce::String::fromUTF8(reprise.record.originalFolder.c_str()));

    demanderOuiNon(
        juce::AlertWindow::QuestionIcon,
        tr(u8"Session interrompue"),
        message, tr(u8"Récupérer"),
        tr(u8"Ignorer et effacer"), this,
        juce::ModalCallbackFunction::create([this, dossier, origine](int resultat) {
            if (resultat != 1) {
                vsm::app::AutosaveService::discard(dossier);
                return;
            }
            // LE PROJET VIENT DE LA COPIE, LES MÉDIAS DE SON DOSSIER D'ORIGINE.
            // La copie ne contient pas les médias -- c'est ce qui la rend
            // écrivable toutes les trente secondes --, et leurs chemins sont
            // restés relatifs au dossier d'origine.
            loadProjectBundleFromFolder(dossier, origine);
            // ET ELLE EST EFFACÉE : elle a servi. La garder la ferait
            // reproposer au prochain lancement, indéfiniment.
            vsm::app::AutosaveService::discard(dossier);
            // Le projet récupéré n'est PAS enregistré : il vient d'une copie
            // de travail. Le marquer sale fait qu'une nouvelle photo part tout
            // de suite, et l'utilisateur garde la main sur le vrai
            // enregistrement.
            markProjectDirty();
        }));
}

void MainComponent::autosaveIfNeeded() {
    // D36.2 : LE PROJET EST SALE DÈS QUE L'HISTORIQUE A BOUGÉ, et cela ne se
    // déclare plus geste par geste.
    //
    // POURQUOI CE RENVERSEMENT. `markProjectDirty` était appelé À LA MAIN, et
    // `beginProjectEdit` le faisait au passage -- mais le piano roll, la lane
    // de vélocité, l'onglet MIDI CC et la piste de tempo n'empruntent pas
    // `beginProjectEdit` : ils poussent leur instantané dans `history_`
    // directement. Leurs éditions -- les trente-deux gestes de notes, c'est-à-
    // dire le coeur du logiciel -- s'annulaient donc parfaitement et n'étaient
    // JAMAIS photographiées : une coupure de courant rendait la copie de
    // secours telle qu'avant la séance, sans un mot.
    //
    // Un pas d'historique EST la preuve qu'on a modifié le projet : le déduire
    // ne peut pas s'oublier, alors que le déclarer s'est oublié quatre fois.
    // `!=` et non `>` : l'annulation aussi modifie le projet, et fait
    // décroître la pile.
    const size_t pasDHistorique = history_.undoDepth();
    if (pasDHistorique != lastAutosaveUndoDepth_) projectDirty_ = true;
    if (!autosave_ || !projectDirty_) return;
    const double maintenant = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    if (maintenant - lastAutosaveSeconds_ < kAutosaveIntervalSeconds) return;
    lastAutosaveSeconds_ = maintenant;
    lastAutosaveUndoDepth_ = pasDHistorique;
    projectDirty_ = false;

    // LA PHOTO EST PRISE ICI, L'ÉCRITURE A LIEU AILLEURS. Capturer les presets
    // demande les machines vivantes, donc le thread de l'interface ; écrire
    // demande le disque, donc surtout pas lui.
    captureSessionIntoProject();
    const std::map<size_t, vsm::interchange::SynthPreset> presets = presetsDeLaSession();   // D76
    autosave_->requestSave(project_, presets, currentProjectFolder_);
}

// --- D10.3 : les raccourcis se lisent et se changent ------------------------

void MainComponent::loadShortcuts() {
    const juce::String texte =
        vsm::app::ui::UiScale::properties().getValue("raccourcis", "");
    if (!vsm::interchange::shortcutTableFromJson(texte.toStdString(), shortcuts_)) {
        // Illisible : on repart des défauts EN LE DISANT. Se retrouver avec les
        // raccourcis d'usine sans savoir pourquoi ferait chercher longtemps.
        montrerBoite(
            juce::AlertWindow::WarningIcon,
            tr(u8"Raccourcis illisibles"),
            tr(u8"Les raccourcis personnalisés n'ont pas pu être relus : ceux d'origine sont rétablis."));
    }
    pianoRoll_.setShortcutTable(&shortcuts_);
    refreshShortcutList();
}

void MainComponent::saveShortcuts() {
    auto& reglages = vsm::app::ui::UiScale::properties();
    reglages.setValue("raccourcis",
                       juce::String::fromUTF8(
                           vsm::interchange::shortcutTableToJson(shortcuts_).c_str()));
    reglages.saveIfNeeded();
}

void MainComponent::refreshShortcutList() { shortcutsPanel_.setTable(&shortcuts_); }

void MainComponent::refreshPreferences() {
    const juce::String designe =
        vsm::app::ui::UiScale::properties().getValue("dossierChaineAnalyse", "");
    preferencesPanel_.refresh(
        vsm::app::ui::UiScale::current(), savedRenderThreadChoice(),
        static_cast<int>(vsm::audio::engine::ProcessGraph::recommendedRenderThreadCount()),
        reconstructionChain_, designe,
        vsm::app::ui::UiScale::properties().getValue("dossierBibliotheque", ""),
        static_cast<int>(vsm::interchange::shortcutCommands().size()),
        static_cast<int>(audioEngine_.midiLearnMappingCount()), retourAuDepart_,
        audioEngine_.processGraph().metronomeLevel(),
        audioEngine_.processGraph().metronomeCountInOnly(),
        audioEngine_.processGraph().metronomeRecordOnly(),
        arrangement_.automationFollowsClips());
}

void MainComponent::showTakeComp() {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;
    if (!takeCompWindow_) {
        takeCompPanel_.onCompose = [this](const std::vector<vsm::sequencer::CompSegment>& troncons) {
            const size_t index = trackList_.selectedTrackIndex();
            if (index >= project_.tracks.size() || troncons.empty()) return;
            uint64_t compteur = project_.peekNextNoteId();
            // On mesure sur une COPIE avant de prendre l'instantané : une
            // composition qui ne produit rien ne doit pas laisser une entrée
            // « Assembler les prises » dans l'historique.
            auto essai = project_.tracks[index];
            if (!vsm::sequencer::applyCompositeTake(essai, troncons, compteur)) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::InfoIcon, tr(u8"Assembler les prises"),
                    tr(u8"Ces tronçons ne prennent aucune note : vérifiez les mesures et les "
                       u8"prises choisies."));
                return;
            }
            beginProjectEdit(u8"Assembler les prises");
            project_.tracks[index] = std::move(essai);
            // D55.2 : LA RECETTE EST POSÉE SUR LA PISTE, en même temps que le
            // matériau qu'elle décrit et dans la MÊME édition annulable --
            // annuler l'assemblage doit rendre les deux, sans quoi la recette
            // décrirait un matériau qui n'est plus là.
            project_.tracks[index].compSegments = troncons;
            project_.ensureNoteIdAbove(compteur - 1);
            rebuildFromProject(false);
            refreshTransportSchedule();
            pianoRollPanel_.refresh();
            arrangement_.repaint();
            refreshHistoryList();
            refreshTakeCompPanel();   // D57 : la prise active est devenue « aucune »
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon, tr(u8"Assembler les prises"),
                tr(u8"%1 notes composées. Les passes sont conservées : on peut "
                   u8"recommencer autrement.")
                    .replace("%1", juce::String(static_cast<int>(project_.tracks[index].notes.size()))));
        };
        takeCompWindow_ = std::make_unique<PanelWindow>(
            juce::String::fromUTF8(u8"Assembler les prises"), takeCompPanel_);
        takeCompWindow_->setDefaultSize(560, 420);
    }
    refreshTakeCompPanel();
    takeCompWindow_->setVisible(true);
    takeCompWindow_->toFront(true);
}

void MainComponent::showPlayOrder() {
    if (!playOrderWindow_) {
        playOrderPanel_.onFlatten = [this](const std::vector<int>& ordre) {
            if (ordre.empty()) return;
            // APLATIR EST LE SEUL MOMENT OÙ L'ORDRE TOUCHE AU MATÉRIAU, et
            // c'est irréversible autrement que par l'annulation : on le dit
            // avant, comme le report de piste (D5.5).
            juce::AlertWindow::showOkCancelBox(
                juce::AlertWindow::QuestionIcon, tr(u8"Aplatir l'ordre de jeu"),
                tr(u8"Les notes, les clips, les courbes et les repères seront réécrits pour "
                   u8"jouer l'ordre demandé (%1 sections). C'est annulable tant que la session "
                   u8"est ouverte, et définitif ensuite.")
                    .replace("%1", juce::String(static_cast<int>(ordre.size()))),
                tr(u8"Aplatir"), vsm::app::ui::trSelon("bouton", u8"Annuler"), nullptr,
                juce::ModalCallbackFunction::create([this, ordre](int choix) {
                    if (choix == 0) return;
                    beginProjectEdit(u8"Aplatir l'ordre de jeu");
                    if (!vsm::sequencer::flattenPlayOrder(project_, ordre)) return;
                    rebuildFromProject(false);
                    refreshTransportSchedule();
                    pianoRollPanel_.refresh();
                    arrangement_.repaint();
                    refreshHistoryList();
                    // LES SECTIONS ONT CHANGÉ : ce sont les repères du projet
                    // aplati, et l'ordre d'avant ne désigne plus rien.
                    playOrderPanel_.setSections(vsm::sequencer::sectionsFromMarkers(project_),
                                                 vsm::sequencer::flattenChangesTempoMeaning(project_));
                }));
        };
        playOrderWindow_ = std::make_unique<PanelWindow>(
            juce::String::fromUTF8(u8"Ordre de jeu"), playOrderPanel_);
        playOrderWindow_->setDefaultSize(520, 420);
    }
    // RELUES À CHAQUE OUVERTURE : les repères ont pu changer depuis la
    // dernière fois, et un panneau qui montrerait les sections d'avant ferait
    // aplatir autre chose que ce qu'il annonce.
    playOrderPanel_.setSections(vsm::sequencer::sectionsFromMarkers(project_),
                                 vsm::sequencer::flattenChangesTempoMeaning(project_));
    playOrderWindow_->setVisible(true);
    playOrderWindow_->toFront(true);
}

void MainComponent::showProjectNotes() {
    if (!projectNotesWindow_) {
        projectNotesEditor_.setMultiLine(true, true);
        projectNotesEditor_.setReturnKeyStartsNewLine(true);
        projectNotesEditor_.setScrollbarsShown(true);
        projectNotesEditor_.setFont(juce::Font(juce::FontOptions(14.0f)));
        projectNotesEditor_.setTextToShowWhenEmpty(
            tr(u8"Ce que la chaîne ne dit pas : pourquoi cette piste vient de ce stem, "
               u8"ce qui est une hypothèse, ce qui est coupé exprès…"),
            vsm::ui::Palette::textSecondary);
        // ÉCRIT DANS LE PROJET À CHAQUE FRAPPE, et marqué modifié : des notes
        // qu'il faudrait penser à valider seraient des notes perdues.
        projectNotesEditor_.onTextChange = [this] {
            project_.notes = projectNotesEditor_.getText().toStdString();
            markProjectDirty();
        };
        projectNotesWindow_ = std::make_unique<PanelWindow>(
            juce::String::fromUTF8(u8"Notes du projet"), projectNotesEditor_);
        projectNotesWindow_->setDefaultSize(520, 380);
    }
    // RELU DEPUIS LE PROJET À CHAQUE OUVERTURE : un autre projet a d'autres
    // notes, et l'éditeur ne doit pas montrer celles du précédent.
    if (projectNotesEditor_.getText().toStdString() != project_.notes)
        projectNotesEditor_.setText(juce::String(project_.notes), juce::dontSendNotification);
    projectNotesWindow_->setVisible(true);
    projectNotesWindow_->toFront(true);
}

void MainComponent::showPreferences() {
    if (!preferencesWindow_) {
        preferencesWindow_ = std::make_unique<PanelWindow>(
            juce::String::fromUTF8(u8"Préférences"), preferencesPanel_);
        // D16.6 : trois rangées de plus (niveau du clic, et les deux « le clic
        // bat »). Une fenêtre restée à sa taille d'avant aurait coupé les
        // Commandes -- et « ça tient dans la case » ne l'emporte jamais sur
        // « ça se lit ».
        preferencesWindow_->setDefaultSize(560, 592);
    }
    refreshPreferences();
    preferencesWindow_->setVisible(true);
    preferencesWindow_->toFront(true);
}

// --- D10.1 : le navigateur --------------------------------------------------

void MainComponent::refreshBrowser() {
    std::vector<vsm::interchange::BrowserItem> entrees;

    // LES MACHINES D'ABORD : c'est ce qu'on cherche le plus souvent, et elles
    // ne coûtent aucune lecture de disque -- le registre les connaît déjà.
    for (const auto& [identifiant, nom] :
         vsm::audio::plugin::PluginRegistry::instance().listAvailable()) {
        vsm::interchange::BrowserItem entree;
        entree.kind = vsm::interchange::BrowserItemKind::Machine;
        entree.name = tr(juce::String::fromUTF8(nom.c_str())).toStdString();   // D103
        entree.reference = identifiant;
        entree.origin = (identifiant.rfind("vsm.", 0) == 0 ? tr("Parc VSM") : tr("Plugin tiers")).toStdString();
        entrees.push_back(std::move(entree));
    }

    // PUIS LES FICHIERS. Le dossier du projet en premier : ses presets sont
    // ceux du morceau ouvert, donc ceux qu'on cherche en priorité.
    if (currentProjectFolder_ != juce::File())
        vsm::interchange::indexFolder(currentProjectFolder_.getFullPathName().toStdString(),
                                       tr("Projet").toStdString(), entrees);
    const juce::String bibliotheque =
        vsm::app::ui::UiScale::properties().getValue("dossierBibliotheque", "");
    if (bibliotheque.isNotEmpty())
        vsm::interchange::indexFolder(bibliotheque.toStdString(), tr(u8"Bibliothèque").toStdString(), entrees);

    browserPanel_.setItems(std::move(entrees));
}

void MainComponent::applyBrowserItem(const vsm::interchange::BrowserItem& item,
                                      size_t trackIndex) {
    if (trackIndex >= project_.tracks.size()) {
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr("Navigateur"),
            tr(u8"Choisissez d'abord une piste."));
        return;
    }
    using Kind = vsm::interchange::BrowserItemKind;
    const juce::String chemin = juce::String::fromUTF8(item.reference.c_str());

    switch (item.kind) {
        case Kind::EffectPreset: {
            // D15.4 : le preset devient un insert de plus sur la piste, réglé
            // comme le fichier le dit ; un type que la fabrique ne sait pas
            // construire est nommé, jamais remplacé.
            const auto lu = vsm::interchange::parseEffectPreset(
                juce::File(chemin).loadFileAsString().toStdString());
            if (!lu.success) {
                montrerBoite(
                    juce::AlertWindow::WarningIcon, tr("Preset d'effet illisible"),
                    vsm::app::ui::trPhrase(juce::String::fromUTF8(lu.error.c_str())));
                return;
            }
            if (!vsm::audio::effect::EffectFactory::create(lu.preset.type)) {
                montrerBoite(
                    juce::AlertWindow::WarningIcon, tr(u8"Preset d'effet non appliqué"),
                    tr(u8"L'effet « %1 » n'est pas disponible.")
                        .replace("%1", juce::String::fromUTF8(lu.preset.type.c_str())));
                return;
            }
            beginProjectEdit(juce::String::fromUTF8(u8"Ajouter un preset d'effet"));
            project_.tracks[trackIndex].effects.push_back(
                vsm::interchange::descriptionFromEffectPreset(lu.preset));
            effectChain_.rebuildFromProject();
            break;
        }
        case Kind::Machine:
            beginProjectEdit(juce::String::fromUTF8(u8"Changer de machine"));
            project_.tracks[trackIndex].instrumentId = item.reference;
            project_.tracks[trackIndex].requestedInstrumentId.clear();   // D76
            audioEngine_.processGraph().setTrackInstrument(trackIndex, item.reference);
            trackList_.refreshTrackRow(trackIndex);
            updateSynthRackForSelection();
            refreshTransportSchedule();
            return;

        case Kind::Preset: {
            const juce::File fichier(chemin);
            const auto lu = vsm::interchange::parseSynthPreset(
                fichier.loadFileAsString().toStdString());
            if (!lu.success) {
                montrerBoite(
                    juce::AlertWindow::WarningIcon, tr("Preset illisible"),
                    vsm::app::ui::trPhrase(juce::String::fromUTF8(lu.error.c_str())));
                return;
            }
            // LE PRESET DIT SA MACHINE, ET ON LA MET SI ELLE MANQUE. Appliquer
            // un preset de TB-303 sur un DX7 réglerait des paramètres qui n'ont
            // pas le même sens, et rien ne dirait pourquoi ça ne sonne pas.
            beginProjectEdit(juce::String::fromUTF8(u8"Appliquer un preset"));
            if (!lu.preset.pluginId.empty()
                && project_.tracks[trackIndex].instrumentId != lu.preset.pluginId) {
                project_.tracks[trackIndex].instrumentId = lu.preset.pluginId;
                project_.tracks[trackIndex].requestedInstrumentId.clear();   // D76
                audioEngine_.processGraph().setTrackInstrument(trackIndex, lu.preset.pluginId);
                trackList_.refreshTrackRow(trackIndex);
            }
            auto* machine = audioEngine_.processGraph().trackInstrument(trackIndex);
            if (machine == nullptr) {
                montrerBoite(
                    juce::AlertWindow::WarningIcon, tr(u8"Preset non appliqué"),
                    tr(u8"La machine « %1 » n'est pas disponible.")
                        .replace("%1", juce::String::fromUTF8(lu.preset.pluginId.c_str())));
                return;
            }
            const auto rapport = vsm::interchange::applyPreset(lu.preset, *machine,
                                                                project_.tracks[trackIndex].instrumentId);
            // D52 : LE RAPPORT DES ÉCHANTILLONS ÉTAIT JETÉ ICI AUSSI, à deux
            // lignes de celui des paramètres qui, lui, était dit. Un
            // échantillon introuvable rend la machine muette sur ces
            // touches-là : c'est exactement ce que la ligne suivante refuse de
            // laisser passer pour un paramètre.
            const auto echantillons = vsm::interchange::applyPresetSamples(
                lu.preset, *machine,
                fichier.getParentDirectory().getFullPathName().toStdString());
            updateSynthRackForSelection();
            refreshTransportSchedule();
            // CE QUI N'A PAS PU ÊTRE APPLIQUÉ EST DIT. Un preset à moitié posé
            // qui se tait donne un son qu'on croit être celui du fichier.
            juce::StringArray reserves;
            if (rapport.unsupportedCount() > 0 || rapport.clampedCount() > 0)
                reserves.add(juce::String::fromUTF8(rapport.summary().c_str()));
            if (echantillons.aQuelqueChoseADire())
                reserves.add(juce::String::fromUTF8(echantillons.summary().c_str()));
            if (!reserves.isEmpty()) {
                montrerBoite(
                    juce::AlertWindow::InfoIcon, tr(u8"Preset appliqué, avec des réserves"),
                    vsm::app::ui::trPhrase(reserves.joinIntoString("\n")));
                std::fputs((juce::String(u8"VSM_PRESET : réserves — ")
                            + reserves.joinIntoString(" ; ") + "\n").toRawUTF8(), stderr);
            }
            return;
        }

        case Kind::Sample:
            // POSER UN ÉCHANTILLON DEMANDE UNE POSITION, et un double-clic n'en
            // porte aucune. On le pose donc au début de la piste choisie, ce
            // qui est la réponse la moins surprenante -- et on rappelle où le
            // geste EXACT se fait, puisqu'il existe désormais.
            if (placeSampleOnTrack(trackIndex, 0, juce::File(chemin)))
                montrerBoite(
                    juce::AlertWindow::InfoIcon, tr("Navigateur"),
                    tr(u8"%1 a été posé au début de la piste.\n\nPour le poser à une mesure précise, "
                       u8"glissez-le sur l'arrangement plutôt que de double-cliquer.")
                        .replace("%1", juce::String::fromUTF8(item.name.c_str())));
            return;

        case Kind::Profile:
            // UN PROFIL MULTI-ÉCHANTILLONS APPARTIENT À UNE MACHINE, pas à une
            // piste ni à une position : c'est `vsm.multisample` qui le charge,
            // depuis sa façade. Le dire vaut mieux que de le faire à moitié --
            // le poser sur une piste qui n'a pas cette machine ne produirait
            // rien, et rien n'expliquerait quoi.
            montrerBoite(
                juce::AlertWindow::InfoIcon,
                tr(u8"Un profil se charge depuis sa machine"),
                tr(u8"%1\n\nUn profil multi-échantillons se charge dans la machine qui l'emploie "
                   u8"(vsm.multisample), depuis le Synth Rack. Le navigateur sert ici à le TROUVER :\n\n%2")
                    .replace("%2", chemin)
                    .replace("%1", juce::String::fromUTF8(item.name.c_str())));
            return;
    }
}

void MainComponent::applyBrowserDrop(size_t trackIndex, const juce::String& description) {
    vsm::interchange::BrowserItemKind kind{};
    juce::String reference;
    if (!vsm::app::ui::BrowserComponent::parseDragDescription(description, kind, reference)) return;
    // ON RETROUVE L'ENTRÉE COMPLÈTE plutôt que de reconstruire un objet à
    // partir de la description : le nom affiché sert aux messages, et
    // l'inventer ici donnerait deux libellés pour la même chose.
    for (const auto& entree : browserPanel_.visibleItems())
        if (entree.kind == kind && entree.reference == reference.toStdString()) {
            applyBrowserItem(entree, trackIndex);
            return;
        }
}

void MainComponent::applyBrowserDropAt(size_t trackIndex, vsm::midi::Tick tick,
                                        const juce::String& description) {
    vsm::interchange::BrowserItemKind kind{};
    juce::String reference;
    if (!vsm::app::ui::BrowserComponent::parseDragDescription(description, kind, reference)) return;

    // UN ÉCHANTILLON EST LE SEUL À AVOIR BESOIN DE LA POSITION. Une machine, un
    // preset, un profil s'appliquent à une piste entière : les faire dépendre
    // de l'endroit où on a lâché laisserait croire qu'ils commencent là.
    if (kind == vsm::interchange::BrowserItemKind::Sample) {
        placeSampleOnTrack(trackIndex, tick, juce::File(reference));
        return;
    }
    applyBrowserDrop(trackIndex, description);
}

bool MainComponent::runBrowserGestureForCapture(const juce::String& geste) {
    // D99 : LE DOUBLE-CLIC ET LE DÉPÔT DU NAVIGATEUR, sans souris. La liste est
    // refaite comme son ouverture la refait, et la référence y est CHERCHÉE : une
    // entrée que le navigateur ne montre pas n'est pas un geste possible, et le
    // banc ne l'invente pas.
    const juce::String quoi = geste.upToFirstOccurrenceOf(":", false, false).trim();
    const juce::String reference = geste.fromFirstOccurrenceOf(":", false, false).trim();
    if (quoi != "double-clic" && quoi != "depot") {
        std::fputs((juce::String("VSM_NAVIGATEUR : geste inconnu : ") + quoi + "\n").toRawUTF8(), stderr);
        return false;
    }
    refreshBrowser();
    const size_t piste = trackList_.selectedTrackIndex();
    for (const auto& entree : browserPanel_.visibleItems()) {
        if (juce::String::fromUTF8(entree.reference.c_str()) != reference) continue;
        std::fputs((juce::String("VSM_NAVIGATEUR : ") + quoi + " " + reference + " sur la piste "
                    + juce::String(static_cast<juce::int64>(piste)) + "\n").toRawUTF8(), stderr);
        if (quoi == "double-clic")
            applyBrowserItem(entree, piste);
        else
            applyBrowserDropAt(piste, 0, vsm::app::ui::BrowserComponent::dragDescriptionFor(entree));
        return true;
    }
    std::fputs((juce::String("VSM_NAVIGATEUR : ") + reference + juce::String(u8" absent du navigateur (")
                + juce::String(static_cast<int>(browserPanel_.visibleItems().size()))
                + juce::String(u8" entrées)\n")).toRawUTF8(), stderr);
    return false;
}

bool MainComponent::placeSampleOnTrack(size_t trackIndex, vsm::midi::Tick tick,
                                        const juce::File& fichier) {
    if (trackIndex >= project_.tracks.size() || !fichier.existsAsFile()) return false;

    // 1. LE PROJET DOIT AVOIR UN DOSSIER. Tous les chemins d'un projet sont
    // RELATIFS au sien, et la lecture refuse même un chemin absolu (D6.4) : un
    // échantillon posé dans un projet jamais enregistré n'aurait nulle part où
    // être écrit, et le projet rouvrirait muet.
    if (currentProjectFolder_ == juce::File()) {
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Projet jamais enregistré"),
            tr(u8"Un échantillon posé sur une piste est COPIÉ dans le dossier du projet : tous "
               u8"les chemins y sont relatifs, et c'est ce qui permet de le rouvrir ailleurs.\n\n"
               u8"Enregistrez le projet, puis reposez le fichier."));
        return false;
    }

    // 2. ON NE PERD PAS DE NOTES EN SILENCE. Une piste MIDI qui porte des notes
    // deviendrait audio, et elles disparaîtraient : c'est peut-être ce qu'on
    // veut, mais ce n'est jamais ce qu'on veut sans le savoir.
    auto& piste = project_.tracks[trackIndex];
    if (piste.kind != Track::Kind::Audio && !piste.notes.empty()) {
        montrerBoite(
            juce::AlertWindow::WarningIcon, tr(u8"Piste déjà occupée"),
            tr(u8"« %1 » porte %2 note(s) : en faire une piste audio les perdrait.\n\n"
               u8"Posez l'échantillon sur une piste vide, ou sur une piste audio.")
                .replace("%2", juce::String(static_cast<int>(piste.notes.size())))
                .replace("%1", juce::String(piste.name)));
        return false;
    }

    // 3. LA COPIE, ET ELLE EST LE CŒUR DE L'AFFAIRE. « Enregistrer, c'est aussi
    // emporter les médias » (D6.4) : un projet qui désignerait un fichier resté
    // dans la bibliothèque de l'utilisateur serait illisible sur une autre
    // machine, et silencieusement incomplet sur celle-ci.
    const juce::File dossierAudio = currentProjectFolder_.getChildFile("audio");
    dossierAudio.createDirectory();
    juce::File destination = dossierAudio.getChildFile(fichier.getFileName());
    // MÊME NOM, MÊME CONTENU : on ne recopie pas. Deux fichiers différents du
    // même nom, en revanche, doivent coexister -- d'où le suffixe.
    if (destination.existsAsFile() && destination.getSize() != fichier.getSize()) {
        int suffixe = 2;
        do {
            destination = dossierAudio.getChildFile(fichier.getFileNameWithoutExtension() + "-"
                                                     + juce::String(suffixe++)
                                                     + fichier.getFileExtension());
        } while (destination.existsAsFile());
    }
    if (!destination.existsAsFile() && !fichier.copyFileTo(destination)) {
        montrerBoite(
            juce::AlertWindow::WarningIcon, tr("Copie impossible"),
            tr("Impossible de copier %1 dans le dossier du projet.").replace("%1", fichier.getFileName()));
        return false;
    }

    // 4. ON LIT LE FICHIER POUR SAVOIR CE QU'IL DURE. Le déclarer d'après ce
    // qu'on croit produirait un clip de la mauvaise longueur, et c'est
    // exactement l'erreur que `loadAudioTracks` corrige déjà en relisant.
    const double sr = audioEngine_.currentSampleRate() > 0.0 ? audioEngine_.currentSampleRate()
                                                              : 48000.0;
    auto lu = vsm::audio::io::loadAudioTrack(destination.getFullPathName().toStdString(), sr);
    if (!lu.success || !lu.source) {
        montrerBoite(
            juce::AlertWindow::WarningIcon, tr(u8"Échantillon illisible"),
            vsm::app::ui::trPhrase(juce::String::fromUTF8(lu.error.c_str())));
        return false;
    }
    const double duree = static_cast<double>(lu.source->frames()) / sr;

    // 5. UNE PISTE, UN FICHIER. Le modèle porte le matériau sur la PISTE et les
    // découpes dans ses clips : poser un second fichier différent sur la même
    // piste remplacerait le premier partout. On le dit plutôt que de le faire.
    //
    // ET ON LE DIT AVANT D'OUVRIR L'ACTION ANNULABLE : un geste refusé ne doit
    // pas laisser une étape dans l'historique. Annuler pour défaire quelque
    // chose qui n'a pas eu lieu défait le geste d'avant.
    const juce::String relatif = "audio/" + destination.getFileName();
    if (!piste.audio.empty() && piste.audio.path != relatif.toStdString()) {
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Piste déjà pourvue"),
            tr(u8"« %1 » joue déjà %2.\n\nUne piste porte UN fichier, découpé en clips : "
               u8"posez celui-ci sur une autre piste.")
                .replace("%2", juce::String(piste.audio.path.c_str()))
                .replace("%1", juce::String(piste.name)));
        return false;
    }

    beginProjectEdit(juce::String::fromUTF8(u8"Poser un échantillon"));
    if (piste.kind != Track::Kind::Audio) {
        piste.kind = Track::Kind::Audio;
        piste.instrumentId.clear();
    }
    piste.audio.path = relatif.toStdString();
    piste.audio.sampleRate = sr;
    piste.audio.frames = lu.source->frames();
    piste.audio.channels = 2;

    vsm::sequencer::Clip clip;
    clip.startTick = std::max<vsm::midi::Tick>(0, tick);
    clip.length = std::max<vsm::midi::Tick>(1, project_.secondsToTicks(
        project_.ticksToSeconds(clip.startTick) + duree) - clip.startTick);
    clip.sourceStartSeconds = 0.0;
    clip.name = destination.getFileNameWithoutExtension().toStdString();
    piste.clips.push_back(clip);

    trackList_.refreshTrackRow(trackIndex);
    loadAudioTracks();
    refreshTransportSchedule();
    arrangement_.repaint();
    return true;
}

void MainComponent::loadReferenceAudio() {
    // Les formats proposés sont ceux que le décodeur sait REELLEMENT lire :
    // la liste vient de lui, elle n'est pas recopiée ici. Proposer un format
    // qu'on refuserait ensuite serait la pire façon de le supporter.
    auto chooser = std::make_shared<juce::FileChooser>(
        tr(u8"Charger l'enregistrement d'origine (%1)...")
            .replace("%1", juce::String(vsm::app::referenceAudioFormatList())),
        juce::File(), vsm::app::referenceAudioFilePatterns());
    const auto chooserFlags = juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles;

    auto suite = [this](const juce::File& file) {
        if (file == juce::File()) return;
        setReferenceAudioFile(file, /*silencieuxSiIllisible=*/false);
    };
    if (prendreLeFichierDeBanc(suite)) return;   // D108 : le banc (VSM_FICHIER)
    chooser->launchAsync(chooserFlags, [chooser, suite](const juce::FileChooser& fc) { suite(fc.getResult()); });
}

/// CHARGER L'ORIGINAL SANS PASSER PAR UN SÉLECTEUR (D9.4).
///
/// L'écoute A/B existait pour un projet qu'on ouvre à la main : on chargeait la
/// reconstruction, puis on allait chercher l'original dans un menu. Or le
/// moment où la comparaison compte le plus est celui où la reconstruction
/// vient de finir -- et c'est précisément le moment où l'application SAIT de
/// quel fichier elle est partie. Le lui faire redemander était une question
/// dont elle avait déjà la réponse.
///
/// `silencieuxSiIllisible` sert à ce cas-là : après une reconstruction réussie,
/// un original qu'on ne sait pas relire ne doit pas ouvrir une fenêtre
/// d'erreur par-dessus le projet qui vient de s'ouvrir. La chaîne, elle, a su
/// le lire -- si le décodeur du DAW n'y arrive pas, c'est une limite du
/// décodeur, pas un échec de la reconstruction.
void MainComponent::setReferenceAudioFile(const juce::File& file, bool silencieuxSiIllisible) {
    {
        // LECTURE ET DÉCODAGE ICI, sur le thread de l'interface. Le tampon est
        // ensuite publié par échange atomique : le thread audio ne fait que
        // lire un pointeur déjà valide.
        auto result = vsm::app::loadReferenceAudioFile(file);
        if (!result.success || result.buffer.empty()) {
            if (!silencieuxSiIllisible)
                montrerBoite(
                    juce::AlertWindow::WarningIcon, tr("Enregistrement illisible"),
                    result.error.isEmpty() ? tr(u8"fichier sans échantillon")
                                           : vsm::app::ui::trPhrase(result.error));
            return;
        }

        publierReference(std::move(result), file, /*activerEcoute=*/true);
    }
}

/// CE QU'ON A CHARGÉ, ÉCRIT QUELQUE PART. Un MP3 décodé, un FLAC et un WAV
/// donnent le même tampon flottant : rien, à l'écoute, ne dit par quel
/// décodeur on est passé ni à quelle fréquence le fichier était. Le menu le
/// rappelle, parce que comparer sans savoir à quoi, c'est comparer pour rien.
/// `activerEcoute` : après une reconstruction ou un chargement demandé, on
/// passe en écoute comparative tout de suite -- charger un original sans
/// l'entendre serait un geste pour rien. À l'ouverture d'un projet à la main,
/// l'original est PRÊT (le bouton s'allume) mais on entend d'abord le projet
/// qu'on vient d'ouvrir.
void MainComponent::publierReference(vsm::app::ReferenceAudioResult&& result, const juce::File& file,
                                     bool activerEcoute) {
    const double duree = static_cast<double>(result.buffer.numFrames())
                       / juce::jmax(1.0, result.buffer.sampleRate);
    referenceDescription_ = file.getFileName() + "  --  " + tr(result.decoder) + ", "
                          + juce::String(result.buffer.sampleRate / 1000.0, 1) + " kHz, "
                          + (result.buffer.isStereo() ? tr(u8"stéréo") : tr("mono")) + ", "
                          + juce::String(static_cast<int>(duree) / 60) + ":"
                          + juce::String(static_cast<int>(duree) % 60).paddedLeft('0', 2);

    auto& reference = audioEngine_.processGraph().referenceTrack();
    reference.setAudio(std::make_shared<const vsm::audio::io::SampleBuffer>(std::move(result.buffer)));
    if (activerEcoute) reference.setMode(vsm::audio::engine::ReferenceTrack::Mode::Mix);
    refreshListeningIndicator();
}

/// L'ORIGINAL D'UN PROJET RECONSTRUIT, CHARGÉ AVEC LUI. L'écoute A/B était
/// prête après une reconstruction lancée depuis l'application (D9.4), mais un
/// projet reconstruit en ligne de commande -- ceux des campagnes -- s'ouvrait
/// sans son original, et il fallait aller le chercher dans un menu alors que
/// le dossier sait d'où il vient : `rapport.json` porte le chemin de la source
/// dans sa provenance, et `comparaison.wav` porte l'original lui-même sur son
/// canal gauche (la reconstruction est à droite). On prend la source si elle
/// existe encore, sinon le canal gauche de la comparaison ; sans les deux, rien
/// -- un projet ouvert à la main n'a pas forcément d'original, et c'est normal.
void MainComponent::chargerOriginalDuProjet(const juce::File& folder) {
    const juce::File fichierRapport = folder.getChildFile("rapport.json");
    if (fichierRapport.existsAsFile()) {
        const auto lu = vsm::interchange::parseJson(fichierRapport.loadFileAsString().toStdString());
        if (lu.success) {
            const std::string source = lu.value["provenance"]["source"].asString("");
            if (!source.empty()) {
                const juce::File fichier(juce::String::fromUTF8(source.c_str()));
                if (fichier.existsAsFile()) {
                    auto result = vsm::app::loadReferenceAudioFile(fichier);
                    if (result.success && !result.buffer.empty()) {
                        publierReference(std::move(result), fichier, /*activerEcoute=*/false);
                        return;
                    }
                }
            }
        }
    }
    const juce::File comparaison = folder.getChildFile("comparaison.wav");
    if (!comparaison.existsAsFile()) return;
    auto result = vsm::app::loadReferenceAudioFile(comparaison);
    if (!result.success || result.buffer.empty() || !result.buffer.isStereo()) return;
    // Gauche = original, droite = reconstruction : on ne garde que l'original.
    result.buffer.right.clear();
    result.decoder = tr(u8"%1 (canal gauche de comparaison.wav)").replace("%1", tr(result.decoder));
    publierReference(std::move(result), comparaison, /*activerEcoute=*/false);
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
        transportBar_.setListening(tr(u8"Écoute A/B : pas d'original"), false, false);
        return;
    }
    switch (reference.mode()) {
        case Mode::Off:  transportBar_.setListening(tr(u8"Écoute : reconstruction"), true, false); break;
        case Mode::Mix:  transportBar_.setListening(tr(u8"Écoute : les deux"), true, true); break;
        case Mode::Solo: transportBar_.setListening(tr(u8"Écoute : original"), true, true); break;
    }
}

// --- D11.6 : projets récents, modèle, plein écran ---------------------------

void MainComponent::rememberRecentProject(const juce::File& folder) {
    if (folder == juce::File() || folder == templateFolder()) return;
    auto liste = recentProjects();
    liste.removeString(folder.getFullPathName());
    liste.insert(0, folder.getFullPathName());
    while (liste.size() > 10) liste.remove(liste.size() - 1);
    // Écrit tout de suite, comme l'échelle : une fin brutale ne doit pas
    // faire perdre la liste.
    vsm::app::ui::UiScale::properties().setValue("projetsRecents", liste.joinIntoString("\n"));
    vsm::app::ui::UiScale::properties().saveIfNeeded();
}

juce::StringArray MainComponent::recentProjects() const {
    juce::StringArray liste;
    liste.addLines(vsm::app::ui::UiScale::properties().getValue("projetsRecents"));
    liste.removeEmptyStrings();
    return liste;
}

juce::File MainComponent::templateFolder() {
    return vsm::app::ui::UiScale::properties().getFile().getParentDirectory().getChildFile("modele-de-projet");
}

void MainComponent::saveAsTemplate() {
    // Le modèle s'écrit là où vivent les préférences, sans toucher au projet
    // courant : son dossier reste le sien, et Ctrl+S continue d'y écrire.
    const juce::File avant = currentProjectFolder_;
    const juce::File dossier = templateFolder();
    dossier.createDirectory();
    const bool ok = writeProjectTo(dossier);
    currentProjectFolder_ = avant;
    if (auto* window = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent()))
        window->setName("Vintage Synth MIDI Studio" + (avant == juce::File() ? juce::String() : " -- " + avant.getFileName()));
    montrerBoite(
        ok ? juce::AlertWindow::InfoIcon : juce::AlertWindow::WarningIcon, tr(u8"Modèle de projet"),
        ok ? tr(u8"Le projet courant est devenu le modèle : Fichier ▸ Nouveau depuis le modèle l'ouvrira, sans chemin, chaque fois.")
           : tr(u8"Le modèle n'a pas pu être écrit dans %1").replace("%1", dossier.getFullPathName()));
}

void MainComponent::newFromTemplate() {
    const juce::File dossier = templateFolder();
    if (!dossier.getChildFile("project.json").existsAsFile()) return;
    loadProjectBundleFromFolder(dossier);
    // Un projet NEUF : pas de chemin, Ctrl+S demandera où. Le modèle ne se
    // réécrit que par « Enregistrer comme modèle ».
    currentProjectFolder_ = juce::File();
    if (auto* window = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent()))
        window->setName(tr(u8"Vintage Synth MIDI Studio -- nouveau projet (depuis le modèle)"));
}

void MainComponent::toggleFullScreen() {
    if (auto* fenetre = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent()))
        fenetre->setFullScreen(!fenetre->isFullScreen());
}

void MainComponent::appliquerCouleurDePiste(size_t index, juce::Colour couleur) {
    if (index >= project_.tracks.size()) return;
    // D38.2 : LA COULEUR SUIT LA SÉLECTION. C'est même le geste où elle sert le
    // plus : on colore un groupe de pistes pour le reconnaître d'un coup d'oeil,
    // et le colorer une par une donne surtout l'occasion d'en manquer une.
    for (size_t i : trackList_.selectedTracks())
        if (i < project_.tracks.size()) project_.tracks[i].colorRgba = couleur.getARGB();
    project_.tracks[index].colorRgba = couleur.getARGB();
    // D37.3 : TROIS REPEINTS, LÀ OÙ IL Y AVAIT DEUX RECONSTRUCTIONS.
    // La couleur est lue au DESSIN par les trois panneaux qui la montrent
    // (`TrackListComponent.cpp:304`, `MixerComponent.cpp:281`, l'arrangement) :
    // aucun ne range sa couleur dans un widget, donc aucun n'a besoin d'être
    // refabriqué. `loadProject` détruisait et recréait toutes les lignes, et
    // `setProject` toutes les tranches -- des dizaines de fois pendant un seul
    // glissé dans le sélecteur de couleur, qui émet un changement par pixel.
    //
    // CE N'EST PAS QU'UNE QUESTION DE COÛT : refabriquer une ligne pendant
    // qu'on s'en sert détruit le widget qui a le focus. La bonne mesure d'un
    // rafraîchissement est ce que le panneau lit, pas ce qu'il contient.
    arrangement_.repaint();
    trackList_.repaint();
    mixer_.repaint();
}

void MainComponent::refreshTrackNamesEverywhere() {
    mixer_.refreshFromTracks();
    updateSynthRackForSelection();   // le grand titre du rack est le nom de la piste
    automation_.refreshTrackNames();
    eventList_.refresh();
    effectChain_.refreshTrackName();  // et NON `rebuildFromProject` : renommer
                                      // une piste ne refabrique pas ses effets
    arrangement_.repaint();          // il lit le nom au dessin : un repaint suffit
}

void MainComponent::refreshHistoryList() {
    if (!historyWindow_ || !historyWindow_->isVisible()) return;
    historyPanel_.setEntries(history_.undoLabels(), history_.redoLabels());
}

void MainComponent::seekAllViews(vsm::midi::Tick tick) {
    transport_.seekToTick(tick);
    audioEngine_.processGraph().seekSeconds(project_.ticksToSeconds(tick));
}

// D11.7 — LE CLAVIER D'ORDINATEUR. La disposition de Live et de tout le
// monde : la rangée du milieu pour les blanches (A S D F G H J K L ;), celle du
// dessus pour les noires (W E T Y U O P). Z et X déplacent l'octave.
bool MainComponent::handleComputerKeyboard(const juce::KeyPress& key) {
    if (!computerKeyboard_ || key.getModifiers().isAnyModifierKeyDown()) return false;
    const juce::juce_wchar c = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
    if (c == 'z' || c == 'x') {
        computerKeyboardOctave_ = juce::jlimit(-3, 3, computerKeyboardOctave_ + (c == 'z' ? -1 : 1));
        return true;
    }
    static const juce::String kBlanches("asdfghjkl;");
    static const juce::String kNoires("wetyuop");
    static const int kDemiTonsBlanches[] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16};
    static const int kDemiTonsNoires[] = {1, 3, 6, 8, 10, 13, 15};
    int demiTons = -1;
    if (const int i = kBlanches.indexOfChar(c); i >= 0) demiTons = kDemiTonsBlanches[i];
    else if (const int j = kNoires.indexOfChar(c); j >= 0) demiTons = kDemiTonsNoires[j];
    if (demiTons < 0) return false;
    const int note = juce::jlimit(0, 127, 60 + 12 * computerKeyboardOctave_ + demiTons);
    // Le clavier RÉPÈTE une touche tenue : la note ne se rejoue pas.
    for (const auto& [code, n] : computerKeysDown_)
        if (code == key.getKeyCode()) return true;
    computerKeysDown_.emplace_back(key.getKeyCode(), static_cast<uint8_t>(note));
    audioEngine_.playComputerKey(static_cast<uint8_t>(note), 100, true);
    return true;
}

bool MainComponent::keyStateChanged(bool, juce::Component*) {
    // JUCE ne dit pas QUELLE touche s'est relâchée : on relit l'état de
    // celles qu'on tient, et l'on éteint les notes des touches disparues.
    bool traite = false;
    for (size_t i = 0; i < computerKeysDown_.size();) {
        if (juce::KeyPress::isKeyCurrentlyDown(computerKeysDown_[i].first)) { ++i; continue; }
        audioEngine_.playComputerKey(computerKeysDown_[i].second, 0, false);
        computerKeysDown_.erase(computerKeysDown_.begin() + static_cast<std::ptrdiff_t>(i));
        traite = true;
    }
    return traite;
}

bool MainComponent::keyPressed(const juce::KeyPress& key, juce::Component*) {
    if (handleComputerKeyboard(key)) return true;
    // EN SAISIE PAS À PAS, Entrée avance sans note et Retour arrière recule :
    // avant la table des raccourcis, parce qu'elles ne sont des commandes que
    // dans ce mode-là.
    if (pianoRoll_.stepInputEnabled() && !key.getModifiers().isAnyModifierKeyDown()) {
        if (key == juce::KeyPress::returnKey) { pianoRoll_.stepInputRest(); return true; }
        if (key == juce::KeyPress::backspaceKey) { pianoRoll_.stepInputBack(); return true; }
    }
    // LA TOUCHE DÉSIGNE UNE COMMANDE, ET LA TABLE FAIT LA CORRESPONDANCE
    // (D10.3). Ce qui était ici -- un test sur `Ctrl+S`, un filtre qui rejetait
    // tout ce qui portait un modificateur, puis deux `case` -- ne disait à
    // personne quelles touches existaient.
    vsm::interchange::ShortcutId commande{};
    if (!vsm::app::ui::lookupShortcut(shortcuts_, key, commande)) return false;

    using Id = vsm::interchange::ShortcutId;
    switch (commande) {
        case Id::FileSave:   saveProject(); return true;
        case Id::FileSaveAs: saveProjectAs(); return true;
        // LA BARRE D'ESPACE LANCE ET ARRÊTE. Elle ne faisait rien, nulle part,
        // alors que c'est le seul raccourci que tout musicien essaie en
        // premier.
        case Id::TransportPlayStop:
            if (transport_.state() == TransportState::Playing) transport_.stop();
            else transport_.play();
            return true;
        // « R » comme référence : la bascule A/B, depuis n'importe quelle
        // fenêtre -- on compare en regardant le piano roll, pas le menu.
        case Id::ReferenceCycle: cycleReferenceMode(); return true;
        // D11.3 — SE REPÉRER EN MUSIQUE : Début, marqueur suivant, précédent.
        // Le marqueur « suivant » est strictement après la tête ; « précédent »
        // strictement avant, avec une noire de tolérance pour qu'un second
        // appui remonte bien au marqueur d'avant et non à celui qu'on vient
        // d'atteindre. Sans marqueur avant, on revient au début.
        case Id::NavGoToStart: seekAllViews(0); return true;
        case Id::NavGoToBar: promptGoToBar(); return true;
        // D24.3 : le transport au clavier, par les boutons de la barre.
        case Id::TransportRecord:
            if (!transportBar_.toggleRecord())
                std::fputs("Enregistrer (F9) : le bouton Rec est gris\u00e9 -- pas de carte son ouverte, ou aucune piste arm\u00e9e\n", stderr);
            return true;
        case Id::TransportLoop: transportBar_.toggleLoop(); return true;
        case Id::TransportMetronome: transportBar_.toggleMetronome(); return true;
        case Id::NavGoToEnd: seekAllViews(project_.secondsToTicks(transport_.endOfSongSeconds())); return true;
        // D25.2 : la piste choisie au clavier.
        case Id::TrackMuteSelected: toggleMuteSelectedTrack(); return true;
        case Id::TrackSoloSelected: toggleSoloSelectedTrack(); return true;
        case Id::NavNextTrack: selectNeighbourTrack(+1); return true;
        case Id::NavPreviousTrack: selectNeighbourTrack(-1); return true;
        // D39.2 : étendre, plutôt que déplacer.
        case Id::TrackExtendNext: trackList_.etendreSelection(+1); return true;
        case Id::TrackExtendPrevious: trackList_.etendreSelection(-1); return true;
        case Id::TrackSelectAll: trackList_.choisirToutesLesPistes(); return true;
        // D28.4 : la tête au début de la sélection -- l'arrangement d'abord, sinon le piano roll.
        // D29.1 / D29.2 : les locateurs à la tête, la tête d'un temps ou d'une mesure.
        case Id::LoopStartAtPlayhead: setLoopBoundaryAtPlayhead(true); return true;
        case Id::LoopEndAtPlayhead: setLoopBoundaryAtPlayhead(false); return true;
        case Id::NavNextBeat: seekByBeats(+1); return true;
        case Id::NavPreviousBeat: seekByBeats(-1); return true;
        case Id::NavNextBar: seekByBars(+1); return true;
        case Id::NavPreviousBar: seekByBars(-1); return true;
        case Id::NavToSelection: {
            vsm::midi::Tick debut = 0;
            if (arrangement_.selectionStartTick(debut) || pianoRoll_.selectionStartTick(debut)) seekAllViews(debut);
            return true;
        }
        // D79 : ANNULER ET RÉTABLIR, QUELLE QUE SOIT LA ZONE QUI A LE FOCUS. Le
        // piano roll traitait ces deux touches lui-même, et elles n'arrivaient
        // ici que quand il n'avait PAS le focus -- pour y tomber dans le
        // `default` et ne rien faire, sans un mot. Même chemin que le menu
        // Édition, le bouton de la barre et la fenêtre d'historique : une
        // annulation à deux chemins finirait par ne pas annuler la même chose.
        // Quand le piano roll a le focus, il consomme la touche avant nous :
        // elle n'annule pas deux fois.
        case Id::EditUndo: pianoRoll_.undo(); return true;
        case Id::EditRedo: pianoRoll_.redo(); return true;
        case Id::EditInsertTimeAtLocators: editTimeAtLocators(true); return true;
        case Id::EditLocatorsFromSelection: locatorsFromSelection(); return true;
        // AJUSTER À LA FENÊTRE vaut pour les DEUX vues (D14.2) : l'arrangement
        // ne l'entendait pas, seul le piano roll répondait.
        case Id::ViewZoomToFit: arrangement_.zoomToFit(); pianoRoll_.zoomToFit(); return true;
        case Id::EditDeleteTimeAtLocators: editTimeAtLocators(false); return true;
        case Id::ViewFullScreen: toggleFullScreen(); return true;
        case Id::NavNextMarker: {
            const auto ici = transport_.currentTick();
            vsm::midi::Tick cible = -1;
            for (const auto& m : project_.markers)
                if (m.tick > ici && (cible < 0 || m.tick < cible)) cible = m.tick;
            if (cible >= 0) seekAllViews(cible);
            return true;
        }
        case Id::NavPreviousMarker: {
            const auto ici = transport_.currentTick() - project_.ticksPerQuarterNote;
            vsm::midi::Tick cible = 0;
            for (const auto& m : project_.markers)
                if (m.tick < ici && m.tick > cible) cible = m.tick;
            seekAllViews(cible);
            return true;
        }
        // Tout le reste appartient au piano roll, qui a sa propre table --
        // la MÊME. On répond faux pour que la touche lui parvienne.
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
        if (lane.points().empty()) continue;

        // LE NOM DE CE QUI EST PILOTÉ. Voir `vsm::sequencer::AutomationCurve`
        // pour les conventions ; ici on les APPLIQUE, et une cible qu'on ne
        // saurait pas nommer n'est pas écrite plutôt que d'être écrite sous un
        // numéro qui désignerait autre chose à la relecture.
        std::string nom;
        using Cible = vsm::audio::engine::AutomationTarget;
        switch (lane.target) {
            case Cible::TrackVolume: nom = "mix.volume"; break;
            case Cible::TrackPan:    nom = "mix.pan"; break;
            case Cible::TrackSend:   nom = "mix.send." + std::to_string(lane.targetSlot + 1); break;
            case Cible::MasterParam: {
                const auto& liste = audioEngine_.processGraph().masterBus().parameterList();
                for (const auto& info : liste)
                    if (info.id == lane.targetParam) nom = "master." + info.name;
                break;
            }
            case Cible::InsertParam: {
                if (lane.targetTrackIndex >= project_.tracks.size()) break;
                const auto& inserts = project_.tracks[lane.targetTrackIndex].effects;
                if (lane.targetSlot >= inserts.size()) break;
                auto fx = vsm::audio::effect::EffectFactory::create(inserts[lane.targetSlot].type);
                if (!fx) break;
                const auto profil = vsm::interchange::buildSemanticProfile(
                    vsm::interchange::effectSemanticPluginId(inserts[lane.targetSlot].type));
                const auto* d = profil.findByParamId(lane.targetParam);
                if (d == nullptr || d->semanticId.empty()) break;
                nom = "insert." + std::to_string(lane.targetSlot + 1) + "." + d->semanticId;
                break;
            }
            case Cible::InstrumentParam: {
                if (lane.targetTrackIndex >= project_.tracks.size()) break;
                const auto& track = project_.tracks[lane.targetTrackIndex];
                if (track.instrumentId.empty()) break;
                const auto profil = vsm::interchange::buildSemanticProfile(track.instrumentId);
                const auto* d = profil.findByParamId(lane.targetParam);
                // Sans identité sémantique, la courbe ne serait écrite que sous
                // un NUMÉRO : une position dans une liste, qui désignerait un
                // autre réglage dès qu'un paramètre serait intercalé.
                if (d != nullptr && !d->semanticId.empty()) nom = d->semanticId;
                break;
            }
        }
        if (nom.empty()) continue;

        // LA COURBE SE RANGE DANS UNE PISTE, faute d'endroit qui n'appartienne
        // à personne : celle qu'elle vise, ou la première pour le master. Le
        // préfixe `master.` suffit à dire qu'elle ne concerne pas cette piste.
        const size_t rangement = lane.target == Cible::MasterParam
                                     ? 0
                                     : lane.targetTrackIndex;
        if (rangement >= project_.tracks.size()) continue;

        vsm::sequencer::AutomationCurve curve;
        curve.parameter = nom;
        for (const auto& point : lane.points())
            // D17.7 : LA COURBURE FAIT L'ALLER-RETOUR. Ce chemin-ci refait les
            // courbes du projet à partir des voies du moteur ; il oubliait la
            // courbure, qui repartait donc à zéro à chaque republication --
            // dessinée droite alors que le fichier la disait courbe, et
            // ÉCRASÉE à la sauvegarde suivante. Trouvé en regardant l'écran :
            // les tests de `core/`, du moteur et du format étaient tous verts,
            // et aucun ne traverse ce point de passage.
            curve.points.push_back({point.tick, point.value,
                                     point.curveToNext == vsm::audio::engine::AutomationCurve::Step,
                                     point.bend});
        project_.tracks[rangement].automation.push_back(std::move(curve));
    }
}

void MainComponent::applyAutomationFromProject() {
    currentAutomation_.clear();
    for (size_t i = 0; i < project_.tracks.size(); ++i) {
        const auto& track = project_.tracks[i];
        for (const auto& curve : track.automation) {
            if (curve.points.empty()) continue;
            vsm::audio::engine::AutomationLane lane;
            lane.targetTrackIndex = i;
            using Cible = vsm::audio::engine::AutomationTarget;

            // LA RÉSOLUTION DU NOM. Chaque préfixe désigne une famille ; sans
            // préfixe connu, c'est un réglage de la machine de la piste, ce qui
            // fait que les projets d'avant D4.6 se relisent inchangés.
            bool resolue = false;
            if (curve.parameter == "mix.volume") {
                lane.target = Cible::TrackVolume;
                resolue = true;
            } else if (curve.parameter == "mix.pan") {
                lane.target = Cible::TrackPan;
                resolue = true;
            } else if (curve.parameter == "mix.trim") {   // D30.4
                lane.target = Cible::TrackTrim;
                resolue = true;
            } else if (curve.parameter.rfind("mix.send.", 0) == 0) {
                const int numero = std::atoi(curve.parameter.substr(9).c_str());
                if (numero >= 1 && numero <= static_cast<int>(
                        vsm::audio::engine::ProcessGraph::kMaxSends)) {
                    lane.target = Cible::TrackSend;
                    lane.targetSlot = static_cast<size_t>(numero - 1);
                    resolue = true;
                }
            } else if (curve.parameter.rfind("master.", 0) == 0) {
                const std::string nom = curve.parameter.substr(7);
                for (const auto& info : audioEngine_.processGraph().masterBus().parameterList())
                    if (info.name == nom) {
                        lane.target = Cible::MasterParam;
                        lane.targetParam = info.id;
                        resolue = true;
                    }
            } else if (curve.parameter.rfind("insert.", 0) == 0) {
                const size_t point = curve.parameter.find('.', 7);
                if (point != std::string::npos) {
                    const int numero = std::atoi(curve.parameter.substr(7, point - 7).c_str());
                    const std::string semantique = curve.parameter.substr(point + 1);
                    const size_t slot = numero >= 1 ? static_cast<size_t>(numero - 1) : 0;
                    if (numero >= 1 && slot < track.effects.size()) {
                        const auto profil = vsm::interchange::buildSemanticProfile(
                            vsm::interchange::effectSemanticPluginId(track.effects[slot].type));
                        const auto* d = profil.findBySemanticId(semantique);
                        if (d != nullptr) {
                            lane.target = Cible::InsertParam;
                            lane.targetSlot = slot;
                            lane.targetParam = d->paramId;
                            resolue = true;
                        }
                    }
                }
            } else if (!track.instrumentId.empty()) {
                const auto profil = vsm::interchange::buildSemanticProfile(track.instrumentId);
                const auto* d = profil.findBySemanticId(curve.parameter);
                if (d != nullptr) {
                    lane.target = Cible::InstrumentParam;
                    lane.targetParam = d->paramId;
                    resolue = true;
                }
            }
            // UNE COURBE QU'ON NE SAIT PAS RÉSOUDRE EST LAISSÉE DANS LE PROJET
            // et simplement pas jouée : elle vise une machine absente, un
            // insert retiré ou une version différente. La supprimer ferait
            // perdre le travail de l'utilisateur à la première ouverture.
            if (!resolue) continue;

            for (const auto& point : curve.points)
                lane.addPoint(point.tick, point.value,
                               point.step ? vsm::audio::engine::AutomationCurve::Step
                                          : vsm::audio::engine::AutomationCurve::Linear,
                               point.curve);
            currentAutomation_.push_back(std::move(lane));
        }
    }
    audioEngine_.processGraph().setAutomationLanes(currentAutomation_);
    automation_.setProject(&project_);
    midiCc_.setProject(&project_);
    eventList_.setProject(&project_);        // D32.2
    tempoLane_.setProject(&project_);
}

bool MainComponent::writeProjectTo(const juce::File& folder) {
    captureSessionIntoProject();

    // Les presets sont capturés depuis les machines VIVANTES : sans cela,
    // `saveProjectBundle` retombe sur l'état PAR DÉFAUT de chaque machine et
    // écrit un projet qui ne sonne pas comme celui qu'on vient de régler.
    // D76 : AVEC LES ÉCHANTILLONS, ET AVEC CE QUE LES MACHINES ABSENTES OU
    // LIBÉRÉES NE PEUVENT PLUS DIRE (`presetsDeLaSession`). Mesuré avant : une
    // piste désactivée s'écrivait avec le réglage d'usine, une machine absente
    // disparaissait du fichier, et le sampler de `sky-v4` perdait sa voix.
    const std::map<size_t, vsm::interchange::SynthPreset> presets = presetsDeLaSession();

    // D6.4 : ENREGISTRER, C'EST AUSSI EMPORTER LES MÉDIAS. `saveProjectBundle`
    // n'écrit que le projet, le MIDI et les presets. Enregistrer SOUS un autre
    // dossier produisait donc un `project.json` qui désignait des fichiers
    // restés dans l'ancien : illisible sur une autre machine, et silencieusement
    // incomplet sur celle-ci. Sur place, la copie se reconnaît et ne fait rien.
    vsm::interchange::LoadedBundle aEcrire;
    aEcrire.project = project_;
    aEcrire.document = vsm::interchange::documentFromProject(project_);
    aEcrire.folderPath = currentProjectFolder_ == juce::File()
                             ? std::string()
                             : currentProjectFolder_.getFullPathName().toStdString();
    aEcrire.presetsByTrack = presets;

    const auto result = vsm::interchange::exportStandaloneProject(
        aEcrire, folder.getFullPathName().toStdString());
    if (!result.success) {
        montrerBoite(juce::AlertWindow::WarningIcon,
                                                tr("Enregistrement impossible"), vsm::app::ui::trPhrase(juce::String(result.error)));
        return false;
    }
    // CE QUI MANQUE EST DIT AU MOMENT OÙ ON ENREGISTRE, pas découvert en
    // rouvrant le projet ailleurs.
    if (!result.missing.empty()) {
        juce::String message = tr(u8"Le projet est enregistré, mais ces fichiers qu'il désigne "
                                     u8"sont introuvables :") + "\n";
        for (const auto& manquant : result.missing) message += "\n" + juce::String(manquant);
        montrerBoite(juce::AlertWindow::WarningIcon,
                                                 tr(u8"Projet incomplet"), message);
    }
    currentProjectFolder_ = folder;
    rememberRecentProject(folder);
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
        tr("Enregistrer le projet VSM (dossier)..."), currentProjectFolder_);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode
                             | juce::FileBrowserComponent::canSelectDirectories,
                          [this, chooser](const juce::FileChooser& fc) {
        const juce::File folder = fc.getResult();
        if (folder == juce::File()) return;
        folder.createDirectory();
        writeProjectTo(folder);
    });
}

void MainComponent::chooseMidiToImport() {
    auto chooser = std::make_shared<juce::FileChooser>(
        u8"Importer un MIDI dans le projet...", juce::File(), "*.mid;*.midi");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this, chooser](const juce::FileChooser& fc) {
                             const juce::File file = fc.getResult();
                             if (file != juce::File()) importMidiIntoProject(file);
                         });
}

void MainComponent::importMidiIntoProject(const juce::File& file) {
    try {
        ParsedFile parsed = MidiFileParser::parseFile(file.getFullPathName().toStdString());
        const Project source = Project::fromParsedFile(parsed);
        beginProjectEdit(u8"Importer un MIDI");
        const auto bilan = vsm::sequencer::appendTracksFrom(project_, source, transport_.currentTick());
        rebuildFromProject();
        if (!project_.tracks.empty()) trackList_.selectTrackIndex(project_.tracks.size() - 1);
        // CE QUI EST IGNORÉ EST DIT : le tempo et les mesures du fichier.
        if (bilan.tempoChangesIgnored > 0 || bilan.timeSignaturesIgnored > 0)
            montrerBoite(
                juce::AlertWindow::InfoIcon, tr(u8"MIDI importé"),
                tr(u8"%1 piste(s) ajoutée(s) à la tête de lecture. Le tempo et les mesures du fichier ont été "
                   u8"ignorés (%2 changement(s)) : le projet garde les siens.")
                    .replace("%1", juce::String(static_cast<int>(bilan.tracksAdded)))
                    .replace("%2", juce::String(static_cast<int>(bilan.tempoChangesIgnored + bilan.timeSignaturesIgnored))));
    } catch (const std::exception& e) {
        montrerBoite(juce::AlertWindow::WarningIcon,
                                                 tr(u8"Erreur d'import MIDI"), e.what());
    }
}

void MainComponent::setLoopRegionEverywhere(vsm::midi::Tick start, vsm::midi::Tick end, bool active) {
    if (end <= start) return;
    project_.loopEnabled = active;
    project_.loopStartTick = start;
    project_.loopEndTick = end;
    transport_.setLoopRegion(start, end, active);
    audioEngine_.processGraph().setLoopRegion(project_.ticksToSeconds(start),
                                               project_.ticksToSeconds(end), active);
    pianoRoll_.setLoopRegion(start, end, active);
    pianoRollPanel_.refresh();
    transportBar_.setLooping(active);
    arrangement_.repaint();
}

void MainComponent::locatorsFromSelection() {
    vsm::midi::Tick debut = 0, fin = 0;
    bool trouve = arrangement_.selectionBounds(debut, fin);
    if (!trouve) {
        // À défaut de clips : les notes choisies du piano roll.
        if (const auto* track = pianoRoll_.activeTrack()) {
            for (const auto& n : track->notes) {
                if (pianoRoll_.selectedNoteIds().count(n.id) == 0) continue;
                if (!trouve) { debut = n.startTick; fin = n.endTick; trouve = true; }
                else { debut = std::min(debut, n.startTick); fin = std::max(fin, n.endTick); }
            }
        }
    }
    if (!trouve || fin <= debut) return;
    beginProjectEdit(u8"Locateurs sur la sélection");
    setLoopRegionEverywhere(debut, fin, true);
}

void MainComponent::editTimeAtLocators(bool inserer) {
    const auto de = project_.loopStartTick;
    const auto a = project_.loopEndTick;
    if (a <= de) {
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Locateurs"),
            tr(u8"Placez d'abord les locateurs : la région de boucle est la plage à insérer ou à supprimer."));
        return;
    }
    beginProjectEdit(inserer ? u8"Insérer du silence" : u8"Supprimer une plage de temps");
    const auto conversion = [this](vsm::midi::Tick t) { return project_.ticksToSeconds(t); };
    const size_t touches = inserer ? vsm::sequencer::insertTime(project_, de, a - de, conversion)
                                   : vsm::sequencer::deleteTime(project_, de, a, conversion);
    // TOUT CE QUI LIT LE PROJET SE RAFRAÎCHIT : le transport (les notes et le
    // tempo ont bougé), les pistes audio (les clips aussi), et les vues.
    refreshTransportSchedule();
    loadAudioTracks();
    arrangement_.repaint();
    pianoRoll_.repaint();
    juce::ignoreUnused(touches);
}

// D34.1 : LA FORME DES FONDUS CROISÉS, changée EN MARCHE.
//
// UN SEUL CHEMIN POUR LE MENU ET POUR LA COMMANDE DE VÉRIFICATION. Ce qu'on
// photographie doit être ce que le geste fait, sans quoi la capture prouve
// l'existence d'un second chemin et rien d'autre -- c'est ce que D33.3 avait
// déjà écrit du scrub.
// L'ARBRE DES PISTES SUR UNE LIGNE (D35), pour la vérification.
//
// « Batterie/0! Kick1 Snare1 Basse0 » : le nom, la profondeur, `/` pour un
// dossier, `!` pour un muet, `*` pour un solo. Une liste de douze pistes ne se
// juge pas sur une capture d'écran ; écrite ainsi, elle se compare d'un coup
// d'œil, et un aller-retour qui ne revient pas au point de départ se voit.
juce::String MainComponent::trackTreeForCapture() const {
    juce::String texte;
    for (const auto& t : project_.tracks) {
        texte += juce::String::fromUTF8(t.name.c_str()) + (t.isFolder() ? "/" : "")
               + juce::String(t.folderDepth);
        if (t.muted) texte += "!";
        if (t.solo) texte += "*";
        texte += " ";
    }
    return texte.trim();
}

// D35.1 : MONTER ET DESCENDRE UNE PISTE, avec ce qu'elle contient.
//
// D'UN CRAN À LA FOIS, et pour un dossier c'est d'un cran APRÈS son bloc : ce
// qui monte ou descend est la piste et son contenu, jamais l'en-tête seul.
//
// UNE PISTE QUI MONTE ENTRE DANS LE DOSSIER QU'ELLE TRAVERSE, puis en ressort
// par le haut au cran suivant. Ce n'est pas un effet de bord, c'est la règle
// d'adoption de `moveTrackWithFolder` vue de près -- et c'est ce qui rend le
// rangement possible au clavier, sans jamais avoir à viser à la souris.
bool MainComponent::moveSelectedTrack(int direction) {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size() || direction == 0) return false;
    const size_t taille = 1 + vsm::sequencer::folderContents(project_, piste).size();
    if (direction < 0 && piste == 0) return false;
    if (direction > 0 && piste + taille >= project_.tracks.size()) return false;

    captureSessionIntoProject();
    beginProjectEdit(direction < 0 ? u8"Monter la piste" : u8"Descendre la piste");
    const size_t cible = direction < 0 ? piste - 1 : piste + taille + 1;
    const size_t rang = vsm::sequencer::moveTrackWithFolder(project_, piste, cible);
    rebuildFromProject(false);
    trackList_.selectTrackIndex(rang);
    return true;
}

// D34.4 : LA RÈGLE EN TEMPS OU EN MESURES, et elle est CONSERVÉE.
//
// Un seul chemin pour le menu et pour la commande de vérification, comme la
// forme des fondus croisés : ce qu'on photographie doit être ce que le geste
// fait. Le réglage est une préférence d'ATELIER, pas une donnée du morceau --
// il ne change rien à ce que le projet sonne ni à ce qu'il contient --, donc il
// va dans les propriétés de l'application et non dans `project.json`.
void MainComponent::setRulerInTime(bool enTemps) {
    arrangement_.setRulerInTime(enTemps);
    vsm::app::ui::UiScale::properties().setValue("regleEnTemps", enTemps);
}

// D34.3 : POSER SUR UNE PISTE ce que le dépôt a mis de côté.
//
// NOMMÉE PLUTÔT QU'ÉCRITE DANS LE RAPPEL DE LA BOÎTE, pour que la commande de
// vérification appelle EXACTEMENT ce que le bouton appelle. Le clic lui-même
// n'est pas pilotable sans souris, et c'est dit dans la feuille de route
// plutôt que sous-entendu ; ce qui est vérifié est tout le reste du chemin.
void MainComponent::placeDroppedAudioOnTracks() {
    importAudioFiles(pendingDroppedAudios_);
}

/// Combien de pistes audio le projet porte : ce que la vérification de D34.3
/// compte avant et après un dépôt.
size_t MainComponent::audioTrackCount() const {
    size_t compte = 0;
    for (const auto& track : project_.tracks)
        if (track.kind == vsm::sequencer::Track::Kind::Audio) ++compte;
    return compte;
}

/// Combien de clips MIDI du projet partagent leur fenêtre avec un autre : ce
/// que la commande de vérification affiche, faute de pouvoir juger un marqueur
/// de six pixels sur une capture d'écran.
int MainComponent::linkedMidiClipCount() const {
    int compte = 0;
    for (const auto& track : project_.tracks) {
        if (track.kind != vsm::sequencer::Track::Kind::Midi) continue;
        for (const auto& clip : track.clips)
            if (vsm::sequencer::clipIsShared(track.clips, clip.id)) ++compte;
    }
    return compte;
}

/// Combien de bords de clips audio portent un fondu croisé, tous chargés
/// confondus. Ce que la commande de vérification AFFICHE : « la forme est
/// posée » ne prouve rien si aucune jonction ne la reçoit.
int MainComponent::audioSpansWithCrossfade() const {
    int compte = 0;
    for (size_t i = 0; i < project_.tracks.size(); ++i)
        if (const auto source = audioEngine_.processGraph().trackAudio(i))
            for (const auto& span : source->clips)
                if (span.crossfadeInFrames > 0 || span.crossfadeOutFrames > 0) ++compte;
    return compte;
}

void MainComponent::setCrossfadeShape(vsm::sequencer::FadeShape forme) {
    project_.crossfadeShape = forme;
    // LES CLIPS SONT RECHARGÉS : sans quoi le réglage ne prendrait qu'au
    // prochain chargement de projet -- la leçon de D33.2, telle quelle.
    loadAudioTracks();
    markProjectDirty();
}

void MainComponent::loadAudioTracks() {
    const double sr = audioEngine_.currentSampleRate() > 0.0 ? audioEngine_.currentSampleRate()
                                                              : 48000.0;
    juce::StringArray manquants;
    juce::StringArray reechantillonnees;   // D51
    waveformCache_.clear();
    for (size_t i = 0; i < project_.tracks.size(); ++i) {
        const auto& track = project_.tracks[i];
        // UNE PISTE GELÉE JOUE SON FICHIER DE GEL (D5.5), quelle que soit sa
        // nature : c'est tout l'objet du gel. Une piste audio joue le sien.
        const bool gelee = track.frozen && !track.frozenAudio.empty();
        const auto& source = gelee ? track.frozenAudio : track.audio;
        if ((!gelee && track.kind != vsm::sequencer::Track::Kind::Audio) || source.empty()) {
            audioEngine_.processGraph().setTrackAudio(i, nullptr);
            trackList_.setAudioSourceRate(i, 0.0, 0.0);   // D51 : rien à dire
            continue;
        }
        // Le chemin est RELATIF au dossier du projet. Sans dossier -- projet
        // jamais enregistré --, il n'y a rien à résoudre, et le dire vaut mieux
        // que de chercher au hasard dans le dossier courant.
        if (currentProjectFolder_ == juce::File()) {
            manquants.add(juce::String(track.name) + tr(" (projet jamais enregistre)"));
            audioEngine_.processGraph().setTrackAudio(i, nullptr);
            trackList_.setAudioSourceRate(i, 0.0, 0.0);
            continue;
        }
        const juce::File fichier = currentProjectFolder_.getChildFile(source.path);
        auto charge = vsm::audio::io::loadAudioTrack(fichier.getFullPathName().toStdString(), sr);
        if (!charge.success || !charge.source) {
            manquants.add(juce::String(track.name) + " : " + juce::String(charge.error));
            audioEngine_.processGraph().setTrackAudio(i, nullptr);
            trackList_.setAudioSourceRate(i, 0.0, 0.0);
            continue;
        }
        // D51 : LE RÉÉCHANTILLONNAGE SE DIT. `AudioTrackLoadResult` le porte
        // depuis D2 -- son en-tête écrit même que « l'interface doit pouvoir
        // l'écrire » --, et un grep sur tout le dépôt ne trouvait QU'UN lecteur
        // de `resampled` : le rendu hors ligne, qui en fait un avertissement.
        // L'application, elle, chargeait un fichier à 96 kHz dans une session à
        // 44,1 kHz sans un mot. Deux vérités pour un même fait, c'est-à-dire
        // celle qu'on lit et celle qu'on n'a pas.
        //
        // LA MENTION VA SUR LA LIGNE, PAS DANS UNE BOÎTE (règle de D43) : la
        // fréquence d'un fichier ne change pas, on la relit chaque fois qu'on
        // se demande ce que joue cette piste, et une boîte fermée se ferme.
        trackList_.setAudioSourceRate(i, charge.resampled ? charge.fileSampleRate : 0.0, sr,
                                       charge.streamed, charge.residentBytes);
        if (charge.resampled)
            reechantillonnees.add(juce::String(track.name) + " : "
                                  + juce::String(charge.fileSampleRate, 0) + juce::String(u8" → ")
                                  + juce::String(charge.sessionSampleRate, 0) + " Hz");
        // La longueur vient du FICHIER CHARGÉ, pas de ce que le projet déclare :
        // quand les deux divergent, c'est le fichier qui a raison.
        vsm::sequencer::Track pourLesClips = track;
        pourLesClips.audio = source;
        pourLesClips.kind = vsm::sequencer::Track::Kind::Audio;   // pour spansFromTrack
        pourLesClips.audio.sampleRate = sr;
        pourLesClips.audio.frames = charge.source->frames();
        // UN GEL N'EST PAS DÉCOUPÉ : il rend la piste entière, clips compris.
        // Lui appliquer les clips de la piste les appliquerait DEUX fois.
        if (gelee) pourLesClips.clips.clear();
        // LE CACHE D'APERÇU (D5.7). Deux chemins, parce qu'il y a désormais deux
        // façons de tenir le matériau (D8.2) : quand il est résident, on lit le
        // tableau qui est déjà là ; quand il est diffusé, on relit le fichier
        // par tranches sans jamais le tenir en entier -- sinon la diffusion
        // n'aurait servi à rien, le dessin rechargeant ce que la lecture a
        // renoncé à charger.
        if (const auto* memoire = dynamic_cast<const vsm::audio::engine::MemorySampleStore*>(
                charge.source->samples.get())) {
            waveformCache_[i] = std::make_shared<const std::vector<vsm::audio::io::PeakBin>>(
                vsm::audio::io::computePeaks(memoire->leftChannel().data(),
                                              memoire->rightChannel().empty()
                                                  ? memoire->leftChannel().data()
                                                  : memoire->rightChannel().data(),
                                              charge.source->frames()));
        } else {
            auto relecture = vsm::audio::io::WavStreamReader::open(
                fichier.getFullPathName().toStdString());
            if (relecture.reader)
                waveformCache_[i] = std::make_shared<const std::vector<vsm::audio::io::PeakBin>>(
                    vsm::audio::io::computePeaksFromFile(*relecture.reader, sr));
        }

        charge.source->clips = vsm::audio::engine::spansFromTrack(
            pourLesClips, sr, [this](int64_t tick) { return project_.ticksToSeconds(tick); },
            project_.crossfadeShape);
        // LES CLIPS QUI SUIVENT LE TEMPO (D12.5) : les attaques du fichier se
        // cherchent ICI, une fois par piste, hors du thread audio -- comme le
        // cache d'aperçu juste au-dessus, et pour la même raison.
        vsm::audio::engine::prepareWarpedSpans(*charge.source);
        // D33.2 : LE FONDU DE SÉCURITÉ, converti UNE FOIS en trames à la
        // fréquence réelle du moteur -- le chemin de lecture compte des
        // trames, et convertir à chaque échantillon pour une constante serait
        // payer une division par échantillon.
        charge.source->safetyFadeFrames =
            static_cast<int64_t>(std::llround(safetyFadeMs_ / 1000.0 * sr));
        audioEngine_.processGraph().setTrackAudio(i, charge.source);
    }
    // UNE PISTE AUDIO QUI NE CHARGE PAS NE SE DISTINGUE PAS, À L'OREILLE, D'UNE
    // PISTE DONT ON AURAIT BAISSÉ LE VOLUME. Elle se dit donc, une fois, au
    // lieu de laisser chercher.
    if (!manquants.isEmpty())
        montrerBoite(
            juce::AlertWindow::WarningIcon, tr(u8"Audio non chargé"),
            tr(u8"Ces pistes audio n'ont pas pu être lues :") + "\n\n"
                + manquants.joinIntoString("\n"));
    // D51 : ET LE MÊME FAIT SUR LE TERMINAL, pour les mêmes raisons que les
    // avertissements de `vsm-render` -- une capture montre la ligne, un banc
    // automatique a besoin d'une phrase à lire. Ce n'est pas une trace de mise
    // au point : c'est le seul moyen de vérifier sans écran que la mention est
    // bien celle du fichier chargé.
    for (const auto& dit : reechantillonnees)
        std::fputs((juce::String(u8"VSM_AUDIO : rééchantillonné — ") + dit + "\n").toRawUTF8(), stderr);
    audioTracksLoadedAtRate_ = sr;   // D51.2
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
    //
    // D51.2 : MAIS SEULEMENT SI LA FRÉQUENCE A VRAIMENT CHANGÉ POUR ELLES.
    // `appliedSampleRate_` part de zéro, si bien que le premier passage du
    // minuteur rechargeait TOUT -- y compris ce que l'ouverture du projet
    // venait de charger à la même fréquence une milliseconde plus tôt. Trouvé
    // en lisant la trace de D51, qui écrivait chaque rééchantillonnage DEUX
    // fois : douze chargements pour six pistes. Ce n'est pas un doublon
    // d'affichage, c'est un double décodage.
    if (std::abs(audioTracksLoadedAtRate_ - sr) > 0.5) loadAudioTracks();

    // Les effets de bus : mêmes types, mêmes réglages, à la bonne fréquence.
    applySendBuses();
}

std::vector<vsm::sequencer::SendBusDescription> MainComponent::defaultSendBuses() {
    std::vector<vsm::sequencer::SendBusDescription> bus;
    vsm::sequencer::SendBusDescription reverb;
    reverb.name = "Reverberation";
    reverb.effectType = "reverb";
    bus.push_back(std::move(reverb));
    vsm::sequencer::SendBusDescription delay;
    delay.name = "Delay";
    delay.effectType = "delay";
    bus.push_back(std::move(delay));
    return bus;
}

void MainComponent::adoptDefaultSendsIfNeeded() {
    if (!project_.sends.empty()) return;
    // UN PROJET SANS BUS DÉCLARÉ QUI A POURTANT DES NIVEAUX D'ENVOI vient
    // forcément d'AVANT D4.2 : les niveaux étaient sauvegardés, mais les deux
    // effets qu'ils alimentaient étaient figés dans le code et n'étaient donc
    // écrits nulle part. Lui rendre ces deux bus-là, c'est lui rendre le
    // mixage qu'il avait ; ne rien faire le priverait en silence de sa
    // réverbération.
    //
    // Un projet sans bus ET sans niveau, lui, n'a rien perdu : on le laisse
    // tranquille, parce qu'un utilisateur a le droit de ne vouloir aucun
    // départ et qu'ils reviendraient à chaque ouverture.
    for (const auto& piste : project_.tracks)
        for (float niveau : piste.sendLevels)
            if (niveau > 0.0f) { project_.sends = defaultSendBuses(); return; }
}

void MainComponent::ColourApplier::changeListenerCallback(juce::ChangeBroadcaster* source) {
    auto* selecteur = dynamic_cast<juce::ColourSelector*>(source);
    if (selecteur == nullptr || index_ >= parent_.project_.tracks.size()) return;
    // UN GLISSÉ DANS LE SÉLECTEUR PRODUIT DES DIZAINES DE CHANGEMENTS : un
    // instantané d'annulation par changement empilerait trois cents pas pour un
    // seul geste. On n'en ouvre qu'un, au premier.
    if (!parent_.colourEditOpen_) {
        parent_.colourEditOpen_ = true;
        parent_.beginProjectEdit(u8"Couleur d'une piste");
    }
    parent_.appliquerCouleurDePiste(index_, selecteur->getCurrentColour());
}

void MainComponent::ClipColourApplier::changeListenerCallback(juce::ChangeBroadcaster* source) {
    auto* selecteur = dynamic_cast<juce::ColourSelector*>(source);
    if (selecteur == nullptr) return;
    if (!parent_.colourEditOpen_) {
        parent_.colourEditOpen_ = true;
        parent_.beginProjectEdit(u8"Couleur d'un clip");
    }
    if (auto* clip = parent_.findClip(index_, clip_))
        clip->colorRgba = selecteur->getCurrentColour().getARGB();
    parent_.arrangement_.repaint();
}

vsm::sequencer::Clip* MainComponent::findClip(size_t trackIndex, uint64_t clipId) {
    if (trackIndex >= project_.tracks.size()) return nullptr;
    for (auto& clip : project_.tracks[trackIndex].clips)
        if (clip.id == clipId) return &clip;
    return nullptr;
}

void MainComponent::sendBusesChanged() {
    applySendBuses();
    mixer_.setProject(&project_);   // le nombre de boutons a pu changer
    mixDirty_ = true;               // republie le projet (niveaux d'envoi) au moteur
}

void MainComponent::noterReserveDEffet(const juce::String& reserve) {
    // LA SORTIE D'ERREUR TOUT DE SUITE, comme `VSM_PRESET` en D52 : l'écran de
    // rapport est une fenêtre, et un banc sans souris doit pouvoir relire la
    // phrase. Les doublons sont écartés -- `applySendBuses` est rappelée à
    // chaque republication du projet, et répéter la même réserve à chaque
    // geste ferait un journal qu'on n'ouvre plus.
    if (std::find(reservesEffets_.begin(), reservesEffets_.end(), reserve) != reservesEffets_.end())
        return;
    reservesEffets_.push_back(reserve);
    std::fputs(("VSM_EFFET : " + reserve.toStdString() + "\n").c_str(), stderr);
}

void MainComponent::retraduire() {
    // D73 : CE QUE LA BARRE DE MENUS N'A PAS BESOIN QU'ON FASSE. Elle se
    // reconstruit à chaque ouverture (`getMenuForIndex`), donc elle parle la
    // langue courante toute seule ; il suffit de lui redemander ses NOMS de
    // menus, qui sont posés une fois. Le reste -- ce qui est écrit une fois
    // pour toutes à la construction d'un composant -- se repose ici.
    //
    // POURQUOI PAS « AU PROCHAIN DÉMARRAGE », qui aurait tenu en une ligne :
    // Cubase et Live le demandent, et c'est précisément ce qu'on leur reproche.
    // Une interface qui exige de relancer le logiciel pour lire son propre menu
    // dans sa langue n'a pas fini le travail.
    for (int i = 0; i < bottomTabs_.getNumTabs() && i < nomsDesOnglets_.size(); ++i)
        bottomTabs_.setTabName(i, tr(nomsDesOnglets_[i]));
    transportBar_.retraduire();
    trackList_.retraduire();
    pianoRollPanel_.retraduireBarre();
    effectChain_.retraduire();   // D77
    historyPanel_.retraduire();   // D82
    // D84 : le volet de rapport -- ses boutons, et le rapport d'ouverture s'il
    // est le dernier à l'avoir rempli, refait volet ouvert ou fermé.
    importReport_.retraduire();
    preferencesPanel_.retraduire();   // D85
    takeCompPanel_.retraduire();      // D86
    playOrderPanel_.retraduire();
    shortcutsPanel_.retraduire();     // D87 : les boutons, puis les lignes par leur client
    refreshShortcutList();
    midiLearnPanel_.retraduire();
    refreshMidiLearnList();
    browserPanel_.retraduire();
    // D99 : LES ORIGINES DES ENTRÉES sont écrites dans la langue de la liste --
    // le filtre de recherche les lit, il doit trouver les mots qu'on voit. La
    // liste se refait donc, si le navigateur a déjà été ouvert.
    if (browserWindow_) refreshBrowser();
    // D94 : LES PANNEAUX TOUJOURS VISIBLES -- le mixeur, les trois voies, la
    // liste d'événements et le rack. Ce que les autres DESSINENT (piano roll,
    // arrangement, spectre) suit par le `repaint()` final.
    mixer_.retraduire();
    midiCc_.retraduire();
    tempoLane_.retraduire();
    automation_.retraduire();
    eventList_.retraduire();
    synthRack_.retraduire();
    // D100 : LES TITRES DES QUINZE FENÊTRES FLOTTANTES. La clé de leur position,
    // elle, reste le titre français (PanelWindow) : un changement de langue ne
    // doit pas faire oublier où l'utilisateur les a mises.
    for (auto* fenetre : { &trackListWindow_, &pianoRollWindow_, &synthRackWindow_, &mixerWindow_,
                           &arrangementWindow_ })
        fenetre->retraduire();
    for (auto* fenetre : { reconstructionWindow_.get(), midiLearnWindow_.get(), historyWindow_.get(),
                           spectrumWindow_.get(), shortcutsWindow_.get(), preferencesWindow_.get(),
                           browserWindow_.get(), takeCompWindow_.get(), playOrderWindow_.get(),
                           projectNotesWindow_.get() })
        if (fenetre != nullptr) fenetre->retraduire();
    refreshPreferences();             // les textes d'état, refaits par leur client
    // D93 : CHAQUE CLIENT REFAIT SON RAPPORT, volet ouvert ou fermé -- le dernier
    // à l'avoir rempli, et lui seul (D84 ne connaissait que le rapport d'ouverture).
    const bool voletOuvert = importReport_.isVisible();
    switch (clientDuRapport_) {
        case ClientDuRapport::ouverture:      afficherRapportDOuverture(voletOuvert); break;
        case ClientDuRapport::importDaw:      importReport_.showReport(dernierImport_, voletOuvert); break;
        case ClientDuRapport::echecImport:    afficherEchecImport(voletOuvert); break;
        case ClientDuRapport::reconstruction: showReconstructionReport(voletOuvert); break;
        case ClientDuRapport::aucun:          break;
    }
    // D78 : LE BOUTON D'ÉCOUTE, par la fonction qui le pose au démarrage --
    // D77 a trouvé « Écoute A/B : pas d'original » sur une image basculée en
    // anglais, là où le démarrage écrivait « A/B monitoring: no original ».
    refreshListeningIndicator();
    menuItemsChanged();
    repaint();
}

void MainComponent::applySendBuses() {
    const double sr = audioEngine_.currentSampleRate() > 0.0 ? audioEngine_.currentSampleRate() : 48000.0;
    const int blockSize = audioEngine_.currentBlockSize() > 0 ? audioEngine_.currentBlockSize() : 512;

    for (size_t bus = 0; bus < vsm::audio::engine::ProcessGraph::kMaxSends; ++bus) {
        if (bus >= project_.sends.size()) {
            // AU-DELÀ DE CE QUE LE PROJET DÉCLARE, ON EFFACE. Laisser en place
            // l'effet d'un bus supprimé le ferait revenir au chargement du
            // projet suivant, sans que rien ne le mentionne.
            audioEngine_.processGraph().setSendEffect(bus, nullptr);
            continue;
        }
        const auto& decrit = project_.sends[bus];
        auto fx = vsm::audio::effect::EffectFactory::create(decrit.effectType);
        if (!fx) {
            // D71 : DIT, ET PAS SEULEMENT SAUTÉ. Le rendu hors ligne du même
            // dossier écrit « Bus de départ « … » : effet « … » inconnu, non
            // appliqué » ; ici le bus devenait muet sans un mot, et un projet
            // qui envoie 40 % dans une réverbération inconnue s'ouvrait sec.
            // D111 : le littéral EST le modèle de D89 -- la donnée ne change pas d'un octet.
            noterReserveDEffet(juce::String(u8"bus de départ « %1 » : effet « %2 » inconnu, non appliqué")
                                   .replace("%1", juce::String::fromUTF8(decrit.name.c_str()))
                                   .replace("%2", juce::String::fromUTF8(decrit.effectType.c_str())));
            audioEngine_.processGraph().setSendEffect(bus, nullptr);
            continue;
        }
        // Les réglages sont REPOSÉS depuis leurs identités sémantiques, comme
        // pour les inserts : c'est ce qui les fait survivre à un changement de
        // version de l'effet.
        vsm::sequencer::TrackEffect described;
        described.type = decrit.effectType;
        described.parameters = decrit.parameters;
        const auto applique = vsm::interchange::applyEffectDescription(described, *fx);
        for (const auto& inconnu : applique.unknownParameters)
            noterReserveDEffet(juce::String(u8"bus de départ « %1 » : réglage inconnu « %2 »")
                                   .replace("%1", juce::String::fromUTF8(decrit.name.c_str()))
                                   .replace("%2", juce::String::fromUTF8(inconnu.c_str())));
        fx->prepare(sr, blockSize);
        audioEngine_.processGraph().setSendEffect(bus, std::shared_ptr<vsm::audio::effect::IAudioEffect>(std::move(fx)));
        audioEngine_.processGraph().setSendReturn(bus, decrit.returnGain);
    }
}

void MainComponent::newProject() {
    clearHistory();   // l'annulation d'un autre morceau n'a aucun sens ici
    project_ = Project{};
    oublierLesMachines();   // D76
    project_.title = "Nouveau projet";
    project_.sends = defaultSendBuses();
    rebuildFromProject();
}

void MainComponent::addTrack(Track::Kind kind, const std::string& nom) {
    const bool audio = kind == Track::Kind::Audio;
    const bool groupe = kind == Track::Kind::Group;
    beginProjectEdit(groupe ? juce::String(u8"Ajouter un groupe")
                    : audio  ? juce::String(u8"Ajouter une piste audio")
                              : juce::String(u8"Ajouter une piste"));
    // D33.5 : LA PALETTE VIENT DE `core/`, comme partout ailleurs. Il y en
    // avait QUATRE : celle-ci, celle de `DawImport`, celle de la chaîne Python
    // et le défaut bleu de `Track`. Quatre palettes veulent dire que la même
    // piste change de couleur selon la porte par laquelle elle est entrée.
    const size_t n = project_.tracks.size();

    Track t;
    t.kind = kind;
    // LE NOM VIENT DE L'APPELANT QUAND IL EN A UN (D34.3), et il est posé
    // AVANT `rebuildFromProject` : le poser après ne rafraîchissait que la
    // liste des pistes, et le mélangeur gardait « Audio 5 » sur une piste que
    // la liste appelait « prise3 ». Deux vues d'une même piste qui ne disent
    // pas la même chose, sur le geste même — importer douze stems — que D33.1
    // venait de rendre possible.
    t.name = !nom.empty() ? nom
           : tr(groupe ? u8"Groupe %1" : audio ? u8"Audio %1" : u8"Piste %1")   // D107 : la langue du moment
                 .replace("%1", juce::String(static_cast<int>(n) + 1)).toStdString();
    t.channel = static_cast<uint8_t>(n % 16);      // canaux MIDI 1..16 en boucle
    t.colorRgba = vsm::sequencer::trackColourForIndex(n);
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
    // D38.2 : TOUTE LA SÉLECTION, ET DE LA FIN VERS LE DÉBUT.
    //
    // POURQUOI CET ORDRE, ET C'EST LA FAUTE CLASSIQUE DE CE GESTE : supprimer
    // les pistes 2, 5 et 7 en montant supprime la 2, ce qui fait glisser tout
    // ce qui suit d'un rang -- la « 5 » qu'on supprime ensuite est l'ancienne
    // 6, et la « 7 » l'ancienne 9. On efface trois pistes, dont deux qu'on
    // n'avait pas désignées, et rien ne le dit. En descendant, ce qu'on
    // supprime ne déplace que des index déjà traités.
    std::vector<size_t> aSupprimer(trackList_.selectedTracks().begin(),
                                    trackList_.selectedTracks().end());
    std::sort(aSupprimer.begin(), aSupprimer.end(), std::greater<size_t>());
    // Après l'instant où l'on sait qu'il y a bien quelque chose à supprimer :
    // un instantané pris pour un geste sans effet ajouterait un pas
    // d'annulation qui ne défait rien.
    beginProjectEdit(aSupprimer.size() > 1 ? juce::String::fromUTF8(u8"Supprimer des pistes")
                                            : juce::String("Supprimer une piste"));

    // La suppression et la RÉPARATION DES ROUTAGES sont une règle du modèle,
    // pas de l'interface : voir `vsm::sequencer::removeTrack`.
    //
    // D35.3 : UN DOSSIER EMPORTE SON CONTENU, ET ON LE DIT. Avant, l'en-tête
    // seul disparaissait et `normalizeFolderDepths` mettait ses membres à
    // plat : le tiroir se dissolvait sans que personne l'ait demandé. Emporter
    // le contenu est franc, annulable — et n'est honnête qu'à condition d'être
    // annoncé.
    size_t retirees = 0;
    for (size_t i : aSupprimer)
        if (i < project_.tracks.size()) retirees += vsm::sequencer::removeTrackWithFolder(project_, i);
    rebuildFromProject();
    // CE QUI EST PARTI EST DIT quand ce n'est pas exactement ce qu'on a
    // désigné : un dossier emporte son contenu (D35.3), et un lot en emporte
    // d'autant plus.
    if (retirees > aSupprimer.size())
        std::fputs((juce::String::fromUTF8(u8"Supprimé : ") + juce::String(int(retirees))
                     + juce::String::fromUTF8(u8" piste(s), dossiers et contenus compris.\n")).toRawUTF8(),
                    stderr);

    if (!project_.tracks.empty()) {
        const size_t next = std::min(idx, project_.tracks.size() - 1);
        trackList_.selectTrackIndex(next);
    }
}

void MainComponent::duplicateSelectedTrack() {
    const size_t idx = trackList_.selectedTrackIndex();
    if (idx >= project_.tracks.size()) return;
    beginProjectEdit(u8"Dupliquer une piste");
    // D35.3 : UN DOSSIER SE DUPLIQUE AVEC SON CONTENU. Avant, la copie de
    // l'en-tête s'insérait ENTRE le dossier et ses membres : ceux-ci passaient
    // sous la copie et l'original restait vide -- dupliquer un dossier lui
    // VOLAIT son contenu.
    const size_t copie = vsm::sequencer::duplicateTrackWithFolder(project_, idx);
    rebuildFromProject();
    // L'ÉTAT VIVANT DE L'INSTRUMENT n'est pas dans le modèle (D0.1 : il vit
    // dans la machine, le fichier le relit à l'ouverture). La copie vient
    // d'être instanciée sur son patch d'usine : on lui recopie l'état de
    // l'original, réglage par réglage et état natif compris.
    auto* original = audioEngine_.processGraph().trackInstrument(idx);
    auto* duplique = audioEngine_.processGraph().trackInstrument(copie);
    if (original != nullptr && duplique != nullptr) duplique->loadState(original->saveState());
    trackList_.selectTrackIndex(copie);
}

juce::String MainComponent::frozenPathFor(size_t trackIndex) const {
    // Dans le DOSSIER DU PROJET, sous un chemin relatif, comme tout ce que le
    // format référence : c'est ce qui permet d'ouvrir le projet ailleurs.
    return "gel/piste-" + juce::String(static_cast<int>(trackIndex) + 1) + ".wav";
}

void MainComponent::toggleFreezeSelectedTrack() {
    const size_t index = trackList_.selectedTrackIndex();
    if (index >= project_.tracks.size()) return;
    auto& piste = project_.tracks[index];

    if (piste.frozen) {
        // DÉGELER : l'instrument reprend, et le fichier s'en va. Le garder
        // laisserait dans le dossier un rendu que plus rien ne référence, et
        // qu'on retrouverait des mois plus tard sans savoir ce qu'il est.
        beginProjectEdit(u8"Dégeler une piste");
        if (currentProjectFolder_ != juce::File() && !piste.frozenAudio.path.empty())
            currentProjectFolder_.getChildFile(juce::String(piste.frozenAudio.path)).deleteFile();
        piste.frozen = false;
        piste.frozenAudio = {};
        rebuildFromProject(false);
        return;
    }

    // GELER EXIGE UN DOSSIER DE PROJET, comme l'enregistrement audio et pour la
    // même raison : le format range ses fichiers par chemin relatif.
    if (currentProjectFolder_ == juce::File()) {
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Projet jamais enregistré"),
            tr(u8"Un gel est un FICHIER, et le format range les fichiers d'un projet "
               u8"par chemin relatif à son dossier. Enregistrez d'abord le projet "
               u8"(Ctrl+S) : le gel ira dans son sous-dossier gel/."));
        return;
    }

    captureSessionIntoProject();
    vsm::interchange::LoadedBundle bundle;
    bundle.project = project_;
    bundle.document = vsm::interchange::documentFromProject(project_);
    bundle.folderPath = currentProjectFolder_.getFullPathName().toStdString();
    if (!project_.tracks[index].instrumentId.empty())
        if (auto* machine = audioEngine_.processGraph().trackInstrument(index))
            bundle.presetsByTrack[index] = vsm::interchange::capturePreset(
                *machine, project_.tracks[index].instrumentId, project_.tracks[index].name,
                bundle.folderPath);   // D76 : avec ses échantillons

    vsm::interchange::RenderOptions options;
    options.sampleRate = audioEngine_.currentSampleRate() > 0.0 ? audioEngine_.currentSampleRate()
                                                                 : 48000.0;
    options.blockSize = audioEngine_.currentBlockSize() > 0 ? audioEngine_.currentBlockSize() : 512;
    options.format = vsm::audio::io::SampleFormat::Float32;

    vsm::audio::engine::RenderedAudio gel;
    const auto rendu = vsm::interchange::renderTrackForFreeze(bundle, index, gel, options);
    if (!rendu.success) {
        montrerBoite(juce::AlertWindow::WarningIcon,
                                                 tr(u8"Gel impossible"), rendu.error);
        return;
    }

    const juce::String relatif = frozenPathFor(index);
    const juce::File fichier = currentProjectFolder_.getChildFile(relatif);
    fichier.getParentDirectory().createDirectory();
    try {
        vsm::audio::io::WavFileWriter::writeFile(gel.left.data(), gel.right.data(),
                                                  gel.numFrames(), options.sampleRate,
                                                  options.format,
                                                  fichier.getFullPathName().toStdString());
    } catch (const std::exception& e) {
        montrerBoite(juce::AlertWindow::WarningIcon,
                                                 tr(u8"Gel impossible"), e.what());
        return;
    }

    beginProjectEdit(u8"Geler une piste");
    piste.frozen = true;
    piste.frozenAudio.path = relatif.toStdString();
    piste.frozenAudio.sampleRate = options.sampleRate;
    piste.frozenAudio.frames = static_cast<int64_t>(gel.numFrames());
    piste.frozenAudio.channels = 2;
    rebuildFromProject(false);
}

void MainComponent::bounceSelectedTrack() {
    const size_t index = trackList_.selectedTrackIndex();
    if (index >= project_.tracks.size()) return;
    if (project_.tracks[index].kind != Track::Kind::Midi) return;

    if (currentProjectFolder_ == juce::File()) {
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Projet jamais enregistré"),
            tr(u8"Un report est un FICHIER, et le format range les fichiers d'un "
               u8"projet par chemin relatif à son dossier. Enregistrez d'abord le "
               u8"projet (Ctrl+S)."));
        return;
    }

    // REPORTER EST UNE DÉCISION, GELER N'EN EST PAS UNE : le report remplace le
    // matériau, et on le demande avant de le faire. L'annulation le rattrape
    // dans la session, mais pas après une fermeture -- c'est exactement ce que
    // veut dire « définitif », et le dire vaut mieux que de le découvrir.
    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::QuestionIcon, tr(u8"Reporter la piste en audio"),
        tr(u8"Les notes, l'instrument et les inserts de « %1 » seront remplacés par leur rendu. "
           u8"C'est annulable tant que la session est ouverte, et définitif ensuite.\n\nPour un "
           u8"allègement réversible, préférez GELER la piste.")
            .replace("%1", juce::String(project_.tracks[index].name)),
        tr(u8"Reporter"), vsm::app::ui::trSelon("bouton", u8"Annuler"), nullptr,
        juce::ModalCallbackFunction::create([this, index](int choix) {
            if (choix == 0) return;
            performBounce(index);
        }));
}

void MainComponent::newFolderAboveSelectedTrack() {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;
    captureSessionIntoProject();
    beginProjectEdit(u8"Nouveau dossier");

    vsm::sequencer::Track dossier;
    dossier.kind = vsm::sequencer::Track::Kind::Folder;
    dossier.name = "Dossier";
    dossier.colorRgba = project_.tracks[piste].colorRgba;
    dossier.folderDepth = project_.tracks[piste].folderDepth;

    // LES INDEX QUI POINTENT APRÈS L'INSERTION RECULENT D'UN RANG, comme
    // partout ailleurs (D18.7b) : un dossier n'est pas un signal, mais il est
    // bien une piste de plus dans la liste.
    const int insere = static_cast<int>(piste);
    for (auto& t : project_.tracks) {
        if (t.outputGroup >= insere) t.outputGroup += 1;
        if (t.outputSourceTrack >= insere) t.outputSourceTrack += 1;
    }
    project_.tracks.insert(project_.tracks.begin() + insere, std::move(dossier));
    // La piste choisie, désormais juste après, entre dans le dossier.
    project_.tracks[static_cast<size_t>(insere) + 1].folderDepth += 1;
    vsm::sequencer::normalizeFolderDepths(project_);
    rebuildFromProject(false);
}

void MainComponent::changeSelectedTrackFolderDepth(int delta) {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;
    captureSessionIntoProject();
    beginProjectEdit(delta > 0 ? u8"Entrer dans le dossier" : u8"Sortir du dossier");
    auto& cible = project_.tracks[piste];
    cible.folderDepth = std::max(0, cible.folderDepth + delta);
    // EN SORTANT, ON EMMÈNE CE QU'ON CONTENAIT : un dossier qu'on sort d'un
    // tiroir ne laisse pas ses pistes derrière lui, sinon elles se
    // retrouveraient rangées dans le voisin d'à côté.
    if (cible.isFolder()) {
        for (size_t t = piste + 1; t < project_.tracks.size(); ++t) {
            if (project_.tracks[t].folderDepth <= cible.folderDepth - delta) break;
            project_.tracks[t].folderDepth = std::max(0, project_.tracks[t].folderDepth + delta);
        }
    }
    vsm::sequencer::normalizeFolderDepths(project_);
    rebuildFromProject(false);
}

void MainComponent::explodeSelectedTrackByPitch() {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;

    // LE NOM DES PIÈCES VIENT D'ICI, pas de `core/` : c'est l'application qui
    // connaît les machines. `drumVoiceName` sait déjà nommer « charleston
    // fermé » à partir de la machine assignée ou de la convention General
    // MIDI, et le piano roll s'en sert depuis longtemps -- il n'y avait aucune
    // raison d'en écrire un second.
    const std::string machine = project_.tracks[piste].instrumentId;
    // D109 : un nom que l'application fabrique se donne dans la langue du moment
    // (D107) -- la table de `drumVoiceName` reste française, c'est la clé.
    auto nommer = [machine](uint8_t note) {
        return tr(juce::String::fromUTF8(vsm::app::ui::drumVoiceName(machine, note).c_str())).toStdString();
    };

    captureSessionIntoProject();
    beginProjectEdit(u8"Éclater par hauteur");
    const size_t creees = vsm::sequencer::explodeTrackByPitch(project_, piste, nommer);
    rebuildFromProject(false);

    montrerBoite(
        juce::AlertWindow::InfoIcon, tr(u8"Éclater par hauteur"),
        creees > 0
            ? tr(u8"%1 piste(s) créée(s) — la hauteur la plus grave reste sur la piste d'origine.")
                  .replace("%1", juce::String(creees))
            : tr(u8"Rien à faire : cette piste n'a qu'une seule hauteur."));
}

void MainComponent::publishInstrumentOutputsOfSelectedTrack() {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;
    auto* machine = audioEngine_.processGraph().trackInstrument(piste);
    if (machine == nullptr || machine->outputCount() < 2) return;

    // LES NOMS VIENNENT DE LA MACHINE : « Caisse claire » plutôt que
    // « sortie 2 », faute de quoi il faudrait écouter chaque piste pour savoir
    // laquelle est laquelle.
    std::vector<std::string> noms;
    noms.reserve(static_cast<size_t>(machine->outputCount()));
    for (int k = 0; k < machine->outputCount(); ++k) noms.emplace_back(machine->outputName(k));

    captureSessionIntoProject();
    beginProjectEdit(u8"Publier les sorties de l'instrument");
    const size_t creees = vsm::sequencer::publishInstrumentOutputs(project_, piste, noms);
    rebuildFromProject(false);

    // PANNE MUETTE INTERDITE : zéro piste créée est un résultat, pas un
    // silence. Il arrive quand tout est déjà publié, et le dire évite de
    // relancer la commande en croyant qu'elle n'a pas marché.
    montrerBoite(
        juce::AlertWindow::InfoIcon, tr(u8"Publier les sorties"),
        creees > 0
            ? tr(u8"%1 piste(s) créée(s) — la sortie n° 0 reste sur la piste qui porte la machine.")
                  .replace("%1", juce::String(creees))
            : tr(u8"Rien à faire : toutes les sorties de cette machine sont déjà "
                 u8"publiées sur des pistes."));
}

void MainComponent::bounceSelectionToNewTracks() {
    if (!arrangement_.hasSelection()) return;
    if (currentProjectFolder_ == juce::File()) {
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Projet jamais enregistré"),
            tr(u8"Un report est un FICHIER, et le format range les fichiers d'un "
               u8"projet par chemin relatif à son dossier. Enregistrez d'abord le "
               u8"projet (Ctrl+S)."));
        return;
    }
    vsm::midi::Tick debutTick = 0, finTick = 0;
    if (!arrangement_.selectionTickRange(debutTick, finTick)) return;

    const auto& choisis = arrangement_.selectedClipIds();
    // QUELLES PISTES SONT CONCERNÉES : une par piste portant un clip choisi.
    // Cubase rend chaque piste sur la sienne, et c'est la seule réponse qui ne
    // mélange pas ce que l'utilisateur avait pris soin de séparer.
    std::vector<size_t> sources;
    for (size_t i = 0; i < project_.tracks.size(); ++i)
        for (const auto& clip : project_.tracks[i].clips)
            if (choisis.count(clip.id) > 0) { sources.push_back(i); break; }
    if (sources.empty()) return;

    captureSessionIntoProject();
    vsm::interchange::RenderOptions options;
    options.sampleRate = audioEngine_.currentSampleRate() > 0.0 ? audioEngine_.currentSampleRate()
                                                                 : 48000.0;
    options.blockSize = audioEngine_.currentBlockSize() > 0 ? audioEngine_.currentBlockSize() : 512;
    options.format = vsm::audio::io::SampleFormat::Float32;
    // LE RENDU PART DE ZÉRO ET LA PLAGE EST DÉCOUPÉE (D6.1) : à la mesure 33,
    // une réverbération porte la queue de ce qui précède, et un report qui
    // démarrerait à froid rendrait un extrait que personne n'a entendu.
    options.startSeconds = project_.ticksToSeconds(debutTick);
    const double finSecondes = project_.ticksToSeconds(finTick);
    options.durationSeconds = finSecondes - options.startSeconds + options.tailSeconds;
    if (options.durationSeconds <= 0.0) return;

    std::vector<vsm::sequencer::Track> neuves;
    juce::StringArray echecs;
    for (size_t index : sources) {
        // LE BUNDLE NE GARDE QUE LES CLIPS CHOISIS de cette piste : le reste
        // de la piste n'est pas ce qu'on a demandé de reporter, et le rendre
        // ferait entrer dans le fichier ce qu'on avait exclu en le
        // désélectionnant. Les NOTES restent : un clip est une fenêtre sur
        // elles, et les retirer viderait la fenêtre.
        vsm::interchange::LoadedBundle bundle;
        bundle.project = project_;
        auto& piste = bundle.project.tracks[index];
        std::vector<vsm::sequencer::Clip> gardes;
        for (const auto& clip : piste.clips)
            if (choisis.count(clip.id) > 0) gardes.push_back(clip);
        piste.clips = std::move(gardes);
        bundle.document = vsm::interchange::documentFromProject(bundle.project);
        bundle.folderPath = currentProjectFolder_.getFullPathName().toStdString();
        if (!piste.instrumentId.empty())
            if (auto* machine = audioEngine_.processGraph().trackInstrument(index))
                bundle.presetsByTrack[index] = vsm::interchange::capturePreset(
                    *machine, piste.instrumentId, piste.name, bundle.folderPath);   // D76

        vsm::audio::engine::RenderedAudio rendu;
        // LE MÊME RENDU QUE LE GEL ET QUE LE REPORT DE PISTE : trois chemins
        // différents finiraient par ne plus sonner pareil.
        const auto resultat = vsm::interchange::renderTrackForFreeze(bundle, index, rendu, options);
        if (!resultat.success) {
            echecs.add(juce::String(project_.tracks[index].name) + " : " + juce::String(resultat.error));
            continue;
        }

        const juce::String relatif = "audio/report-selection-piste-"
                                     + juce::String(static_cast<int>(index) + 1) + "-"
                                     + juce::String(static_cast<long long>(debutTick)) + ".wav";
        const juce::File fichier = currentProjectFolder_.getChildFile(relatif);
        fichier.getParentDirectory().createDirectory();
        try {
            vsm::audio::io::WavFileWriter::writeFile(rendu.left.data(), rendu.right.data(),
                                                      rendu.numFrames(), options.sampleRate,
                                                      options.format,
                                                      fichier.getFullPathName().toStdString());
        } catch (const std::exception& e) {
            echecs.add(juce::String(project_.tracks[index].name) + " : " + juce::String(e.what()));
            continue;
        }

        vsm::sequencer::Track neuve;
        neuve.kind = Track::Kind::Audio;
        neuve.name = project_.tracks[index].name + " (report)";
        neuve.colorRgba = project_.tracks[index].colorRgba;
        neuve.audio.path = relatif.toStdString();
        neuve.audio.sampleRate = options.sampleRate;
        neuve.audio.frames = static_cast<int64_t>(rendu.numFrames());
        neuve.audio.channels = 2;
        // À SA PLACE SUR LA LIGNE DE TEMPS, et pas au début du morceau : le
        // fichier commence là où la sélection commençait.
        vsm::sequencer::Clip clip;
        clip.startTick = debutTick;
        clip.length = project_.secondsToTicks(options.startSeconds + options.durationSeconds)
                      - debutTick;
        clip.sourceLength = clip.length;
        clip.name = neuve.name;
        clip.colorRgba = neuve.colorRgba;
        neuve.clips.push_back(std::move(clip));
        neuves.push_back(std::move(neuve));
    }

    if (!neuves.empty()) {
        beginProjectEdit(u8"Reporter la sélection en audio");
        for (auto& piste : neuves) project_.tracks.push_back(std::move(piste));
        project_.assignClipIds();
        rebuildFromProject(false);
    }
    // PANNE MUETTE INTERDITE : ce qui n'a pas pu être reporté est nommé, piste
    // par piste, plutôt que de laisser compter les pistes neuves.
    if (!echecs.isEmpty())
        montrerBoite(
            juce::AlertWindow::WarningIcon, tr(u8"Reporter la sélection"),
            tr(u8"Ces pistes n'ont pas pu être reportées :") + "\n" + echecs.joinIntoString("\n"));
    else
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Reporter la sélection"),
            tr(neuves.size() > 1 ? u8"%1 pistes de report posées" : u8"%1 piste de report posée")
                    .replace("%1", juce::String(static_cast<int>(neuves.size())))
                + tr(u8", à la place de la sélection. Les pistes d'origine n'ont pas été "
                     u8"touchées — désactivez-les si vous voulez entendre le report seul."));
}

void MainComponent::performBounce(size_t index) {
    if (index >= project_.tracks.size()) return;

    captureSessionIntoProject();
    vsm::interchange::LoadedBundle bundle;
    bundle.project = project_;
    bundle.document = vsm::interchange::documentFromProject(project_);
    bundle.folderPath = currentProjectFolder_.getFullPathName().toStdString();
    if (!project_.tracks[index].instrumentId.empty())
        if (auto* machine = audioEngine_.processGraph().trackInstrument(index))
            bundle.presetsByTrack[index] = vsm::interchange::capturePreset(
                *machine, project_.tracks[index].instrumentId, project_.tracks[index].name,
                bundle.folderPath);   // D76 : avec ses échantillons

    vsm::interchange::RenderOptions options;
    options.sampleRate = audioEngine_.currentSampleRate() > 0.0 ? audioEngine_.currentSampleRate()
                                                                 : 48000.0;
    options.blockSize = audioEngine_.currentBlockSize() > 0 ? audioEngine_.currentBlockSize() : 512;
    options.format = vsm::audio::io::SampleFormat::Float32;

    // LE MÊME RENDU QUE LE GEL, et c'est voulu : reporter et geler capturent
    // exactement la même chose, et seule la suite diffère. Deux rendus
    // différents finiraient par ne plus sonner pareil.
    vsm::audio::engine::RenderedAudio rendu;
    const auto resultat = vsm::interchange::renderTrackForFreeze(bundle, index, rendu, options);
    if (!resultat.success) {
        montrerBoite(juce::AlertWindow::WarningIcon,
                                                 tr(u8"Report impossible"), vsm::app::ui::trPhrase(juce::String(resultat.error)));
        return;
    }

    const juce::String relatif = "audio/report-piste-" + juce::String(static_cast<int>(index) + 1) + ".wav";
    const juce::File fichier = currentProjectFolder_.getChildFile(relatif);
    fichier.getParentDirectory().createDirectory();
    try {
        vsm::audio::io::WavFileWriter::writeFile(rendu.left.data(), rendu.right.data(),
                                                  rendu.numFrames(), options.sampleRate,
                                                  options.format,
                                                  fichier.getFullPathName().toStdString());
    } catch (const std::exception& e) {
        montrerBoite(juce::AlertWindow::WarningIcon,
                                                 tr(u8"Report impossible"), vsm::app::ui::trPhrase(juce::String(e.what())));
        return;
    }

    beginProjectEdit(u8"Reporter une piste en audio");
    auto& piste = project_.tracks[index];
    piste.kind = Track::Kind::Audio;
    piste.audio.path = relatif.toStdString();
    piste.audio.sampleRate = options.sampleRate;
    piste.audio.frames = static_cast<int64_t>(rendu.numFrames());
    piste.audio.channels = 2;
    // CE QUI EST DANS LE FICHIER N'A PLUS À TOURNER : notes, instrument,
    // inserts et découpe sont désormais du son. Les garder les appliquerait
    // une seconde fois, par-dessus leur propre rendu.
    piste.notes.clear();
    piste.instrumentId.clear();
    piste.effects.clear();
    piste.clips.clear();
    piste.frozen = false;
    piste.frozenAudio = {};
    // L'AUTOMATION DU MIXAGE SURVIT, celle des machines part avec elles : la
    // première pilote encore quelque chose, la seconde ne vise plus rien.
    piste.automation.erase(
        std::remove_if(piste.automation.begin(), piste.automation.end(),
                        [](const vsm::sequencer::AutomationCurve& c) {
                            return c.parameter.rfind("mix.", 0) != 0;
                        }),
        piste.automation.end());
    rebuildFromProject(false);
}

void MainComponent::exportMidiFile() {
    auto chooser = std::make_shared<juce::FileChooser>(
        tr("Exporter en MIDI..."), juce::File(), "*.mid");

    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser](const juce::FileChooser& fc) {
        juce::File file = fc.getResult();
        if (file == juce::File()) return;

        try {
            // D56.1 : L'ARRANGEMENT, pas le matériau. Celui qui ouvre ce
            // fichier ne reçoit pas les clips : lui écrire les notes qu'aucun
            // clip ne montre, et lui retirer les reprises des boucles, lui
            // donnerait un morceau que personne n'a jamais entendu.
            ParsedFile parsed = project_.toParsedFileArranged();
            MidiFileWriter::writeFile(parsed, file.getFullPathName().toStdString());
            // D31.5 : CE QUE LE .MID NE PORTE PAS, ON LE DIT. L'export écrit
            // le MATÉRIAU (`toParsedFile`), pas ce qui est joué : ni la chaîne
            // d'effets MIDI (D31), ni la transposition de piste (D17.5). Un
            // fichier qui sonnerait autrement ailleurs sans qu'on l'ait dit
            // est exactement la panne muette que ce projet s'interdit -- et
            // celle de D17.5 durait depuis un an.
            if (const juce::StringArray divergentes = tracksWhoseMidiExportWillDiffer();
                !divergentes.isEmpty()) {
                const juce::String texte =
                    tr(u8"Le fichier .mid porte les NOTES du projet, pas ce que la lecture en fait. "
                       u8"Ces pistes sonneront donc autrement dans un autre logiciel :")
                    + "\n\n" + divergentes.joinIntoString("\n") + "\n\n"
                    + tr(u8"Pour les rendre définitives : Piste ▸ Effets MIDI ▸ « Reporter les effets MIDI dans les notes ».");
                std::fputs((juce::String::fromUTF8(u8"Export MIDI — divergence : ")
                             + texte.replace("\n", " ; ") + "\n").toRawUTF8(), stderr);
                montrerBoite(
                    juce::AlertWindow::InfoIcon,
                    tr(u8"Ce que le .mid ne porte pas"), texte);
            }
            // D15.5 : le MIDI ne connaît pas les rampes ; elles partent en
            // paliers d'une noire, et on le dit plutôt que de le taire.
            if (project_.tempoMap.hasRamps()) {
                const size_t paliers = project_.tempoMap.flattened(project_.ticksPerQuarterNote,
                                                                   project_.ticksPerQuarterNote).size()
                                     - project_.tempoMap.changes().size();
                montrerBoite(
                    juce::AlertWindow::InfoIcon, tr(u8"Rampes de tempo exportées en paliers"),
                    tr(u8"Le format MIDI ne connaît pas les rampes : elles sont rendues en %1 paliers "
                       u8"d'une noire, à la durée totale près.")
                        .replace("%1", juce::String(static_cast<int>(paliers))));
            }
        } catch (const std::exception& e) {
            montrerBoite(juce::AlertWindow::WarningIcon,
                                                     tr("Erreur d'export MIDI"), e.what());
        }
    });
}

// ---------------------------------------------------------------------------
// D3.3 — ENREGISTREMENT MIDI TEMPS RÉEL
//
// Le trajet complet, parce qu'il traverse trois threads et qu'il vaut mieux
// l'avoir écrit une fois : le clavier arrive sur le THREAD MIDI, où
// `AudioEngine` le date sur la ligne de temps et le pousse dans une file
// lock-free ; le THREAD UI la vide ici à chaque tour de timer et la verse dans
// `MidiRecorder` ; à l'arrêt, l'enregistreur apparie les touches en notes et
// les écrit dans les pistes armées, en une seule action annulable. Le THREAD
// AUDIO, lui, ne connaît rien de tout cela : il publie seulement l'ancre qui
// permet de dater, et joue ce qu'on lui envoie en écoute.
// ---------------------------------------------------------------------------

std::vector<size_t> MainComponent::armedTrackIndices() const {
    std::vector<size_t> armees;
    for (size_t i = 0; i < project_.tracks.size() && i < vsm::audio::engine::ProcessGraph::kMaxTracks; ++i)
        if (project_.tracks[i].armed) armees.push_back(i);
    return armees;
}

std::vector<size_t> MainComponent::armedTrackIndices(Track::Kind kind) const {
    std::vector<size_t> armees;
    for (size_t i = 0; i < project_.tracks.size() && i < vsm::audio::engine::ProcessGraph::kMaxTracks; ++i)
        if (project_.tracks[i].armed && project_.tracks[i].kind == kind) armees.push_back(i);
    return armees;
}

juce::String MainComponent::nextTakeRelativePath(const juce::String& nomDePiste) const {
    // Un nom LISIBLE, et surtout LIBRE : on ne réutilise jamais celui d'une
    // prise existante. Écraser une prise précédente parce qu'on a rearmé la
    // même piste serait la faute la moins pardonnable d'un enregistreur.
    juce::String base = nomDePiste.isEmpty() ? "prise" : nomDePiste;
    base = base.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_");
    if (base.isEmpty()) base = "prise";
    for (int n = 1; n < 10000; ++n) {
        const juce::String relatif = "audio/" + base + "-" + juce::String(n) + ".wav";
        if (!currentProjectFolder_.getChildFile(relatif).existsAsFile()) return relatif;
    }
    return "audio/" + base + "-" + juce::String(juce::Time::currentTimeMillis()) + ".wav";
}

bool MainComponent::applyAudioTake(size_t trackIndex, const juce::File& fichier, int64_t frames) {
    if (trackIndex >= project_.tracks.size() || frames <= 0 || !fichier.existsAsFile()) return false;
    Track& piste = project_.tracks[trackIndex];

    // UNE PISTE AUDIO PORTE UN SEUL FICHIER (`Track::audio`), et c'est ce qui
    // décide du comportement ici : une nouvelle prise REMPLACE le matériau de la
    // piste, quel que soit le mode d'enregistrement. Superposer deux prises
    // audio sur une même piste demanderait plusieurs matériaux par piste, ce que
    // le modèle n'a pas -- c'est l'objet de D3.5, où les prises s'empilent et se
    // choisissent. Le mode « superposer / remplacer » ne concerne donc que le
    // MIDI, et le menu le dit.
    piste.audio.path = audioTakeRelativePath_.toStdString();
    piste.audio.sampleRate = audioEngine_.diskRecorder().sampleRate();
    piste.audio.frames = frames;
    piste.audio.channels = audioEngine_.diskRecorder().channels();

    // Le clip est posé AU POINT D'ENTRÉE, et sa longueur est laissée à zéro --
    // ce qui veut dire « jusqu'au bout du fichier » (voir `Clip::length`). Le
    // premier échantillon du fichier est celui du point d'entrée : c'est le
    // rappel audio qui s'en assure, à l'échantillon près.
    piste.clips.clear();
    vsm::sequencer::Clip clip;
    clip.startTick = punchTick_;
    clip.length = 0;
    clip.name = juce::File(audioTakeRelativePath_).getFileNameWithoutExtension().toStdString();
    clip.colorRgba = piste.colorRgba;
    piste.clips.push_back(clip);
    return true;
}

void MainComponent::measureInputLatency() {
    if (recordPhase_ != RecordPhase::Off) return;   // pas pendant une prise
    if (!audioEngine_.startLatencyMeasurement()) {
        boiteMesureImpossible();
        return;
    }

    // LA MESURE DURE UNE DEMI-SECONDE ET SE FAIT DANS LE RAPPEL AUDIO. On
    // revient la chercher après, sur le thread de l'interface -- attendre ici
    // gèlerait la fenêtre pendant que la carte travaille.
    const int attente = static_cast<int>(
        (vsm::audio::engine::LatencyProbe::kProbeSeconds
         + vsm::audio::engine::LatencyProbe::kListenSeconds) * 1000.0) + 250;
    juce::Timer::callAfterDelay(attente, [this] {
        const auto resultat = audioEngine_.finishLatencyMeasurement();
        const double sr = audioEngine_.currentSampleRate();

        // UN CHIFFRE PEU NET EST REFUSÉ, PAS PUBLIÉ. C'est le cas du câble non
        // branché : la corrélation trouve bien un maximum quelque part dans le
        // bruit, et l'appliquer décalerait toutes les prises suivantes d'une
        // valeur inventée qu'on ne remettrait jamais en question.
        constexpr double kNetteteMinimale = 10.0;
        if (!resultat.trouve() || resultat.nettete < kNetteteMinimale || sr <= 0.0) {
            boiteRienNestRevenu(resultat.nettete);
            return;
        }

        const double secondes = static_cast<double>(resultat.decalageEchantillons) / sr;
        audioEngine_.setMeasuredRoundTripSeconds(secondes);
        vsm::app::ui::UiScale::properties().setValue("latenceAllerRetour", secondes);
        boiteLatenceMesuree(secondes, resultat.decalageEchantillons, sr, resultat.nettete);
    });
}

// D112 : LES BOÎTES DE L'ENREGISTREMENT, UNE FONCTION CHACUNE. Le chemin réel et
// le banc (`VSM_BOITE_ESSAI`) passent par la même : un banc qui montrerait sa
// propre copie de la phrase vérifierait un texte que personne ne lit. Aucune ne
// fait autre chose que montrer -- la latence est retenue par l'appelant.
void MainComponent::boiteNotesPerdues() {
    montrerBoite(
        juce::AlertWindow::WarningIcon, tr(u8"Notes perdues à l'enregistrement"),
        tr(u8"La file de capture a débordé : des notes jouées ne sont PAS dans la "
           u8"prise. Signalez-le -- ce n'est pas censé pouvoir arriver."));
}

void MainComponent::boiteMesureImpossible() {
    montrerBoite(
        juce::AlertWindow::InfoIcon, tr(u8"Mesure impossible"),
        tr(u8"La carte n'ouvre aucune entrée : il n'y a rien à mesurer. "
           u8"Voir Fichier > Réglages audio."));
}

void MainComponent::boiteRienNestRevenu(double nettete) {
    montrerBoite(
        juce::AlertWindow::WarningIcon, tr(u8"Rien n'est revenu"),
        tr(u8"Le balayage émis n'a pas été retrouvé dans l'entrée (netteté %1). Branchez la "
           u8"sortie de la carte sur son entrée, ou placez un micro devant un haut-parleur, et "
           u8"recommencez. Aucune valeur n'a été retenue : mieux vaut ne pas compenser que "
           u8"compenser d'un chiffre inventé.")
            .replace("%1", juce::String(nettete, 1)));
}

void MainComponent::boiteLatenceMesuree(double secondes, int decalageEchantillons, double sr,
                                        double nettete) {
    montrerBoite(
        juce::AlertWindow::InfoIcon, tr(u8"Latence mesurée"),
        // Trois paragraphes, trois clés : une clé portant « \n\n » serait la seule de la table.
        tr(u8"Aller-retour : %1 ms (%2 échantillons à %3 kHz)")
                .replace("%1", juce::String(secondes * 1000.0, 2))
                .replace("%2", juce::String(decalageEchantillons))
                .replace("%3", juce::String(sr / 1000.0, 1))
            + "\n\n" + tr(u8"Netteté du pic : %1").replace("%1", juce::String(nettete, 1))
            + "\n\n"
            + tr(u8"Les prises AUDIO sont désormais avancées d'autant. Les prises MIDI, elles, "
                 u8"continuent d'employer la latence de sortie annoncée par le pilote : un clavier "
                 u8"n'est pas dans la boucle, et cette mesure ne peut rien en dire."));
}

// D114 : la raison est celle que `ReconstructionChain::locate` a rendue -- des
// DONNÉES, françaises à la source (interchange/).
void MainComponent::boiteReconstructionIndisponible() {
    montrerBoite(
        juce::AlertWindow::InfoIcon,
        tr(u8"Reconstruction indisponible"),
        vsm::app::ui::trPhrase(juce::String::fromUTF8(reconstructionChain_.reason.c_str())) + "\n\n"
            + vsm::app::ui::trPhrase(juce::String::fromUTF8(reconstructionChain_.remedy.c_str())));
}

bool MainComponent::showBoxForCapture(const juce::String& nom) {
    // D112 : chiffres fixes, écrits au ROADMAP -- 590 échantillons à 48 kHz.
    if (nom == "perdues") { boiteNotesPerdues(); return true; }
    if (nom == "impossible") { boiteMesureImpossible(); return true; }
    if (nom == "rien") { boiteRienNestRevenu(3.2); return true; }
    if (nom == "latence") { boiteLatenceMesuree(590.0 / 48000.0, 590, 48000.0, 42.5); return true; }
    if (nom == "disque") { signalerDisqueTropLent(3); return true; }
    if (nom == "indisponible") { boiteReconstructionIndisponible(); return true; }   // D114
    return false;
}

void MainComponent::ouvrirLEditionDEnregistrement() {
    if (recordEditOpened_) return;
    recordEditOpened_ = true;
    beginProjectEdit("Enregistrement");
}

double MainComponent::punchOutSeconds() const {
    if (!project_.punchEnabled || project_.punchEndTick <= project_.punchStartTick)
        return std::numeric_limits<double>::infinity();
    return project_.ticksToSeconds(project_.punchEndTick);
}

void MainComponent::closePass(uint32_t passe, double debutSecondes, double finSecondes) {
    // UNE PASSE QU'ON N'A PAS JOUÉE NE DEVIENT PAS UNE PRISE. Empiler des
    // prises vides obligerait à les écarter une par une, et la pile ne
    // servirait plus à rien.
    const bool desNotes = recorder_.hasPass(passe);
    const bool duSon = audioTakeTrack_ != static_cast<size_t>(-1);
    if (!desNotes && !duSon) return;

    const vsm::midi::Tick debutTick = project_.secondsToTicks(debutSecondes);
    const vsm::midi::Tick finTick = project_.secondsToTicks(finSecondes);
    const juce::String nom = "Prise " + juce::String(static_cast<int>(passe) + 1);

    // L'INSTANTANÉ D'ANNULATION EST PRIS ICI, à la première passe qui produit
    // quelque chose -- pas à l'arrêt. En boucle, les passes précédentes ont déjà
    // modifié le projet quand on s'arrête : un instantané pris à ce moment-là ne
    // permettrait de défaire que la dernière, et annuler un enregistrement doit
    // le défaire EN ENTIER.
    ouvrirLEditionDEnregistrement();

    uint64_t compteur = project_.peekNextNoteId();
    if (desNotes) for (size_t index : armedTrackIndices(Track::Kind::Midi)) {
        if (index >= project_.tracks.size()) continue;
        vsm::sequencer::Take prise;
        prise.name = nom.toStdString();
        prise.startTick = debutTick;
        prise.endTick = finTick;
        prise.notes = recorder_.finishPass(passe, finSecondes,
                                            [this](double s) { return project_.secondsToTicks(s); },
                                            compteur);
        for (auto& note : prise.notes) note.channel = project_.tracks[index].channel;
        vsm::sequencer::pushTake(project_.tracks[index], std::move(prise));
    }
    if (compteur > 0) project_.ensureNoteIdAbove(compteur - 1);

    // LA PRISE AUDIO D'UNE PASSE EST UNE FENÊTRE, PAS UN FICHIER. Toutes les
    // passes partagent le fichier ouvert au début de la session : le découper
    // au passage exact de la boucle demanderait de fermer et rouvrir un fichier
    // au seul endroit où il ne faut surtout pas faire de pause.
    if (duSon && audioTakeTrack_ < project_.tracks.size()) {
        vsm::sequencer::Take prise;
        prise.name = nom.toStdString();
        prise.startTick = debutTick;
        prise.endTick = finTick;
        prise.audio.path = audioTakeRelativePath_.toStdString();
        prise.audio.sampleRate = audioEngine_.diskRecorder().sampleRate();
        prise.audio.frames = audioEngine_.diskRecorder().framesWritten();
        prise.audio.channels = audioEngine_.diskRecorder().channels();
        vsm::sequencer::Clip clip;
        clip.startTick = debutTick;
        clip.length = finTick - debutTick;
        clip.sourceStartSeconds =
            std::max(0.0, debutSecondes - audioTakeSessionStartSeconds_)
            + static_cast<double>(passe) * std::max(0.0, finSecondes - debutSecondes);
        clip.name = nom.toStdString();
        prise.clips.push_back(clip);
        vsm::sequencer::pushTake(project_.tracks[audioTakeTrack_], std::move(prise));
    }
}

void MainComponent::refreshArmedTracks() {
    const auto armees = armedTrackIndices();
    recordDeviceWasOpen_ = audioEngine_.isDeviceOpen();
    transportBar_.setRecordAvailable(recordDeviceWasOpen_, static_cast<int>(armees.size()));
    // Seules les pistes MIDI reçoivent le clavier : une piste audio armée
    // attend un signal, pas des notes, et lui en envoyer ne ferait rien de
    // visible tout en laissant croire le contraire à la lecture du code.
    audioEngine_.setArmedTracks(armedTrackIndices(Track::Kind::Midi));
}

double MainComponent::countInSeconds(vsm::midi::Tick punchTick) const {
    if (countInBars_ <= 0) return 0.0;
    const vsm::midi::Tick parMesure =
        project_.timeSignatureMap.ticksPerBar(punchTick, project_.ticksPerQuarterNote);
    if (parMesure <= 0) return 0.0;
    // La DURÉE d'un décompte de N mesures se mesure sur la carte de tempo,
    // depuis le point d'entrée en remontant : à tempo variable, deux mesures
    // avant la mesure 30 ne durent pas ce que durent les deux premières.
    const vsm::midi::Tick debut = punchTick - parMesure * countInBars_;
    return project_.ticksToSeconds(punchTick) - project_.ticksToSeconds(debut);
}

void MainComponent::startRecording() {
    auto armees = armedTrackIndices();
    if (armees.empty() || !audioEngine_.isDeviceOpen()) {
        // Le bouton est censé être désactivé dans ces deux cas ; si on arrive
        // quand même ici, on le DIT plutôt que d'enregistrer dans le vide.
        transportBar_.setRecording(false);
        montrerBoite(
            juce::AlertWindow::InfoIcon,
            armees.empty() ? tr(u8"Aucune piste armée") : tr("Aucune carte son"),
            armees.empty()
                ? tr(u8"Armez au moins une piste (bouton R dans la liste des pistes) : "
                     u8"sans elle, la prise n'aurait nulle part où aller.")
                : tr(u8"Sans carte son ouverte, le transport n'avance pas et aucun clavier "
                     u8"MIDI n'est écouté. Voir Fichier > Réglages audio."));
        return;
    }
    if (recordPhase_ != RecordPhase::Off) return;

    // POINT D'ENTRÉE : là où se trouve la tête de lecture. Si le transport joue
    // déjà, on entre en marche (punch in) et il n'y a pas de décompte -- compter
    // par-dessus la musique qui joue n'aurait aucun sens.
    const bool dejaEnLecture = transport_.state() == TransportState::Playing;
    // LA RÉGION DE PUNCH L'EMPORTE quand elle est active : c'est tout son objet,
    // refaire un passage précis sans avoir à viser la tête de lecture à la
    // souris. Sans elle, le point d'entrée reste là où l'on est.
    const bool punchDefini = project_.punchEnabled
                             && project_.punchEndTick > project_.punchStartTick;
    punchTick_ = punchDefini
                     ? project_.punchStartTick
                     : (dejaEnLecture
                            ? project_.secondsToTicks(std::max(0.0, audioEngine_.processGraph().currentSeconds()))
                            : transport_.currentTick());
    punchSeconds_ = project_.ticksToSeconds(punchTick_);
    // Le décompte a encore un sens sur un punch : on entre en marche, mais on
    // n'a pas forcément écouté ce qui précède.
    const double decompte = (dejaEnLecture && !punchDefini) ? 0.0 : countInSeconds(punchTick_);

    // LA PRISE AUDIO, s'il y a une piste audio armée. Tout ce qui peut échouer
    // (pas de dossier de projet, pas d'entrée, fichier impossible à créer)
    // échoue MAINTENANT, avant qu'on ait joué -- découvrir après trois minutes
    // que rien n'a été écrit serait la pire façon de l'apprendre.
    audioTakeTrack_ = static_cast<size_t>(-1);
    audioTakeFile_ = juce::File();
    audioTakeRelativePath_.clear();
    auto armeesAudio = armedTrackIndices(Track::Kind::Audio);
    if (armeesAudio.size() > 1) {
        transportBar_.setRecording(false);
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Plusieurs pistes audio armées"),
            tr(u8"Une seule entrée, une seule prise : n'armez qu'une piste audio à "
                          u8"la fois. Écrire le même signal dans deux fichiers ne ferait que "
                          u8"doubler la place occupée."));
        return;
    }
    if (!armeesAudio.empty()) {
        if (currentProjectFolder_ == juce::File()) {
            transportBar_.setRecording(false);
            montrerBoite(
                juce::AlertWindow::InfoIcon, tr(u8"Projet jamais enregistré"),
                tr(u8"Une prise audio est un FICHIER, et le format range les fichiers "
                              u8"d'un projet par chemin relatif à son dossier -- c'est ce qui "
                              u8"permet d'ouvrir le projet sur une autre machine. Enregistrez "
                              u8"d'abord le projet (Ctrl+S), la prise ira dans son sous-dossier "
                              u8"audio/."));
            return;
        }
        const size_t index = armeesAudio.front();
        audioTakeRelativePath_ = nextTakeRelativePath(juce::String(project_.tracks[index].name));
        audioTakeFile_ = currentProjectFolder_.getChildFile(audioTakeRelativePath_);
        juce::String erreur;
        if (!audioEngine_.startAudioRecording(audioTakeFile_, punchSeconds_, erreur)) {
            transportBar_.setRecording(false);
            montrerBoite(
                juce::AlertWindow::WarningIcon, tr(u8"Enregistrement audio impossible"),
                vsm::app::ui::trPhrase(erreur));
            return;
        }
        audioTakeTrack_ = index;
    }

    recorder_.begin(punchSeconds_, punchOutSeconds());
    audioEngine_.setRecordPunchOut(punchOutSeconds());
    recordDrain_.clear();
    recordDropReported_ = false;
    recordEditOpened_ = false;
    loopPassesClosed_ = 0;
    audioTakeSessionStartSeconds_ = punchSeconds_;
    audioEngine_.setRecording(true);
    transportBar_.setRecording(true);

    if (decompte > 0.0) {
        recordPhase_ = RecordPhase::CountIn;
        // Le décompte est un morceau de ligne de temps situé AVANT le point
        // d'entrée : le moteur y saute, le métronome y bat de lui-même (voir
        // ProcessGraph::processBlock), et le transport MIDI attend son tour.
        transport_.seekSeconds(punchSeconds_ - decompte);
        transport_.play();
    } else {
        recordPhase_ = RecordPhase::Recording;
        if (!dejaEnLecture) {
            transport_.seekSeconds(punchSeconds_);
            transport_.play();
        }
    }
}

void MainComponent::drainRecording() {
    recordDrain_.clear();
    audioEngine_.drainRecordedEvents(recordDrain_);
    for (const auto& evenement : recordDrain_) recorder_.push(evenement);
    // D24.2 : les contrôleurs avec les notes.
    recordControlDrain_.clear();
    audioEngine_.drainRecordedControls(recordControlDrain_);
    for (const auto& c : recordControlDrain_) recorder_.pushControl(c);
}

void MainComponent::stopRecording() {
    if (recordPhase_ == RecordPhase::Off) return;

    drainRecording();   // ce qui restait dans la file appartient à la prise
    audioEngine_.setRecording(false);
    const RecordPhase phase = recordPhase_;
    recordPhase_ = RecordPhase::Off;
    transportBar_.setRecording(false);
    transportBar_.setCountIn(0);

    // LE FICHIER SE FERME DANS TOUS LES CAS, décompte interrompu compris :
    // laisser un rédacteur ouvert garderait le fichier verrouillé et le thread
    // d'écriture au travail sur une prise que personne n'attend plus.
    const int64_t tramesAudio = audioEngine_.stopAudioRecording();
    const uint64_t blocsPerdus = audioEngine_.diskRecorder().droppedBlocks();

    // Arrêté pendant le décompte : il n'y a rien à écrire, et il ne faut
    // surtout pas laisser le moteur à une position négative.
    if (phase == RecordPhase::CountIn) {
        audioEngine_.processGraph().setPlaying(false);
        audioEngine_.processGraph().seekSeconds(punchSeconds_);
        if (audioTakeFile_ != juce::File()) audioTakeFile_.deleteFile();  // prise vide
        audioTakeTrack_ = static_cast<size_t>(-1);
        return;
    }

    const double finSecondes =
        std::max(punchSeconds_, audioEngine_.processGraph().currentSeconds());
    const vsm::midi::Tick finTick = project_.secondsToTicks(finSecondes);
    auto armees = armedTrackIndices(Track::Kind::Midi);
    const bool priseAudio = audioTakeTrack_ != static_cast<size_t>(-1) && tramesAudio > 0;
    const bool priseMidi = !recorder_.empty() && !armees.empty();

    if (!priseMidi && !priseAudio) {
        // Rien n'a été joué : pas de pas d'annulation pour un geste sans effet,
        // et pas de fichier vide qui traîne dans le dossier du projet.
        if (audioTakeFile_ != juce::File() && tramesAudio <= 0) audioTakeFile_.deleteFile();
        audioTakeTrack_ = static_cast<size_t>(-1);
        return;
    }

    // UNE SEULE ACTION ANNULABLE pour toute la prise, même si elle atterrit sur
    // plusieurs pistes et sur plusieurs passes de boucle : annuler un
    // enregistrement, c'est le défaire en entier.
    ouvrirLEditionDEnregistrement();

    const size_t audioTakeTrackApplique = audioTakeTrack_;

    // MODE EMPILÉ : la dernière passe -- qui n'est pas forcément complète -- est
    // une prise comme les autres, et le matériau de la piste ne se mélange à
    // rien. Les passes précédentes ont déjà été empilées au fil des
    // rebouclages.
    if (recordMode_ == vsm::sequencer::RecordMode::Stack) {
        closePass(static_cast<uint32_t>(loopPassesClosed_), punchSeconds_, finSecondes);
        // TOUTES LES PASSES PARTAGENT UN FICHIER, dont la longueur définitive
        // n'est connue qu'ici : chacune avait noté celle qu'il avait au moment
        // où elle s'est fermée. Le projet écrirait sinon des longueurs fausses
        // -- rattrapées au chargement, qui relit le fichier, mais fausses
        // quand même sur le disque.
        if (priseAudio && audioTakeTrackApplique < project_.tracks.size())
            for (auto& prise : project_.tracks[audioTakeTrackApplique].takes)
                if (!prise.audio.path.empty()) prise.audio.frames = tramesAudio;
        audioTakeTrack_ = static_cast<size_t>(-1);
        lastTake_.clear();
        if (transport_.state() == TransportState::Playing)
            audioEngine_.processGraph().setProject(project_);
        else
            refreshTransportSchedule();
        if (priseAudio) {
            loadAudioTracks();
            trackList_.refreshTrackRow(audioTakeTrackApplique);
        }
        pianoRollPanel_.refresh();
        pianoRoll_.repaint();
        if (priseAudio && blocsPerdus > 0) signalerDisqueTropLent(blocsPerdus);
        return;
    }

    if (priseAudio) applyAudioTake(audioTakeTrack_, audioTakeFile_, tramesAudio);
    audioTakeTrack_ = static_cast<size_t>(-1);

    lastTake_.clear();
    // Un SEUL compteur d'identifiants pour toutes les pistes armées : la même
    // prise écrite sur deux pistes doit donner des notes distinctes, sinon la
    // sélection et l'automation liée confondraient les unes avec les autres.
    uint64_t compteur = project_.peekNextNoteId();
    for (size_t index : armees) {
        if (index >= project_.tracks.size()) continue;
        auto notes = recorder_.finish(finSecondes,
                                       [this](double s) { return project_.secondsToTicks(s); },
                                       compteur);
        vsm::sequencer::NoteSelection ids;
        for (const auto& note : notes) ids.insert(note.id);
        vsm::sequencer::applyRecording(project_.tracks[index], notes, recordMode_,
                                        punchTick_, finTick);
        // D24.2 : LES CONTRÔLEURS DE LA PRISE, sur la même piste, même plage,
        // même règle -- remplacer efface, superposer ajoute.
        if (recorder_.hasControls())
            vsm::sequencer::applyRecordedControls(
                project_.tracks[index],
                recorder_.finishControls(finSecondes, [this](double s) { return project_.secondsToTicks(s); }),
                recordMode_ == vsm::sequencer::RecordMode::Replace, punchTick_, finTick);
        lastTake_.emplace_back(index, std::move(ids));
    }
    if (compteur > 0) project_.ensureNoteIdAbove(compteur - 1);

    // La prise est SÉLECTIONNÉE dans le piano roll : c'est ce qui rend la
    // quantification après coup possible sans écrire un second chemin de
    // quantification -- la commande Quantifier porte alors exactement sur ce
    // qu'on vient de jouer.
    // PUBLIER SANS INTERROMPRE. `refreshTransportSchedule()` arrête et relance
    // le transport MIDI, dont l'arrêt REMET LA POSITION À ZÉRO : l'employer ici
    // renverrait la lecture au début du morceau à chaque sortie en marche
    // (punch out). Le moteur audio, lui, republie sans rien interrompre.
    if (transport_.state() == TransportState::Playing)
        audioEngine_.processGraph().setProject(project_);
    else
        refreshTransportSchedule();
    // La prise audio n'est audible qu'une fois RELUE depuis le disque : c'est le
    // même chemin que pour n'importe quel fichier du projet, et c'est aussi ce
    // qui vérifie tout de suite que le fichier écrit est lisible.
    if (priseAudio) {
        loadAudioTracks();
        trackList_.refreshTrackRow(audioTakeTrackApplique);
    }
    pianoRollPanel_.refresh();
    if (!lastTake_.empty()) {
        trackList_.selectTrackIndex(lastTake_.front().first);
        pianoRoll_.selectNotes(lastTake_.front().second);
    }

    if (priseAudio && blocsPerdus > 0) signalerDisqueTropLent(blocsPerdus);
}

void MainComponent::signalerDisqueTropLent(uint64_t blocsPerdus) {
    // UN TROU DANS LE FICHIER SE DIT. Le tampon d'une seconde n'est pas censé
    // déborder ; s'il a débordé, le disque n'a pas suivi et la prise a perdu des
    // échantillons -- une chose qu'on n'entend pas forcément à la première
    // écoute et qu'on découvrirait bien plus tard.
    montrerBoiteLisible(   // D126 : « it. » restait seul en anglais (D125)
        juce::AlertWindow::WarningIcon, tr(u8"Le disque n'a pas suivi"),
        tr(u8"La prise a perdu %1 bloc(s) : le fichier a des trous. Un disque plus rapide, "
           u8"ou une taille de bloc audio plus grande, y remédient.")
            .replace("%1", juce::String(static_cast<int>(blocsPerdus))));
}

void MainComponent::quantizeLastTake() {
    if (lastTake_.empty()) return;
    // On repasse par la SÉLECTION et par la commande existante du piano roll :
    // la grille, le swing et la force sont ceux que l'utilisateur a réglés dans
    // sa barre d'outils, et il n'y a qu'une seule quantification dans le
    // logiciel -- donc pas deux comportements à faire coïncider.
    trackList_.selectTrackIndex(lastTake_.front().first);
    pianoRoll_.setActiveTrackIndex(lastTake_.front().first);
    pianoRoll_.selectNotes(lastTake_.front().second);
    pianoRoll_.quantizeSelection(1.0f, false);
    pianoRollPanel_.refresh();
}

void MainComponent::beginProjectEdit(const juce::String& label) {
    history_.beginEdit(project_, label.toStdString());
    refreshHistoryList();
    // TOUTES LES MODIFICATIONS ANNULABLES PASSENT PAR ICI (D10.4) : c'est
    // l'endroit qui ne peut pas être oublié, parce qu'oublier de l'appeler
    // casserait déjà l'annulation, ce qui se voit tout de suite.
    markProjectDirty();
}

bool MainComponent::materializeImplicitClips() {
    bool cree = false;
    for (auto& piste : project_.tracks) {
        const bool aDuMateriau =
            !piste.notes.empty()
            || (piste.kind == vsm::sequencer::Track::Kind::Audio
                && piste.audio.sampleRate > 0.0);
        if (!aDuMateriau || !piste.clips.empty()) continue;
        vsm::sequencer::Clip clip;
        clip.name = piste.name;
        clip.colorRgba = piste.colorRgba;
        piste.clips.push_back(std::move(clip));
        cree = true;
    }
    project_.assignClipIds();
    return cree;
}

void MainComponent::setTimeSignatureAtPlayhead(int numerator, int denominator) {
    // AU DÉBUT DE LA MESURE QUI CONTIENT LA TÊTE : une signature qui changerait
    // au milieu d'une mesure ferait une mesure de longueur impossible.
    auto& carte = project_.timeSignatureMap;
    const vsm::midi::Tick ici = std::max<vsm::midi::Tick>(0, transport_.currentTick());
    vsm::midi::Tick origine = 0;
    for (const auto& c : carte.changes())
        if (c.tick <= ici) origine = c.tick;
    const vsm::midi::Tick parMesure = std::max<vsm::midi::Tick>(1, carte.ticksPerBar(ici, project_.ticksPerQuarterNote));
    const vsm::midi::Tick debut = origine + ((ici - origine) / parMesure) * parMesure;
    if (numerator <= 0) {
        // RETIRER : seulement un changement posé au début de CETTE mesure.
        if (debut == 0) {
            montrerBoite(juce::AlertWindow::InfoIcon, tr(u8"Signature"),
                                                     tr(u8"La signature du départ ne se retire pas : un morceau en a toujours une. Changez-la."));
            return;
        }
        beginProjectEdit(u8"Retirer un changement de signature");
        if (!carte.removeChangeAt(debut)) {
            montrerBoite(juce::AlertWindow::InfoIcon, tr(u8"Signature"),
                                                     tr(u8"Aucun changement de signature ne commence à cette mesure."));
            return;
        }
    } else {
        int pow2 = 2;
        while ((1 << pow2) < denominator) ++pow2;
        beginProjectEdit(juce::String(u8"Signature ") + juce::String(numerator) + "/" + juce::String(denominator));
        carte.addChange(debut, static_cast<uint8_t>(numerator), static_cast<uint8_t>(pow2));
    }
    markProjectDirty();
    derniereSignatureNum_ = 0;   // la barre de transport relira la signature sous la tête
    arrangement_.repaint();
    pianoRollPanel_.refresh();
    pianoRoll_.repaint();
    std::fputs((juce::String(u8"Signature : ") + juce::String(carte.numeratorAt(debut)) + "/"
                + juce::String(static_cast<int>(carte.denominatorAt(debut))) + juce::String(u8" \u00e0 partir du tick ")
                + juce::String(static_cast<int64_t>(debut)) + "\n").toRawUTF8(), stderr);
}

void MainComponent::soloTrackExclusively(size_t index) {
    if (index >= project_.tracks.size()) return;
    // SI ELLE ÉTAIT DÉJÀ SEULE EN SOLO, tout s'éteint : un second Ctrl+clic
    // rend le mélange entier, comme dans Cubase et Live.
    const size_t solos = static_cast<size_t>(std::count_if(
        project_.tracks.begin(), project_.tracks.end(), [](const Track& t) { return t.solo; }));
    const bool dejaSeule = project_.tracks[index].solo && solos == 1;
    beginProjectEdit(u8"Solo exclusif");
    for (size_t i = 0; i < project_.tracks.size(); ++i)
        project_.tracks[i].solo = !dejaSeule && i == index;
    mixer_.refreshMuteSolo();
    trackList_.repaint();
    if (mixer_.onMixChanged) mixer_.onMixChanged();
}

void MainComponent::toggleSoloSafeSelectedTrack() {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;
    auto& track = project_.tracks[piste];
    beginProjectEdit(track.soloSafe ? u8"Ne plus protéger du solo" : u8"Protéger du solo");
    track.soloSafe = !track.soloSafe;
    // Le moteur lit le champ dans l'instantané du projet, à chaque bloc : il
    // suffit de le lui republier. Les tranches, elles, ont un libellé à
    // changer -- « S » devient « S+ ».
    mixer_.refreshMuteSolo();
    refreshTrackViews();
    captureSessionIntoProject();
}

void MainComponent::toggleDisableSelectedTrack() {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;
    auto& track = project_.tracks[piste];
    const bool eteinte = !track.disabled;
    beginProjectEdit(eteinte ? u8"Désactiver une piste" : u8"Réactiver une piste");
    track.disabled = eteinte;
    // ICI IL FAUT REPUBLIER, et c'est toute la différence avec le muet, le
    // masquage et le verrou : ce qui change n'est pas ce qu'on entend d'un
    // rendu inchangé, c'est ce que le moteur TIENT -- l'instrument et la
    // chaîne d'inserts sont défaits, puis refaits tels quels au retour.
    rebuildFromProject(/*stopPlayback=*/false);
    // PANNE MUETTE INTERDITE : une piste qui sort du morceau le DIT. Sur
    // stderr et non dans une boîte : c'est un geste qu'on répète, et une boîte
    // à fermer à chaque fois ne serait pas lue longtemps -- mais un geste
    // piloté par VSM_MENU dont on ne lit aucun compte rendu est un geste qu'on
    // croit fait.
    juce::String message = juce::String::fromUTF8(eteinte ? u8"Piste désactivée : « "
                                                          : u8"Piste réactivée : « ");
    message += juce::String::fromUTF8(track.name.c_str());
    message += juce::String::fromUTF8(eteinte
        ? u8" ». Sa machine et ses inserts sont libérés ; ses notes et ses réglages restent.\n"
        : u8" ». Sa machine et ses inserts sont revenus.\n");
    std::fputs(message.toRawUTF8(), stderr);
}

void MainComponent::copySelectedTrackChain() {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;
    chainClipboard_ = project_.tracks[piste].effects;
    // Pas d'entrée d'annulation : COPIER NE CHANGE RIEN au projet. Ce qui
    // s'annule est le collage.
    juce::String message = juce::String::fromUTF8(u8"Chaîne copiée : ")
                           + juce::String(static_cast<int>(chainClipboard_.size()))
                           + juce::String::fromUTF8(u8" insert(s) de « ")
                           + juce::String::fromUTF8(project_.tracks[piste].name.c_str())
                           + juce::String::fromUTF8(u8" ».\n");
    std::fputs(message.toRawUTF8(), stderr);
}

void MainComponent::pasteChainIntoSelectedTrack(bool remplace) {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size() || chainClipboard_.empty()) return;
    auto& cible = project_.tracks[piste];
    const size_t avant = cible.effects.size();
    beginProjectEdit(remplace ? u8"Coller la chaîne d'inserts"
                              : u8"Ajouter la chaîne d'inserts");
    if (remplace) cible.effects = chainClipboard_;
    else cible.effects.insert(cible.effects.end(), chainClipboard_.begin(), chainClipboard_.end());
    // Les instances sont refabriquées depuis la DESCRIPTION, par le chemin
    // habituel : c'est ce qui garantit qu'une chaîne collée est montée
    // exactement comme une chaîne saisie à la main, état natif compris.
    effectChain_.rebuildFromProject();
    effectChain_.setActiveTrack(static_cast<int>(piste));
    refreshTrackViews();
    juce::String message = juce::String::fromUTF8(remplace ? u8"Chaîne collée sur « "
                                                           : u8"Chaîne ajoutée à « ");
    message += juce::String::fromUTF8(cible.name.c_str());
    message += juce::String::fromUTF8(u8" » : ") + juce::String(static_cast<int>(avant))
               + juce::String::fromUTF8(u8" insert(s) avant, ")
               + juce::String(static_cast<int>(cible.effects.size()))
               + juce::String::fromUTF8(u8" après.\n");
    std::fputs(message.toRawUTF8(), stderr);
}

void MainComponent::thinAutomationOfSelectedTrack() {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;
    auto& track = project_.tracks[piste];
    if (track.automation.empty()) return;

    // LE TÉMOIN D'ABORD : on garde une copie des courbes pour MESURER l'écart
    // entre ce qu'on entendait et ce qu'on entendra. Sans elle, on annoncerait
    // une réduction sans pouvoir dire ce qu'elle a coûté.
    const std::vector<vsm::sequencer::AutomationCurve> avant = track.automation;

    size_t pointsAvant = 0, retires = 0;
    for (const auto& c : avant) pointsAvant += c.points.size();

    beginProjectEdit(u8"Réduire les points d'automation");
    float pireEcart = 0.0f;
    float pireTolerance = 0.0f;
    juce::StringArray sansBornes;
    for (size_t i = 0; i < track.automation.size(); ++i) {
        auto& courbe = track.automation[i];
        float mini = 0.0f, maxi = 1.0f;
        // LES BORNES VIENNENT D'OÙ ELLES VIENNENT DÉJÀ (D5.4) : la même
        // fonction que la vue d'arrangement interroge pour dessiner. Deux
        // sources d'amplitude finiraient par donner deux tolérances.
        if (!arrangement_.automationRange(piste, courbe.parameter, mini, maxi)) {
            // PANNE MUETTE INTERDITE : un paramètre dont on ne connaît pas
            // l'amplitude n'est pas réduit « au jugé », il est LAISSÉ ENTIER et
            // NOMMÉ. Une tolérance inventée retirerait des points selon une
            // échelle qui n'est pas la sienne.
            sansBornes.add(juce::String::fromUTF8(courbe.parameter.c_str()));
            continue;
        }
        const float tolerance = std::fabs(maxi - mini) * 0.01f;   // 1 % de l'amplitude
        pireTolerance = std::max(pireTolerance, tolerance);
        retires += vsm::sequencer::thinAutomation(courbe, tolerance);
        pireEcart = std::max(pireEcart, vsm::sequencer::maxAutomationDeviation(avant[i], courbe));
    }

    applyAutomationFromProject();
    refreshTrackViews();

    size_t pointsApres = 0;
    for (const auto& c : track.automation) pointsApres += c.points.size();
    juce::String message = juce::String::fromUTF8(u8"Réduire les points d'automation : ")
                           + juce::String(static_cast<int>(pointsAvant))
                           + juce::String::fromUTF8(u8" -> ")
                           + juce::String(static_cast<int>(pointsApres))
                           + juce::String::fromUTF8(u8" (") + juce::String(static_cast<int>(retires))
                           + juce::String::fromUTF8(u8" retirés), écart maximal ")
                           + juce::String(pireEcart, 6)
                           + juce::String::fromUTF8(u8" pour une tolérance de ")
                           + juce::String(pireTolerance, 6);
    if (!sansBornes.isEmpty())
        message += juce::String::fromUTF8(u8" ; laissée(s) entière(s) faute d'amplitude connue : ")
                   + sansBornes.joinIntoString(", ");
    std::fputs((message + "\n").toRawUTF8(), stderr);
}

void MainComponent::addMidiEffectToSelectedTrack(const std::string& type) {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;
    const auto params = vsm::sequencer::midiEffectParameters(type);
    if (params.empty()) {
        // PANNE MUETTE INTERDITE, jusque dans l'ajout : un type que `core/` ne
        // connaît pas n'entre pas dans la chaîne, plutôt que d'y dormir.
        std::fputs(("Effet MIDI inconnu, non ajouté : " + type + "\n").c_str(), stderr);
        return;
    }
    beginProjectEdit(u8"Ajouter un effet MIDI");
    vsm::sequencer::MidiEffect effet;
    effet.type = type;
    // LES DÉFAUTS VIENNENT DE `core/`, la même source que les bornes du volet :
    // deux tables de défauts finiraient par en donner deux.
    for (const auto& p : params) effet.parameters[p.name] = p.defaultValue;
    project_.tracks[piste].midiEffects.push_back(std::move(effet));
    effectChain_.rebuildFromProject();
    effectChain_.setActiveTrack(static_cast<int>(piste));
    // CE QU'ON VIENT D'AJOUTER EST CE QU'ON VEUT RÉGLER : le nouvel effet est
    // choisi, ses paramètres s'affichent, et l'on n'a pas à le chercher.
    effectChain_.selectLastMidiEffect();
    refreshTransportSchedule();
    refreshTrackViews();
    std::fputs((juce::String::fromUTF8(u8"Effet MIDI ajouté à « ")
                + juce::String::fromUTF8(project_.tracks[piste].name.c_str())
                + juce::String::fromUTF8(u8" » : ")
                + juce::String::fromUTF8(vsm::sequencer::midiEffectDisplayName(type).c_str())
                + juce::String::fromUTF8(u8" (") 
                + juce::String(static_cast<int>(project_.tracks[piste].midiEffects.size()))
                + juce::String::fromUTF8(u8" en chaîne)\n")).toRawUTF8(), stderr);
}

void MainComponent::clearMidiEffectsOfSelectedTrack() {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size() || project_.tracks[piste].midiEffects.empty()) return;
    beginProjectEdit(u8"Retirer les effets MIDI");
    const size_t combien = project_.tracks[piste].midiEffects.size();
    project_.tracks[piste].midiEffects.clear();
    effectChain_.rebuildFromProject();
    refreshTransportSchedule();
    refreshTrackViews();
    std::fputs((juce::String::fromUTF8(u8"Effets MIDI retirés : ")
                + juce::String(static_cast<int>(combien))
                + juce::String::fromUTF8(u8" ; les notes n'ont pas bougé.\n")).toRawUTF8(), stderr);
}

void MainComponent::bakeMidiEffectsOfSelectedTrack() {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size() || project_.tracks[piste].midiEffects.empty()) return;
    auto& track = project_.tracks[piste];

    vsm::sequencer::MidiEffectReport rapport;
    std::vector<vsm::sequencer::Note> jouees = vsm::sequencer::applyMidiEffects(
        track.midiEffects, track.notes, project_.ticksPerQuarterNote, &rapport);

    const size_t avant = track.notes.size();
    beginProjectEdit(u8"Reporter les effets MIDI dans les notes");
    track.notes = std::move(jouees);
    // LA CHAÎNE EST VIDÉE, sans quoi elle s'appliquerait une SECONDE fois à ce
    // qu'elle vient d'écrire : un arpège arpégé, c'est-à-dire le geste rendu
    // deux fois pour un seul clic.
    track.midiEffects.clear();
    effectChain_.rebuildFromProject();
    refreshTransportSchedule();
    refreshTrackViews();
    pianoRollPanel_.refresh();

    juce::String message = juce::String::fromUTF8(u8"Effets MIDI reportés dans « ")
                           + juce::String::fromUTF8(track.name.c_str())
                           + juce::String::fromUTF8(u8" » : ") + juce::String(static_cast<int>(avant))
                           + juce::String::fromUTF8(u8" note(s) -> ")
                           + juce::String(static_cast<int>(track.notes.size()));
    if (rapport.droppedOutOfRange > 0)
        message += juce::String::fromUTF8(u8" ; ") + juce::String(static_cast<int>(rapport.droppedOutOfRange))
                   + juce::String::fromUTF8(u8" note(s) écartée(s) hors 0..127");
    if (rapport.unknownEffects > 0)
        message += juce::String::fromUTF8(u8" ; ") + juce::String(static_cast<int>(rapport.unknownEffects))
                   + juce::String::fromUTF8(u8" effet(s) inconnu(s) sans effet");
    std::fputs((message + ". La chaîne est vidée.\n").toRawUTF8(), stderr);
}

/// REPUBLIER LE PANNEAU D'ASSEMBLAGE depuis la piste choisie.
///
/// SÉPARÉ DE `showTakeComp` PARCE QUE TROIS GESTES LE CHANGENT SANS L'OUVRIR :
/// retirer une prise (D57), en choisir une autre — le panneau marque « celle
/// qu'on entend » —, et composer. Sans cela le panneau restait sur ce qu'il
/// avait lu à l'ouverture : « 2 au panneau, 1 sur la piste », mesuré à la
/// vérification de D57. Une valeur, deux endroits, un seul qui la relit : c'est
/// la faute que D37 a nommée.
void MainComponent::refreshTakeCompPanel() {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;
    std::vector<juce::String> noms;
    for (const auto& prise : project_.tracks[piste].takes)
        noms.push_back(juce::String(prise.name));
    takeCompPanel_.setTake(std::move(noms), project_.tracks[piste].activeTake,
                            project_.timeSignatureMap.ticksPerBar(0, project_.ticksPerQuarterNote),
                            project_.lastSoundingTick(),
                            project_.tracks[piste].compSegments);   // D55.2
}

void MainComponent::removeTakeFromSelectedTrack(int index) {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;
    // ON MESURE SUR UNE COPIE avant de prendre l'instantané : une suppression
    // qui n'a rien à retirer ne doit pas laisser une entrée dans l'historique.
    auto essai = project_.tracks[piste];
    const auto bilan = vsm::sequencer::removeTake(essai, index);
    if (!bilan.done) {
        std::fputs("Retirer une prise : index hors bornes\n", stderr);
        return;
    }
    beginProjectEdit(u8"Retirer une prise");
    project_.tracks[piste] = std::move(essai);
    rebuildFromProject(false);
    refreshTransportSchedule();
    pianoRollPanel_.refresh();
    arrangement_.repaint();
    refreshHistoryList();
    refreshTakeCompPanel();

    // CE QUE CELA A COÛTÉ EST DIT, jamais subi : un tronçon d'assemblage qui
    // disparaît sans un mot ferait recomposer autre chose la fois d'après.
    juce::String texte = tr(u8"Prise « %1 » retirée du tiroir.")
                             .replace("%1", juce::String::fromUTF8(bilan.name.c_str()));
    if (bilan.wasActive)
        texte += " " + tr(u8"C'était celle qu'on entend : son matériau RESTE sur la piste, il "
                          u8"n'appartient plus à aucune passe.");
    if (bilan.droppedSegments > 0)
        texte += " " + tr(u8"%1 tronçon(s) d'assemblage la désignaient : retirés.")
                           .replace("%1", juce::String(static_cast<int>(bilan.droppedSegments)));
    if (bilan.shiftedSegments > 0)
        texte += " " + tr(u8"%1 tronçon(s) ont reculé d'un rang.")
                           .replace("%1", juce::String(static_cast<int>(bilan.shiftedSegments)));
    std::fputs((texte + "\n").toRawUTF8(), stderr);
    montrerBoite(juce::AlertWindow::InfoIcon,
                                            tr(u8"Retirer une prise"), texte);
}

bool MainComponent::exportProjectMidiForCapture(const juce::File& fichier) {
    captureSessionIntoProject();
    try {
        MidiFileWriter::writeFile(project_.toParsedFileArranged(),
                                   fichier.getFullPathName().toStdString());
    } catch (const std::exception& e) {
        std::fputs(("VSM_EXPORT_MIDI : " + std::string(e.what()) + "\n").c_str(), stderr);
        return false;
    }
    std::fputs(("VSM_EXPORT_MIDI : " + fichier.getFullPathName().toStdString() + "\n").c_str(), stderr);
    return true;
}

juce::StringArray MainComponent::tracksWhoseMidiExportWillDiffer() const {
    juce::StringArray noms;
    // D56.2 : L'AVERTISSEMENT COUVRAIT DEUX CAUSES SUR SIX, et sa phrase de
    // repli — « le .mid portera tout ce qui est joué » — était donc fausse dès
    // qu'une piste était découpée, muette ou décalée. Les clips ne sont plus
    // de la liste : depuis D56.1 l'export les APPLIQUE. Ce qui reste ici est ce
    // que le format ne sait pas porter et qu'on refuse de cuire en silence.
    const bool unSolo = vsm::sequencer::anySoloActive(project_.tracks);
    for (size_t i = 0; i < project_.tracks.size(); ++i) {
        const auto& track = project_.tracks[i];
        if (track.isFolder()) continue;   // un dossier ne porte pas de note
        juce::StringArray causes;
        bool chaine = false;
        for (const auto& fx : track.midiEffects) chaine = chaine || fx.enabled;
        if (chaine) causes.add(tr(u8"effets MIDI"));
        // LA TRANSPOSITION DE PISTE : sa divergence existait depuis D17.5 et
        // n'avait jamais été dite avant D31.5.
        if (track.transposeSemitones != 0)
            causes.add(tr(u8"transposition ")
                        + (track.transposeSemitones > 0 ? "+" : "")
                        + juce::String(track.transposeSemitones));
        // LE SILENCE, ET D'OÙ IL VIENT. Le muet n'est pas cuit dans le fichier
        // — c'est un état de mixage qu'on change dix fois par heure, et l'y
        // écrire ferait dépendre l'export du dernier bouton pressé. Mais une
        // piste qu'on n'entend pas et qui sonnera ailleurs doit être NOMMÉE.
        if (!vsm::sequencer::trackAudible(project_.tracks, i, unSolo)) {
            if (track.disabled) causes.add(tr(u8"piste désactivée"));
            else if (track.muted) causes.add(tr(u8"piste muette"));
            else if (unSolo && !track.solo)
                causes.add(tr(u8"tue par le solo d'une autre"));
            else causes.add(tr(u8"tue par son dossier"));
        }
        // D16.7 : le décalage de piste ne suit pas le tempo et n'a pas
        // d'équivalent dans le format.
        if (track.delayMs != 0.0)
            causes.add(tr(u8"décalage %1 ms").replace("%1", juce::String(track.delayMs, 1)));
        if (causes.isEmpty()) continue;
        noms.add(juce::String::fromUTF8(track.name.c_str()) + " ("
                  + causes.joinIntoString(", ") + ")");
    }
    return noms;
}

void MainComponent::auditionSample(const juce::File& fichier) {
    if (!fichier.existsAsFile()) {
        std::fputs((juce::String::fromUTF8(u8"Pré-écoute : fichier introuvable — ")
                     + fichier.getFullPathName() + "\n").toRawUTF8(), stderr);
        return;
    }
    // LE DÉCODAGE A LIEU ICI, sur le thread de l'interface, et le tampon est
    // publié par échange atomique : le thread audio ne fait que lire un
    // pointeur déjà valide. C'est la règle de la piste de référence et des
    // échantillons du sampler, et elle vaut pour la même raison.
    auto lu = vsm::app::loadReferenceAudioFile(fichier);
    if (!lu.success) {
        // PANNE MUETTE INTERDITE : un clic qui ne rend aucun son doit dire
        // pourquoi, sans quoi on croit la pré-écoute cassée alors que c'est le
        // fichier qui l'est.
        std::fputs((juce::String::fromUTF8(u8"Pré-écoute impossible — ") + lu.error + "\n").toRawUTF8(),
                    stderr);
        return;
    }
    auto tampon = std::make_shared<vsm::audio::io::SampleBuffer>(std::move(lu.buffer));
    const double duree = tampon->sampleRate > 0.0
                             ? static_cast<double>(tampon->numFrames()) / tampon->sampleRate : 0.0;
    audioEngine_.processGraph().auditionPlayer().trigger(std::move(tampon));
    std::fputs((juce::String::fromUTF8(u8"Pré-écoute : ") + fichier.getFileName()
                 + juce::String::fromUTF8(u8" (") + juce::String(duree, 2)
                 + juce::String::fromUTF8(u8" s, ") + lu.decoder
                 + juce::String::fromUTF8(u8")\n")).toRawUTF8(), stderr);
}

size_t MainComponent::renameTracksInSeries(const juce::String& motif) {
    // UN MOTIF SANS « # » NE RENOMME RIEN. Quarante pistes qui porteraient
    // toutes le même nom seraient pires qu'avant : on ne saurait plus laquelle
    // est laquelle, et l'on aurait perdu les noms d'origine par-dessus le
    // marché. Le refus est dit, pas subi.
    if (motif.isEmpty() || !motif.contains("#")) {
        std::fputs(juce::String::fromUTF8(
                        u8"Renommage en série : le motif doit contenir « # » (le numéro). "
                        u8"Rien n'a été renommé.\n").toRawUTF8(), stderr);
        return 0;
    }
    size_t visibles = 0;
    for (const auto& t : project_.tracks) if (!t.hidden) ++visibles;
    if (visibles == 0) return 0;

    beginProjectEdit(u8"Renommer les pistes en série");
    int numero = 1;
    size_t renommees = 0;
    for (auto& piste : project_.tracks) {
        // LES PISTES MASQUÉES SONT ÉPARGNÉES, et le numéro ne les compte pas :
        // on renomme ce qu'on VOIT, et un trou dans la numérotation ferait
        // chercher la piste manquante.
        if (piste.hidden) continue;
        piste.name = motif.replace("#", juce::String(numero)).toStdString();
        ++numero;
        ++renommees;
    }
    // RENOMMER N'EST PAS DÉPLACER : ni le nombre de pistes ni leur ordre ne
    // bougent, et rien n'est republié au moteur -- seules les vues qui
    // dessinent des noms ont quelque chose à apprendre.
    refreshTrackViews();
    mixer_.setProject(&project_);
    eventList_.refresh();
    std::fputs((juce::String::fromUTF8(u8"Renommage en série : ")
                 + juce::String(static_cast<int>(renommees))
                 + juce::String::fromUTF8(u8" piste(s) renommée(s) d'après « ") + motif
                 + juce::String::fromUTF8(u8" ».\n")).toRawUTF8(), stderr);
    return renommees;
}

void MainComponent::promptRenameTracksInSeries() {
    auto fenetre = std::make_shared<BoiteLisible>(   // D121
        tr(u8"Renommer les pistes en série"),
        // D117 : L'EXEMPLE SUR SA PROPRE LIGNE. « Batterie # » se coupait entre ses
        // mots à 150 % ; des espaces insécables l'empêchaient, mais s'affichaient
        // deux fois plus larges -- « Drums  # » a l'air d'un motif à deux espaces.
        // Un retour ENTRE deux idées protège ce qu'un retour au milieu coupait.
        tr(u8"Le « # » est remplacé par le numéro d'ordre.\n"
           u8"Exemple : « Batterie # » donne « Batterie 1 », « Batterie 2 »...\n"
           u8"Seules les pistes VISIBLES sont renommées."),
        juce::AlertWindow::QuestionIcon);
    fenetre->addTextEditor("motif", tr(u8"Piste #"), tr(u8"Motif"));
    fenetre->addButton(tr(u8"Renommer"), 1);
    fenetre->addButton(vsm::app::ui::trSelon("bouton", u8"Annuler"), 0);
    fenetre->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, fenetre](int choix) {
            if (choix == 1) renameTracksInSeries(fenetre->getTextEditorContents("motif"));
            fenetre->exitModalState(0);
            fenetre->setVisible(false);
        }), false);
}

juce::String MainComponent::projectStatisticsText() const {
    size_t notes = 0, clips = 0, courbes = 0, points = 0;
    size_t cc = 0, plis = 0, poly = 0, pression = 0, programmes = 0;
    size_t midi = 0, audio = 0, groupes = 0, dossiers = 0, masquees = 0, eteintes = 0;
    std::map<std::string, int> machines;
    vsm::midi::Tick fin = 0;
    for (const auto& piste : project_.tracks) {
        notes += piste.notes.size();
        clips += piste.clips.size();
        courbes += piste.automation.size();
        for (const auto& c : piste.automation) points += c.points.size();
        cc += piste.controlChanges.size();
        plis += piste.pitchBends.size();
        poly += piste.polyAftertouch.size();
        pression += piste.channelPressure.size();
        programmes += piste.programChanges.size();
        if (piste.isFolder()) ++dossiers;
        else if (piste.kind == Track::Kind::Group) ++groupes;
        else if (piste.kind == Track::Kind::Audio) ++audio;
        else ++midi;
        if (piste.hidden) ++masquees;
        if (piste.disabled) ++eteintes;
        if (!piste.instrumentId.empty()) ++machines[piste.instrumentId];
        for (const auto& n : piste.notes) fin = std::max(fin, n.endTick);
    }
    const double secondes = project_.ticksToSeconds(fin);

    juce::String t;
    auto ligne = [&t](const juce::String& cle, const juce::String& valeur) {
        t += cle + " : " + valeur + "\n";
    };
    // D97 : TRADUIT À LA SOURCE. Ce texte part aussi au terminal, mais personne
    // ne l'y relit : son seul lecteur est la boîte (ROADMAP-daw.md, D97).
    ligne(tr(u8"Pistes"),
          tr(u8"%1  (%2 MIDI, %3 audio, %4 groupe(s), %5 dossier(s))")
              .replace("%1", juce::String(static_cast<int>(project_.tracks.size())))
              .replace("%2", juce::String(static_cast<int>(midi)))
              .replace("%3", juce::String(static_cast<int>(audio)))
              .replace("%4", juce::String(static_cast<int>(groupes)))
              .replace("%5", juce::String(static_cast<int>(dossiers))));
    if (masquees > 0 || eteintes > 0)
        ligne(tr(u8"  dont"),
              tr(u8"%1 masquée(s), %2 désactivée(s)")
                  .replace("%1", juce::String(static_cast<int>(masquees)))
                  .replace("%2", juce::String(static_cast<int>(eteintes))));
    ligne(tr(u8"Notes"), juce::String(static_cast<int>(notes)));
    ligne(tr(u8"Clips"), juce::String(static_cast<int>(clips)));
    ligne(tr(u8"Automation"),
          tr(u8"%1 courbe(s), %2 point(s)")
              .replace("%1", juce::String(static_cast<int>(courbes)))
              .replace("%2", juce::String(static_cast<int>(points))));
    ligne(tr(u8"Contrôleurs (CC)"), juce::String(static_cast<int>(cc)));
    ligne(tr(u8"Plis de hauteur"), juce::String(static_cast<int>(plis)));
    ligne(tr(u8"Pression polyphonique"), juce::String(static_cast<int>(poly)));
    ligne(tr(u8"Pression de canal"), juce::String(static_cast<int>(pression)));
    ligne(tr(u8"Changements de programme"), juce::String(static_cast<int>(programmes)));
    ligne(tr(u8"Machines employées"), juce::String(static_cast<int>(machines.size())));
    for (const auto& [id, combien] : machines)
        ligne("   " + juce::String::fromUTF8(id.c_str()),
              tr(u8"%1 piste(s)").replace("%1", juce::String(combien)));
    ligne(tr(u8"Durée du matériau"),
          tr(u8"%1 s  (%2 ticks)").replace("%1", juce::String(secondes, 2))
                                  .replace("%2", juce::String(static_cast<int>(fin))));
    ligne(tr(u8"Tempo au départ"),
          juce::String(project_.tempoMap.bpmAt(0), 2) + " BPM");
    return t;
}

void MainComponent::showProjectStatistics() {
    const juce::String texte = projectStatisticsText();
    // SUR STDERR AUSSI, comme partout : une boîte de message n'entre pas dans
    // un autoportrait, et un chiffre qu'on ne peut pas relire hors de l'écran
    // ne sert pas à comparer deux reconstructions.
    std::fputs((juce::String::fromUTF8(u8"Statistiques du projet —\n") + texte).toRawUTF8(), stderr);
    montrerBoite(
        juce::AlertWindow::InfoIcon, tr(u8"Statistiques du projet"), texte);
}

void MainComponent::hideSelectedTrack() {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;
    const std::set<size_t> cible = trackList_.selectedTracks();   // D38.2
    beginProjectEdit(cible.size() > 1 ? juce::String::fromUTF8(u8"Masquer des pistes")
                                       : juce::String::fromUTF8(u8"Masquer une piste"));
    for (size_t i : cible) if (i < project_.tracks.size()) project_.tracks[i].hidden = true;
    // RIEN N'EST REPUBLIÉ AU MOTEUR : masquer n'est pas couper, et la piste
    // continue de sonner exactement comme avant. Seules les trois vues qui
    // dessinent des pistes ont quelque chose à apprendre.
    refreshTrackViews();
}

void MainComponent::showAllTracks() {
    bool changement = false;
    for (const auto& t : project_.tracks) if (t.hidden) { changement = true; break; }
    if (!changement) return;
    beginProjectEdit(u8"Afficher toutes les pistes");
    for (auto& t : project_.tracks) t.hidden = false;
    refreshTrackViews();
}

void MainComponent::refreshTrackViews() {
    arrangement_.repaint();
    trackList_.resized();
    trackList_.repaint();
    mixer_.resized();
    mixer_.repaint();
}

vsm::midi::Tick MainComponent::snapCutToZeroCrossing(size_t trackIndex, vsm::midi::Tick tick,
                                                     double* deplacementMs) {
    if (deplacementMs) *deplacementMs = 0.0;
    if (trackIndex >= project_.tracks.size()) return tick;
    const auto& piste = project_.tracks[trackIndex];
    if (piste.kind != vsm::sequencer::Track::Kind::Audio || piste.audio.empty()) return tick;
    // LA SOURCE QUE LE MOTEUR JOUE, déjà rééchantillonnée à la fréquence de la
    // session : rien à relire, quelques échantillons à regarder.
    const auto source = audioEngine_.processGraph().trackAudio(trackIndex);
    if (!source || !source->samples) return tick;
    const double sr = audioEngine_.currentSampleRate() > 0.0 ? audioEngine_.currentSampleRate() : 48000.0;
    vsm::midi::Tick finMateriau = 0;
    if (piste.audio.sampleRate > 0.0) finMateriau = project_.secondsToTicks(piste.audio.durationSeconds());
    for (const auto& c : piste.clips) {
        const auto longueur = vsm::sequencer::clipPlayedLength(c, finMateriau);
        if (!(c.startTick < tick && tick < c.startTick + longueur)) continue;
        // Un clip étiré ou à l'envers : la correspondance tick → échantillon
        // n'est plus une droite, la coupe reste où elle est.
        if (vsm::sequencer::clipIsWarped(c) || c.reversed) return tick;
        const double secondes = c.sourceStartSeconds + (project_.ticksToSeconds(tick) - project_.ticksToSeconds(c.startTick));
        const auto trame = static_cast<int64_t>(std::llround(secondes * sr));
        const auto magasin = source->samples;
        const int64_t zero = vsm::audio::io::nearestZeroCrossing(
            [&magasin](int64_t i, float& g, float& d) { return magasin->frameAt(i, g, d); },
            trame, static_cast<int64_t>(std::llround(0.002 * sr)));
        if (zero == trame) return tick;
        const double nouvelles = c.sourceStartSeconds + static_cast<double>(zero) / sr;
        const vsm::midi::Tick aimantee = project_.secondsToTicks(
            project_.ticksToSeconds(c.startTick) + (nouvelles - c.sourceStartSeconds));
        if (!(c.startTick < aimantee && aimantee < c.startTick + longueur)) return tick;
        if (deplacementMs) *deplacementMs = 1000.0 * static_cast<double>(zero - trame) / sr;
        return aimantee;
    }
    return tick;
}

void MainComponent::sliceSelectedClipsAtOnsets() {
    // D20.3 : LES CLIPS AUDIO CHOISIS, COUPÉS À CHAQUE ATTAQUE. Même lecture
    // du fichier que « Rogner au son », même passage par `splitClips` : une
    // coupe posée ici est exactement celle que Ctrl+E poserait à la main, la
    // fenêtre en secondes du clip audio comprise.
    const vsm::sequencer::ClipSelection selection = arrangement_.selectedClipIds();
    if (selection.empty()) {
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Découper aux transitoires"),
            tr(u8"Choisissez d'abord un clip audio dans l'arrangement."));
        return;
    }
    const double sr = audioEngine_.currentSampleRate() > 0.0 ? audioEngine_.currentSampleRate() : 48000.0;
    const auto versSecondes = [this](vsm::midi::Tick t) { return project_.ticksToSeconds(t); };
    int coupes = 0, clipsAudio = 0, sansAttaque = 0, aimantees = 0;
    double plusGrandDeplacement = 0.0;
    juce::StringArray refus;
    bool debute = false;
    for (size_t t = 0; t < project_.tracks.size(); ++t) {
        auto& piste = project_.tracks[t];
        std::vector<uint64_t> cibles;
        for (const auto& c : piste.clips)
            if (selection.count(c.id) > 0) cibles.push_back(c.id);
        if (cibles.empty()) continue;
        // CE QUI N'EST PAS DÉCOUPÉ EST DIT, piste par piste : un clip MIDI n'a
        // pas de transitoire à trouver, une piste verrouillée ne se coupe pas.
        if (piste.kind != vsm::sequencer::Track::Kind::Audio || piste.audio.empty()) {
            refus.add(juce::String(u8"%1 : pas une piste audio").replace("%1", juce::String::fromUTF8(piste.name.c_str())));
            continue;
        }
        if (piste.locked) {
            refus.add(juce::String(u8"%1 : piste verrouillée").replace("%1", juce::String::fromUTF8(piste.name.c_str())));
            continue;
        }
        const juce::File fichier = currentProjectFolder_.getChildFile(juce::String(piste.audio.path));
        auto charge = vsm::audio::io::loadAudioTrack(fichier.getFullPathName().toStdString(), sr);
        if (!charge.source || !charge.source->samples) {
            refus.add(juce::String(u8"%1 : fichier illisible — %2").replace("%2", fichier.getFullPathName())
                                                                   .replace("%1", juce::String::fromUTF8(piste.name.c_str())));
            continue;
        }
        const auto magasin = charge.source->samples;
        vsm::midi::Tick finMateriau = 0;
        if (piste.audio.sampleRate > 0.0) finMateriau = project_.secondsToTicks(piste.audio.durationSeconds());
        for (uint64_t id : cibles) {
            auto it = std::find_if(piste.clips.begin(), piste.clips.end(),
                                    [id](const vsm::sequencer::Clip& c) { return c.id == id; });
            if (it == piste.clips.end()) continue;
            if (it->reversed) {
                refus.add(juce::String(u8"%1 : clip à l'envers, non découpé").replace("%1", juce::String::fromUTF8(it->name.c_str())));
                continue;
            }
            ++clipsAudio;
            const auto jouee = vsm::sequencer::clipPlayedLength(*it, finMateriau);
            const double duree = project_.ticksToSeconds(it->startTick + jouee) - project_.ticksToSeconds(it->startTick);
            const auto depart = static_cast<int64_t>(std::llround(it->sourceStartSeconds * sr));
            const auto compte = static_cast<int64_t>(std::llround(duree * sr));
            const double sourceDebut = it->sourceStartSeconds;
            const auto attaques = vsm::audio::io::detectOnsets(
                [&magasin, depart](int64_t i, float& g, float& d) { return magasin->frameAt(depart + i, g, d); },
                compte, sr);
            if (attaques.empty()) { ++sansAttaque; continue; }
            // CHAQUE COUPE EST DITE AVEC SON INSTANT (secondes dans le
            // fichier) : c'est ainsi qu'on voit, au journal, une attaque
            // trouvée là où il n'y en a pas.
            {
                juce::String instants;
                for (int64_t attaque : attaques)
                    instants += (instants.isEmpty() ? "" : ", ")
                                + juce::String(sourceDebut + static_cast<double>(attaque) / sr, 3);
                std::fputs((juce::String(u8"Découper aux transitoires : « ") + juce::String::fromUTF8(it->name.c_str())
                            + juce::String(u8" » : ") + juce::String(static_cast<int>(attaques.size()))
                            + juce::String(u8" attaque(s) à ") + instants + " s\n").toRawUTF8(), stderr);
            }
            if (!debute) { beginProjectEdit(u8"Découper aux transitoires"); debute = true; }
            uint64_t compteur = project_.peekNextClipId();
            for (int64_t attaque : attaques) {
                // L'attaque est un instant DU FICHIER ; le clip qui le couvre
                // -- après les coupes précédentes, c'est la moitié droite de
                // la dernière -- dit où il tombe sur la ligne de temps.
                const double sourceSecondes = sourceDebut + static_cast<double>(attaque) / sr;
                uint64_t couvrant = 0;
                vsm::midi::Tick tick = 0;
                for (const auto& c : piste.clips) {
                    const auto longueur = vsm::sequencer::clipPlayedLength(c, finMateriau);
                    const vsm::midi::Tick ici = vsm::sequencer::clipIsWarped(c)
                        ? c.startTick + vsm::sequencer::warpTickAtSeconds(c, sourceSecondes)
                        : project_.secondsToTicks(project_.ticksToSeconds(c.startTick) + (sourceSecondes - c.sourceStartSeconds));
                    if (c.startTick < ici && ici < c.startTick + longueur) { couvrant = c.id; tick = ici; break; }
                }
                if (couvrant == 0) continue;
                // D21.3 : chaque coupe s'aimante au passage par zéro.
                double bouge = 0.0;
                tick = snapCutToZeroCrossing(t, tick, &bouge);
                if (bouge != 0.0) { ++aimantees; plusGrandDeplacement = std::max(plusGrandDeplacement, std::fabs(bouge)); }
                coupes += static_cast<int>(vsm::sequencer::splitClips(piste, {couvrant}, tick, finMateriau,
                                                                      compteur, versSecondes));
            }
            project_.ensureClipIdAbove(compteur - 1);
        }
    }
    if (debute) {
        refreshTransportSchedule();
        loadAudioTracks();
        arrangement_.repaint();
    }
    // D95 : LE MESSAGE RESTE FRANÇAIS -- la sortie d'erreur le relit, les bancs
    // aussi -- et s'écrit par ses MODÈLES (`kModeles`, Langue.cpp) : la boîte le
    // traduit à l'affichage, ligne par ligne, par `trPhrase`.
    juce::String message = juce::String(u8"%#1 coupe(s) sur %#2 clip(s) audio.")
                               .replace("%#1", juce::String(coupes)).replace("%#2", juce::String(clipsAudio));
    if (sansAttaque > 0)
        message += "\n" + juce::String(u8"%#1 clip(s) sans attaque trouvée, laissé(s) entier(s).")
                              .replace("%#1", juce::String(sansAttaque));
    for (const auto& r : refus) message += "\n" + r;
    if (aimantees > 0)
        message += "\n" + juce::String(u8"%#1 coupe(s) aimantée(s) au passage par zéro (au plus %2 ms).")
                              .replace("%#1", juce::String(aimantees))
                              .replace("%2", juce::String(plusGrandDeplacement, 2));
    if (coupes > 0)
        message += "\n" + juce::String(u8"Le fichier n'a pas été touché : ce sont des fenêtres, et chaque coupe s'annule.");
    std::fputs((juce::String(u8"Découper aux transitoires : ") + message.replace("\n", " ; ") + "\n").toRawUTF8(), stderr);
    montrerBoite(juce::AlertWindow::InfoIcon, tr(u8"Découper aux transitoires"),
                                           vsm::app::ui::trPhrase(message));
}

void MainComponent::transcribeSelectedClip() {
    // D20.4 : LE PREMIER CLIP AUDIO CHOISI DEVIENT DES NOTES, sur une piste
    // neuve posée après la sienne. Le transcripteur est celui de la chaîne
    // (`analyse/transcrire_clip.py`), lancé par l'interpréteur que D9 a
    // trouvé, dans un processus enfant : Basic Pitch met plusieurs secondes à
    // charger, et l'interface ne les attend pas.
    if (!reconstructionChain_.available) {
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Transcrire en MIDI"),
            tr(u8"La chaîne d'analyse n'est pas disponible : %1")
                .replace("%1", juce::String::fromUTF8(reconstructionChain_.reason.c_str()))
                + (reconstructionChain_.remedy.empty() ? juce::String()
                                                       : "\n" + juce::String::fromUTF8(reconstructionChain_.remedy.c_str())));
        return;
    }
    if (clipTranscriber_.isRunning()) {
        montrerBoite(juce::AlertWindow::InfoIcon, tr(u8"Transcrire en MIDI"),
                                                 tr(u8"Une transcription est déjà en cours."));
        return;
    }
    const vsm::sequencer::ClipSelection selection = arrangement_.selectedClipIds();
    size_t index = project_.tracks.size();
    const vsm::sequencer::Clip* choisi = nullptr;
    for (size_t t = 0; t < project_.tracks.size() && choisi == nullptr; ++t) {
        const auto& piste = project_.tracks[t];
        if (piste.kind != vsm::sequencer::Track::Kind::Audio || piste.audio.empty()) continue;
        for (const auto& c : piste.clips)
            if (selection.count(c.id) > 0) { choisi = &c; index = t; break; }
    }
    if (choisi == nullptr) {
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Transcrire en MIDI"),
            tr(u8"Choisissez d'abord un clip d'une piste AUDIO : une piste MIDI porte déjà ses notes."));
        return;
    }
    const auto& piste = project_.tracks[index];
    const vsm::sequencer::Clip clip = *choisi;
    const juce::File fichier = currentProjectFolder_.getChildFile(juce::String(piste.audio.path));
    if (!fichier.existsAsFile()) {
        montrerBoite(juce::AlertWindow::InfoIcon, tr(u8"Transcrire en MIDI"),
                                                 tr(u8"Le fichier de la piste est introuvable : %1").replace("%1", fichier.getFullPathName()));
        return;
    }
    // LA PLAGE DU CLIP DANS LE FICHIER, en secondes : c'est ce que le
    // transcripteur reçoit, et il rend des instants dans le fichier.
    vsm::midi::Tick finMateriau = 0;
    if (piste.audio.sampleRate > 0.0) finMateriau = project_.secondsToTicks(piste.audio.durationSeconds());
    const auto jouee = vsm::sequencer::clipPlayedLength(clip, finMateriau);
    const double debut = clip.sourceStartSeconds;
    const double fin = vsm::sequencer::clipIsWarped(clip)
                           ? vsm::sequencer::warpSourceSecondsAt(clip, jouee)
                           : debut + (project_.ticksToSeconds(clip.startTick + jouee) - project_.ticksToSeconds(clip.startTick));
    const juce::File json = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getChildFile("vsm-transcription-" + juce::String(juce::Time::currentTimeMillis()) + ".json");
    juce::StringArray commande;
    commande.add(juce::String::fromUTF8(reconstructionChain_.interpreterPath.c_str()));
    commande.add(juce::File(juce::String::fromUTF8(reconstructionChain_.chainFolder.c_str()))
                     .getChildFile("transcrire_clip.py").getFullPathName());
    commande.add(fichier.getFullPathName());
    commande.add("--debut"); commande.add(juce::String(debut, 3));
    commande.add("--fin");   commande.add(juce::String(fin, 3));
    commande.add("--sortie"); commande.add(json.getFullPathName());
    std::fputs((juce::String(u8"Transcrire en MIDI : ") + commande.joinIntoString(" ") + "\n").toRawUTF8(), stderr);
    const size_t plusieurs = selection.size();
    clipTranscriber_.onFinished = [this, index, clip, json, plusieurs](bool succes, juce::File fichierJson,
                                                                          juce::String journal) {
        if (!succes) {
            juce::StringArray lignes;
            lignes.addLines(journal);
            while (lignes.size() > 12) lignes.remove(0);
            std::fputs((juce::String(u8"Transcrire en MIDI : ÉCHEC\n") + journal + "\n").toRawUTF8(), stderr);
            // D114 : les lignes du journal sont des DONNÉES (la chaîne, le lanceur) --
            // traduites à l'affichage, ligne par ligne, comme le volet de rapport.
            juce::StringArray affichees;
            for (const auto& ligne : lignes) affichees.add(vsm::app::ui::trPhrase(ligne));
            montrerBoite(juce::AlertWindow::WarningIcon, tr(u8"Transcrire en MIDI"),
                                                     tr(u8"La transcription a échoué :") + "\n" + affichees.joinIntoString("\n"));
            return;
        }
        const juce::var lu = juce::JSON::parse(fichierJson);
        fichierJson.deleteFile();
        const juce::var notesVar = lu.getProperty("notes", juce::var());
        const juce::Array<juce::var>* tableau = notesVar.getArray();
        if (index >= project_.tracks.size() || tableau == nullptr || tableau->isEmpty()) {
            montrerBoite(juce::AlertWindow::InfoIcon, tr(u8"Transcrire en MIDI"),
                                                     tr(u8"Aucune note trouvée dans ce clip : aucune piste créée."));
            return;
        }
        // LES INSTANTS DU FICHIER DEVIENNENT DES TICKS PAR LA FENÊTRE DU CLIP,
        // suivi de tempo compris -- la même règle que pour une coupe.
        auto tickDe = [this, &clip](double sourceSecondes) {
            return vsm::sequencer::clipIsWarped(clip)
                       ? clip.startTick + vsm::sequencer::warpTickAtSeconds(clip, sourceSecondes)
                       : project_.secondsToTicks(project_.ticksToSeconds(clip.startTick)
                                                  + (sourceSecondes - clip.sourceStartSeconds));
        };
        const auto& source = project_.tracks[index];
        vsm::sequencer::Track neuve;
        neuve.kind = vsm::sequencer::Track::Kind::Midi;
        neuve.name = (clip.name.empty() ? source.name : clip.name) + " (transcrit)";
        neuve.colorRgba = source.colorRgba;
        neuve.folderDepth = source.folderDepth;
        vsm::midi::Tick finMateriau = 0;
        for (const auto& n : *tableau) {
            vsm::sequencer::Note note;
            const double depart = static_cast<double>(n.getProperty("start", 0.0));
            const double duree = static_cast<double>(n.getProperty("duration", 0.0));
            note.startTick = std::max<vsm::midi::Tick>(0, tickDe(depart));
            note.endTick = std::max<vsm::midi::Tick>(note.startTick + 1, tickDe(depart + duree));
            note.number = static_cast<uint8_t>(juce::jlimit(0, 127, static_cast<int>(n.getProperty("note", 60))));
            note.velocity = static_cast<uint8_t>(juce::jlimit(1, 127, static_cast<int>(n.getProperty("velocity", 100))));
            note.confidence = static_cast<float>(static_cast<double>(n.getProperty("confidence", 1.0)));
            note.id = project_.nextNoteId();
            finMateriau = std::max(finMateriau, note.endTick);
            neuve.notes.push_back(note);
        }
        const size_t douteuses = vsm::sequencer::countDoubtfulNotes(neuve.notes);
        beginProjectEdit(u8"Transcrire un clip en MIDI");
        // UN CLIP SUR LA PLAGE DU CLIP AUDIO : la piste neuve joue là où il
        // jouait, et rien d'autre.
        uint64_t compteur = project_.peekNextClipId();
        const auto fenetre = vsm::sequencer::createClip(neuve.clips, clip.startTick,
                                                          std::max<vsm::midi::Tick>(1, finMateriau - clip.startTick),
                                                          compteur, finMateriau);
        project_.ensureClipIdAbove(compteur - 1);
        for (auto& c : neuve.clips)
            if (c.id == fenetre.id) { c.name = neuve.name; c.colorRgba = neuve.colorRgba; }
        // APRÈS LA PISTE AUDIO, par `moveTrack`, qui répare les index de
        // routage -- insérer au milieu à la main est la façon dont ils
        // pourrissent (D18.7b).
        project_.tracks.push_back(std::move(neuve));
        vsm::sequencer::moveTrack(project_, project_.tracks.size() - 1, index + 1);
        rebuildFromProject(false);
        trackList_.selectTrackIndex(index + 1);
        // D95 : français, écrit par ses modèles, traduit à l'affichage (voir
        // « Découper aux transitoires »). Deux modèles pour la première ligne : le
        // compte des notes douteuses change la phrase entière, pas un segment.
        const juce::String nomNeuve = juce::String::fromUTF8(project_.tracks[index + 1].name.c_str());
        juce::String message = douteuses > 0
            ? juce::String(u8"%#1 note(s) posée(s) sur « %2 », après la piste audio, dont %#3 douteuse(s) "
                           u8"(confiance sous %4 — « Note douteuse suivante » les parcourt).")
                  .replace("%#1", juce::String(static_cast<int>(tableau->size())))
                  .replace("%#3", juce::String(static_cast<int>(douteuses)))
                  .replace("%4", juce::String(vsm::sequencer::kDoubtfulNoteThreshold, 2))
                  .replace("%2", nomNeuve)
            : juce::String(u8"%#1 note(s) posée(s) sur « %2 », après la piste audio.")
                  .replace("%#1", juce::String(static_cast<int>(tableau->size())))
                  .replace("%2", nomNeuve);
        message += "\n" + juce::String(u8"La piste est SANS instrument : choisissez-en un dans le rack. "
                                       u8"Les vélocités viennent de l'énergie du son, comme dans la chaîne.");
        if (plusieurs > 1) message += "\n" + juce::String(u8"Un clip à la fois : le premier choisi a été transcrit.");
        std::fputs((juce::String(u8"Transcrire en MIDI : ") + message.replace("\n", " ; ") + "\n").toRawUTF8(), stderr);
        montrerBoite(juce::AlertWindow::InfoIcon, tr(u8"Transcrire en MIDI"),
                                               vsm::app::ui::trPhrase(message));
    };
    clipTranscriber_.start(commande, json);
}

void MainComponent::trimClipToSound(size_t trackIndex, uint64_t clipId) {
    if (trackIndex >= project_.tracks.size()) return;
    auto& piste = project_.tracks[trackIndex];
    if (piste.kind != vsm::sequencer::Track::Kind::Audio || piste.audio.empty()) {
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Rogner au son"),
            tr(u8"Cette commande cherche le silence dans un FICHIER : elle ne s'applique qu'à un "
               u8"clip de piste audio. Sur une piste MIDI, une note qui ne sonne pas n'existe pas."));
        return;
    }
    auto it = std::find_if(piste.clips.begin(), piste.clips.end(),
                            [clipId](const vsm::sequencer::Clip& c) { return c.id == clipId; });
    if (it == piste.clips.end()) return;

    const juce::File fichier = currentProjectFolder_.getChildFile(juce::String(piste.audio.path));
    const double sr = audioEngine_.currentSampleRate() > 0.0 ? audioEngine_.currentSampleRate() : 48000.0;
    auto charge = vsm::audio::io::loadAudioTrack(fichier.getFullPathName().toStdString(), sr);
    if (!charge.source) {
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Rogner au son"),
            tr(u8"Le fichier de la piste n'a pas pu être relu : %1").replace("%1", fichier.getFullPathName()));
        return;
    }

    // LA FENÊTRE DU CLIP DANS LE FICHIER, en trames.
    vsm::midi::Tick finMateriau = 0;
    if (piste.audio.sampleRate > 0.0)
        finMateriau = project_.secondsToTicks(piste.audio.durationSeconds());
    const auto jouee = vsm::sequencer::clipPlayedLength(*it, finMateriau);
    const double duree = project_.ticksToSeconds(it->startTick + jouee)
                         - project_.ticksToSeconds(it->startTick);
    const auto depart = static_cast<int64_t>(std::llround(it->sourceStartSeconds * sr));
    const auto compte = static_cast<int64_t>(std::llround(duree * sr));

    const auto magasin = charge.source->samples;
    if (!magasin || compte <= 0) return;
    const auto bornes = vsm::audio::io::detectSound(
        [&magasin, depart](int64_t i, float& g, float& d) {
            return magasin->frameAt(depart + i, g, d);
        },
        compte, sr);
    if (!bornes.found) {
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Rogner au son"),
            tr(u8"Tout ce que ce clip joue est sous le seuil de silence : rien n'a été rogné. "
               u8"Un clip entièrement silencieux réduit à rien disparaîtrait, et ce n'est pas "
               u8"ce que vous avez demandé."));
        return;
    }
    if (bornes.firstFrame == 0 && bornes.lastFrame == compte) {
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Rogner au son"),
            tr(u8"Ce clip commence et finit déjà sur le son : rien à rogner."));
        return;
    }

    // LES DEUX MÊMES GESTES QU'À LA MAIN, mesurés au lieu d'être visés :
    // tirer le bord gauche masque du matériau par la tête en laissant ce qui
    // reste exactement où il était, tirer le bord droit raccourcit la durée
    // jouée. Passer par `ClipEdit` plutôt que d'écrire les champs à la main,
    // c'est hériter de toutes leurs règles -- le verrou, la longueur minimale,
    // la fenêtre en secondes d'un clip audio.
    beginProjectEdit(u8"Rogner au son");
    const auto enTicks = [this](double secondes) {
        return project_.secondsToTicks(secondes);
    };
    const auto versSecondes = [this](vsm::midi::Tick t) { return project_.ticksToSeconds(t); };
    const vsm::sequencer::ClipSelection cible{clipId};
    if (bornes.lastFrame < compte)
        vsm::sequencer::resizeClipsEnd(piste, cible,
                                        -enTicks(static_cast<double>(compte - bornes.lastFrame) / sr),
                                        finMateriau);
    if (bornes.firstFrame > 0)
        vsm::sequencer::resizeClipsStart(piste, cible,
                                          enTicks(static_cast<double>(bornes.firstFrame) / sr),
                                          finMateriau, versSecondes);
    refreshTransportSchedule();
    loadAudioTracks();
    arrangement_.repaint();
    montrerBoite(
        juce::AlertWindow::InfoIcon, tr(u8"Rogner au son"),
        tr(u8"%1 ms retirées au début, %2 ms à la fin. Le fichier n'a pas été touché : c'est la fenêtre du "
           u8"clip qui a bougé.")
            .replace("%1", juce::String(static_cast<double>(bornes.firstFrame) / sr * 1000.0, 0))
            .replace("%2", juce::String(static_cast<double>(compte - bornes.lastFrame) / sr * 1000.0, 0)));
}

// --- D17.8 : LE GROOVE ------------------------------------------------------
//
// Le groove COURANT vit dans l'application et non dans le projet : c'est un
// outil qu'on porte d'un morceau à l'autre, comme un preset, pas une propriété
// du morceau. Le projet garde les NOTES telles que le groove les a laissées ;
// il n'a pas à se souvenir d'où elles tiennent leur placement.

void MainComponent::extractGrooveFromSelectedTrack() {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;
    const auto& source = project_.tracks[piste];
    if (source.notes.empty()) {
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Extraire le groove"),
            tr(u8"Cette piste n'a aucune note : il n'y a pas de placement à en tirer."));
        return;
    }
    const auto parMesure = project_.timeSignatureMap.ticksPerBar(0, project_.ticksPerQuarterNote);
    grooveCourant_ = vsm::sequencer::extractGroove(source.notes, parMesure, 16,
                                                    source.name.empty() ? std::string("Groove")
                                                                        : source.name);
    size_t presents = 0;
    for (const auto& pas : grooveCourant_.steps) if (pas.present) ++presents;
    montrerBoite(
        juce::AlertWindow::InfoIcon, tr(u8"Extraire le groove"),
        tr(u8"Groove « %1 » : %2 pas sur 16 renseignés. Les pas où la piste ne jouait rien "
           u8"laisseront les notes tranquilles.")
            .replace("%2", juce::String(static_cast<int>(presents)))
            .replace("%1", juce::String(grooveCourant_.name)));
}

void MainComponent::applyGrooveToSelection() {
    auto* piste = pianoRoll_.activeTrack();
    if (piste == nullptr || grooveCourant_.empty() || !pianoRoll_.hasSelection()) return;
    const auto parMesure = project_.timeSignatureMap.ticksPerBar(0, project_.ticksPerQuarterNote);

    // L'instantané n'est pris que si quelque chose va changer -- on mesure
    // d'abord sur une copie, comme partout ailleurs.
    auto essai = piste->notes;
    const size_t deplacees = vsm::sequencer::applyGroove(
        essai, pianoRoll_.selectedNoteIds(), grooveCourant_, parMesure, 1.0f, false);
    if (deplacees == 0) {
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Appliquer le groove"),
            tr(u8"Aucune note n'a bougé : elles tombent toutes sur des pas dont ce groove ne dit "
               u8"rien, ou elles y sont déjà."));
        return;
    }
    beginProjectEdit(u8"Appliquer le groove");
    piste->notes = std::move(essai);
    refreshTransportSchedule();
    pianoRollPanel_.refresh();
    arrangement_.repaint();
    refreshHistoryList();
}

void MainComponent::saveCurrentGroove() {
    if (grooveCourant_.empty()) return;
    const juce::String bibliotheque =
        vsm::app::ui::UiScale::properties().getValue("dossierBibliotheque", "");
    // MÊME RANGEMENT QUE LES AUTRES PRESETS (D15.4) : la bibliothèque si elle
    // est réglée, le projet sinon. Deux dossiers pour deux sortes de presets
    // seraient deux logiciels.
    juce::File dossier = bibliotheque.isNotEmpty()
                             ? juce::File(bibliotheque).getChildFile("grooves")
                             : (currentProjectFolder_ != juce::File()
                                    ? currentProjectFolder_.getChildFile("grooves")
                                    : juce::File::getSpecialLocation(
                                          juce::File::userApplicationDataDirectory)
                                          .getChildFile("VSM").getChildFile("grooves"));
    dossier.createDirectory();
    const juce::File fichier = dossier.getChildFile(
        juce::File::createLegalFileName(juce::String(grooveCourant_.name))
        + juce::String(vsm::interchange::kGroovePresetExtension));
    const auto texte = vsm::interchange::grooveToJson(grooveCourant_).toString();
    if (!fichier.replaceWithText(juce::String(texte))) {
        montrerBoite(
            juce::AlertWindow::WarningIcon, tr(u8"Enregistrer le groove"),
            tr(u8"Écriture impossible : %1").replace("%1", fichier.getFullPathName()));
        return;
    }
    montrerBoite(
        juce::AlertWindow::InfoIcon, tr(u8"Enregistrer le groove"),
        tr(u8"Écrit dans %1").replace("%1", fichier.getFullPathName()));
}

void MainComponent::loadGrooveFromLibrary() {
    auto chooser = std::make_shared<juce::FileChooser>(
        tr(u8"Charger un groove"), juce::File(), "*.groove.json");
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser](const juce::FileChooser& fc) {
        const juce::File fichier = fc.getResult();
        if (fichier == juce::File()) return;
        const auto lu = vsm::interchange::parseGroove(fichier.loadFileAsString().toStdString());
        if (!lu.success) {
            // NOMMÉ, JAMAIS DEVINÉ : un fichier qui n'est pas un groove dit ce
            // qu'il est plutôt que de se charger vide.
            montrerBoite(
                juce::AlertWindow::WarningIcon, tr(u8"Charger un groove"),
                vsm::app::ui::trPhrase(juce::String(lu.error)));
            return;
        }
        grooveCourant_ = lu.groove;
    });
}

// --- D22.2 : aller à une mesure ---------------------------------------------

bool MainComponent::goToBarText(const juce::String& texte) {
    int64_t mesure = 0, temps = 0;
    if (!vsm::sequencer::parseBarBeat(texte.trim().toStdString(), mesure, temps)) {
        std::fputs(("Aller \u00e0 la mesure : \u00ab " + texte.toStdString()
                    + " \u00bb n'est pas une position (attendu \u00ab 17 \u00bb ou \u00ab 17.3 \u00bb)\n").c_str(), stderr);
        return false;
    }
    seekAllViews(project_.timeSignatureMap.tickAtBarBeat(mesure, temps, project_.ticksPerQuarterNote));
    return true;
}

void MainComponent::promptGoToBar() {
    const auto ici = project_.timeSignatureMap.barBeatAt(
        std::max<vsm::midi::Tick>(0, transport_.currentTick()), project_.ticksPerQuarterNote);
    auto* fenetre = new BoiteLisible(   // D121
        tr(u8"Aller à la mesure"),
        tr(u8"Mesure, ou mesure.temps (« 17 », « 17.3 »). La première mesure est la 1."),
        juce::MessageBoxIconType::NoIcon);
    fenetre->addTextEditor("position", juce::String(static_cast<long long>(ici.bar + 1)) + "."
                                            + juce::String(static_cast<long long>(ici.beat + 1)),
                            tr(u8"Position :"));
    fenetre->addButton(tr(u8"Aller"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    fenetre->addButton(vsm::app::ui::trSelon("bouton", u8"Annuler"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    fenetre->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, fenetre](int resultat) {
            if (resultat != 1) return;
            const juce::String texte = fenetre->getTextEditorContents("position");
            if (!goToBarText(texte))
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, tr(u8"Aller à la mesure"),
                    tr(u8"« %1 » n'est pas une position : attendu « 17 » ou « 17.3 » (mesure.temps).")
                        .replace("%1", texte));
        }), true);
}

void MainComponent::startPlaybackForCapture() { transport_.play(); }

// --- D23.2 : l'écoute automatique de l'entrée ---------------------------------

void MainComponent::applyMonitoringMode() {
    if (monitoringMode_ == 0) return;   // manuel : l'interrupteur du menu décide
    bool audioArmee = false;
    for (const auto& t : project_.tracks)
        if (t.armed && t.kind == Track::Kind::Audio) { audioArmee = true; break; }
    const bool lectureSimple = transport_.state() == TransportState::Playing && !audioEngine_.isRecording();
    const bool voulu = audioArmee && (monitoringMode_ == 2 || !lectureSimple);
    if (voulu != audioEngine_.inputMonitoring()) audioEngine_.setInputMonitoring(voulu);
}

// --- D23.3 : la piste choisie en MIDI ----------------------------------------

bool MainComponent::writeSelectedTrackMidi(const juce::File& fichier) {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) {
        std::fputs("Exporter la piste en MIDI : aucune piste choisie\n", stderr);
        return false;
    }
    captureSessionIntoProject();
    const Project seule = project_.extractTrack(piste);
    try {
        MidiFileWriter::writeFile(seule.toParsedFileArranged(),   // D56.1
                                   fichier.getFullPathName().toStdString());
    } catch (const std::exception& e) {
        montrerBoite(juce::AlertWindow::WarningIcon,
                                               tr(u8"Exporter la piste en MIDI"), vsm::app::ui::trPhrase(juce::String(e.what())));
        std::fputs(("Exporter la piste en MIDI : " + std::string(e.what()) + "\n").c_str(), stderr);
        return false;
    }
    std::fputs(("Piste \u00ab " + project_.tracks[piste].name + " \u00bb \u00e9crite en MIDI : "
                + fichier.getFullPathName().toStdString() + "\n").c_str(), stderr);
    return true;
}

// --- D25.2 : la piste choisie au clavier ------------------------------------

// D39.1 : LE RACCOURCI PASSE PAR LE CHEMIN DU BOUTON, ET C'EST UNE CORRECTION
// D'UN DÉFAUT QUE D38 A CRÉÉ.
//
// Ces deux fonctions écrivaient `project_.tracks[piste].muted` en direct, sur
// la seule piste active. Tant que le bouton M en faisait autant, les deux se
// valaient. D38 a appris au bouton à taire toute la sélection et n'a pas
// touché à celles-ci : le même geste rendait dès lors six pistes muettes à la
// souris et une au clavier.
//
// NI LE BANC NI LA CAPTURE DE D38 NE POUVAIENT LE VOIR : tous deux passaient
// par `TrackListComponent::basculerMuet`, c'est-à-dire par le même chemin. Deux
// instruments braqués au même endroit ne valent pas mieux qu'un seul.
void MainComponent::toggleMuteSelectedTrack() {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;
    trackList_.basculerMuet(piste);
}

void MainComponent::toggleSoloSelectedTrack() {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;
    trackList_.basculerSolo(piste);
}

void MainComponent::selectNeighbourTrack(int delta) {
    if (project_.tracks.empty()) return;
    const size_t actuelle = std::min(trackList_.selectedTrackIndex(), project_.tracks.size() - 1);
    // LA VOISINE VISIBLE : une piste masquée (D17.4) ne se choisit pas au
    // clavier, sinon on la choisirait sans la voir.
    size_t i = actuelle;
    while (true) {
        if (delta < 0 && i == 0) return;
        if (delta > 0 && i + 1 >= project_.tracks.size()) return;
        i = static_cast<size_t>(static_cast<long>(i) + delta);
        if (!project_.tracks[i].hidden) break;
    }
    trackList_.selectTrackIndex(i);
}

// --- D29.1 / D29.2 : les locateurs à la tête, la tête au clavier --------------

void MainComponent::setLoopBoundaryAtPlayhead(bool debut) {
    const vsm::midi::Tick tete = std::max<vsm::midi::Tick>(0, transport_.currentTick());
    const vsm::midi::Tick mesure = std::max<vsm::midi::Tick>(
        1, project_.timeSignatureMap.ticksPerBar(tete, project_.ticksPerQuarterNote));
    vsm::midi::Tick de = project_.loopStartTick, a = project_.loopEndTick;
    if (debut) {
        de = tete;
        // UN DÉBUT POSÉ APRÈS LA FIN repousse la fin d'une mesure : une région
        // vide ou inversée ne se joue pas, et c'est ce qu'on voulait éviter.
        if (a <= de) a = de + mesure;
    } else {
        a = tete;
        if (de >= a) de = std::max<vsm::midi::Tick>(0, a - mesure);
        if (a <= de) return;   // la tête à zéro : rien à poser
    }
    beginProjectEdit(debut ? u8"D\u00e9but de boucle \u00e0 la t\u00eate" : u8"Fin de boucle \u00e0 la t\u00eate");
    setLoopRegionEverywhere(de, a, true);
}

void MainComponent::seekByBeats(int temps) {
    const vsm::midi::Tick ici = std::max<vsm::midi::Tick>(0, transport_.currentTick());
    const vsm::midi::Tick pas = std::max<vsm::midi::Tick>(
        1, project_.timeSignatureMap.ticksPerBeat(ici, project_.ticksPerQuarterNote));
    seekAllViews(std::max<vsm::midi::Tick>(0, ici + pas * temps));
}

void MainComponent::seekByBars(int mesures) {
    const vsm::midi::Tick ici = std::max<vsm::midi::Tick>(0, transport_.currentTick());
    const vsm::midi::Tick pas = std::max<vsm::midi::Tick>(
        1, project_.timeSignatureMap.ticksPerBar(ici, project_.ticksPerQuarterNote));
    seekAllViews(std::max<vsm::midi::Tick>(0, ici + pas * mesures));
}

// --- D27.4 : la sortie MIDI matérielle ---------------------------------------

void MainComponent::setSelectedTrackMidiOutput(const std::string& port) {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;
    if (project_.tracks[piste].midiOutputDevice == port) return;
    beginProjectEdit(u8"Sortie MIDI");
    project_.tracks[piste].midiOutputDevice = port;
    refreshTransportSchedule();   // republie, et syncMidiOutputs() suit
    arrangement_.repaint();
    std::fputs(("Piste " + std::to_string(piste + 1) + " : sortie MIDI "
                + (port.empty() ? std::string("aucune") : "\u2192 " + port) + "\n").c_str(), stderr);
}

void MainComponent::syncMidiOutputs() {
    std::vector<std::string> ports;
    std::vector<uint8_t> canaux;
    ports.reserve(project_.tracks.size());
    canaux.reserve(project_.tracks.size());
    for (const auto& t : project_.tracks) {
        ports.push_back(t.midiOutputDevice);
        canaux.push_back(static_cast<uint8_t>(std::clamp(t.midiInputChannel, 0, 16)));
    }
    audioEngine_.setTrackMidiOutputs(std::move(ports));
    audioEngine_.setTrackInputChannels(std::move(canaux));   // D28.3
}

void MainComponent::setSelectedTrackMidiProgram(int programme, int banque) {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;
    auto& track = project_.tracks[piste];
    const int p = programme < 0 ? -1 : std::clamp(programme, 0, 127);
    const int b = banque < 0 ? -1 : std::clamp(banque, 0, 16383);
    if (track.midiProgram == p && track.midiBank == b) return;
    beginProjectEdit(u8"Programme MIDI");
    track.midiProgram = p;
    track.midiBank = b;
    refreshTransportSchedule();   // le graphe voit le changement et l'envoie
    arrangement_.repaint();
}

void MainComponent::promptMidiProgram() {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;
    const auto& track = project_.tracks[piste];
    auto* fenetre = new juce::AlertWindow(
        tr(u8"Programme MIDI de la piste"),
        tr(u8"Envoyés sur le port de la piste au départ de la lecture et à chaque changement. "
           u8"Programme de 1 à 128 (vide : aucun) ; banque de 0 à 16383 (vide : aucune)."),
        juce::MessageBoxIconType::NoIcon);
    fenetre->addTextEditor("programme", track.midiProgram >= 0 ? juce::String(track.midiProgram + 1) : juce::String(), tr(u8"Programme :"));
    fenetre->addTextEditor("banque", track.midiBank >= 0 ? juce::String(track.midiBank) : juce::String(), tr(u8"Banque :"));
    fenetre->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    fenetre->addButton(vsm::app::ui::trSelon("bouton", u8"Annuler"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    fenetre->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, fenetre](int resultat) {
            if (resultat != 1) return;
            const juce::String p = fenetre->getTextEditorContents("programme").trim();
            const juce::String b = fenetre->getTextEditorContents("banque").trim();
            setSelectedTrackMidiProgram(p.isEmpty() ? -1 : p.getIntValue() - 1, b.isEmpty() ? -1 : b.getIntValue());
        }), true);
}

void MainComponent::setSelectedTrackInputChannel(int canal) {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;
    const int voulu = std::clamp(canal, 0, 16);
    if (project_.tracks[piste].midiInputChannel == voulu) return;
    beginProjectEdit(u8"Canal d'entr\u00e9e MIDI");
    project_.tracks[piste].midiInputChannel = voulu;
    syncMidiOutputs();
    arrangement_.repaint();
}

// --- D24.5 : un fichier audio sur une piste neuve --------------------------

bool MainComponent::importAudioFileOnNewTrack(const juce::File& fichier) {
    if (!fichier.existsAsFile()) {
        std::fputs(("Importer un fichier audio : introuvable -- " + fichier.getFullPathName().toStdString() + "\n").c_str(), stderr);
        return false;
    }
    if (currentProjectFolder_ == juce::File()) {
        // MÊME EXIGENCE QUE LE LÂCHER SUR UNE PISTE : le fichier est COPIÉ dans
        // le dossier du projet (D6.4), et sans dossier il n'y a nulle part où
        // le copier. Dit avant de créer la piste, pour ne pas laisser une piste
        // vide dans l'historique.
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Projet jamais enregistr\u00e9"),
            tr(u8"Un fichier audio import\u00e9 est COPI\u00c9 dans le dossier du projet. "
               u8"Enregistrez d'abord le projet (Ctrl+S)."));
        std::fputs("Importer un fichier audio : projet jamais enregistr\u00e9, rien n'a \u00e9t\u00e9 fait\n", stderr);
        return false;
    }
    addTrack(Track::Kind::Audio, fichier.getFileNameWithoutExtension().toStdString());
    const size_t index = project_.tracks.size() - 1;
    if (!placeSampleOnTrack(index, 0, fichier)) {
        // La piste neuve ne sert à rien sans son fichier : on la retire, et
        // l'historique garde les deux gestes -- annuler deux fois ramène au
        // point de départ, ce qui est exact.
        trackList_.selectTrackIndex(index);
        removeSelectedTrack();
        return false;
    }
    trackList_.selectTrackIndex(index);
    refreshTrackViews();
    return true;
}

void MainComponent::importAudioFilePrompt() {
    auto chooser = std::make_shared<juce::FileChooser>(
        tr(u8"Importer des fichiers audio, un par piste neuve"), juce::File(),
        "*.wav;*.flac;*.ogg;*.mp3;*.aif;*.aiff");
    // D33.1 : PLUSIEURS FICHIERS D'UN COUP. Une reconstruction qui rend douze
    // stems se réimportait en douze gestes ; `canSelectMultipleItems` et
    // `getResults()` en font un.
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::canSelectMultipleItems,
                         [this, chooser](const juce::FileChooser& fc) {
        importAudioFiles(fc.getResults());
    });
}

void MainComponent::importAudioFiles(const juce::Array<juce::File>& fichiers) {
    if (fichiers.isEmpty()) return;
    size_t entres = 0;
    juce::StringArray refuses;
    for (const auto& fichier : fichiers) {
        // CE QUI ÉCHOUE N'ARRÊTE PAS LE RESTE, et est NOMMÉ. Un import qui
        // s'arrêterait au premier fichier illisible serait pire que pas
        // d'import multiple du tout : on aurait douze stems à poser et l'on
        // s'arrêterait au troisième sans savoir lesquels sont entrés.
        if (importAudioFileOnNewTrack(fichier)) ++entres;
        else refuses.add(fichier.getFileName());
    }
    juce::String message = tr(u8"Import audio : %1 piste(s) créée(s) sur %2 fichier(s)")
                               .replace("%1", juce::String(static_cast<int>(entres)))
                               .replace("%2", juce::String(fichiers.size()));
    if (!refuses.isEmpty())
        message += " ; " + tr(u8"refusé(s) : %1").replace("%1", refuses.joinIntoString(", "));
    std::fputs((message + ".\n").toRawUTF8(), stderr);
    if (!refuses.isEmpty())
        montrerBoite(
            juce::AlertWindow::WarningIcon, tr(u8"Import audio"), message);
}

void MainComponent::exportSelectedTrackMidi() {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;
    auto chooser = std::make_shared<juce::FileChooser>(
        tr(u8"Exporter la piste « %1 » en MIDI...").replace("%1", juce::String(project_.tracks[piste].name)),
        juce::File(), "*.mid");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                         [this, chooser](const juce::FileChooser& fc) {
        const juce::File fichier = fc.getResult();
        if (fichier == juce::File()) return;
        writeSelectedTrackMidi(fichier);
    });
}

void MainComponent::playNoteForCapture(uint8_t note, int restant) {
    audioEngine_.playComputerKey(note, 100, true);
    juce::Timer::callAfterDelay(100, [this, note] { audioEngine_.playComputerKey(note, 0, false); });
    if (restant > 0)
        juce::Timer::callAfterDelay(200, [this, note, restant] { playNoteForCapture(note, restant - 1); });
}

// --- D22.5 : les presets de piste -------------------------------------------

juce::File MainComponent::trackPresetFolder() const {
    const juce::String bibliotheque =
        vsm::app::ui::UiScale::properties().getValue("dossierBibliotheque", "");
    // MÊME RANGEMENT QUE LES GROOVES ET LES PRESETS D'EFFET (D15.4, D17.8) :
    // la bibliothèque si elle est réglée, le projet sinon, les données de
    // l'application à défaut.
    return bibliotheque.isNotEmpty()
               ? juce::File(bibliotheque).getChildFile("pistes")
               : (currentProjectFolder_ != juce::File()
                      ? currentProjectFolder_.getChildFile("pistes")
                      : juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                            .getChildFile("VSM").getChildFile("pistes"));
}

juce::Array<juce::File> MainComponent::trackPresetFiles() const {
    juce::Array<juce::File> fichiers;
    trackPresetFolder().findChildFiles(fichiers, juce::File::findFiles, false,
                                       "*" + juce::String(vsm::interchange::kTrackPresetExtension));
    // Triés par nom : un menu qui change d'ordre à chaque ouverture ne se
    // parcourt pas.
    std::sort(fichiers.begin(), fichiers.end(), [](const juce::File& a, const juce::File& b) {
        return a.getFileName().compareIgnoreCase(b.getFileName()) < 0;
    });
    return fichiers;
}

bool MainComponent::saveSelectedTrackAsPreset(const juce::String& nom) {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size() || nom.trim().isEmpty()) return false;
    captureSessionIntoProject();
    const auto& track = project_.tracks[piste];
    // L'ÉTAT DE LA MACHINE VIENT DE LA MACHINE VIVANTE (D0.1) : le modèle ne
    // le porte pas, et un preset pris sur le patch d'usine ne sonnerait pas
    // comme la piste qu'on vient de régler.
    std::optional<vsm::interchange::SynthPreset> synth;
    if (!track.instrumentId.empty())
        if (auto* machine = audioEngine_.processGraph().trackInstrument(piste))
            synth = vsm::interchange::capturePreset(*machine, track.instrumentId, nom.toStdString());
    const auto preset = vsm::interchange::trackPresetFromTrack(track, nom.trim().toStdString(), synth);
    const juce::File dossier = trackPresetFolder();
    dossier.createDirectory();
    const juce::File fichier = dossier.getChildFile(
        juce::File::createLegalFileName(nom.trim()) + juce::String(vsm::interchange::kTrackPresetExtension));
    const auto texte = vsm::interchange::trackPresetToJson(preset).toString();
    if (!fichier.replaceWithText(juce::String(texte))) {
        montrerBoite(
            juce::AlertWindow::WarningIcon, tr(u8"Enregistrer la piste comme preset"),
            tr(u8"Écriture impossible : %1").replace("%1", fichier.getFullPathName()));
        std::fputs(("Preset de piste : \u00e9criture impossible dans "
                    + fichier.getFullPathName().toStdString() + "\n").c_str(), stderr);
        return false;
    }
    std::fputs(("Preset de piste \u00e9crit : " + fichier.getFullPathName().toStdString() + "\n").c_str(), stderr);
    return true;
}

void MainComponent::promptSaveTrackPreset() {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;
    const auto& track = project_.tracks[piste];
    auto* fenetre = new juce::AlertWindow(
        tr(u8"Enregistrer la piste comme preset"),
        tr(u8"La machine et son état, les inserts, les départs, le volume, le "
           u8"panoramique, la couleur, la transposition et le décalage -- pas les "
           u8"notes ni les clips. Écrit dans %1")
            .replace("%1", trackPresetFolder().getFullPathName()),
        juce::MessageBoxIconType::NoIcon);
    fenetre->addTextEditor("nom", track.name.empty() ? tr(u8"Piste %1").replace("%1", juce::String(piste + 1))
                                                     : juce::String(track.name), tr(u8"Nom :"));
    fenetre->addButton(tr(u8"Enregistrer"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    fenetre->addButton(vsm::app::ui::trSelon("bouton", u8"Annuler"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    fenetre->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, fenetre](int resultat) {
            if (resultat != 1) return;
            const juce::String nom = fenetre->getTextEditorContents("nom").trim();
            if (nom.isEmpty()) return;
            if (saveSelectedTrackAsPreset(nom))
                montrerBoite(
                    juce::AlertWindow::InfoIcon, tr(u8"Enregistrer la piste comme preset"),
                    tr(u8"Écrit : %1").replace("%1", trackPresetFolder().getChildFile(
                        juce::File::createLegalFileName(nom)
                        + juce::String(vsm::interchange::kTrackPresetExtension)).getFullPathName()));
        }), true);
}

void MainComponent::applyTrackPresetFile(const juce::File& fichier) {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;
    const auto lu = vsm::interchange::parseTrackPreset(fichier.loadFileAsString().toStdString());
    if (!lu.success) {
        // NOMMÉ, JAMAIS DEVINÉ : un fichier qui n'est pas un preset de piste
        // dit ce qu'il est plutôt que de s'appliquer vide.
        montrerBoite(
            juce::AlertWindow::WarningIcon, tr(u8"Appliquer un preset de piste"),
            fichier.getFileName() + " : " + vsm::app::ui::trPhrase(juce::String::fromUTF8(lu.error.c_str())));
        std::fputs(("Preset de piste illisible : " + lu.error + "\n").c_str(), stderr);
        return;
    }
    beginProjectEdit(u8"Appliquer un preset de piste");
    vsm::interchange::applyTrackPresetToTrack(lu.preset, project_.tracks[piste]);
    // Les inserts, les départs, la machine : refabriqués depuis le modèle,
    // comme après un annuler.
    rebuildFromProject(false);
    trackList_.selectTrackIndex(piste);
    if (lu.preset.synth && project_.tracks[piste].kind == vsm::sequencer::Track::Kind::Midi) {
        auto* machine = audioEngine_.processGraph().trackInstrument(piste);
        if (machine == nullptr) {
            montrerBoite(
                juce::AlertWindow::WarningIcon, tr(u8"Preset de piste appliqué sans sa machine"),
                tr(u8"La machine « %1 » n'est pas disponible : les inserts et le mixage sont appliqués, "
                   u8"l'état de la machine non.")
                    .replace("%1", juce::String::fromUTF8(lu.preset.instrumentId.c_str())));
        } else {
            // D52 : CES DEUX RAPPORTS ÉTAIENT JETÉS. `applyPreset` et
            // `applyPresetSamples` rendent chacun un compte rendu dont
            // l'en-tête promet que « rien n'est jamais appliqué en douce » --
            // et le chemin du preset de SYNTHÉ, dans ce même fichier, ouvre
            // une boîte « Preset appliqué, avec des reserves ». Le chemin du
            // preset de PISTE, lui, appelait les deux fonctions comme des
            // instructions et laissait tomber ce qu'elles disaient : un
            // paramètre que la machine cible ne connaît pas disparaissait sans
            // un mot, et l'on croyait entendre le preset du fichier.
            const auto applique = vsm::interchange::applyPreset(
                *lu.preset.synth, *machine, project_.tracks[piste].instrumentId);
            const auto echantillons = vsm::interchange::applyPresetSamples(
                *lu.preset.synth, *machine,
                fichier.getParentDirectory().getFullPathName().toStdString());
            juce::StringArray reserves;
            if (applique.unsupportedCount() > 0 || applique.clampedCount() > 0)
                reserves.add(juce::String::fromUTF8(applique.summary().c_str()));
            if (echantillons.aQuelqueChoseADire())
                reserves.add(juce::String::fromUTF8(echantillons.summary().c_str()));
            if (!reserves.isEmpty()) {
                montrerBoite(
                    juce::AlertWindow::InfoIcon, tr(u8"Preset de piste appliqué, avec des réserves"),
                    vsm::app::ui::trPhrase(reserves.joinIntoString("\n")));
                // Et sur le terminal, pour que la boîte -- qu'aucune capture ne
                // traverse -- ne soit pas le seul endroit où la chose existe.
                std::fputs((juce::String(u8"VSM_PRESET : réserves — ")
                            + reserves.joinIntoString(" ; ") + "\n").toRawUTF8(), stderr);
            }
        }
    }
    updateSynthRackForSelection();
    refreshTrackViews();
}

void MainComponent::toggleLockSelectedTrack() {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;
    auto& track = project_.tracks[piste];
    beginProjectEdit(track.locked ? u8"Déverrouiller une piste" : u8"Verrouiller une piste");
    track.locked = !track.locked;
    // VERROUILLER N'EST PAS TAIRE : rien n'est republié au moteur, la piste
    // continue de sonner exactement comme elle sonnait. Seules les vues qui
    // la dessinent ont quelque chose à apprendre.
    arrangement_.repaint();
    trackList_.repaint();
    pianoRollPanel_.refresh();
}

void MainComponent::recoverRetrospectiveTake() {
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size() || retrospectif_.empty()) return;
    if (project_.tracks[piste].kind != vsm::sequencer::Track::Kind::Midi) {
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Récupérer ce qui vient d'être joué"),
            tr(u8"Choisissez une piste MIDI : ce qui a été joué au clavier est fait de notes, "
               u8"et une piste audio n'en porte pas."));
        return;
    }

    uint64_t compteur = project_.peekNextNoteId();
    const auto notes = vsm::sequencer::recoverRetrospective(
        retrospectif_,
        [this](double secondes) { return project_.secondsToTicks(secondes); }, compteur);
    // D25.4 : LES CONTRÔLEURS DE LA MÊME FENÊTRE -- la molette jouée avec
    // les notes revient avec elles, superposée aussi.
    const auto controles = retrospectif_.takeControls(
        retrospectif_.earliestSeconds(), std::numeric_limits<double>::infinity(),
        [this](double secondes) { return project_.secondsToTicks(secondes); });
    if (notes.empty() && controles.empty()) return;

    beginProjectEdit(u8"Récupérer ce qui vient d'être joué");
    // SUPERPOSÉ, jamais substitué : on récupère ce qu'on vient de jouer
    // par-dessus ce qui était là, comme un overdub. Remplacer effacerait un
    // travail que personne n'a demandé d'effacer.
    vsm::sequencer::applyRecording(project_.tracks[piste], notes,
                                    vsm::sequencer::RecordMode::Overdub, 0, 0);
    vsm::sequencer::applyRecordedControls(project_.tracks[piste], controles, false, 0, 0);
    project_.ensureNoteIdAbove(compteur - 1);
    // LE TAMPON EST VIDÉ : sans cela, un second « récupérer » reposerait les
    // mêmes notes une seconde fois, en double et sans que rien ne le dise.
    retrospectif_.clear();
    rebuildFromProject(false);
    refreshTransportSchedule();
    pianoRollPanel_.refresh();
    arrangement_.repaint();
    refreshHistoryList();
    montrerBoite(
        juce::AlertWindow::InfoIcon, tr(u8"Récupérer ce qui vient d'être joué"),
        (notes.size() > 1 ? tr(u8"%1 notes posées sur « %2 », à leur place sur la ligne de temps.")
                          : tr(u8"%1 note posée sur « %2 », à leur place sur la ligne de temps."))
            .replace("%1", juce::String(static_cast<int>(notes.size())))
            .replace("%2", juce::String(project_.tracks[piste].name)));
}

void MainComponent::createClipOnTrack(size_t trackIndex, vsm::midi::Tick tick) {
    if (trackIndex >= project_.tracks.size()) return;
    auto& piste = project_.tracks[trackIndex];
    // UN GROUPE N'A PAS DE MATÉRIAU : c'est un bus, pas un dossier (§ 4 de la
    // feuille de route). Lui poser un clip ne jouerait rien et laisserait
    // croire le contraire.
    if (piste.kind == vsm::sequencer::Track::Kind::Group) {
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Créer un clip"),
            tr(u8"Un groupe est un bus de mixage, pas une piste de matériau : il ne porte pas de clip."));
        return;
    }

    const vsm::midi::Tick mesure = std::max<vsm::midi::Tick>(
        1, project_.timeSignatureMap.ticksPerBar(std::max<vsm::midi::Tick>(0, tick),
                                                  project_.ticksPerQuarterNote));
    // La fin du matériau de la piste : le même calcul que dans l'arrangement.
    vsm::midi::Tick finMateriau = 0;
    for (const auto& note : piste.notes) finMateriau = std::max(finMateriau, note.endTick);
    if (piste.kind == vsm::sequencer::Track::Kind::Audio && piste.audio.sampleRate > 0.0)
        finMateriau = std::max(finMateriau, project_.secondsToTicks(piste.audio.durationSeconds()));

    uint64_t compteur = project_.peekNextClipId();
    // On travaille sur une COPIE : `beginProjectEdit` prend l'instantané
    // d'annulation, et il ne doit le prendre que si quelque chose va changer --
    // un refus qui laisse une entrée « Créer un clip » dans l'historique ferait
    // annuler du vide.
    auto essai = piste.clips;
    const auto faite = vsm::sequencer::createClip(essai, tick, mesure, compteur,
                                                   finMateriau);
    if (faite.id == 0) {
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Créer un clip"),
            tr(u8"Il y a déjà un clip à cet endroit de la piste. Deux clips qui se recouvrent "
               u8"joueraient le même matériau deux fois : posez-le sur un espace libre, ou tirez "
               u8"le bord du clip existant."));
        return;
    }

    beginProjectEdit(u8"Créer un clip");
    piste.clips = std::move(essai);
    project_.ensureClipIdAbove(faite.id);
    for (auto& clip : piste.clips)
        if (clip.id == faite.id) { clip.name = piste.name; clip.colorRgba = piste.colorRgba; }
    refreshTransportSchedule();
    arrangement_.repaint();
    // PANNE MUETTE INTERDITE, même quand le geste réussit à moitié : un clip
    // raccourci par son voisin n'est pas celui qu'on a demandé, et rien à
    // l'écran ne dirait pourquoi il fait une demi-mesure.
    if (faite.truncated)
        montrerBoite(
            juce::AlertWindow::InfoIcon, tr(u8"Créer un clip"),
            tr(u8"Le clip s'arrête au clip suivant : il fait %1 mesure au lieu d'une.")
                .replace("%1", juce::String(static_cast<double>(faite.length) / static_cast<double>(mesure), 2)));
}

void MainComponent::refreshMarkerViews() {
    pianoRollPanel_.refresh();
    arrangement_.repaint();
}

void MainComponent::requestMarker(vsm::midi::Tick tick) {
    auto fenetre = std::make_shared<juce::AlertWindow>(
        tr(u8"Poser un repère"), tr(u8"Nom du repère :"), juce::AlertWindow::NoIcon);
    fenetre->addTextEditor("nom", "", "");
    // D91 : « Poser » un repère se dit « Add » ; « Poser » un fichier sur une
    // piste, « Place ». Un mot français, deux verbes anglais : trSelon.
    fenetre->addButton(vsm::app::ui::trSelon("repere", u8"Poser"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    fenetre->addButton(vsm::app::ui::trSelon("bouton", u8"Annuler"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    fenetre->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, tick, fenetre](int resultat) {
            const juce::String nom = fenetre->getTextEditorContents("nom").trim();
            fenetre->exitModalState(resultat);
            fenetre->setVisible(false);
            if (resultat != 1 || nom.isEmpty()) return;
            beginProjectEdit(u8"Poser un repère");
            project_.markers.push_back({tick, nom.toStdString()});
            std::sort(project_.markers.begin(), project_.markers.end(),
                       [](const vsm::sequencer::Marker& a, const vsm::sequencer::Marker& b) {
                           return a.tick < b.tick;
                       });
            refreshMarkerViews();
        }), false);
}

void MainComponent::renameMarker(size_t index) {
    if (index >= project_.markers.size()) return;
    auto fenetre = std::make_shared<juce::AlertWindow>(
        tr(u8"Renommer le repère"), tr(u8"Nom du repère :"), juce::AlertWindow::NoIcon);
    fenetre->addTextEditor("nom", juce::String(project_.markers[index].name), "");
    fenetre->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    fenetre->addButton(vsm::app::ui::trSelon("bouton", u8"Annuler"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    fenetre->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, index, fenetre](int resultat) {
            const juce::String nom = fenetre->getTextEditorContents("nom").trim();
            fenetre->exitModalState(resultat);
            fenetre->setVisible(false);
            if (resultat != 1 || nom.isEmpty() || index >= project_.markers.size()) return;
            beginProjectEdit(u8"Renommer un repère");
            project_.markers[index].name = nom.toStdString();
            refreshMarkerViews();
        }), false);
}

void MainComponent::removeMarker(size_t index) {
    if (index >= project_.markers.size()) return;
    beginProjectEdit(u8"Retirer un repère");
    project_.markers.erase(project_.markers.begin() + static_cast<long>(index));
    refreshMarkerViews();
}

namespace {

/// D76 : REPOSER UN RÉGLAGE GARDÉ sur la machine recréée de sa piste. Un
/// réglage capturé sur une machine puis reposé sur la même doit passer
/// entier : s'il ne passe pas, c'est un défaut, et il se DIT (`VSM_REGLAGE`).
void reposerReglage(size_t piste, vsm::audio::plugin::ISynthPlugin& machine,
                    const vsm::interchange::SynthPreset& reglage, const std::string& dossier) {
    const auto applique = vsm::interchange::applyPreset(reglage, machine, reglage.pluginId);
    if (applique.unsupportedCount() > 0 || applique.clampedCount() > 0)
        std::fputs(("VSM_REGLAGE : " + vsm::interchange::libellePiste(piste) + " : "
                    + applique.summary() + "\n").c_str(), stderr);
    // LES ÉCHANTILLONS À CHEMIN ABSOLU sont ceux d'un projet jamais enregistré,
    // qui n'a pas de dossier auquel les rapporter. `applyPresetSamples` les
    // refuse, et il a raison pour un FICHIER (un projet doit rester
    // transportable) ; dans la session, le chemin est juste, et on le recharge
    // tel quel.
    vsm::interchange::SynthPreset relatifs = reglage;
    relatifs.samples.clear();
    auto* chargeur = dynamic_cast<vsm::audio::plugin::ISampleLoader*>(&machine);
    for (const auto& [emplacement, chemin] : reglage.samples) {
        if (!std::filesystem::path(chemin).is_absolute()) { relatifs.samples[emplacement] = chemin; continue; }
        std::string erreur;
        if (chargeur == nullptr || !chargeur->loadSample(emplacement, chemin, erreur))
            std::fputs(("VSM_REGLAGE : " + vsm::interchange::libellePiste(piste) + " : échantillon « "
                        + chemin + " » non rechargé" + (erreur.empty() ? "" : " : " + erreur) + "\n").c_str(),
                       stderr);
    }
    const auto echantillons = vsm::interchange::applyPresetSamples(relatifs, machine, dossier);
    if (echantillons.aQuelqueChoseADire())
        std::fputs(("VSM_REGLAGE : " + vsm::interchange::libellePiste(piste) + " : "
                    + echantillons.summary() + "\n").c_str(), stderr);
}

} // namespace

std::map<size_t, vsm::interchange::SynthPreset> MainComponent::presetsDeLaSession() {
    const std::string dossier = currentProjectFolder_ == juce::File()
                                    ? std::string()
                                    : currentProjectFolder_.getFullPathName().toStdString();
    std::map<size_t, vsm::interchange::SynthPreset> presets;
    for (size_t i = 0; i < project_.tracks.size(); ++i) {
        const auto& piste = project_.tracks[i];
        if (piste.instrumentId.empty() && piste.requestedInstrumentId.empty()) continue;
        auto* machine = (piste.instrumentId.empty() || piste.disabled)
                            ? nullptr : audioEngine_.processGraph().trackInstrument(i);
        if (machine != nullptr)
            presets[i] = vsm::interchange::capturePreset(*machine, piste.instrumentId, piste.name, dossier);
        else if (auto garde = reglagesGardes_.find(piste.uid); garde != reglagesGardes_.end())
            presets[i] = garde->second;
    }
    return presets;
}

void MainComponent::oublierLesMachines() {
    // Les instances restent en place jusqu'à la reconstruction qui suit : un
    // emplacement dont l'occupant est « inconnu » (0) est recréé, jamais gardé.
    std::fill(uidParEmplacement_.begin(), uidParEmplacement_.end(), uint64_t{0});
    reglagesGardes_.clear();
}

void MainComponent::rebuildFromProject(bool stopPlayback) {
    // LE RACK LÂCHE SA PISTE AVANT TOUTE CHOSE (trouvé en D34.3).
    //
    // UNE LECTURE APRÈS LIBÉRATION, ET ELLE FAISAIT TOMBER L'APPLICATION.
    // `updateSynthRackForSelection` donnait au rack un POINTEUR BRUT dans
    // `project_.tracks` (`setTrack(&project_.tracks[idx])`), et le moindre
    // `push_back` sur ce vecteur le réalloue : ajouter une piste pendant qu'une
    // machine à grille de pas était choisie laissait le séquenceur relire les
    // notes d'une piste détruite. `patternFromNotes` recevait un `std::vector`
    // de capacité 2 345 625 308 412 et le processus mourait sur une faute de
    // segmentation.
    //
    // TROUVÉ EN POSANT DEUX FICHIERS AUDIO LÂCHÉS SUR LA FENÊTRE, mais le
    // chemin n'a rien de neuf : « Ajouter une piste », l'import audio du menu
    // (D33.1) et tout ce qui allonge la liste des pistes passaient par là. Ce
    // qui manquait n'était pas le code, c'était de LANCER l'application après
    // l'avoir écrit -- exactement la leçon que ce dépôt avait déjà payée sur le
    // point d'entrée de D7.5.
    //
    // ICI PLUTÔT QU'À CHAQUE MUTATION : toute modification de la liste des
    // pistes finit par appeler cette fonction, et un seul endroit vaut mieux
    // que quinze dont le seizième oubliera. Le pointeur est reposé juste après,
    // par `updateSynthRackForSelection`.
    synthRack_.setTrack(nullptr);
#if VSM_WITH_VST3
    // LES FAÇADES NATIVES SE FERMENT D'ABORD (D7.4). Cette fonction refabrique
    // les instruments : une fenêtre qui resterait ouverte dessinerait un plugin
    // détruit. Les rouvrir est un geste de l'utilisateur, pas quelque chose
    // qu'on lui rend d'office -- et rien n'est perdu, l'état est dans le
    // plugin, pas dans la fenêtre.
    pluginEditorWindows_.clear();
#endif

    if (stopPlayback) transport_.stop();

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
    midiCc_.setProject(&project_);
    tempoLane_.setProject(&project_);
    pianoRoll_.setProject(&project_);
    pianoRoll_.setActiveTrackIndex(regardee);
    eventList_.setActiveTrack(static_cast<int>(regardee));   // D32.2
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
    // D30.2 : UNE PISTE DÉSACTIVÉE NE REÇOIT PAS D'INSTRUMENT. C'est ici que
    // la désactivation coûte ce qu'elle promet : le slot est vidé, la machine
    // n'est pas instanciée, et rien de son état n'est perdu -- `instrumentId`
    // reste écrit dans la piste et la retrouve à la réactivation.
    //
    // D76 : ET CETTE BOUCLE RECRÉAIT TOUTES LES MACHINES À CHAQUE APPEL.
    // `setTrackInstrument` fabrique une instance neuve même pour un identifiant
    // inchangé ; or tout ce qui touche la liste des pistes, et TOUTE annulation,
    // passe par ici. Mesuré : « Ajouter une piste MIDI » remettait à l'usine 4
    // réglages sur 9 d'une TB-303 qu'on n'avait pas touchée, et le commentaire
    // de D30.2 ci-dessus (« rien de son état n'est perdu ») était faux.
    //
    // TROIS CAS, DÉSORMAIS. L'emplacement garde sa piste et sa machine : on n'y
    // touche pas -- rien n'est détruit, rien ne peut être perdu, c'est le cas
    // courant. Sa piste a bougé ou sa machine est libérée : le réglage est
    // CAPTURÉ AVANT toute réaffectation (une réaffectation peut détruire la
    // machine qu'une autre piste attend), puis REPOSÉ sur la machine recréée.
    // Aucune piste ne le reprend (piste désactivée, supprimée) : il reste gardé
    // pour la réactivation, l'annulation, ou l'enregistrement qui l'écrit tel
    // quel. L'instance n'est jamais DÉPLACÉE d'un emplacement à l'autre : le
    // graphe ne dit pas quand le fil audio a fini le bloc où il la tient encore
    // par l'ancien, et deux fils de rendu la joueraient à la fois.
    project_.assignTrackUids();
    auto& graphe = audioEngine_.processGraph();
    const size_t emplacements = std::max(maxAssignedTracks_, project_.tracks.size());
    if (uidParEmplacement_.size() < emplacements) uidParEmplacement_.resize(emplacements, 0);
    const std::string dossierDuProjet = currentProjectFolder_ == juce::File()
                                            ? std::string()
                                            : currentProjectFolder_.getFullPathName().toStdString();
    const auto voulue = [this](size_t i) {
        return i >= project_.tracks.size() || project_.tracks[i].disabled ? std::string{}
                                                                           : project_.tracks[i].instrumentId;
    };
    const auto gardee = [&](size_t i) {
        return i < project_.tracks.size() && uidParEmplacement_[i] == project_.tracks[i].uid
            && !voulue(i).empty() && graphe.trackInstrumentId(i) == voulue(i)
            && graphe.trackInstrument(i) != nullptr;
    };
    std::vector<bool> garder(emplacements);
    for (size_t s = 0; s < emplacements; ++s) garder[s] = gardee(s);
    for (size_t s = 0; s < emplacements; ++s) {
        if (garder[s] || uidParEmplacement_[s] == 0) continue;
        if (auto* machine = graphe.trackInstrument(s))
            reglagesGardes_[uidParEmplacement_[s]] = vsm::interchange::capturePreset(
                *machine, graphe.trackInstrumentId(s), std::string(), dossierDuProjet);
    }
    size_t gardees = 0, recreees = 0, neuves = 0;
    for (size_t i = 0; i < emplacements; ++i) {
        if (garder[i]) { ++gardees; continue; }
        const std::string id = voulue(i);
        graphe.setTrackInstrument(i, id);
        uidParEmplacement_[i] = 0;
        auto* machine = graphe.trackInstrument(i);
        if (machine == nullptr) continue;
        const uint64_t uid = project_.tracks[i].uid;
        uidParEmplacement_[i] = uid;
        const auto reglage = reglagesGardes_.find(uid);
        if (reglage != reglagesGardes_.end() && reglage->second.pluginId == id) {
            reposerReglage(i, *machine, reglage->second, dossierDuProjet);
            reglagesGardes_.erase(reglage);
            ++recreees;
        } else {
            ++neuves;
        }
    }
    maxAssignedTracks_ = std::max(maxAssignedTracks_, project_.tracks.size());
    // LE CHEMIN PRIS SE DIT : sans cette ligne, un banc vérifierait un
    // résultat en croyant vérifier un chemin.
    std::fputs((juce::String("VSM_MACHINES : ") + juce::String(static_cast<int>(gardees))
                + juce::String(u8" gardée(s), ") + juce::String(static_cast<int>(recreees))
                + juce::String(u8" recréée(s) avec leur réglage, ") + juce::String(static_cast<int>(neuves))
                + juce::String(u8" neuve(s)\n")).toRawUTF8(), stderr);

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
    pianoRollPanel_.setPunchRegion(project_.punchStartTick, project_.punchEndTick,
                                    project_.punchEnabled);

    if (!project_.masterParameters.empty())
        vsm::interchange::applyMasterDescription(project_.masterParameters,
                                                  audioEngine_.processGraph().masterBus());
    adoptDefaultSendsIfNeeded();
    applySendBuses();
    loadAudioTracks();
    effectChain_.rebuildFromProject();
    for (size_t i = project_.tracks.size(); i < maxAssignedTracks_; ++i)
        audioEngine_.processGraph().setTrackEffectChain(i, nullptr);
    applyAutomationFromProject();

    arrangement_.setProject(&project_);
    refreshTransportSchedule();
    updateSynthRackForSelection();
    // L'armement suit le projet : après un chargement ou une suppression de
    // piste, les index publiés au moteur ne désigneraient plus les mêmes pistes.
    refreshArmedTracks();
    // Une prise appartient au projet qu'on vient de quitter : la garder ferait
    // porter « Quantifier la dernière prise » sur des identifiants de notes qui
    // n'existent plus ici.
    lastTake_.clear();
}

void MainComponent::refreshTransportSchedule() {
    // Republier le projet au moteur veut dire qu'il a changé : la sauvegarde
    // automatique doit le savoir, même quand le changement n'est pas passé par
    // l'historique (une prise qu'on vient de poser, un instrument assigné).
    markProjectDirty();
    // PLUS RIEN À INTERROMPRE (D8.3). Cette fonction arrêtait et relançait le
    // transport MIDI, dont l'arrêt remettait la position à zéro -- d'où les
    // deux endroits qui l'évitaient soigneusement pendant la lecture. Le
    // transport ne tient plus de position : lui donner le projet ne fait plus
    // que rafraîchir sa carte de tempo et l'endroit où le morceau finit.
    transport_.setProject(project_);
    audioEngine_.processGraph().setProject(project_);
    syncMidiOutputs();   // D27.4 : les ports suivent le projet publié
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
                            ? tr(u8"Piste %1").replace("%1", juce::String(static_cast<int>(idx) + 1)).toStdString()
                            : project_.tracks[idx].name;
    synthRack_.setSynth(synth, juce::String(name), project_.tracks[idx].instrumentId);
    // La grille de pas édite directement les notes de la piste : c'est la même
    // musique que celle du piano roll, vue autrement (voir StepPattern.h).
    synthRack_.setTrack(&project_.tracks[idx]);
}
