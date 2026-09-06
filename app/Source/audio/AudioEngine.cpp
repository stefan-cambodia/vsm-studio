#include "AudioEngine.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
    stop();
}

void AudioEngine::start(const juce::XmlElement* etatSauvegarde) {
    // DEUX ENTRÉES DEMANDÉES, ET C'EST NOUVEAU. Le moteur ouvrait la carte avec
    // ZÉRO entrée : pas de capture, donc pas d'enregistrement, ni MIDI ni
    // audio, et le rappel ignorait explicitement ses paramètres d'entrée. Un
    // logiciel qui ne peut rien capter n'est pas un studio, c'est un lecteur.
    //
    // Les entrées sont DEMANDÉES et non exigées : une machine sans entrée doit
    // rester utilisable pour éditer, mixer et exporter. Si la carte n'en donne
    // aucune, `currentInputChannels()` vaut zéro et l'interface le dit, au lieu
    // de laisser chercher pourquoi l'enregistrement ne marche pas.
    juce::String error = deviceManager_.initialise(2, 2, etatSauvegarde, true);
    if (error.isNotEmpty()) {
        // Repli SANS entrée plutôt qu'aucun son du tout : c'est le cas d'une
        // machine dont la carte n'expose que des sorties.
        error = deviceManager_.initialise(0, 2, etatSauvegarde, true);
    }
    if (error.isNotEmpty()) {
        lastError_ = error;
        return; // pas de device : l'app reste utilisable, juste sans son (voir isDeviceOpen())
    }
    lastError_.clear();
    deviceManager_.addAudioCallback(this);

    // D27.3 : LE PORT VIRTUEL, toujours là -- c'est ce qui permet de vérifier
    // la sortie sans matériel (aseqdump, un autre logiciel). Et l'émetteur.
    if (!portVirtuel_) portVirtuel_ = juce::MidiOutput::createNewDevice("VSM Studio");
    if (!emetteur_.isThreadRunning()) emetteur_.startThread(juce::Thread::Priority::high);

    // Entrées MIDI : active tous les périphériques disponibles et s'abonne
    // à leurs messages (pour le MIDI Learn et le futur MIDI-thru).
    enabledMidiInputs_.clear();
    for (const auto& device : juce::MidiInput::getAvailableDevices()) {
        // D27.3 : NOTRE PROPRE PORT DE SORTIE SE PRÉSENTE AUSSI COMME UNE ENTRÉE
        // (côté lisible d'un port virtuel ALSA). L'écouter referme la boucle :
        // ce qui sort revient, rejoue la piste choisie, ressort -- une note
        // par bloc, 3 440 reçues pour 8 jouées à la première preuve de D27.
        if (device.name == "VSM Studio") continue;
        deviceManager_.addMidiInputDeviceCallback(device.identifier, this);
        enabledMidiInputs_.push_back(device.identifier);
    }
}

void AudioEngine::stop() {
    emetteur_.stopThread(500);
    {
        std::lock_guard<std::mutex> verrou(portsMutex_);
        ports_.clear();
    }
    portVirtuel_.reset();
    for (const auto& id : enabledMidiInputs_)
        deviceManager_.removeMidiInputDeviceCallback(id, this);
    enabledMidiInputs_.clear();
    deviceManager_.removeAudioCallback(this);
    deviceManager_.closeAudioDevice();
}

