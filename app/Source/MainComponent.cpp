#include "MainComponent.h"
#include "vsm/sequencer/TimeEdit.h"
#include "vsm/sequencer/ProjectImport.h"
#include "vsm/interchange/DawImport.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include "vsm/midi/MidiFileParser.h"
#include "vsm/midi/MidiFileWriter.h"
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
#include "audio/ReferenceAudioLoader.h"
#include "vsm/audio/io/AudioTrackLoader.h"
#include "vsm/audio/io/WavFileReader.h"
#include "ui/UiScale.h"
#include "vsm/interchange/Json.h"
#include "ui/Shortcuts.h"

using namespace vsm::sequencer;
using namespace vsm::midi;
using vsm::audio::engine::TransportState;

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
    bottomTabs_.addTab("Mixer", vsm::ui::Palette::panel, &mixer_, false);
    bottomTabs_.addTab("Automation", vsm::ui::Palette::panel, &automation_, false);
    bottomTabs_.addTab("Effets", vsm::ui::Palette::panel, &effectChain_, false);
    bottomTabs_.addTab("MIDI CC", vsm::ui::Palette::panel, &midiCc_, false);
    bottomTabs_.addTab("Tempo", vsm::ui::Palette::panel, &tempoLane_, false);

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
    mixer_.onMixChanged = [this] { mixDirty_ = true; markProjectDirty(); };
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
    pianoRoll_.onNotesEdited = [this] { refreshTransportSchedule(); };
    // LA SAISIE PAS À PAS (D13.5) : le piano roll arme le moteur, le moteur
    // poste la note, le piano roll l'écrit. Un seul chemin pour le clavier
    // MIDI et le clavier d'ordinateur, puisque le second passe par le premier.
    pianoRoll_.onStepInputChanged = [this](bool armee) { audioEngine_.setStepInputArmed(armee); };
    audioEngine_.onStepInputNote = [this](uint8_t note, uint8_t velocity) {
        pianoRoll_.stepInputNote(note, velocity);
    };
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
        return juce::String(u8"mes. ") + juce::String(static_cast<long long>(bb.bar + 1))
               + juce::String(u8" \u00b7 ") + juce::String(static_cast<long long>(bb.beat + 1));
    };
    arrangement_.onPlayheadRequested = [this](vsm::midi::Tick tick) {
        transport_.seekToTick(tick);
        audioEngine_.processGraph().seekSeconds(project_.ticksToSeconds(tick));
    };
    arrangement_.onTrackSelected = [this](size_t index) { trackList_.selectTrackIndex(index); };
    // D11.1 : ce qu'un changement de piste a refusé se DIT — un clip audio
    // vers une piste qui porte un autre fichier, un groupe, un genre qui ne
    // correspond pas. Le geste a fait le reste ; ceci n'est pas une erreur.
    arrangement_.onClipsRefused = [](size_t refuses) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon, u8"Changement de piste",
            juce::String(static_cast<int>(refuses))
                + juce::String(refuses > 1 ? u8" clips n'ont pas changé de piste" : u8" clip n'a pas changé de piste")
                + juce::String(u8" : un clip audio ne va que vers une piste audio qui porte le même fichier "
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
            u8"Renommer le clip", u8"Le nom s'affiche sur le clip et se sauvegarde avec le projet.",
            juce::MessageBoxIconType::NoIcon);
        fenetre->addTextEditor("nom", juce::String(clip->name), u8"Nom :");
        fenetre->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
        fenetre->addButton("Annuler", 0, juce::KeyPress(juce::KeyPress::escapeKey));
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
            u8"Le clip fait N mesures",
            u8"Le clip s'étirera pour durer ce nombre de mesures, sans changer de hauteur.\n"
            u8"Le tempo d'origine du matériau sera déduit et affiché.",
            juce::MessageBoxIconType::NoIcon);
        fenetre->addTextEditor("mesures", "4", u8"Mesures :");
        fenetre->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
        fenetre->addButton("Annuler", 0, juce::KeyPress(juce::KeyPress::escapeKey));
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
                    u8"Tempo du clip",
                    juce::String(u8"Le matériau de ce clip a été enregistré à environ ")
                        + juce::String(bpm, 1) + juce::String(u8" BPM.\n")
                        + juce::String(u8"Il joue désormais à ")
                        + juce::String(project_.tempoMap.bpmAt(0), 1)
                        + juce::String(u8" BPM, sans changer de hauteur.\n\n")
                        + juce::String(u8"Adopter ce tempo pour le projet le cale sur la boucle, qui joue alors telle quelle."),
                    juce::MessageBoxIconType::InfoIcon);
                choix->addButton(u8"Garder le tempo du projet", 0, juce::KeyPress(juce::KeyPress::escapeKey));
                choix->addButton(u8"Adopter ce tempo pour le projet", 1, juce::KeyPress(juce::KeyPress::returnKey));
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
        selecteur->setName("Couleur du clip");
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
        menu.addSectionHeader("Transport");
        ajouter(Kind::TransportPlay, juce::String::fromUTF8(u8"Lecture / arrêt"));
        ajouter(Kind::TransportStop, juce::String::fromUTF8(u8"Arrêt"));
        ajouter(Kind::TransportRecord, "Enregistrement");
        ajouter(Kind::TransportLoop, "Boucle");

        const size_t piste = trackList_.selectedTrackIndex();
        if (piste < project_.tracks.size()) {
            menu.addSectionHeader(juce::String::fromUTF8(u8"Piste ") + juce::String(static_cast<int>(piste) + 1)
                                   + " — " + juce::String::fromUTF8(project_.tracks[piste].name.c_str()));
            ajouter(Kind::TrackVolume, "Volume");
            ajouter(Kind::TrackPan, "Panoramique");
            ajouter(Kind::TrackMute, "Muet");
            ajouter(Kind::TrackSolo, "Solo");
            for (size_t bus = 0; bus < project_.sends.size()
                                 && bus < vsm::audio::engine::ProcessGraph::kMaxSends; ++bus)
                ajouter(Kind::TrackSend,
                         juce::String::fromUTF8(u8"Départ ") + juce::String(static_cast<char>('A' + bus)),
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
            juce::String::fromUTF8(u8"Dossier de la bibliothèque (presets, profils, échantillons)"),
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
                    qui += juce::String("\n  · ") + juce::String::fromUTF8(c->label);
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                juce::String::fromUTF8(u8"Touche déjà prise"),
                description + juce::String::fromUTF8(u8" est déjà associée à :") + qui
                    + juce::String::fromUTF8(u8"\n\nLibérez-la d'abord, ou choisissez-en une autre."));
            return true;
        }
        shortcuts_.setKey(rebindTarget_, description.toStdString());
        saveShortcuts();
        refreshShortcutList();
        return true;
    };
    shortcutsPanel_.onExport = [this] {
        auto chooser = std::make_shared<juce::FileChooser>(
            juce::String::fromUTF8(u8"Enregistrer la table des raccourcis..."),
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

void MainComponent::resized() {
    auto area = getLocalBounds();
#if !JUCE_MAC
    menuBarComponent_.setBounds(area.removeFromTop(26));
#endif
    if (singleWindow_ && trackList_.getParentComponent() == this) {
        transportBar_.setBounds(area.removeFromTop(56));
        layoutDockedPanels(area);
        return;
    }
    transportBar_.setBounds(area);
}

void MainComponent::layoutDockedPanels(juce::Rectangle<int> area) {
    // La géométrie de l'ancienne disposition flottante, repliée dans une seule
    // fenêtre : pistes à gauche, console en bas, rack à droite, le morceau au
    // centre. Chaque volet ne prend sa place que s'il est VISIBLE -- le menu
    // Affichage cache un volet, l'espace revient au centre -- et chaque
    // frontière porte une POIGNÉE : les tailles se tirent à la souris et
    // survivent au redémarrage. Les bornes gardent toujours un centre lisible.
    constexpr int poignee = 7;
    dockBas_ = juce::jlimit(120, juce::jmax(121, area.getHeight() - 220), dockBas_);
    dockGauche_ = juce::jlimit(180, juce::jmax(181, area.getWidth() / 2), dockGauche_);
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
    else if (nom == "tempo")       bottomTabs_.setCurrentTabIndex(4);
    // CHOISIR UNE PISTE (piste:N, à partir de 0) : le piano roll, le rack et
    // l'onglet Effets suivent la piste choisie, et sans souris seule la
    // première se laissait photographier.
    else if (nom.startsWith("piste:"))
        trackList_.selectTrackIndex(static_cast<size_t>(std::max(0, nom.substring(6).getIntValue())));
    // LANCER LA LECTURE pour l'autoportrait : le compteur de CPU de la barre
    // de transport ne dit rien tant que rien ne joue, et c'est justement lui
    // qu'il faut regarder pour juger un projet à soixante-quatre machines.
    // Sans ce jeton, la charge du fil audio ne se vérifie qu'à la souris.
    else if (nom == "jouer")       transport_.play();
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
        else if (!lecture && etaitEnLecture_ && retourAuDepart_) seekAllViews(departLecture_);
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
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, u8"Notes perdues à l'enregistrement",
                u8"La file de capture a débordé : des notes jouées ne sont PAS dans la "
                u8"prise. Signalez-le -- ce n'est pas censé pouvoir arriver.");
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
    pianoRoll_.setPlayheadTick(playhead);
    synthRack_.setPlayheadTick(playhead); // éclaire le pas en cours sur les grilles
    arrangement_.setPlayheadTick(playhead);
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
    transportBar_.setSampleRate(audioEngine_.currentSampleRate());

    // Republication coalescée des changements de mix (fader/pan/mute/solo)
    // sans interrompre la lecture (contrairement à refreshTransportSchedule).
    if (mixDirty_) {
        audioEngine_.processGraph().setProject(project_);
        mixDirty_ = false;
    }

    auto& mb = audioEngine_.processGraph().masterBus();
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
    return { "Fichier", u8"Édition", "Piste", "Enregistrement", "Mixage", "Affichage", "Aide" };
}

juce::PopupMenu MainComponent::getMenuForIndex(int topLevelMenuIndex, const juce::String&) {
    juce::PopupMenu menu;
    switch (topLevelMenuIndex) {
        case 0:
            menu.addItem(kMenuFileNewProject, "Nouveau projet");
            menu.addItem(kMenuFileOpen, "Ouvrir MIDI...");
            menu.addItem(kMenuFileImportMidiIntoProject, u8"Importer un MIDI dans le projet...");
            menu.addItem(kMenuFileOpenBundle, "Ouvrir un projet VSM...");
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
                                    dossier.getFileName() + juce::String(u8"  \u2014  ") + dossier.getParentDirectory().getFullPathName()
                                        + (existe ? juce::String() : juce::String(u8"  (introuvable)")),
                                    existe);
                }
                if (liste.isEmpty()) recents.addItem(kMenuFileRecentFirst, "(aucun)", false);
                menu.addSubMenu(u8"Projets récents", recents);
            }
            menu.addItem(kMenuFileImportDaw,
                         u8"Importer un projet (Ableton, FL Studio, Cubase)...");
            // GRISÉE tant qu'aucun import n'a eu lieu, plutôt qu'absente : une
            // entrée qui apparaît puis disparaît ne s'apprend pas. Là, on voit
            // qu'un rapport EXISTE et où le retrouver.
            menu.addItem(kMenuFileImportReport, u8"Voir le dernier rapport d'import",
                         importReport_.hasReport(), false);
            // Le rapport de RECONSTRUCTION du projet ouvert (§ 4.3 du CDC
            // multipiste) : grisé quand le projet n'en a pas — un projet
            // ouvert à la main n'en a pas, et c'est normal.
            menu.addItem(kMenuFileReconstructionReport,
                         u8"Voir le rapport de reconstruction",
                         rapportReconstruction_ != juce::File(), false);
            // LA PARITÉ, COCHÉE PAR DÉFAUT : autant de pistes que le morceau a
            // de parties. C'est un choix de travail — il vaut pour toutes les
            // reconstructions — et il se voit, coché, plutôt que de vivre
            // dans un fichier de préférences que personne n'ouvre.
            menu.addItem(kMenuFileParite,
                         u8"Reconstruire en visant la parité des pistes (le défaut de la chaîne)", true,
                         vsm::app::ui::UiScale::properties()
                             .getBoolValue("reconstruireEnParite", true));
            menu.addItem(kMenuFileSave, "Enregistrer" +
                          juce::String(currentProjectFolder_ == juce::File() ? "..." : "")
                          + " (Ctrl+S)");
            menu.addItem(kMenuFileSaveAs, "Enregistrer sous...");
            menu.addSeparator();
            // D11.6 : LE MODÈLE. Un seul, dans le dossier des préférences : le
            // projet qu'on ouvre pour commencer (pistes, machines, routage,
            // tempo). « Nouveau depuis le modèle » rend un projet SANS chemin :
            // Ctrl+S demandera où, et le modèle ne s'écrase pas par mégarde.
            menu.addItem(kMenuFileSaveTemplate, u8"Enregistrer comme modèle de projet");
            menu.addItem(kMenuFileNewFromTemplate, u8"Nouveau depuis le modèle",
                         templateFolder().getChildFile("project.json").existsAsFile());
            menu.addSeparator();
            // Écoute A/B : l'enregistrement d'origine en regard de la
            // reconstruction. Les trois modes sont dans le même menu, cochés,
            // pour qu'on voie d'un coup d'œil ce qu'on est en train d'écouter.
            menu.addItem(kMenuFileLoadReference, u8"Charger l'original (référence A/B)...");
            {
                const bool aUneReference = audioEngine_.processGraph().referenceTrack().hasAudio();
                const auto mode = audioEngine_.processGraph().referenceTrack().mode();
                using Mode = vsm::audio::engine::ReferenceTrack::Mode;
                if (aUneReference && referenceDescription_.isNotEmpty()) {
                    menu.addSectionHeader(referenceDescription_);
                }
                menu.addItem(kMenuFileReferenceOff, u8"Écoute : reconstruction", aUneReference,
                              mode == Mode::Off);
                menu.addItem(kMenuFileReferenceMix, u8"Écoute : les deux", aUneReference,
                              mode == Mode::Mix);
                menu.addItem(kMenuFileReferenceSolo, u8"Écoute : original", aUneReference,
                              mode == Mode::Solo);
                menu.addItem(kMenuFileReferenceCycle, u8"Basculer l'écoute A/B (touche R)", aUneReference);
            }
            menu.addItem(kMenuFileExport, "Exporter MIDI...");
            menu.addItem(kMenuFileExportWav, "Exporter audio (WAV)...");
            menu.addItem(kMenuFileExportStems, u8"Exporter les stems (un WAV par piste)...");
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
                              juce::String::fromUTF8(u8"Reconstruire un morceau..."), dispo);
                if (!reconstructionChain_.available) {
                    menu.addItem(-1, juce::String::fromUTF8(u8"    ↳ ")
                                          + juce::String::fromUTF8(reconstructionChain_.reason.c_str()),
                                  false, false);
                    if (!reconstructionChain_.remedy.empty())
                        menu.addItem(-1, juce::String::fromUTF8(u8"    ↳ ")
                                              + juce::String::fromUTF8(reconstructionChain_.remedy.c_str()),
                                      false, false);
                    menu.addItem(kMenuFileChainFolder,
                                  juce::String::fromUTF8(u8"Indiquer le dossier de la chaîne..."));
                } else if (reconstructionRunner_.isRunning()) {
                    menu.addItem(-1, juce::String::fromUTF8(u8"    ↳ une reconstruction est déjà en cours"),
                                  false, false);
                }
            }
            menu.addSeparator();
            menu.addItem(kMenuFileAudioSettings, u8"Réglages audio...");
            menu.addItem(kMenuFilePreferences, juce::String::fromUTF8(u8"Préférences..."));
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
                                 juce::String(juce::CharPointer_UTF8("Automatique ("))
                                     + juce::String(static_cast<int>(recommande))
                                     + " threads auxiliaires ici)",
                                 true, choix == kRenderThreadsAutomatic);
                threads.addSeparator();
                const int maximum = std::min<int>(
                    static_cast<int>(vsm::audio::engine::RenderThreadPool::kMaxWorkers),
                    std::max(1, static_cast<int>(std::thread::hardware_concurrency())) - 1);
                for (int n = 0; n <= maximum; ++n) {
                    const juce::String pluriel = n > 1 ? juce::String("s") : juce::String();
                    const juce::String libelle =
                        n == 0 ? juce::String(juce::CharPointer_UTF8("Mono-cœur (aucun thread auxiliaire)"))
                               : juce::String(n) + " thread" + pluriel + " auxiliaire" + pluriel;
                    threads.addItem(kMenuAudioThreadsFirst + 1 + n, libelle, true, choix == n);
                }
                menu.addSubMenu("Threads de rendu", threads);
            }
            menu.addSeparator();
            menu.addItem(kMenuFileQuit, "Quitter");
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
                         u8"Insérer du silence entre les locateurs (Ctrl+Maj+I)",
                         project_.loopEndTick > project_.loopStartTick);
            menu.addItem(kMenuEditDeleteTimeAtLocators,
                         u8"Supprimer le temps entre les locateurs (Ctrl+Maj+K)",
                         project_.loopEndTick > project_.loopStartTick);
            menu.addItem(kMenuEditLocatorsFromSelection, u8"Locateurs sur la s\u00e9lection (P)",
                         arrangement_.hasSelection() || pianoRoll_.hasSelection());
            break;
        case 2:
            menu.addItem(kMenuTrackAdd, "Ajouter une piste MIDI");
            menu.addItem(kMenuTrackAddAudio, "Ajouter une piste audio");
            menu.addItem(kMenuTrackAddGroup, "Ajouter un groupe");
            menu.addItem(kMenuTrackRemove, u8"Supprimer la piste sélectionnée",
                         !project_.tracks.empty());
            menu.addItem(kMenuTrackDuplicate, u8"Dupliquer la piste sélectionnée",
                         !project_.tracks.empty());
            menu.addSeparator();
            {
                const size_t piste = trackList_.selectedTrackIndex();
                const bool gelable = piste < project_.tracks.size()
                                     && project_.tracks[piste].kind == Track::Kind::Midi;
                const bool gelee = gelable && project_.tracks[piste].frozen;
                menu.addItem(kMenuTrackFreeze,
                              gelee ? u8"Dégeler la piste (l'instrument reprend)"
                                    : u8"Geler la piste (l'instrument s'arrête)",
                              gelable);
                menu.addItem(kMenuTrackBounce, u8"Reporter la piste en audio (définitif)",
                              gelable);
#if VSM_WITH_CLAP || VSM_WITH_VST3
                menu.addSeparator();
#endif
#if VSM_WITH_CLAP
                menu.addItem(kMenuTrackClapPlugin, u8"Charger un plugin CLAP sur la piste...",
                              piste < project_.tracks.size()
                                  && project_.tracks[piste].kind == Track::Kind::Midi);
#endif
#if VSM_WITH_CLAP || VSM_WITH_VST3
                menu.addItem(kMenuTrackScanPlugins,
                              pluginScanner_ != nullptr
                                  ? juce::String(u8"Balayage des plugins en cours...")
                                  : juce::String(u8"Rechercher les plugins installes..."),
                              pluginScanner_ == nullptr);
                menu.addItem(kMenuTrackPluginFromCatalogue,
                              u8"Instrument parmi les plugins trouves...",
                              !pluginCatalogue_.instruments().empty()
                                  && piste < project_.tracks.size()
                                  && project_.tracks[piste].kind == Track::Kind::Midi);
#endif
#if VSM_WITH_VST3
                menu.addItem(kMenuTrackVst3Plugin, u8"Charger un instrument VST3 sur la piste...",
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
                                  u8"Ouvrir l'interface du plugin de la piste", aFacade);
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
                menu.addSectionHeader(u8"Décompte");
                menu.addItem(kMenuRecordCountInNone, "Aucun", true, mesures == 0);
                menu.addItem(kMenuRecordCountInOne, "1 mesure", true, mesures == 1);
                menu.addItem(kMenuRecordCountInTwo, "2 mesures", true, mesures == 2);
                menu.addSectionHeader(u8"Ce que fait la prise MIDI");
                menu.addItem(kMenuRecordOverdub, "Superposer",
                              true, recordMode_ == vsm::sequencer::RecordMode::Overdub);
                menu.addItem(kMenuRecordReplace, "Remplacer",
                              true, recordMode_ == vsm::sequencer::RecordMode::Replace);
                menu.addItem(kMenuRecordStack, u8"Empiler les prises",
                              true, recordMode_ == vsm::sequencer::RecordMode::Stack);
                // HORS DU MODE EMPILÉ, une prise audio remplace toujours le
                // matériau de sa piste -- une piste audio porte un seul fichier.
                // Le menu le dit plutôt que de laisser croire que
                // « superposer » la concerne.
                menu.addItem(-1, u8"(en boucle et en mode empilé, chaque passage "
                                  u8"devient une prise)", false, false);
                menu.addSeparator();

                // LA RÉGION DE PUNCH : entre ces deux points, et seulement là,
                // l'enregistrement capte. Elle se dessine à la souris sur la
                // règle du piano roll avec Alt -- comme la boucle avec Maj --
                // et le menu offre les deux gestes qu'on fait le plus souvent.
                const bool punchPose = project_.punchEndTick > project_.punchStartTick;
                menu.addSectionHeader(u8"Région de punch (Alt sur la règle)");
                menu.addItem(kMenuRecordPunchToggle, u8"Active", punchPose, project_.punchEnabled);
                menu.addItem(kMenuRecordPunchFromLoop, u8"La prendre sur la boucle",
                              project_.loopEndTick > project_.loopStartTick);
                menu.addItem(kMenuRecordPunchClear, u8"L'effacer", punchPose);
                menu.addSeparator();

                // LA LATENCE, PUBLIÉE. Le critère de D3.6 dit « le chiffre est
                // publié » : il ne suffit pas de corriger, il faut pouvoir lire
                // de combien -- sans quoi on ne saurait pas si la correction a
                // seulement eu lieu.
                {
                    const double r = audioEngine_.measuredRoundTripSeconds();
                    const double sr = audioEngine_.currentSampleRate();
                    menu.addSectionHeader(u8"Latence d'entrée");
                    menu.addItem(-2,
                                  r > 0.0
                                      ? juce::String(u8"Mesurée : ") + juce::String(r * 1000.0, 2)
                                            + " ms (" + juce::String(juce::roundToInt(r * sr))
                                            + juce::String(u8" échantillons)")
                                      : juce::String(u8"Jamais mesurée — les prises audio ne sont "
                                                      u8"pas compensées"),
                                  false, false);
                    // D11.7 : S'ENTENDRE. L'entrée recopiée vers la sortie, en
                    // direct — à la latence du périphérique, que la commande
                    // suivante mesure. Coché quand c'est actif ; jamais par défaut.
                    menu.addItem(kMenuRecordMonitorInput,
                                 u8"\u00c9couter l'entr\u00e9e en direct (latence du p\u00e9riph\u00e9rique)",
                                 audioEngine_.isDeviceOpen(), audioEngine_.inputMonitoring());
                    menu.addItem(kMenuRecordMeasureLatency,
                                  u8"Mesurer (brancher la sortie sur l'entrée)...");
                    menu.addItem(kMenuRecordClearLatency, u8"Oublier la mesure", r > 0.0);
                }
                menu.addSeparator();

                // LES PRISES DE LA PISTE SÉLECTIONNÉE. C'est le « se
                // choisissent » du critère de D3.5 : sans ce menu, les prises
                // seraient conservées et inatteignables.
                const size_t pisteChoisie = trackList_.selectedTrackIndex();
                if (pisteChoisie < project_.tracks.size()
                    && !project_.tracks[pisteChoisie].takes.empty()) {
                    const auto& prises = project_.tracks[pisteChoisie].takes;
                    menu.addSectionHeader(juce::String(u8"Prises de « ")
                                           + juce::String(project_.tracks[pisteChoisie].name)
                                           + juce::String(u8" »"));
                    for (size_t i = 0; i < prises.size() && i <= 63; ++i)
                        menu.addItem(kMenuRecordTakeFirst + static_cast<int>(i),
                                      juce::String(prises[i].name.empty()
                                                       ? ("Prise " + std::to_string(i + 1))
                                                       : prises[i].name),
                                      true,
                                      static_cast<int>(i) == project_.tracks[pisteChoisie].activeTake);
                    menu.addSeparator();
                }
                menu.addItem(kMenuRecordQuantizeTake,
                              u8"Quantifier la dernière prise (grille du piano roll)",
                              !lastTake_.empty());
            }
            break;
        case 4:
            // LE MIXAGE. Les bus de départ y sont NOMMÉS et leur effet s'y
            // choisit : ils étaient deux, figés dans le code sur une
            // réverbération et un delay, et rien -- ni le projet, ni
            // l'interface -- ne disait ce que les boutons alimentaient.
            {
                menu.addSectionHeader(u8"Bus de départ");
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
                                      u8"Retour audible", true, decrit.returnGain > 0.0f);
                    sousMenu.addItem(kMenuMixSendPreFaderFirst + static_cast<int>(bus),
                                      decrit.preFader
                                          ? u8"Pré-fader (le fader ne l'affecte pas)"
                                          : u8"Post-fader (le fader l'emporte avec lui)",
                                      true, decrit.preFader);
                    sousMenu.addSeparator();
                    sousMenu.addItem(kMenuMixRemoveSendFirst + static_cast<int>(bus),
                                      u8"Retirer ce bus");
                    menu.addSubMenu(juce::String(decrit.name.empty() ? "Bus" : decrit.name)
                                         + "  (" + juce::String(decrit.effectType) + ")",
                                     sousMenu);
                }
                if (project_.sends.empty())
                    menu.addItem(-1, u8"(aucun — les tranches n'ont pas de bouton de départ)",
                                  false, false);
                menu.addSeparator();
                menu.addItem(kMenuMixAddSend, u8"Ajouter un bus de départ",
                              project_.sends.size() < vsm::audio::engine::ProcessGraph::kMaxSends);
            }
            break;
        case 5:
            menu.addItem(kMenuViewSingleWindow, juce::String::fromUTF8(u8"Fenêtre unique"),
                          true, singleWindow_);
            menu.addItem(kMenuViewComputerKeyboard,
                         juce::String::fromUTF8(u8"Clavier d'ordinateur (A S D F… jouent la piste choisie, Z/X : octave)"),
                         true, computerKeyboard_);
            {
                auto* fenetre = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent());
                menu.addItem(kMenuViewFullScreen, u8"Plein \u00e9cran (F11)", fenetre != nullptr,
                              fenetre != nullptr && fenetre->isFullScreen());
            }
            menu.addSeparator();
            menu.addItem(kMenuViewTracks, "Pistes", true,
                          singleWindow_ ? trackList_.isVisible() : trackListWindow_.isVisible());
            menu.addItem(kMenuViewPianoRoll, "Piano Roll", true,
                          singleWindow_ ? pianoRollPanel_.isVisible() : pianoRollWindow_.isVisible());
            menu.addItem(kMenuViewSynthRack, "Synth Rack", true,
                          singleWindow_ ? synthRack_.isVisible() : synthRackWindow_.isVisible());
            menu.addItem(kMenuViewMixer, "Mixer", true,
                          singleWindow_ ? bottomTabs_.isVisible() : mixerWindow_.isVisible());
            menu.addItem(kMenuViewArrangement, "Arrangement", true,
                          singleWindow_ ? arrangement_.isVisible() : arrangementWindow_.isVisible());
            menu.addItem(kMenuViewBrowser, juce::String::fromUTF8(u8"Navigateur"),
                          true, browserWindow_ && browserWindow_->isVisible());
            menu.addItem(kMenuViewShortcuts,
                          juce::String::fromUTF8(u8"Raccourcis clavier..."),
                          true, shortcutsWindow_ && shortcutsWindow_->isVisible());
            menu.addItem(kMenuViewHistory,
                          juce::String::fromUTF8(u8"Historique des modifications..."),
                          true, historyWindow_ && historyWindow_->isVisible());
            menu.addItem(kMenuViewSpectrum,
                          juce::String::fromUTF8(u8"Analyseur de spectre..."),
                          true, spectrumWindow_ && spectrumWindow_->isVisible());
            menu.addItem(kMenuViewMidiLearn,
                          juce::String::fromUTF8(u8"Associations MIDI (")
                              + juce::String(static_cast<int>(audioEngine_.midiLearnMappingCount()))
                              + ")",
                          true, midiLearnWindow_ && midiLearnWindow_->isVisible());
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
        case 6:
            menu.addItem(kMenuHelpAbout, u8"À propos de Vintage Synth MIDI Studio");
            break;
        default:
            break;
    }
    return menu;
}

