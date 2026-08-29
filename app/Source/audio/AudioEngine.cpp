#include "AudioEngine.h"
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

    // Entrées MIDI : active tous les périphériques disponibles et s'abonne
    // à leurs messages (pour le MIDI Learn et le futur MIDI-thru).
    enabledMidiInputs_.clear();
    for (const auto& device : juce::MidiInput::getAvailableDevices()) {
        deviceManager_.setMidiInputDeviceEnabled(device.identifier, true);
        deviceManager_.addMidiInputDeviceCallback(device.identifier, this);
        enabledMidiInputs_.push_back(device.identifier);
    }
}

void AudioEngine::stop() {
    for (const auto& id : enabledMidiInputs_)
        deviceManager_.removeMidiInputDeviceCallback(id, this);
    enabledMidiInputs_.clear();
    deviceManager_.removeAudioCallback(this);
    deviceManager_.closeAudioDevice();
}

void AudioEngine::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message) {
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

        // ENREGISTREMENT. La date vient de l'horodatage du PILOTE, pas de
        // l'instant où ce code s'exécute : entre les deux il y a le
        // réveil du thread MIDI, qui n'a aucune raison d'être régulier.
        // Certains pilotes ne datent rien (horodatage nul) ; on retombe alors
        // sur l'heure courante, qui est ce qu'on peut savoir de moins faux.
        if (recording_.load(std::memory_order_acquire)) {
            const double horodatage = message.getTimeStamp() > 0.0
                                          ? message.getTimeStamp()
                                          : juce::Time::getMillisecondCounterHiRes() * 0.001;
            vsm::sequencer::RecordedNoteEvent capture;
            capture.seconds = transportSecondsAtClock(horodatage);
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
    // setInstrumentParameter est lui-même thread-safe (hors verrou).
    if (resolved)
        graph_.setInstrumentParameter(target.trackIndex, target.paramId, paramValue);
}

void AudioEngine::setArmedTracks(std::vector<size_t> tracks) {
    armedTracks_.store(std::make_shared<const std::vector<size_t>>(std::move(tracks)),
                        std::memory_order_release);
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
    std::atomic_thread_fence(std::memory_order_release);
    anchorVersion_.store(version + 2, std::memory_order_relaxed);
}

double AudioEngine::transportSecondsAtClock(double clockSeconds) const {
    // Lecture d'une PAIRE cohérente : si le compteur a bougé pendant la
    // lecture, l'ancre a changé sous nos pieds et il faut recommencer. Quelques
    // essais suffisent -- le rédacteur n'écrit qu'une fois par bloc audio.
    for (int essai = 0; essai < 8; ++essai) {
        const uint32_t avant = anchorVersion_.load(std::memory_order_relaxed);
        if ((avant & 1u) != 0u) continue;   // écriture en cours
        std::atomic_thread_fence(std::memory_order_acquire);
        const double horloge = anchorClockSeconds_.load(std::memory_order_relaxed);
        const double transport = anchorTransportSeconds_.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (anchorVersion_.load(std::memory_order_relaxed) != avant) continue;
        if (horloge <= 0.0) break;          // aucun bloc audio n'a encore tourné
        return transport + (clockSeconds - horloge)
               - declaredLatency_.load(std::memory_order_relaxed);
    }
    // Pas d'ancre utilisable : la position du transport, sans interpolation.
    // C'est moins précis, jamais faux.
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
        int decalage = 0;
        const double punch = audioPunchSeconds_.load(std::memory_order_relaxed);
        if (positionDebutBloc < punch && frequence > 0.0) {
            const double avant = (punch - positionDebutBloc) * frequence;
            decalage = avant >= static_cast<double>(numSamples)
                           ? numSamples
                           : static_cast<int>(std::llround(avant));
        }

        // `ThreadedWriter::write` n'accepte AUCUN canal nul, et exige exactement
        // le nombre de canaux du fichier. Si la carte a changé de configuration
        // sous nos pieds, on préfère compter un trou que d'écrire n'importe
        // quoi -- un fichier faux est plus difficile à diagnostiquer qu'un
        // fichier court.
        if (decalage < numSamples && canaux > 0 && numInputChannels >= canaux) {
            const float* canauxEcrits[2] = { nullptr, nullptr };
            bool complet = true;
            for (int c = 0; c < canaux && c < 2; ++c) {
                if (inputChannelData[c] == nullptr) { complet = false; break; }
                canauxEcrits[c] = inputChannelData[c] + decalage;
            }
            if (complet)
                diskRecorder_.write(canauxEcrits, numSamples - decalage);
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

    // Canaux au-delà de la stéréo (ex: interfaces surround) : silence.
    for (int ch = 2; ch < numOutputChannels; ++ch)
        if (outputChannelData[ch] != nullptr)
            std::fill(outputChannelData[ch], outputChannelData[ch] + numSamples, 0.0f);
}