void AudioEngine::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) {
    // D22.4 : compté AVANT tout tri -- un message qu'on ignore est quand même
    // ARRIVÉ, et c'est ce que le voyant IN a à dire.
    midiInCount_.fetch_add(1, std::memory_order_relaxed);
    // Notes : jouées immédiatement, et -- depuis D3.3 -- enregistrées si une
    // prise est en cours. Les DEUX chemins sont distincts et le restent :
    // l'écoute passe par la file "live" du ProcessGraph, la capture par la file
    // d'enregistrement. Confondre les deux ferait dépendre ce qu'on GARDE de ce
    // qu'on ENTEND, alors qu'on doit pouvoir enregistrer une piste muette.
    //
    // C'est bien le THREAD MIDI ici, distinct du thread UI : d'où la source
    // MidiInput, qui a sa propre file (LockFreeRingBuffer est strictement un
    // producteur / un consommateur, voir ProcessGraph::LiveNoteSource).
    if (message.isNoteOnOrOff()) {
        const auto note = static_cast<uint8_t>(message.getNoteNumber());
        const auto velocity = static_cast<uint8_t>(message.getVelocity());
        // Un NoteOn de vélocité 0 est un NoteOff déguisé (convention MIDI).
        const bool noteOn = message.isNoteOn() && velocity > 0;

        // LA SAISIE PAS À PAS (D13.5) : postée au fil d'interface, jamais
        // écrite d'ici. Le son continue de passer, on s'entend en saisissant.
        if (noteOn && stepInputArmed_.load(std::memory_order_relaxed) && onStepInputNote) {
            juce::MessageManager::callAsync([this, note, velocity] {
                if (onStepInputNote) onStepInputNote(note, velocity);
            });
        }

        // ENREGISTREMENT. La date vient de l'horodatage du PILOTE, pas de
        // l'instant où ce code s'exécute : entre les deux il y a le
        // réveil du thread MIDI, qui n'a aucune raison d'être régulier.
        // Certains pilotes ne datent rien (horodatage nul) ; on retombe alors
        // sur l'heure courante, qui est ce qu'on peut savoir de moins faux.
        // D17.3 : LA FILE SE REMPLIT TOUJOURS, et non plus seulement pendant
        // l'enregistrement. Hors enregistrement, l'application la vide dans le
        // tampon rétrospectif à chaque tour de minuterie ; ce qui entre
        // pendant l'enregistrement va au même endroit qu'avant, et le point
        // d'entrée de `MidiRecorder` écarte comme toujours ce qui le précède.
        {
            const double horodatage = message.getTimeStamp() > 0.0
                                          ? message.getTimeStamp()
                                          : juce::Time::getMillisecondCounterHiRes() * 0.001;
            vsm::sequencer::RecordedNoteEvent capture;
            uint64_t passe = 0;
            capture.seconds = transportSecondsAtClock(horodatage, &passe);
            // TRANSPORT À L'ARRÊT : le temps du morceau ne passe pas, et
            // l'ancre rendrait la même position pour toute une phrase -- un
            // accord de douze notes là où l'on a joué une mélodie. On construit
            // donc la position sur le temps RÉEL écoulé depuis la première note
            // de la rafale, posée à la tête de lecture.
            if (!graph_.isPlaying()) {
                double ancre = burstAnchorClock_.load(std::memory_order_relaxed);
                if (ancre <= 0.0) {
                    burstAnchorClock_.store(horodatage, std::memory_order_relaxed);
                    burstAnchorTransport_.store(capture.seconds, std::memory_order_relaxed);
                    ancre = horodatage;
                }
                capture.seconds = burstAnchorTransport_.load(std::memory_order_relaxed)
                                  + (horodatage - ancre);
            } else {
                burstAnchorClock_.store(0.0, std::memory_order_relaxed);
            }
            capture.pass = static_cast<uint32_t>(passe);
            capture.note = note;
            capture.velocity = velocity;
            capture.channel = static_cast<uint8_t>(juce::jlimit(1, 16, message.getChannel()) - 1);
            capture.noteOn = noteOn;
            // FILE PLEINE : on compte, on ne bloque pas. Attendre ici ferait
            // patiner le thread MIDI, donc décalerait les notes SUIVANTES --
            // on perdrait deux notes au lieu d'une, sans le dire.
            if (!recordQueue_.push(capture))
                droppedRecorded_.fetch_add(1, std::memory_order_relaxed);
        }

        // ÉCOUTE. Les pistes ARMÉES d'abord : armer une piste, c'est dire que
        // c'est elle qui écoute le clavier. Sans piste armée, la piste
        // sélectionnée, comme avant.
        auto armees = armedTracks_.load(std::memory_order_acquire);
        if (armees && !armees->empty()) {
            for (size_t track : *armees)
                graph_.sendLiveNote(vsm::audio::engine::ProcessGraph::LiveNoteSource::MidiInput,
                                     track, note, velocity, noteOn);
        } else {
            graph_.sendLiveNote(vsm::audio::engine::ProcessGraph::LiveNoteSource::MidiInput,
                                 liveInputTrack_.load(std::memory_order_acquire),
                                 note, velocity, noteOn);
        }
        return;
    }

    // D24.1 / D24.2 : LES CONTRÔLEURS. Ils étaient jetés ici (« si ce n'est
    // pas un CC, retour »), et les CC ne servaient qu'au MIDI Learn : un
    // clavier branché ne faisait entendre que ses notes, et une prise ne
    // gardait ni la molette ni la pédale. Chaque contrôleur va désormais à la
    // machine des pistes qui écoutent (les mêmes que les notes) ET dans la file
    // de capture, datée comme une note. Un CC lié par MIDI Learn reste au
    // paramètre lié : c'est le geste que l'utilisateur a demandé.
    auto versLesMachines = [this](const vsm::audio::plugin::MidiControlEvent& evenement) {
        auto armees = armedTracks_.load(std::memory_order_acquire);
        if (armees && !armees->empty()) {
            for (size_t track : *armees)
                graph_.sendLiveControl(vsm::audio::engine::ProcessGraph::LiveNoteSource::MidiInput, track, evenement);
        } else {
            graph_.sendLiveControl(vsm::audio::engine::ProcessGraph::LiveNoteSource::MidiInput,
                                   liveInputTrack_.load(std::memory_order_acquire), evenement);
        }
    };
    auto dansLaPrise = [this, &message](vsm::sequencer::RecordedControlEvent::Kind genre, uint8_t index, int16_t valeur) {
        const double horodatage = message.getTimeStamp() > 0.0
                                      ? message.getTimeStamp()
                                      : juce::Time::getMillisecondCounterHiRes() * 0.001;
        vsm::sequencer::RecordedControlEvent capture;
        uint64_t passe = 0;
        capture.seconds = transportSecondsAtClock(horodatage, &passe);
        capture.pass = static_cast<uint32_t>(passe);
        capture.kind = genre;
        capture.channel = static_cast<uint8_t>(juce::jlimit(1, 16, message.getChannel()) - 1);
        capture.index = index;
        capture.value = valeur;
        if (!recordControlQueue_.push(capture))
            droppedRecorded_.fetch_add(1, std::memory_order_relaxed);
    };
    const auto canal = static_cast<uint8_t>(juce::jlimit(1, 16, message.getChannel()) - 1);
    if (message.isPitchWheel()) {
        vsm::audio::plugin::MidiControlEvent evenement;
        evenement.kind = vsm::audio::plugin::MidiControlEvent::Kind::PitchBend;
        evenement.channel = canal;
        // Même conversion que le planning : 14 bits signés vers ± 2 demi-tons.
        evenement.value = static_cast<float>(message.getPitchWheelValue() - 8192) / 8192.0f * 2.0f;
        versLesMachines(evenement);
        dansLaPrise(vsm::sequencer::RecordedControlEvent::Kind::PitchBend, 0,
                    static_cast<int16_t>(message.getPitchWheelValue() - 8192));
        return;
    }
    if (message.isChannelPressure()) {
        vsm::audio::plugin::MidiControlEvent evenement;
        evenement.kind = vsm::audio::plugin::MidiControlEvent::Kind::ChannelPressure;
        evenement.channel = canal;
        evenement.value = static_cast<float>(message.getChannelPressureValue()) / 127.0f;
        versLesMachines(evenement);
        dansLaPrise(vsm::sequencer::RecordedControlEvent::Kind::ChannelPressure, 0,
                    static_cast<int16_t>(message.getChannelPressureValue()));
        return;
    }
    if (message.isAftertouch()) {
        vsm::audio::plugin::MidiControlEvent evenement;
        evenement.kind = vsm::audio::plugin::MidiControlEvent::Kind::PolyPressure;
        evenement.channel = canal;
        evenement.index = static_cast<uint8_t>(message.getNoteNumber());
        evenement.value = static_cast<float>(message.getAfterTouchValue()) / 127.0f;
        versLesMachines(evenement);
        dansLaPrise(vsm::sequencer::RecordedControlEvent::Kind::PolyPressure,
                    static_cast<uint8_t>(message.getNoteNumber()),
                    static_cast<int16_t>(message.getAfterTouchValue()));
        return;
    }
    if (!message.isController()) return;
    const auto cc = static_cast<uint8_t>(message.getControllerNumber());
    const auto value = static_cast<uint8_t>(message.getControllerValue());

    if (learnArmed_.load(std::memory_order_acquire)) {
        // Mode apprentissage : lie ce CC à la cible en attente.
        std::lock_guard<std::mutex> lock(learnMutex_);
        if (pendingLearnTarget_.valid)
            learnMap_.bind(cc, pendingLearnTarget_);
        learnArmed_.store(false, std::memory_order_release);
        return;
    }

    // Mode normal : applique le mapping s'il existe.
    vsm::audio::engine::MidiLearnTarget target;
    float paramValue = 0.0f;
    bool resolved = false;
    {
        std::lock_guard<std::mutex> lock(learnMutex_);
        resolved = learnMap_.resolve(cc, value, target, paramValue);
    }
    if (!resolved) {
        // CC LIBRE : à la machine, et dans la prise (D24.1, D24.2).
        vsm::audio::plugin::MidiControlEvent evenement;
        evenement.kind = vsm::audio::plugin::MidiControlEvent::Kind::ControlChange;
        evenement.channel = canal;
        evenement.index = cc;
        evenement.value = static_cast<float>(value) / 127.0f;
        versLesMachines(evenement);
        dansLaPrise(vsm::sequencer::RecordedControlEvent::Kind::ControlChange, cc, static_cast<int16_t>(value));
        return;
    }

    // DEUX CHEMINS, ET LA FRONTIÈRE EST CELLE DES THREADS, pas celle du
    // confort. Un paramètre de machine se règle par un `std::atomic` : on peut
    // l'écrire d'ici. Tout le reste -- volume, panoramique, muet, départs,
    // transport -- vit dans le PROJET, que seul le thread de l'interface a le
    // droit de modifier. On dépose donc, et l'interface applique.
    if (target.kind == vsm::audio::engine::MidiLearnKind::InstrumentParam) {
        graph_.setInstrumentParameter(target.trackIndex, target.paramId, paramValue);
        return;
    }
    LearnedControl commande;
    commande.target = target;
    commande.value = paramValue;
    commande.rawValue = value;
    // File pleine : on abandonne plutôt que d'attendre. Perdre un pas de
    // potentiomètre est sans conséquence -- le suivant arrive dans dix
    // millisecondes --, bloquer le thread MIDI ne l'est pas.
    learnQueue_.push(commande);
}