void MainComponent::menuItemSelected(int menuItemID, int /*topLevelMenuIndex*/) {
    if (menuItemID == kMenuEditInsertTimeAtLocators) { editTimeAtLocators(true); return; }
    if (menuItemID == kMenuEditDeleteTimeAtLocators) { editTimeAtLocators(false); return; }
    if (menuItemID == kMenuEditLocatorsFromSelection) { locatorsFromSelection(); return; }
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
                juce::String::fromUTF8(u8"Reconstruire un morceau (wav, mp3, flac...)"),
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
        case kMenuTrackFreeze:   toggleFreezeSelectedTrack(); break;
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
            if (singleWindow_) { trackList_.setVisible(!trackList_.isVisible()); resized(); }
            else togglePanel(trackListWindow_);
            break;
        case kMenuViewPianoRoll:
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
            if (singleWindow_) { synthRack_.setVisible(!synthRack_.isVisible()); resized(); }
            else togglePanel(synthRackWindow_);
            break;
        case kMenuViewMixer:
            if (singleWindow_) { bottomTabs_.setVisible(!bottomTabs_.isVisible()); resized(); }
            else togglePanel(mixerWindow_);
            break;
        case kMenuViewArrangement:
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

void MainComponent::loadClapPluginOnSelectedTrack() {
#if VSM_WITH_CLAP
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;

    auto chooser = std::make_shared<juce::FileChooser>(
        u8"Choisir un plugin CLAP...", juce::File("/usr/lib/clap"), "*.clap");
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::canSelectDirectories,
                          [this, chooser, piste](const juce::FileChooser& fc) {
        const juce::File fichier = fc.getResult();
        if (fichier == juce::File()) return;

        // ON REGARDE CE QU'IL Y A DEDANS AVANT DE L'INSTANCIER. Un fichier
        // .clap peut contenir plusieurs plugins, et un fichier cassé ne doit
        // jamais faire tomber l'application qui l'ouvre -- c'est déjà la
        // promesse de `scanClapFile`.
        std::string erreur;
        const auto trouves = vsm::clap::scanClapFile(fichier.getFullPathName().toStdString(),
                                                      erreur);
        if (trouves.empty()) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, u8"Plugin CLAP illisible",
                juce::String(u8"Ce fichier n'a livré aucun plugin.\n\n")
                    + juce::String(erreur));
            return;
        }

        auto poser = [this, piste, fichier](const std::string& pluginId,
                                             const std::string& nomAffiche) {
            beginProjectEdit(u8"Charger un plugin CLAP");
            auto& cible = project_.tracks[piste];
            cible.instrumentId = vsm::clap::clapInstrumentId(
                fichier.getFullPathName().toStdString(), pluginId);
            // LE PRESET DE L'ANCIENNE MACHINE NE SUIT PAS : ses identifiants
            // sémantiques ne veulent rien dire pour celle-ci, et les appliquer
            // en silence donnerait un son que personne n'a réglé.
            cible.presetId.clear();
            rebuildFromProject();
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon, u8"Plugin charge",
                juce::String(nomAffiche) + juce::String(u8" joue maintenant sur la piste ")
                    + juce::String(static_cast<int>(piste) + 1) + ".");
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
            u8"Plusieurs plugins dans ce fichier", u8"Lequel charger ?",
            juce::AlertWindow::NoIcon);
        juce::StringArray noms;
        for (const auto& info : trouves)
            noms.add(juce::String(info.name) + " -- " + juce::String(info.vendor));
        fenetre->addComboBox("plugin", noms, u8"Plugin");
        fenetre->addButton(u8"Charger", 1, juce::KeyPress(juce::KeyPress::returnKey));
        fenetre->addButton(u8"Annuler", 0, juce::KeyPress(juce::KeyPress::escapeKey));
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
    });
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
                fenetre->setName("Vintage Synth MIDI Studio -- balayage " + juce::String(fait)
                                  + "/" + juce::String(total) + " : " + courant);
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
                juce::String(pluginCatalogue_.instruments().size())
                + juce::String(u8" instrument(s), ")
                + juce::String(pluginCatalogue_.effects().size())
                + juce::String(u8" effet(s) trouves.");
            // LES FAUTIFS SONT NOMMÉS. Un fichier qui disparaît du balayage
            // sans un mot laisse l'utilisateur chercher pourquoi son plugin
            // n'apparaît nulle part.
            if (!pluginCatalogue_.faulty.empty()) {
                message += juce::String("\n\n")
                           + juce::String(pluginCatalogue_.faulty.size())
                           + juce::String(u8" fichier(s) n'ont pas pu etre lus. Ils sont isoles : "
                                          u8"ils n'ont pas fait tomber l'application, et ne "
                                          u8"seront pas rouverts.\n");
                for (const auto& fautif : pluginCatalogue_.faulty)
                    message += "\n" + juce::String(fautif.path) + "\n   "
                               + juce::String(fautif.reason);
            }
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                                                     u8"Balayage termine", message);
        });
    };
    pluginScanner_->start(false);

    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::InfoIcon, u8"Balayage lance",
        juce::String(u8"Les plugins installes sont ouverts un par un, dans un processus a "
                     u8"part.\n\nVous pouvez continuer a travailler : un plugin qui ferait "
                     u8"tomber son processus de balayage sera signale, pas fatal."));
#endif
}

void MainComponent::chooseInstrumentFromCatalogue() {
#if VSM_WITH_CLAP || VSM_WITH_VST3
    const size_t piste = trackList_.selectedTrackIndex();
    if (piste >= project_.tracks.size()) return;
    const auto instruments = pluginCatalogue_.instruments();
    if (instruments.empty()) return;

    auto fenetre = std::make_shared<juce::AlertWindow>(
        u8"Instruments trouves sur cette machine", u8"Lequel poser sur la piste ?",
        juce::AlertWindow::NoIcon);
    juce::StringArray noms;
    for (const auto& plugin : instruments)
        noms.add(juce::String(plugin.name) + "  --  " + juce::String(plugin.vendor)
                 + "  [" + juce::String(plugin.format) + "]");
    fenetre->addComboBox("plugin", noms, u8"Instrument");
    fenetre->addButton(u8"Charger", 1, juce::KeyPress(juce::KeyPress::returnKey));
    fenetre->addButton(u8"Annuler", 0, juce::KeyPress(juce::KeyPress::escapeKey));
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
            cible.instrumentId = instruments[static_cast<size_t>(choix) - 1].instrumentId();
            cible.presetId.clear();
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
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon, u8"Pas d'interface native",
            juce::String(u8"Cette machine n'a pas de facade a elle. Ses reglages restent "
                         u8"accessibles dans le Synth Rack."));
        return;
    }
    class FenetreFacade final : public juce::DocumentWindow {
    public:
        FenetreFacade(const juce::String& titre, std::function<void()> quandFermee)
            : juce::DocumentWindow(titre, juce::Colours::black,
                                    juce::DocumentWindow::closeButton),
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
        [this, piste] { pluginEditorWindows_.erase(piste); });
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
            u8"Effets trouves sur cette machine", u8"Lequel inserer ?",
            juce::AlertWindow::NoIcon);
        juce::StringArray noms;
        for (const auto& plugin : effetsConnus)
            noms.add(juce::String(plugin.name) + "  --  " + juce::String(plugin.vendor)
                     + "  [" + juce::String(plugin.format) + "]");
        noms.add(juce::String(u8"Parcourir un fichier..."));
        fenetre->addComboBox("effet", noms, u8"Effet");
        fenetre->addButton(u8"Inserer", 1, juce::KeyPress(juce::KeyPress::returnKey));
        fenetre->addButton(u8"Annuler", 0, juce::KeyPress(juce::KeyPress::escapeKey));
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
    auto chooser = std::make_shared<juce::FileChooser>(
        u8"Choisir un effet (.clap ou .vst3)...", juce::File(), "*.clap;*.vst3");
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::canSelectDirectories,
                          [this, chooser, quandChoisi](const juce::FileChooser& fc) {
        const juce::File fichier = fc.getResult();
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
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, u8"Effet illisible",
                juce::String(u8"Aucun effet n'a pu être chargé depuis ce fichier.\n\n")
                    + juce::String(erreur));
            return;
        }
        juce::ignoreUnused(nomAffiche);
        quandChoisi(identifiant);
    });
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

    auto chooser = std::make_shared<juce::FileChooser>(
        u8"Choisir un instrument VST3...", depart.isDirectory() ? depart : juce::File(),
        "*.vst3");
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::canSelectDirectories,
                          [this, chooser, piste](const juce::FileChooser& fc) {
        const juce::File fichier = fc.getResult();
        if (fichier == juce::File()) return;

        std::string erreur;
        const auto trouves = vsm::vst3::scanVst3File(fichier.getFullPathName().toStdString(),
                                                      erreur);
        if (trouves.empty()) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, u8"Plugin VST3 illisible",
                juce::String(u8"Ce fichier n'a livré aucun plugin.\n\n") + juce::String(erreur));
            return;
        }

        // ON NE PROPOSE QUE LES INSTRUMENTS. Un effet posé là où la piste
        // attend un instrument donnerait du silence, et il faudrait le deviner
        // à l'oreille. Les effets viendront en D7.3.
        std::vector<vsm::vst3::Vst3PluginInfo> instruments;
        for (const auto& info : trouves)
            if (info.isInstrument) instruments.push_back(info);
        if (instruments.empty()) {
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, u8"Pas d'instrument dans ce fichier",
                juce::String(u8"Ce fichier ne contient que des effets. Les héberger viendra "
                             u8"avec l'etape D7.3."));
            return;
        }

        auto poser = [this, piste, fichier](const std::string& pluginId,
                                             const std::string& nomAffiche) {
            beginProjectEdit(u8"Charger un instrument VST3");
            auto& cible = project_.tracks[piste];
            cible.instrumentId = vsm::vst3::vst3InstrumentId(
                fichier.getFullPathName().toStdString(), pluginId);
            // LE PRESET DE L'ANCIENNE MACHINE NE SUIT PAS : ses identités
            // sémantiques ne veulent rien dire pour celle-ci.
            cible.presetId.clear();
            rebuildFromProject();
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon, u8"Instrument charge",
                juce::String(nomAffiche) + juce::String(u8" joue maintenant sur la piste ")
                    + juce::String(static_cast<int>(piste) + 1) + ".");
        };

        if (instruments.size() == 1) {
            poser(instruments[0].id, instruments[0].name);
            return;
        }

        auto fenetre = std::make_shared<juce::AlertWindow>(
            u8"Plusieurs instruments dans ce fichier", u8"Lequel charger ?",
            juce::AlertWindow::NoIcon);
        juce::StringArray noms;
        for (const auto& info : instruments)
            noms.add(juce::String(info.name) + " -- " + juce::String(info.vendor));
        fenetre->addComboBox("plugin", noms, u8"Instrument");
        fenetre->addButton(u8"Charger", 1, juce::KeyPress(juce::KeyPress::returnKey));
        fenetre->addButton(u8"Annuler", 0, juce::KeyPress(juce::KeyPress::escapeKey));
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
    });