size_t AudioEngine::drainLearnedControls(std::vector<LearnedControl>& out) {
    size_t combien = 0;
    LearnedControl commande;
    while (learnQueue_.pop(commande)) { out.push_back(commande); ++combien; }
    return combien;
}

void AudioEngine::clearMidiLearnController(uint8_t controller) {
    std::lock_guard<std::mutex> lock(learnMutex_);
    learnMap_.clearController(controller);
}

vsm::audio::engine::MidiLearnMap AudioEngine::midiLearnMap() const {
    std::lock_guard<std::mutex> lock(learnMutex_);
    return learnMap_;
}

void AudioEngine::setMidiLearnMap(vsm::audio::engine::MidiLearnMap map) {
    std::lock_guard<std::mutex> lock(learnMutex_);
    learnMap_ = std::move(map);
}

void AudioEngine::setArmedTracks(std::vector<size_t> tracks) {
    armedTracks_.store(std::make_shared<const std::vector<size_t>>(std::move(tracks)),
                        std::memory_order_release);
}

size_t AudioEngine::drainRecordedControls(std::vector<vsm::sequencer::RecordedControlEvent>& out) {
    size_t ajoutes = 0;
    vsm::sequencer::RecordedControlEvent capture;
    while (recordControlQueue_.pop(capture)) {
        out.push_back(capture);
        ++ajoutes;
    }
    return ajoutes;
}