#endif
}

void MainComponent::exportAudioFile() {
    // D6.1 : ON DEMANDE AVANT D'ÉCRIRE. La version précédente rendait toujours
    // le morceau entier en 48 kHz / 24 bits, sans jamais le dire ni permettre
    // d'en changer : un projet travaillé à 96 kHz s'exportait rééchantillonné
    // en silence, et exporter huit mesures obligeait à exporter tout puis à
    // couper ailleurs.
    auto fenetre = std::make_shared<juce::AlertWindow>(
        u8"Exporter en audio", u8"Ce qui sera rendu :", juce::AlertWindow::NoIcon);

    juce::StringArray plages;
    plages.add(u8"Le morceau entier");
    plages.add(u8"La boucle");
    plages.add(u8"La selection");
    fenetre->addComboBox("plage", plages, u8"Plage");
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
    fenetre->addComboBox("frequence", frequences, u8"Frequence");
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
    profondeurs.add(u8"16 bits entiers");
    profondeurs.add(u8"24 bits entiers");
    profondeurs.add(u8"32 bits flottants");
    fenetre->addComboBox("profondeur", profondeurs, u8"Profondeur");
    if (auto* box = fenetre->getComboBoxComponent("profondeur"))
        box->setSelectedId(2, juce::dontSendNotification);

    // LA QUEUE EST EN SECONDES ET SE RÈGLE : deux secondes suffisent à une
    // pièce sèche et coupent net une grande réverbération, ce qui s'entend.
    fenetre->addTextEditor("queue", "2.0", u8"Queue (secondes)");

    // D6.5 : L'OPTION EST EXPLICITE ET JAMAIS COCHÉE D'AVANCE. Les machines de
    // ce projet sont déterministes : un rendu accéléré leur donne exactement
    // les mêmes échantillons, et neuf minutes rendues en dix secondes valent
    // mieux que neuf minutes rendues en neuf minutes. Un plugin qui EXIGE le
    // temps réel l'obtient de lui-même, sans que personne ait à cocher quoi que
    // ce soit -- et le rendu le dit alors dans ses avertissements.
    fenetre->addTextBlock(u8"Rendu en temps réel : uniquement si un plugin l'exige "
                          u8"(il le demande alors lui-même). Cocher ci-dessous force "
                          u8"le rendu a la vitesse du morceau.");
    juce::StringArray vitesses;
    vitesses.add(u8"Aussi vite que possible (identique au bit pres)");
    vitesses.add(u8"Au pas du temps reel");
    fenetre->addComboBox("vitesse", vitesses, u8"Vitesse de rendu");
    if (auto* box = fenetre->getComboBoxComponent("vitesse"))
        box->setSelectedId(1, juce::dontSendNotification);

    fenetre->addButton(u8"Exporter...", 1, juce::KeyPress(juce::KeyPress::returnKey));
    fenetre->addButton(u8"Annuler", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    fenetre->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, fenetre, aBoucle, aSelection, selDebut, selFin](int resultat) {
        const int plage = fenetre->getComboBoxComponent("plage")->getSelectedId();
        const int frequence = fenetre->getComboBoxComponent("frequence")->getSelectedId();
        const int profondeur = fenetre->getComboBoxComponent("profondeur")->getSelectedId();
        const double queue = std::max(0.0, fenetre->getTextEditorContents("queue").getDoubleValue());
        const int vitesse = fenetre->getComboBoxComponent("vitesse")->getSelectedId();
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

        exportAudioWithOptions(options);
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
    for (size_t i = 0; i < project_.tracks.size(); ++i) {
        if (project_.tracks[i].instrumentId.empty()) continue;
        if (auto* plugin = audioEngine_.processGraph().trackInstrument(i))
            bundle.presetsByTrack[i] = vsm::interchange::capturePreset(
                *plugin, project_.tracks[i].instrumentId, project_.tracks[i].name);
    }
    return bundle;
}

void MainComponent::exportStems() {
    auto fenetre = std::make_shared<juce::AlertWindow>(
        u8"Exporter les stems", u8"Un fichier WAV par piste, dans un dossier.\n"
        u8"La tranche master n'y est PAS : leur somme redonne le mixage tel qu'il\n"
        u8"arrive au master. C'est ce qu'on attend de stems.",
        juce::AlertWindow::NoIcon);

    juce::StringArray granularites;
    granularites.add(u8"Une piste par fichier");
    granularites.add(u8"Un groupe par fichier");
    fenetre->addComboBox("granularite", granularites, u8"Decoupage");
    if (auto* box = fenetre->getComboBoxComponent("granularite"))
        box->setSelectedId(1, juce::dontSendNotification);

    juce::StringArray profondeurs;
    profondeurs.add(u8"16 bits entiers");
    profondeurs.add(u8"24 bits entiers");
    profondeurs.add(u8"32 bits flottants");
    fenetre->addComboBox("profondeur", profondeurs, u8"Profondeur");
    // 24 BITS PAR DÉFAUT, comme pour le mixage : des stems destinés à être
    // ADDITIONNÉS ailleurs perdent à passer par 16 bits, où le bruit de
    // quantification de chaque fichier s'additionne aussi.
    if (auto* box = fenetre->getComboBoxComponent("profondeur"))
        box->setSelectedId(2, juce::dontSendNotification);

    fenetre->addTextEditor("queue", "2.0", u8"Queue (secondes)");
    fenetre->addButton(u8"Choisir le dossier...", 1, juce::KeyPress(juce::KeyPress::returnKey));
    fenetre->addButton(u8"Annuler", 0, juce::KeyPress(juce::KeyPress::escapeKey));
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
            u8"Dossier des stems...", juce::File(), "");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode
                                 | juce::FileBrowserComponent::canSelectDirectories,
                              [this, chooser, options, granularite](const juce::FileChooser& fc) {
            const juce::File dossier = fc.getResult();
            if (dossier == juce::File()) return;

            captureSessionIntoProject();
            const auto bundle = bundleFromSession();
            const auto sortie = vsm::interchange::renderStemsToFolder(
                bundle, dossier.getFullPathName().toStdString(), granularite, options);
            if (!sortie.success) {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                         u8"Erreur d'export des stems", sortie.error);
                return;
            }
            juce::String message = juce::String(sortie.stems.size())
                                  + juce::String(u8" stems ecrits dans :\n")
                                  + dossier.getFullPathName() + "\n";
            for (const auto& stem : sortie.stems) message += "\n" + juce::String(stem.name) + ".wav";
            for (const auto& warning : sortie.warnings) message += "\n\n" + juce::String(warning);
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                                                     u8"Export des stems termine", message);
        });
    }), false);
}

void MainComponent::exportAudioWithOptions(const vsm::interchange::RenderOptions& options) {
    auto chooser = std::make_shared<juce::FileChooser>(
        "Exporter en audio WAV...", juce::File(), "*.wav");

    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser, options](const juce::FileChooser& fc) {
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
        const vsm::interchange::LoadedBundle bundle = bundleFromSession();

        const auto rendered = vsm::interchange::renderBundleToWav(
            bundle, file.getFullPathName().toStdString(), options);
        if (!rendered.success) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                     "Erreur d'export audio", rendered.error);
            return;
        }

        // Les avertissements du rendu sont MONTRÉS. Un export qui laisse une
        // piste muette ou saute un effet doit le dire au moment où il le fait.
        juce::String message = juce::String(u8"Rendu écrit :\n") + file.getFullPathName() + "\n\n"
                              + juce::String(rendered.renderedSeconds, 1) + juce::String(u8" s, ")
                              + juce::String(options.sampleRate / 1000.0, 1) + juce::String(u8" kHz, ")
                              + juce::String(options.format == vsm::audio::io::SampleFormat::Int16 ? u8"16 bits"
                                            : options.format == vsm::audio::io::SampleFormat::Float32 ? u8"32 bits flottants"
                                                                                                       : u8"24 bits")
                              + juce::String(u8", crête ") + juce::String(rendered.peakLevel, 3) + ".";
        for (const auto& warning : rendered.warnings)
            message += "\n" + juce::String(warning);
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                                                 u8"Export audio terminé", message);
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
    options.dialogTitle = u8"Réglages audio";
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
            pianoRoll_.cadrerSurLesNotes();  // un projet qui arrive se regarde là où sont ses notes
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
        juce::String::fromUTF8("Importer un projet d'un autre DAW..."), juce::File(),
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
        importReport_.showFailure(juce::String::fromUTF8("Import impossible"),
                                  juce::String::fromUTF8(erreur.what()));
        std::fputs((std::string("Import : ") + erreur.what() + "\n").c_str(), stderr);
        return false;
    }

    history_.clear();
    project_ = resultat.project;
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
    importReport_.showReport(resultat.report);
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
void MainComponent::showReconstructionReport() {
    if (rapportReconstruction_ == juce::File()) return;
    const auto lu = vsm::interchange::parseJson(
        rapportReconstruction_.loadFileAsString().toStdString());
    if (!lu.success) {
        importReport_.showFailure(juce::String::fromUTF8("Rapport illisible"),
                                  juce::String::fromUTF8(lu.error.c_str()));
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
    juce::String resume;
    resume << pistesJouees << juce::String::fromUTF8(" piste(s) reconstruite(s)");
    if (bus > 0) resume << juce::String::fromUTF8(" sous ") << bus << juce::String::fromUTF8(" bus de groupe");
    const double distance = racine["globalDistance"].asNumber(-1.0);
    if (distance >= 0.0)
        resume << juce::String::fromUTF8(" · distance globale ")
               << juce::String(distance, 4)
               << juce::String::fromUTF8(" (0 = identique, 1 = silence)");
    lignes.add({resume, Ton::resume});

    // --- Le partage : qui porte le morceau -------------------------------
    const auto& partage = racine["partage"];
    if (partage.isArray() && partage.size() > 0) {
        lignes.add({{}, Ton::info});
        for (const auto& part : partage.elements()) {
            const double pourcent = part["partEnergie"].asNumber(0.0);
            juce::String texte;
            texte << juce::String::fromUTF8(part["stem"].asString("?").c_str())
                  << juce::String::fromUTF8(" : ") << juce::String(pourcent, 1)
                  << juce::String::fromUTF8(" % de l'énergie du morceau");
            // Le même seuil que le cri de la chaîne : au-delà de la moitié
            // sur un stem, « N pistes » est une description trompeuse.
            if (pourcent >= 50.0)
                texte << juce::String::fromUTF8(" — cette piste porte le morceau "
                                                "à elle seule");
            lignes.add({texte, pourcent >= 50.0 ? Ton::attention : Ton::info});
        }
    }

    // --- Les pistes mélodiques : machine et densité -----------------------
    const auto& stems = racine["stems"];
    if (stems.isArray() && stems.size() > 0) {
        lignes.add({{}, Ton::info});
        for (const auto& stem : stems.elements()) {
            juce::String texte;
            texte << juce::String::fromUTF8(stem["name"].asString("?").c_str())
                  << juce::String::fromUTF8(" → ")
                  << juce::String::fromUTF8(stem["machine"].asString("?").c_str());
            const std::string profil = stem["profile"].asString("");
            if (!profil.empty())
                texte << juce::String::fromUTF8(" [") << juce::String::fromUTF8(profil.c_str())
                      << juce::String::fromUTF8("]");
            const double poly = stem["polyphonieMoyenne"].asNumber(-1.0);
            const double ambitus = stem["ambitusDemiTons"].asNumber(-1.0);
            bool fourreTout = false;
            if (poly >= 0.0 && ambitus >= 0.0) {
                texte << juce::String::fromUTF8(" · polyphonie ")
                      << juce::String(poly, 1)
                      << juce::String::fromUTF8(" (max ")
                      << static_cast<int>(stem["polyphonieMax"].asNumber(0.0))
                      << juce::String::fromUTF8(") · ambitus ")
                      << static_cast<int>(ambitus)
                      << juce::String::fromUTF8(" demi-tons");
                // LES SEUILS DU FOURRE-TOUT, les mêmes que ceux de la chaîne
                // (analyse/analyzer/vsm_reconstruct.py, `stem_fourre_tout`) :
                // au moins 3 notes simultanées en moyenne ET 3 octaves. Le
                // jour où la chaîne publiera le verdict dans le rapport, ce
                // recalcul disparaîtra — c'est noté au § 4.3 du CDC.
                fourreTout = poly >= 3.0 && ambitus >= 36.0;
                if (fourreTout)
                    texte << juce::String::fromUTF8(" — PLUSIEURS parties sur une "
                                                    "seule piste");
            }
            lignes.add({texte, fourreTout ? Ton::attention : Ton::info});
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
        juce::String tete;
        tete << juce::String::fromUTF8("Batterie → ")
             << juce::String::fromUTF8(batterie["machine"].asString("?").c_str())
             << juce::String::fromUTF8(" · ") << static_cast<int>(pieces.size())
             << juce::String::fromUTF8(" pièce(s), ")
             << static_cast<int>(batterie["hits"].asNumber(0.0))
             << juce::String::fromUTF8(" frappe(s)");
        // LE DÉCOUPAGE PAR PIÈCE, quand il a eu lieu : sans cette ligne, un
        // projet à huit pistes de batterie ne se distinguerait pas d'un
        // projet à une seule dans ce rapport.
        const auto& decoupe = batterie["splitByPiece"];
        const bool eclatee = decoupe.isArray() && decoupe.size() > 1;
        if (eclatee)
            tete << juce::String::fromUTF8(", ÉCLATÉE en ")
                 << static_cast<int>(decoupe.size()) << juce::String::fromUTF8(" pistes");
        lignes.add({tete, Ton::info});
        for (const auto& piece : pieces.elements()) {
            juce::String ligne;
            ligne << juce::String::fromUTF8("    ")
                  << juce::String::fromUTF8(piece["family"].asString("?").c_str())
                  << juce::String::fromUTF8(" : ")
                  << static_cast<int>(piece["hits"].asNumber(0.0))
                  << juce::String::fromUTF8(" frappe(s)");
            lignes.add({ligne, Ton::info});
        }
        // Ce que la machine a dû concéder — familles sans voix, toms rabattus
        // sur un clap : le rapport les portait, l'écran les taisait.
        for (const auto& avertissement : batterie["warnings"].elements())
            lignes.add({juce::String::fromUTF8("    ")
                        + juce::String::fromUTF8(avertissement.asString("").c_str()),
                        Ton::perte});
        if (!eclatee && pieces.size() >= 2)
            lignes.add({juce::String::fromUTF8("    ")
                        + juce::String(static_cast<int>(pieces.size()))
                        + juce::String::fromUTF8(" parties sur une seule piste — la chaîne "
                                                 "sait les séparer (--batterie-par-piece)"),
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
                nouvelles.add({piste + juce::String::fromUTF8(" : le morceau mesuré est MEILLEUR sans cette "
                                                             "piste (")
                               + juce::String(sans, 4) + juce::String::fromUTF8(" contre ")
                               + juce::String(avec, 4)
                               + juce::String::fromUTF8(") — conservée : couper est une décision humaine"),
                               Ton::perte});
            if (gardee.isNotEmpty() && gardee != juce::String::fromUTF8("réglage"))
                nouvelles.add({piste + juce::String::fromUTF8(" : au mélange, gardé « ") + gardee
                               + juce::String::fromUTF8(" » plutôt que le réglage de piste"),
                               Ton::info});
            if (!nouvelles.isEmpty() && !entete) {
                lignes.add({{}, Ton::info});
                lignes.add({juce::String::fromUTF8("Verdict du mélange"), Ton::info});
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
            texte << juce::String::fromUTF8("Réverbération au mélange : RETENUE, pièce ")
                  << juce::String(taille, 1) << juce::String::fromUTF8(" à ")
                  << static_cast<int>(std::lround(dosage * 100.0))
                  << juce::String::fromUTF8(" % sur ")
                  << static_cast<int>(reverb["pistes"].size())
                  << juce::String::fromUTF8(" piste(s) mélodique(s)");
            if (temoin > 0.0 && distanceRetenue >= 0.0)
                texte << juce::String::fromUTF8(" · ") << juce::String(temoin, 4)
                      << juce::String::fromUTF8(" → ") << juce::String(distanceRetenue, 4)
                      << juce::String::fromUTF8(" (")
                      << juce::String((distanceRetenue / temoin - 1.0) * 100.0, 2)
                      << juce::String::fromUTF8(" %)");
            lignes.add({texte, Ton::info});
        } else {
            texte << juce::String::fromUTF8("Réverbération au mélange : aucune — aucun point "
                                            "de la grille ne rapproche de l'original");
            if (temoin > 0.0)
                texte << juce::String::fromUTF8(" (témoin sec ") << juce::String(temoin, 4)
                      << juce::String::fromUTF8(")");
            lignes.add({texte, Ton::info});
        }
    }

    importReport_.showLines(
        juce::String::fromUTF8("Rapport de reconstruction"),
        currentProjectFolder_ != juce::File() ? currentProjectFolder_.getFileName()
                                              : juce::String(),
        lignes);
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
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, "Projet illisible",
            juce::String::fromUTF8(loaded.error.c_str()));
        return;
    }

    history_.clear();
    project_ = loaded.bundle.project;
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
    }
    project_.assignClipIds();
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
    rebuildFromProject();
    pianoRoll_.cadrerSurLesNotes();  // un projet qui arrive se regarde là où sont ses notes
    chargerOriginalDuProjet(folder);

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
                        + juce::String(u8" : machine indisponible, preset non appliqué"));
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
                rapport.add(juce::String(static_cast<int>(douteuses))
                            + juce::String(u8" note(s) signalée(s) comme douteuses sur ")
                            + juce::String(static_cast<int>(marquees))
                            + juce::String(u8" transcrite(s) : elles sont marquées dans le "
                                            u8"piano roll, et la touche D y mène une par une"));
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
    auto chooser = std::make_shared<juce::FileChooser>(
        juce::String::fromUTF8(u8"Où se trouve le dossier analyse/ de la chaîne ?"),
        juce::File(), "");
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories,
                          [this, chooser](const juce::FileChooser& fc) {
                              const juce::File dossier = fc.getResult();
                              if (dossier == juce::File()) return;
                              vsm::app::ui::UiScale::properties().setValue(
                                  "dossierChaineAnalyse", dossier.getFullPathName());
                              vsm::app::ui::UiScale::properties().saveIfNeeded();
                              refreshReconstructionChain();
                              refreshPreferences();
                              // ON DIT TOUT DE SUITE SI ÇA A MARCHÉ. Enregistrer
                              // un chemin faux sans rien dire ferait chercher le
                              // problème ailleurs.
                              if (!reconstructionChain_.available)
                                  juce::AlertWindow::showMessageBoxAsync(
                                      juce::AlertWindow::InfoIcon,
                                      juce::String::fromUTF8(u8"Chaîne d'analyse"),
                                      juce::String::fromUTF8(reconstructionChain_.reason.c_str())
                                          + "\n\n"
                                          + juce::String::fromUTF8(reconstructionChain_.remedy.c_str()));
                          });
}