size_t AudioEngine::drainRecordedEvents(std::vector<vsm::sequencer::RecordedNoteEvent>& out) {
    size_t ajoutes = 0;
    vsm::sequencer::RecordedNoteEvent capture;
    while (recordQueue_.pop(capture)) {
        out.push_back(capture);
        ++ajoutes;
    }
    return ajoutes;
}

bool AudioEngine::startAudioRecording(const juce::File& fichier, double punchSeconds,
                                       juce::String& erreur) {
    const int canaux = juce::jlimit(0, 2, currentInputChannels_.load(std::memory_order_acquire));
    if (canaux <= 0) {
        erreur = "Aucune entree audio ouverte : la carte n'en donne pas. "
                 "Voir Fichier > Reglages audio.";
        return false;
    }
    const double frequence = currentSampleRate_.load(std::memory_order_acquire);
    audioPunchSeconds_.store(punchSeconds, std::memory_order_release);
    if (!diskRecorder_.start(fichier, frequence, canaux, erreur)) return false;
    recordingAudio_.store(true, std::memory_order_release);
    return true;
}

int64_t AudioEngine::stopAudioRecording() {
    // L'ORDRE COMPTE : on coupe d'abord le robinet côté thread audio, on ferme
    // le fichier ensuite. L'inverse laisserait un bloc en vol écrire dans un
    // rédacteur en train d'être détruit.
    recordingAudio_.store(false, std::memory_order_release);
    return diskRecorder_.stop();
}

bool AudioEngine::startLatencyMeasurement() {
    if (currentInputChannels_.load(std::memory_order_acquire) <= 0) return false;
    if (probeState_.load(std::memory_order_acquire) != ProbeState::Idle) return false;

    const double frequence = currentSampleRate_.load(std::memory_order_acquire);
    if (frequence <= 0.0) return false;

    // TOUT EST FABRIQUÉ ET ALLOUÉ ICI, sur le thread de l'interface. Le rappel
    // audio ne fera que lire un tableau et en remplir un autre.
    probeSignal_ = vsm::audio::engine::LatencyProbe::makeProbe(frequence);
    probeCapture_.assign(
        static_cast<size_t>(frequence * vsm::audio::engine::LatencyProbe::kListenSeconds), 0.0f);
    probeEmitted_.store(0, std::memory_order_release);
    probeCaptured_.store(0, std::memory_order_release);
    probeState_.store(ProbeState::Emitting, std::memory_order_release);
    return true;
}