void MainComponent::startReconstruction(const juce::File& audioFile) {
    if (!reconstructionChain_.available) {
        // JAMAIS UNE ERREUR : une explication, et le moyen d'y remédier.
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon,
            juce::String::fromUTF8(u8"Reconstruction indisponible"),
            juce::String::fromUTF8(reconstructionChain_.reason.c_str()) + "\n\n"
                + juce::String::fromUTF8(reconstructionChain_.remedy.c_str()));
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
        reconstructionPanel_.setSource(audioFile.getFileName()
                                       + juce::String::fromUTF8(" — parité des pistes"));
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
    juce::File audio;
    for (const auto& f : files)
        if (vsm::interchange::isReconstructableAudio(f.toStdString())) { audio = juce::File(f); break; }
    if (audio == juce::File()) return;

    // ON DEMANDE AVANT DE PARTIR POUR DIX MINUTES. Un fichier lâché sur une
    // fenêtre est un geste ambigu -- on peut vouloir l'écouter, le poser sur
    // une piste, ou le reconstruire --, et lancer d'autorité l'opération la
    // plus longue des trois serait le pire des choix par défaut.
    if (!reconstructionChain_.available) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon,
            juce::String::fromUTF8(u8"Reconstruction indisponible"),
            audio.getFileName() + "\n\n"
                + juce::String::fromUTF8(reconstructionChain_.reason.c_str()) + "\n\n"
                + juce::String::fromUTF8(reconstructionChain_.remedy.c_str()));
        return;
    }
    pendingDroppedAudio_ = audio;
    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::QuestionIcon,
        juce::String::fromUTF8(u8"Reconstruire ce morceau ?"),
        audio.getFileName()
            + juce::String::fromUTF8(u8"\n\nLa chaîne d'analyse va le séparer, le transcrire et "
                                      u8"chercher les machines. Cela prend plusieurs minutes."),
        juce::String::fromUTF8(u8"Reconstruire"), "Annuler", this,
        juce::ModalCallbackFunction::create([this](int resultat) {
            if (resultat == 1 && pendingDroppedAudio_ != juce::File())
                startReconstruction(pendingDroppedAudio_);
            pendingDroppedAudio_ = juce::File();
        }));
}

// --- D10.2 : le MIDI learn se voit, se défait, et se souvient --------------

void MainComponent::loadMidiLearnMappings() {
    const juce::String texte =
        vsm::app::ui::UiScale::properties().getValue("midiLearnMappings", "");
    const auto lu = vsm::interchange::midiLearnFromJson(texte.toStdString());
    if (!lu.success) {
        // ON NE PERD PAS EN SILENCE. Des associations illisibles, c'est un
        // studio recâblé à la main sans savoir pourquoi.
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            juce::String::fromUTF8(u8"Associations MIDI illisibles"),
            juce::String::fromUTF8(lu.error.c_str()));
        return;
    }
    audioEngine_.setMidiLearnMap(lu.map);
    midiLearnSeenCount_ = lu.map.size();
    if (lu.discarded > 0)
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon,
            juce::String::fromUTF8(u8"Associations MIDI"),
            juce::String(static_cast<int>(lu.discarded))
                + juce::String::fromUTF8(u8" association(s) enregistrée(s) n'ont pas été relues : "
                                          u8"elles désignent une cible que cette version ne connaît "
                                          u8"pas. Elles ont été écartées plutôt que devinées."));
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
                if (piste) { project_.tracks[cible.trackIndex].volume = commande.value; projetTouche = true; }
                break;
            case Kind::TrackPan:
                // La plage a été enregistrée AVEC l'association (-1 à +1) :
                // la valeur arrive donc déjà à l'échelle du réglage. La borner
                // reste utile pour un fichier de préférences édité à la main.
                if (piste) {
                    project_.tracks[cible.trackIndex].pan = juce::jlimit(-1.0f, 1.0f, commande.value);
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
        ? juce::String::fromUTF8(u8"(projet sans titre)")
        : juce::String::fromUTF8(reprise.record.title.c_str());
    juce::String quand = minutes <= 0
        ? juce::String::fromUTF8(u8"il y a moins d'une minute")
        : juce::String::fromUTF8(u8"il y a ") + juce::String(minutes)
              + juce::String::fromUTF8(u8" minute") + (minutes > 1 ? "s" : "");

    juce::String message = titre + " — " + juce::String(reprise.record.trackCount)
        + juce::String::fromUTF8(u8" piste(s), ") + juce::String(reprise.record.noteCount)
        + juce::String::fromUTF8(u8" note(s), enregistré automatiquement ") + quand + ".";
    if (reprise.record.originalFolder.empty())
        message += juce::String::fromUTF8(
            u8"\n\nCe projet n'avait JAMAIS été enregistré : sans cette copie, il serait perdu.");
    if (sessions.size() > 1)
        message += juce::String::fromUTF8(u8"\n\n(") + juce::String(static_cast<int>(sessions.size()) - 1)
                   + juce::String::fromUTF8(u8" autre(s) session(s) interrompue(s) seront conservées "
                                             u8"et proposées au prochain lancement.)");

    const juce::File dossier = reprise.folder;
    const juce::File origine = reprise.record.originalFolder.empty()
        ? juce::File()
        : juce::File(juce::String::fromUTF8(reprise.record.originalFolder.c_str()));

    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::QuestionIcon,
        juce::String::fromUTF8(u8"Session interrompue"),
        message, juce::String::fromUTF8(u8"Récupérer"),
        juce::String::fromUTF8(u8"Ignorer et effacer"), this,
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
    if (!autosave_ || !projectDirty_) return;
    const double maintenant = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    if (maintenant - lastAutosaveSeconds_ < kAutosaveIntervalSeconds) return;
    lastAutosaveSeconds_ = maintenant;
    projectDirty_ = false;

    // LA PHOTO EST PRISE ICI, L'ÉCRITURE A LIEU AILLEURS. Capturer les presets
    // demande les machines vivantes, donc le thread de l'interface ; écrire
    // demande le disque, donc surtout pas lui.
    captureSessionIntoProject();
    std::map<size_t, vsm::interchange::SynthPreset> presets;
    for (size_t i = 0; i < project_.tracks.size(); ++i) {
        const auto& piste = project_.tracks[i];
        if (piste.instrumentId.empty()) continue;
        if (auto* machine = audioEngine_.processGraph().trackInstrument(i))
            presets[i] = vsm::interchange::capturePreset(*machine, piste.instrumentId, piste.name);
    }
    autosave_->requestSave(project_, presets, currentProjectFolder_);
}

// --- D10.3 : les raccourcis se lisent et se changent ------------------------

void MainComponent::loadShortcuts() {
    const juce::String texte =
        vsm::app::ui::UiScale::properties().getValue("raccourcis", "");
    if (!vsm::interchange::shortcutTableFromJson(texte.toStdString(), shortcuts_)) {
        // Illisible : on repart des défauts EN LE DISANT. Se retrouver avec les
        // raccourcis d'usine sans savoir pourquoi ferait chercher longtemps.
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            juce::String::fromUTF8(u8"Raccourcis illisibles"),
            juce::String::fromUTF8(u8"Les raccourcis personnalisés n'ont pas pu être relus : "
                                    u8"ceux d'origine sont rétablis."));
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
        static_cast<int>(audioEngine_.midiLearnMappingCount()), retourAuDepart_);
}

void MainComponent::showPreferences() {
    if (!preferencesWindow_) {
        preferencesWindow_ = std::make_unique<PanelWindow>(
            juce::String::fromUTF8(u8"Préférences"), preferencesPanel_);
        preferencesWindow_->setDefaultSize(560, 472);
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
        entree.name = nom;
        entree.reference = identifiant;
        entree.origin = identifiant.rfind("vsm.", 0) == 0 ? "Parc VSM" : "Plugin tiers";
        entrees.push_back(std::move(entree));
    }

    // PUIS LES FICHIERS. Le dossier du projet en premier : ses presets sont
    // ceux du morceau ouvert, donc ceux qu'on cherche en priorité.
    if (currentProjectFolder_ != juce::File())
        vsm::interchange::indexFolder(currentProjectFolder_.getFullPathName().toStdString(),
                                       "Projet", entrees);
    const juce::String bibliotheque =
        vsm::app::ui::UiScale::properties().getValue("dossierBibliotheque", "");
    if (bibliotheque.isNotEmpty())
        vsm::interchange::indexFolder(bibliotheque.toStdString(), "Bibliothèque", entrees);

    browserPanel_.setItems(std::move(entrees));
}

void MainComponent::applyBrowserItem(const vsm::interchange::BrowserItem& item,
                                      size_t trackIndex) {
    if (trackIndex >= project_.tracks.size()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon, "Navigateur",
            juce::String::fromUTF8(u8"Choisissez d'abord une piste."));
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
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, "Preset d'effet illisible",
                    juce::String::fromUTF8(lu.error.c_str()));
                return;
            }
            if (!vsm::audio::effect::EffectFactory::create(lu.preset.type)) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, juce::String::fromUTF8(u8"Preset d'effet non appliqué"),
                    juce::String::fromUTF8(u8"L'effet « ") + juce::String::fromUTF8(lu.preset.type.c_str())
                        + juce::String::fromUTF8(u8" » n'est pas disponible."));
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
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, "Preset illisible",
                    juce::String::fromUTF8(lu.error.c_str()));
                return;
            }
            // LE PRESET DIT SA MACHINE, ET ON LA MET SI ELLE MANQUE. Appliquer
            // un preset de TB-303 sur un DX7 réglerait des paramètres qui n'ont
            // pas le même sens, et rien ne dirait pourquoi ça ne sonne pas.
            beginProjectEdit(juce::String::fromUTF8(u8"Appliquer un preset"));
            if (!lu.preset.pluginId.empty()
                && project_.tracks[trackIndex].instrumentId != lu.preset.pluginId) {
                project_.tracks[trackIndex].instrumentId = lu.preset.pluginId;
                audioEngine_.processGraph().setTrackInstrument(trackIndex, lu.preset.pluginId);
                trackList_.refreshTrackRow(trackIndex);
            }
            auto* machine = audioEngine_.processGraph().trackInstrument(trackIndex);
            if (machine == nullptr) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, "Preset non appliqué",
                    juce::String::fromUTF8(u8"La machine « ")
                        + juce::String::fromUTF8(lu.preset.pluginId.c_str())
                        + juce::String::fromUTF8(u8" » n'est pas disponible."));
                return;
            }
            const auto rapport = vsm::interchange::applyPreset(lu.preset, *machine,
                                                                project_.tracks[trackIndex].instrumentId);
            vsm::interchange::applyPresetSamples(
                lu.preset, *machine,
                fichier.getParentDirectory().getFullPathName().toStdString());
            updateSynthRackForSelection();
            refreshTransportSchedule();
            // CE QUI N'A PAS PU ÊTRE APPLIQUÉ EST DIT. Un preset à moitié posé
            // qui se tait donne un son qu'on croit être celui du fichier.
            if (rapport.unsupportedCount() > 0 || rapport.clampedCount() > 0)
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::InfoIcon, "Preset appliqué, avec des reserves",
                    juce::String::fromUTF8(rapport.summary().c_str()));
            return;
        }

        case Kind::Sample:
            // POSER UN ÉCHANTILLON DEMANDE UNE POSITION, et un double-clic n'en
            // porte aucune. On le pose donc au début de la piste choisie, ce
            // qui est la réponse la moins surprenante -- et on rappelle où le
            // geste EXACT se fait, puisqu'il existe désormais.
            if (placeSampleOnTrack(trackIndex, 0, juce::File(chemin)))
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::InfoIcon, "Navigateur",
                    juce::String::fromUTF8(item.name.c_str())
                        + juce::String::fromUTF8(
                              u8" a été posé au début de la piste.\n\nPour le poser à une mesure "
                              u8"précise, glissez-le sur l'arrangement plutôt que de "
                              u8"double-cliquer."));
            return;

        case Kind::Profile:
            // UN PROFIL MULTI-ÉCHANTILLONS APPARTIENT À UNE MACHINE, pas à une
            // piste ni à une position : c'est `vsm.multisample` qui le charge,
            // depuis sa façade. Le dire vaut mieux que de le faire à moitié --
            // le poser sur une piste qui n'a pas cette machine ne produirait
            // rien, et rien n'expliquerait quoi.
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon,
                juce::String::fromUTF8(u8"Un profil se charge depuis sa machine"),
                juce::String::fromUTF8(item.name.c_str())
                    + juce::String::fromUTF8(
                          u8"\n\nUn profil multi-échantillons se charge dans la machine qui "
                          u8"l'emploie (vsm.multisample), depuis le Synth Rack. Le navigateur "
                          u8"sert ici à le TROUVER :\n\n")
                    + chemin);
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

bool MainComponent::placeSampleOnTrack(size_t trackIndex, vsm::midi::Tick tick,
                                        const juce::File& fichier) {
    if (trackIndex >= project_.tracks.size() || !fichier.existsAsFile()) return false;

    // 1. LE PROJET DOIT AVOIR UN DOSSIER. Tous les chemins d'un projet sont
    // RELATIFS au sien, et la lecture refuse même un chemin absolu (D6.4) : un
    // échantillon posé dans un projet jamais enregistré n'aurait nulle part où
    // être écrit, et le projet rouvrirait muet.
    if (currentProjectFolder_ == juce::File()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon, juce::String::fromUTF8(u8"Projet jamais enregistré"),
            juce::String::fromUTF8(
                u8"Un échantillon posé sur une piste est COPIÉ dans le dossier du projet : tous "
                u8"les chemins y sont relatifs, et c'est ce qui permet de le rouvrir ailleurs.\n\n"
                u8"Enregistrez le projet, puis reposez le fichier."));
        return false;
    }

    // 2. ON NE PERD PAS DE NOTES EN SILENCE. Une piste MIDI qui porte des notes
    // deviendrait audio, et elles disparaîtraient : c'est peut-être ce qu'on
    // veut, mais ce n'est jamais ce qu'on veut sans le savoir.
    auto& piste = project_.tracks[trackIndex];
    if (piste.kind != Track::Kind::Audio && !piste.notes.empty()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, juce::String::fromUTF8(u8"Piste déjà occupée"),
            juce::String::fromUTF8(u8"« ") + juce::String(piste.name)
                + juce::String::fromUTF8(u8" » porte ") + juce::String(static_cast<int>(piste.notes.size()))
                + juce::String::fromUTF8(u8" note(s) : en faire une piste audio les perdrait.\n\n"
                                          u8"Posez l'échantillon sur une piste vide, ou sur une "
                                          u8"piste audio."));
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
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, juce::String::fromUTF8(u8"Copie impossible"),
            juce::String::fromUTF8(u8"Impossible de copier ") + fichier.getFileName()
                + juce::String::fromUTF8(u8" dans le dossier du projet."));
        return false;
    }

    // 4. ON LIT LE FICHIER POUR SAVOIR CE QU'IL DURE. Le déclarer d'après ce
    // qu'on croit produirait un clip de la mauvaise longueur, et c'est
    // exactement l'erreur que `loadAudioTracks` corrige déjà en relisant.
    const double sr = audioEngine_.currentSampleRate() > 0.0 ? audioEngine_.currentSampleRate()
                                                              : 48000.0;
    auto lu = vsm::audio::io::loadAudioTrack(destination.getFullPathName().toStdString(), sr);
    if (!lu.success || !lu.source) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, juce::String::fromUTF8(u8"Échantillon illisible"),
            juce::String::fromUTF8(lu.error.c_str()));
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
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon, juce::String::fromUTF8(u8"Piste déjà pourvue"),
            juce::String::fromUTF8(u8"« ") + juce::String(piste.name)
                + juce::String::fromUTF8(u8" » joue déjà ")
                + juce::String(piste.audio.path.c_str())
                + juce::String::fromUTF8(u8".\n\nUne piste porte UN fichier, découpé en clips : "
                                          u8"posez celui-ci sur une autre piste."));
        return false;
    }

    beginProjectEdit(juce::String::fromUTF8(u8"Poser un échantillon"));
    if (piste.kind != Track::Kind::Audio) {
        piste.kind = Track::Kind::Audio;
        piste.instrumentId.clear();
        piste.presetId.clear();
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
        "Charger l'enregistrement d'origine (" + vsm::app::referenceAudioFormatList() + ")...",
        juce::File(), vsm::app::referenceAudioFilePatterns());
    const auto chooserFlags = juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(chooserFlags, [this, chooser](const juce::FileChooser& fc) {
        const juce::File file = fc.getResult();
        if (file == juce::File()) return;
        setReferenceAudioFile(file, /*silencieuxSiIllisible=*/false);
    });
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
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, "Enregistrement illisible",
                    result.error.isEmpty() ? juce::String("fichier sans echantillon") : result.error);
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
    referenceDescription_ = file.getFileName() + "  --  " + result.decoder + ", "
                          + juce::String(result.buffer.sampleRate / 1000.0, 1) + " kHz, "
                          + (result.buffer.isStereo() ? "stereo, " : "mono, ")
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
    result.decoder = result.decoder + " (canal gauche de comparaison.wav)";
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
        transportBar_.setListening(u8"Écoute A/B : pas d'original", false, false);
        return;
    }
    switch (reference.mode()) {
        case Mode::Off:  transportBar_.setListening(u8"Écoute : reconstruction", true, false); break;
        case Mode::Mix:  transportBar_.setListening(u8"Écoute : les deux", true, true); break;
        case Mode::Solo: transportBar_.setListening(u8"Écoute : original", true, true); break;
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
    juce::AlertWindow::showMessageBoxAsync(
        ok ? juce::AlertWindow::InfoIcon : juce::AlertWindow::WarningIcon, u8"Modèle de projet",
        ok ? juce::String(u8"Le projet courant est devenu le modèle : Fichier \u25b8 Nouveau depuis le modèle l'ouvrira, sans chemin, chaque fois.")
           : juce::String(u8"Le modèle n'a pas pu être écrit dans ") + dossier.getFullPathName());
}

void MainComponent::newFromTemplate() {
    const juce::File dossier = templateFolder();
    if (!dossier.getChildFile("project.json").existsAsFile()) return;
    loadProjectBundleFromFolder(dossier);
    // Un projet NEUF : pas de chemin, Ctrl+S demandera où. Le modèle ne se
    // réécrit que par « Enregistrer comme modèle ».
    currentProjectFolder_ = juce::File();
    if (auto* window = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent()))
        window->setName("Vintage Synth MIDI Studio -- nouveau projet (depuis le mod\u00e8le)");
}

void MainComponent::toggleFullScreen() {
    if (auto* fenetre = dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent()))
        fenetre->setFullScreen(!fenetre->isFullScreen());
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
            curve.points.push_back({point.tick, point.value,
                                     point.curveToNext == vsm::audio::engine::AutomationCurve::Step});
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
                                          : vsm::audio::engine::AutomationCurve::Linear);
            currentAutomation_.push_back(std::move(lane));
        }
    }
    audioEngine_.processGraph().setAutomationLanes(currentAutomation_);
    automation_.setProject(&project_);
    midiCc_.setProject(&project_);
    tempoLane_.setProject(&project_);
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
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                "Enregistrement impossible", result.error);
        return false;
    }
    // CE QUI MANQUE EST DIT AU MOMENT OÙ ON ENREGISTRE, pas découvert en
    // rouvrant le projet ailleurs.
    if (!result.missing.empty()) {
        juce::String message(u8"Le projet est enregistré, mais ces fichiers qu'il désigne "
                             u8"sont introuvables :\n");
        for (const auto& manquant : result.missing) message += "\n" + juce::String(manquant);
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                 u8"Projet incomplet", message);
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
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon, u8"MIDI importé",
                juce::String(static_cast<int>(bilan.tracksAdded)) + juce::String(u8" piste(s) ajoutée(s) à la tête de lecture. ")
                    + juce::String(u8"Le tempo et les mesures du fichier ont été ignorés (")
                    + juce::String(static_cast<int>(bilan.tempoChangesIgnored + bilan.timeSignaturesIgnored))
                    + juce::String(u8" changement(s)) : le projet garde les siens."));
    } catch (const std::exception& e) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                 u8"Erreur d'import MIDI", e.what());
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
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon, u8"Locateurs",
            u8"Placez d'abord les locateurs : la région de boucle est la plage à insérer ou à supprimer.");
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