vsm::audio::engine::LatencyProbe::Resultat AudioEngine::finishLatencyMeasurement() {
    vsm::audio::engine::LatencyProbe::Resultat resultat;
    if (probeState_.load(std::memory_order_acquire) != ProbeState::Done) return resultat;
    resultat = vsm::audio::engine::LatencyProbe::detecter(probeCapture_, probeSignal_);
    probeState_.store(ProbeState::Idle, std::memory_order_release);
    return resultat;
}

void AudioEngine::publishTransportAnchor(double positionTransport) {
    // Un seul rédacteur (le thread audio), d'où le compteur impair pendant
    // l'écriture : le lecteur voit « en cours » et recommence.
    const uint32_t version = anchorVersion_.load(std::memory_order_relaxed);
    anchorVersion_.store(version + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    // Time::getMillisecondCounterHiRes() est un simple appel d'horloge
    // monotone : ni allocation, ni verrou, ni entrée-sortie. C'est la MÊME
    // horloge que celle dont le pilote MIDI date ses messages, ce qui est toute
    // la raison de son emploi ici plutôt qu'une autre.
    anchorClockSeconds_.store(juce::Time::getMillisecondCounterHiRes() * 0.001,
                               std::memory_order_relaxed);
    anchorTransportSeconds_.store(positionTransport, std::memory_order_relaxed);
    anchorLoopWraps_.store(graph_.loopWrapCount(), std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    anchorVersion_.store(version + 2, std::memory_order_relaxed);
}

double AudioEngine::transportSecondsAtClock(double clockSeconds, uint64_t* passe) const {
    // Lecture d'une PAIRE cohérente : si le compteur a bougé pendant la
    // lecture, l'ancre a changé sous nos pieds et il faut recommencer. Quelques
    // essais suffisent -- le rédacteur n'écrit qu'une fois par bloc audio.
    for (int essai = 0; essai < 8; ++essai) {
        const uint32_t avant = anchorVersion_.load(std::memory_order_relaxed);
        if ((avant & 1u) != 0u) continue;   // écriture en cours
        std::atomic_thread_fence(std::memory_order_acquire);
        const double horloge = anchorClockSeconds_.load(std::memory_order_relaxed);
        const double transport = anchorTransportSeconds_.load(std::memory_order_relaxed);
        const uint64_t passes = anchorLoopWraps_.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (anchorVersion_.load(std::memory_order_relaxed) != avant) continue;
        if (horloge <= 0.0) break;          // aucun bloc audio n'a encore tourné
        if (passe) *passe = passes;
        return transport + (clockSeconds - horloge)
               - declaredLatency_.load(std::memory_order_relaxed);
    }
    // Pas d'ancre utilisable : la position du transport, sans interpolation.
    // C'est moins précis, jamais faux.
    if (passe) *passe = graph_.loopWrapCount();
    return graph_.currentSeconds();
}

void AudioEngine::armMidiLearn(const vsm::audio::engine::MidiLearnTarget& target) {
    std::lock_guard<std::mutex> lock(learnMutex_);
    pendingLearnTarget_ = target;
    learnArmed_.store(true, std::memory_order_release);
}

void AudioEngine::cancelMidiLearn() {
    learnArmed_.store(false, std::memory_order_release);
}

void AudioEngine::clearMidiLearn() {
    std::lock_guard<std::mutex> lock(learnMutex_);
    learnMap_.clearAll();
}

size_t AudioEngine::midiLearnMappingCount() const {
    std::lock_guard<std::mutex> lock(learnMutex_);
    return learnMap_.size();
}

float AudioEngine::currentCpuUsagePercent() const {
    return static_cast<float>(deviceManager_.getCpuUsage() * 100.0);
}

void AudioEngine::audioDeviceAboutToStart(juce::AudioIODevice* device) {
    double sampleRate = device->getCurrentSampleRate();
    int bufferSize = device->getCurrentBufferSizeSamples();

    currentSampleRate_.store(sampleRate, std::memory_order_release);
    currentBlockSize_.store(std::max(bufferSize, 1), std::memory_order_release);
    currentInputChannels_.store(device->getActiveInputChannels().countNumberOfSetBits(),
                                 std::memory_order_release);
    monoFallbackBuffer_.assign(static_cast<size_t>(std::max(bufferSize, 1)), 0.0f);
    // Ce que le pilote DIT de sa latence de sortie (voir declaredLatencySeconds()
    // pour la raison de la retrancher, et la limite de l'exercice).
    declaredLatency_.store(sampleRate > 0.0
                                ? static_cast<double>(device->getOutputLatencyInSamples()) / sampleRate
                                : 0.0,
                            std::memory_order_release);
    graph_.prepare(sampleRate, bufferSize);
}

void AudioEngine::audioDeviceStopped() {
    // Rien d'obligatoire : ProcessGraph reste dans un état valide, il n'est
    // simplement plus alimenté tant que le device n'est pas relancé.
}

void AudioEngine::audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                                     float* const* outputChannelData, int numOutputChannels,
                                                     int numSamples, const juce::AudioIODeviceCallbackContext&) {
    if (numOutputChannels <= 0 || numSamples <= 0) return;

    // L'ANCRE D'ABORD, avant que le bloc n'avance le transport : elle doit dire
    // « à cette heure-là, le transport en était LÀ », et non « il en sera là ».
    // La position du début du bloc est lue UNE fois ici et resservie plus bas :
    // la relire après le rendu donnerait celle de la fin.
    const double positionDebutBloc = graph_.currentSeconds();
    publishTransportAnchor(positionDebutBloc);

    // NIVEAU D'ENTRÉE. Rien d'autre n'est fait de l'entrée pour l'instant -- la
    // capture vers un fichier est D3.4 -- mais la mesurer est ce qui permet de
    // brancher un micro et de VOIR qu'il arrive, avant d'espérer l'enregistrer.
    // Une crête, un `std::atomic`, aucune allocation.
    if (inputChannelData != nullptr && numInputChannels > 0) {
        float crete = 0.0f;
        for (int c = 0; c < numInputChannels; ++c) {
            const float* canal = inputChannelData[c];
            if (canal == nullptr) continue;
            for (int i = 0; i < numSamples; ++i) crete = std::max(crete, std::abs(canal[i]));
        }
        float precedente = inputPeak_.load(std::memory_order_relaxed);
        while (crete > precedente
               && !inputPeak_.compare_exchange_weak(precedente, crete, std::memory_order_acq_rel)) {}
    }

    // ÉCRITURE DE LA PRISE AUDIO SUR LE DISQUE (D3.4). Rien de plus qu'un dépôt
    // dans une file : le fichier est écrit par un autre thread (voir
    // DiskRecorder), et ce rappel n'attend jamais le disque.
    if (recordingAudio_.load(std::memory_order_acquire)
        && inputChannelData != nullptr && numInputChannels > 0) {
        const int canaux = diskRecorder_.channels();
        const double frequence = currentSampleRate_.load(std::memory_order_relaxed);

        // LE POINT D'ENTRÉE TOMBE OÙ IL TOMBE, y compris au milieu d'un bloc.
        // On n'écrit donc que la QUEUE du bloc à partir de lui : commencer au
        // début du bloc qui le contient donnerait à chaque prise un décalage
        // aléatoire allant jusqu'à une taille de bloc, soit 10,7 ms -- le
        // défaut même qu'on a évité côté MIDI avec l'ancre.
        // LA COMPENSATION DE LATENCE (D3.6), et voici son raisonnement complet.
        // L'échantillon d'entrée qui arrive au bloc dont le transport est à P a
        // été JOUÉ en réaction à ce qu'on entendait, c'est-à-dire à ce que le
        // moteur avait émis un aller-retour plus tôt. Son intention musicale se
        // situe donc à P - R, où R est la latence d'aller-retour mesurée.
        //
        // Pour que le fichier COMMENCE à l'intention du point d'entrée, il faut
        // donc commencer à écrire quand P vaut punch + R. Sans cette correction,
        // toute prise audio serait en retard de R -- une dizaine de
        // millisecondes sur une carte ordinaire, davantage sur une carte USB.
        //
        // R vaut zéro tant qu'on n'a rien mesuré : on ne corrige pas d'un
        // chiffre qu'on aurait deviné.
        const double allerRetour = measuredRoundTrip_.load(std::memory_order_relaxed);

        int decalage = 0;
        const double punch = audioPunchSeconds_.load(std::memory_order_relaxed) + allerRetour;
        if (positionDebutBloc < punch && frequence > 0.0) {
            const double avant = (punch - positionDebutBloc) * frequence;
            decalage = avant >= static_cast<double>(numSamples)
                           ? numSamples
                           : static_cast<int>(std::llround(avant));
        }

        // LE POINT DE SORTIE, à l'échantillon lui aussi : le fichier s'arrête
        // exactement là où la région de punch s'arrête, sans quoi la prise
        // déborderait sur ce qu'on avait décidé de garder.
        int fin = numSamples;
        const double sortie = punchOutSeconds_.load(std::memory_order_relaxed) + allerRetour;
        if (std::isfinite(sortie) && frequence > 0.0) {
            const double jusquA = (sortie - positionDebutBloc) * frequence;
            if (jusquA <= 0.0) fin = 0;
            else if (jusquA < static_cast<double>(numSamples))
                fin = static_cast<int>(std::llround(jusquA));
        }

        // `ThreadedWriter::write` n'accepte AUCUN canal nul, et exige exactement
        // le nombre de canaux du fichier. Si la carte a changé de configuration
        // sous nos pieds, on préfère compter un trou que d'écrire n'importe
        // quoi -- un fichier faux est plus difficile à diagnostiquer qu'un
        // fichier court.
        if (decalage < fin && canaux > 0 && numInputChannels >= canaux) {
            const float* canauxEcrits[2] = { nullptr, nullptr };
            bool complet = true;
            for (int c = 0; c < canaux && c < 2; ++c) {
                if (inputChannelData[c] == nullptr) { complet = false; break; }
                canauxEcrits[c] = inputChannelData[c] + decalage;
            }
            if (complet)
                diskRecorder_.write(canauxEcrits, fin - decalage);
        }
    }

    // MESURE DE LATENCE (D3.6). Deux gestes, et rien d'autre : poser le balayage
    // dans la sortie, recopier l'entrée. La capture COMMENCE avec l'émission,
    // pour que le décalage trouvé soit celui de l'aller-retour complet et non
    // celui d'une origine arbitraire.
    const ProbeState mesure = probeState_.load(std::memory_order_acquire);
    if (mesure == ProbeState::Emitting || mesure == ProbeState::Capturing) {
        if (inputChannelData != nullptr && numInputChannels > 0 && inputChannelData[0] != nullptr) {
            int ecrits = probeCaptured_.load(std::memory_order_relaxed);
            const int place = static_cast<int>(probeCapture_.size()) - ecrits;
            const int n = std::min(numSamples, std::max(0, place));
            for (int i = 0; i < n; ++i)
                probeCapture_[static_cast<size_t>(ecrits + i)] = inputChannelData[0][i];
            ecrits += n;
            probeCaptured_.store(ecrits, std::memory_order_release);
            if (ecrits >= static_cast<int>(probeCapture_.size()))
                probeState_.store(ProbeState::Done, std::memory_order_release);
        } else {
            // Pas d'entrée en cours de mesure : on n'attend pas indéfiniment.
            probeState_.store(ProbeState::Done, std::memory_order_release);
        }
    }

    float* left = outputChannelData[0];
    if (numOutputChannels >= 2 && outputChannelData[1] != nullptr) {
        graph_.processBlock(left, outputChannelData[1], numSamples);
    } else {
        // Device mono (rare) : rend vers le buffer de repli pré-alloué,
        // jamais alloué ici.
        float* fallback = monoFallbackBuffer_.data();
        int n = std::min(numSamples, static_cast<int>(monoFallbackBuffer_.size()));
        graph_.processBlock(left, fallback, n);
    }

    // Le balayage est ajouté APRÈS le rendu du graphe : la mesure doit pouvoir
    // se faire pendant que le morceau joue, et surtout ne rien devoir au
    // contenu du morceau.
    if (mesure == ProbeState::Emitting) {
        int emis = probeEmitted_.load(std::memory_order_relaxed);
        const int reste = static_cast<int>(probeSignal_.size()) - emis;
        const int n = std::min(numSamples, std::max(0, reste));
        for (int i = 0; i < n; ++i) {
            const float e = probeSignal_[static_cast<size_t>(emis + i)];
            for (int c = 0; c < numOutputChannels; ++c)
                if (outputChannelData[c] != nullptr) outputChannelData[c][i] += e;
        }
        emis += n;
        probeEmitted_.store(emis, std::memory_order_release);
        if (emis >= static_cast<int>(probeSignal_.size()))
            probeState_.store(ProbeState::Capturing, std::memory_order_release);
    }

    // Canaux au-delà de la stéréo (ex: interfaces surround) : silence.
    // D11.7 — L'ÉCOUTE D'ENTRÉE : l'entrée s'ajoute à la sortie, canal pour
    // canal (une entrée mono va aux deux côtés). Sans allocation, sans
    // traitement, et seulement si on l'a demandé : entendre son micro dans
    // ses enceintes sans l'avoir voulu, c'est un larsen.
    if (inputMonitoring_.load(std::memory_order_acquire) && inputChannelData != nullptr
        && numInputChannels > 0 && inputChannelData[0] != nullptr && numOutputChannels >= 1
        && outputChannelData[0] != nullptr) {
        const float* gauche = inputChannelData[0];
        const float* droite = (numInputChannels >= 2 && inputChannelData[1] != nullptr) ? inputChannelData[1] : gauche;
        float* sortieG = outputChannelData[0];
        float* sortieD = (numOutputChannels >= 2 && outputChannelData[1] != nullptr) ? outputChannelData[1] : nullptr;
        for (int i = 0; i < numSamples; ++i) {
            sortieG[i] += gauche[i];
            if (sortieD != nullptr) sortieD[i] += droite[i];
        }
    }

    for (int ch = 2; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            std::fill(outputChannelData[ch], outputChannelData[ch] + numSamples, 0.0f);
}

// --- D27.3 : la sortie MIDI matérielle ----------------------------------------

std::vector<std::string> AudioEngine::availableMidiOutputs() const {
    std::vector<std::string> noms;
    if (portVirtuel_) noms.push_back("VSM Studio");
    for (const auto& d : juce::MidiOutput::getAvailableDevices()) {
        const std::string nom = d.name.toStdString();
        if (std::find(noms.begin(), noms.end(), nom) == noms.end()) noms.push_back(nom);
    }
    return noms;
}

void AudioEngine::setTrackMidiOutputs(std::vector<std::string> portsParPiste) {
    std::lock_guard<std::mutex> verrou(portsMutex_);
    // OUVRIR CE QUI MANQUE, par nom -- un identifiant JUCE change d'une
    // session à l'autre, un nom se lit dans project.json.
    for (const auto& nom : portsParPiste) {
        if (nom.empty() || nom == "VSM Studio" || ports_.count(nom) > 0) continue;
        for (const auto& d : juce::MidiOutput::getAvailableDevices())
            if (d.name.toStdString() == nom) {
                if (auto port = juce::MidiOutput::openDevice(d.identifier)) ports_[nom] = std::move(port);
                break;
            }
    }
    // FERMER CE QUE PLUS PERSONNE N'EMPLOIE.
    for (auto it = ports_.begin(); it != ports_.end();) {
        if (std::find(portsParPiste.begin(), portsParPiste.end(), it->first) == portsParPiste.end())
            it = ports_.erase(it);
        else
            ++it;
    }
    portParPiste_.store(std::make_shared<const std::vector<std::string>>(std::move(portsParPiste)),
                        std::memory_order_release);
}

void AudioEngine::Emetteur::run() {
    using Evenement = vsm::audio::engine::ProcessGraph::MidiOutEvent;
    while (!threadShouldExit()) {
        Evenement e;
        while (moteur_.graph_.popMidiOut(e)) {
            // BORNÉ : une file qui gonflerait parce qu'un port ne répond pas
            // finirait par manger la mémoire ; au-delà, le plus ancien part.
            if (enAttente_.size() >= 8192) enAttente_.erase(enAttente_.begin());
            enAttente_.push_back(e);
        }
        const double maintenant =
            std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (!enAttente_.empty()) {
            std::stable_sort(enAttente_.begin(), enAttente_.end(),
                             [](const Evenement& a, const Evenement& b) { return a.hostSeconds < b.hostSeconds; });
            auto ports = moteur_.portParPiste_.load(std::memory_order_acquire);
            std::lock_guard<std::mutex> verrou(moteur_.portsMutex_);
            size_t envoyes = 0;
            for (const auto& ev : enAttente_) {
                if (ev.hostSeconds > maintenant + 0.0005) break;   // pas encore l'heure ; la liste est triée
                ++envoyes;
                juce::MidiOutput* port = nullptr;
                if (ports && ev.trackIndex < ports->size()) {
                    const std::string& nom = (*ports)[ev.trackIndex];
                    if (nom == "VSM Studio") port = moteur_.portVirtuel_.get();
                    else if (auto it = moteur_.ports_.find(nom); it != moteur_.ports_.end()) port = it->second.get();
                }
                if (port == nullptr) { moteur_.midiOutSansPort_.fetch_add(1, std::memory_order_relaxed); continue; }
                const int taille = ((ev.status & 0xF0) == 0xC0 || (ev.status & 0xF0) == 0xD0) ? 2 : 3;
                const juce::MidiMessage message = taille == 2
                    ? juce::MidiMessage(ev.status, ev.data1)
                    : juce::MidiMessage(ev.status, ev.data1, ev.data2);
                port->sendMessageNow(message);
                moteur_.midiOutSent_.fetch_add(1, std::memory_order_relaxed);
                // VSM_TRACE_MIDIOUT=1 : les quarante premiers événements sur stderr, datés
                // par rapport à l'heure d'envoi -- l'outil qui a trouvé le défaut de D27.
                static int traces = std::getenv("VSM_TRACE_MIDIOUT") ? 40 : 0;
                if (traces > 0) {
                    --traces;
                    std::fprintf(stderr, "midi-out piste %u %02X %u %u heure %.6f retard %.4f s carte %d bloc %d sr %.0f\n",
                                 ev.trackIndex, ev.status, ev.data1, ev.data2, ev.hostSeconds,
                                 maintenant - ev.hostSeconds, moteur_.isDeviceOpen() ? 1 : 0,
                                 moteur_.currentBlockSize(), moteur_.currentSampleRate());
                }
            }
            enAttente_.erase(enAttente_.begin(), enAttente_.begin() + static_cast<long>(envoyes));
        }
        wait(1);
    }
}