void MainComponent::loadAudioTracks() {
    const double sr = audioEngine_.currentSampleRate() > 0.0 ? audioEngine_.currentSampleRate()
                                                              : 48000.0;
    juce::StringArray manquants;
    waveformCache_.clear();
    for (size_t i = 0; i < project_.tracks.size(); ++i) {
        const auto& track = project_.tracks[i];
        // UNE PISTE GELÉE JOUE SON FICHIER DE GEL (D5.5), quelle que soit sa
        // nature : c'est tout l'objet du gel. Une piste audio joue le sien.
        const bool gelee = track.frozen && !track.frozenAudio.empty();
        const auto& source = gelee ? track.frozenAudio : track.audio;
        if ((!gelee && track.kind != vsm::sequencer::Track::Kind::Audio) || source.empty()) {
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
        const juce::File fichier = currentProjectFolder_.getChildFile(source.path);
        auto charge = vsm::audio::io::loadAudioTrack(fichier.getFullPathName().toStdString(), sr);
        if (!charge.success || !charge.source) {
            manquants.add(juce::String(track.name) + " : " + juce::String(charge.error));
            audioEngine_.processGraph().setTrackAudio(i, nullptr);
            continue;
        }
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
            pourLesClips, sr, [this](int64_t tick) { return project_.ticksToSeconds(tick); });
        // LES CLIPS QUI SUIVENT LE TEMPO (D12.5) : les attaques du fichier se
        // cherchent ICI, une fois par piste, hors du thread audio -- comme le
        // cache d'aperçu juste au-dessus, et pour la même raison.
        vsm::audio::engine::prepareWarpedSpans(*charge.source);
        audioEngine_.processGraph().setTrackAudio(i, charge.source);
    }
    // UNE PISTE AUDIO QUI NE CHARGE PAS NE SE DISTINGUE PAS, À L'OREILLE, D'UNE
    // PISTE DONT ON AURAIT BAISSÉ LE VOLUME. Elle se dit donc, une fois, au
    // lieu de laisser chercher.
    if (!manquants.isEmpty())
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, u8"Audio non chargé",
            juce::String(u8"Ces pistes audio n'ont pas pu être lues :\n\n")
                + manquants.joinIntoString("\n"));
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
    parent_.project_.tracks[index_].colorRgba = selecteur->getCurrentColour().getARGB();
    parent_.arrangement_.repaint();
    parent_.trackList_.loadProject(parent_.project_);
    parent_.mixer_.setProject(&parent_.project_);
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
            audioEngine_.processGraph().setSendEffect(bus, nullptr);
            continue;
        }
        // Les réglages sont REPOSÉS depuis leurs identités sémantiques, comme
        // pour les inserts : c'est ce qui les fait survivre à un changement de
        // version de l'effet.
        vsm::sequencer::TrackEffect described;
        described.type = decrit.effectType;
        described.parameters = decrit.parameters;
        vsm::interchange::applyEffectDescription(described, *fx);
        fx->prepare(sr, blockSize);
        audioEngine_.processGraph().setSendEffect(bus, std::shared_ptr<vsm::audio::effect::IAudioEffect>(std::move(fx)));
        audioEngine_.processGraph().setSendReturn(bus, decrit.returnGain);
    }
}

void MainComponent::newProject() {
    history_.clear();   // l'annulation d'un autre morceau n'a aucun sens ici
    project_ = Project{};
    project_.title = "Nouveau projet";
    project_.sends = defaultSendBuses();
    rebuildFromProject();
}

void MainComponent::addTrack(Track::Kind kind) {
    const bool audio = kind == Track::Kind::Audio;
    const bool groupe = kind == Track::Kind::Group;
    beginProjectEdit(groupe ? juce::String(u8"Ajouter un groupe")
                    : audio  ? juce::String(u8"Ajouter une piste audio")
                              : juce::String(u8"Ajouter une piste"));
    // Palette de couleurs cyclique pour distinguer visuellement les pistes.
    static const uint32_t kColors[] = {
        0xffE3A24Du, 0xff6B9BFFu, 0xff8ED081u, 0xffD08BC8u, 0xffE0C15Au, 0xff7FD0C8u
    };
    const size_t n = project_.tracks.size();

    Track t;
    t.kind = kind;
    t.name = (groupe ? "Groupe " : audio ? "Audio " : "Piste ") + std::to_string(n + 1);
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

    // La suppression et la RÉPARATION DES ROUTAGES sont une règle du modèle,
    // pas de l'interface : voir `vsm::sequencer::removeTrack`.
    vsm::sequencer::removeTrack(project_, idx);
    rebuildFromProject();

    if (!project_.tracks.empty()) {
        const size_t next = std::min(idx, project_.tracks.size() - 1);
        trackList_.selectTrackIndex(next);
    }
}

void MainComponent::duplicateSelectedTrack() {
    const size_t idx = trackList_.selectedTrackIndex();
    if (idx >= project_.tracks.size()) return;
    beginProjectEdit(u8"Dupliquer une piste");
    const size_t copie = vsm::sequencer::duplicateTrack(project_, idx);
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
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon, u8"Projet jamais enregistré",
            juce::String(u8"Un gel est un FICHIER, et le format range les fichiers d'un projet "
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
                *machine, project_.tracks[index].instrumentId, project_.tracks[index].name);

    vsm::interchange::RenderOptions options;
    options.sampleRate = audioEngine_.currentSampleRate() > 0.0 ? audioEngine_.currentSampleRate()
                                                                 : 48000.0;
    options.blockSize = audioEngine_.currentBlockSize() > 0 ? audioEngine_.currentBlockSize() : 512;
    options.format = vsm::audio::io::SampleFormat::Float32;

    vsm::audio::engine::RenderedAudio gel;
    const auto rendu = vsm::interchange::renderTrackForFreeze(bundle, index, gel, options);
    if (!rendu.success) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                 u8"Gel impossible", rendu.error);
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
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                 u8"Gel impossible", e.what());
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
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon, u8"Projet jamais enregistré",
            juce::String(u8"Un report est un FICHIER, et le format range les fichiers d'un "
                          u8"projet par chemin relatif à son dossier. Enregistrez d'abord le "
                          u8"projet (Ctrl+S)."));
        return;
    }

    // REPORTER EST UNE DÉCISION, GELER N'EN EST PAS UNE : le report remplace le
    // matériau, et on le demande avant de le faire. L'annulation le rattrape
    // dans la session, mais pas après une fermeture -- c'est exactement ce que
    // veut dire « définitif », et le dire vaut mieux que de le découvrir.
    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::QuestionIcon, u8"Reporter la piste en audio",
        juce::String(u8"Les notes, l'instrument et les inserts de « ")
            + juce::String(project_.tracks[index].name)
            + juce::String(u8" » seront remplacés par leur rendu. C'est annulable tant que "
                            u8"la session est ouverte, et définitif ensuite.\n\nPour un "
                            u8"allègement réversible, préférez GELER la piste."),
        u8"Reporter", "Annuler", nullptr,
        juce::ModalCallbackFunction::create([this, index](int choix) {
            if (choix == 0) return;
            performBounce(index);
        }));
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
                *machine, project_.tracks[index].instrumentId, project_.tracks[index].name);

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
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                 u8"Report impossible", resultat.error);
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
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                 u8"Report impossible", e.what());
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
    piste.presetId.clear();
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
        "Exporter en MIDI...", juce::File(), "*.mid");

    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser](const juce::FileChooser& fc) {
        juce::File file = fc.getResult();
        if (file == juce::File()) return;

        try {
            ParsedFile parsed = project_.toParsedFile();
            MidiFileWriter::writeFile(parsed, file.getFullPathName().toStdString());
            // D15.5 : le MIDI ne connaît pas les rampes ; elles partent en
            // paliers d'une noire, et on le dit plutôt que de le taire.
            if (project_.tempoMap.hasRamps()) {
                const size_t paliers = project_.tempoMap.flattened(project_.ticksPerQuarterNote,
                                                                   project_.ticksPerQuarterNote).size()
                                     - project_.tempoMap.changes().size();
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::InfoIcon, juce::String::fromUTF8(u8"Rampes de tempo exportées en paliers"),
                    juce::String::fromUTF8(u8"Le format MIDI ne connaît pas les rampes : elles sont rendues en ")
                        + juce::String(static_cast<int>(paliers))
                        + juce::String::fromUTF8(u8" paliers d'une noire, à la durée totale près."));
            }
        } catch (const std::exception& e) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                     "Erreur d'export MIDI", e.what());
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
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon, u8"Mesure impossible",
            juce::String(u8"La carte n'ouvre aucune entrée : il n'y a rien à mesurer. "
                          u8"Voir Fichier > Réglages audio."));
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
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, u8"Rien n'est revenu",
                juce::String(u8"Le balayage émis n'a pas été retrouvé dans l'entrée "
                              u8"(netteté ") + juce::String(resultat.nettete, 1)
                    + juce::String(u8"). Branchez la sortie de la carte sur son entrée, ou "
                                    u8"placez un micro devant un haut-parleur, et recommencez. "
                                    u8"Aucune valeur n'a été retenue : mieux vaut ne pas "
                                    u8"compenser que compenser d'un chiffre inventé."));
            return;
        }

        const double secondes = static_cast<double>(resultat.decalageEchantillons) / sr;
        audioEngine_.setMeasuredRoundTripSeconds(secondes);
        vsm::app::ui::UiScale::properties().setValue("latenceAllerRetour", secondes);
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon, u8"Latence mesurée",
            juce::String(u8"Aller-retour : ") + juce::String(secondes * 1000.0, 2) + " ms ("
                + juce::String(resultat.decalageEchantillons)
                + juce::String(u8" échantillons à ") + juce::String(sr / 1000.0, 1) + " kHz)"
                + juce::String(u8"\n\nNetteté du pic : ") + juce::String(resultat.nettete, 1)
                + juce::String(u8"\n\nLes prises AUDIO sont désormais avancées d'autant. Les "
                                u8"prises MIDI, elles, continuent d'employer la latence de "
                                u8"sortie annoncée par le pilote : un clavier n'est pas dans "
                                u8"la boucle, et cette mesure ne peut rien en dire."));
    });
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
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon,
            armees.empty() ? juce::String(u8"Aucune piste armée") : juce::String("Aucune carte son"),
            armees.empty()
                ? "Armez au moins une piste (bouton R dans la liste des pistes) : "
                  u8"sans elle, la prise n'aurait nulle part où aller."
                : "Sans carte son ouverte, le transport n'avance pas et aucun clavier "
                  u8"MIDI n'est écouté. Voir Fichier > Réglages audio.");
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
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon, u8"Plusieurs pistes audio armées",
            juce::String(u8"Une seule entrée, une seule prise : n'armez qu'une piste audio à "
                          u8"la fois. Écrire le même signal dans deux fichiers ne ferait que "
                          u8"doubler la place occupée."));
        return;
    }
    if (!armeesAudio.empty()) {
        if (currentProjectFolder_ == juce::File()) {
            transportBar_.setRecording(false);
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::InfoIcon, u8"Projet jamais enregistré",
                juce::String(u8"Une prise audio est un FICHIER, et le format range les fichiers "
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
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon, u8"Enregistrement audio impossible", erreur);
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
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon, u8"Le disque n'a pas suivi",
        juce::String(u8"La prise a perdu ") + juce::String(static_cast<int>(blocsPerdus))
            + juce::String(u8" bloc(s) : le fichier a des trous. Un disque plus rapide, "
                            u8"ou une taille de bloc audio plus grande, y remédient."));
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

void MainComponent::refreshMarkerViews() {
    pianoRollPanel_.refresh();
    arrangement_.repaint();
}

void MainComponent::requestMarker(vsm::midi::Tick tick) {
    auto fenetre = std::make_shared<juce::AlertWindow>(
        u8"Poser un repère", u8"Nom du repère :", juce::AlertWindow::NoIcon);
    fenetre->addTextEditor("nom", "", "");
    fenetre->addButton("Poser", 1, juce::KeyPress(juce::KeyPress::returnKey));
    fenetre->addButton("Annuler", 0, juce::KeyPress(juce::KeyPress::escapeKey));
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
        u8"Renommer le repère", u8"Nom du repère :", juce::AlertWindow::NoIcon);
    fenetre->addTextEditor("nom", juce::String(project_.markers[index].name), "");
    fenetre->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    fenetre->addButton("Annuler", 0, juce::KeyPress(juce::KeyPress::escapeKey));
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

void MainComponent::rebuildFromProject(bool stopPlayback) {
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
