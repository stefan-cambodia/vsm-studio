#pragma once
#include <JuceHeader.h>
#include "vsm/audio/engine/MidiLearnMap.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "DiskRecorder.h"
#include "vsm/audio/engine/LatencyProbe.h"
#include "vsm/sequencer/MidiRecorder.h"
#include <atomic>
#include <functional>
#include <limits>
#include <mutex>
#include <vector>

// Wrapper JUCE autour de vsm::audio::engine::ProcessGraph (voir audio/,
// entièrement testé sans JUCE). AudioEngine ne fait QUE le pont vers le
// device audio réel -- toute la logique de rendu (scheduling
// sample-accurate, mixage, synthèse) reste dans ProcessGraph, réutilisable
// tel quel par un futur wrapper VST3/AU qui n'aura PAS d'AudioDeviceManager
// à lui (c'est l'hôte qui pilote l'audio dans ce cas).
//
// RÈGLE : audioDeviceIOCallbackWithContext() tourne dans le thread audio
// temps réel du système -- aucune allocation, aucun lock ici (ProcessGraph
// respecte déjà cette contrainte ; ce wrapper ne doit pas la casser).
class AudioEngine : public juce::AudioIODeviceCallback,
                    public juce::MidiInputCallback {
public:
    AudioEngine();
    ~AudioEngine() override;

    /// Ouvre le device audio par défaut (0 entrée, 2 sorties) et démarre le
    /// callback. Ne lève jamais d'exception : en cas d'échec (pas de
    /// device disponible), journalise l'erreur et laisse l'app utilisable
    /// sans son plutôt que de planter -- voir lastError().
    /// `etatSauvegarde` est l'état du sélecteur de périphérique conservé d'une
    /// exécution à l'autre (peut être nul).
    void start(const juce::XmlElement* etatSauvegarde = nullptr);
    void stop();

    vsm::audio::engine::ProcessGraph& processGraph() { return graph_; }
    const vsm::audio::engine::ProcessGraph& processGraph() const { return graph_; }

    juce::AudioDeviceManager& deviceManager() { return deviceManager_; }

    double currentSampleRate() const { return currentSampleRate_.load(std::memory_order_acquire); }
    /// Taille de bloc réelle du périphérique. Les effets se préparent dessus :
    /// un effet préparé pour 512 échantillons et nourri par blocs de 1024
    /// déborderait ses lignes à retard.
    int currentBlockSize() const { return currentBlockSize_.load(std::memory_order_acquire); }

    /// Niveau de crête de l'entrée depuis la dernière lecture, et remise à
    /// zéro. Zéro veut dire « rien n'arrive » -- ce qui, sur une entrée
    /// ouverte, est une information et non un défaut.
    float readInputPeak() { return inputPeak_.exchange(0.0f, std::memory_order_acq_rel); }
    /// Nombre de canaux d'entrée réellement ouverts. Zéro = la carte n'en a
    /// pas donné, et l'enregistrement est impossible : il faut le DIRE, pas
    /// laisser chercher.
    int currentInputChannels() const { return currentInputChannels_.load(std::memory_order_acquire); }
    float currentCpuUsagePercent() const;
    juce::String lastError() const { return lastError_; }
    bool isDeviceOpen() const { return deviceManager_.getCurrentAudioDevice() != nullptr; }

    // --- juce::AudioIODeviceCallback ---------------------------------------
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples, const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    // --- juce::MidiInputCallback (thread MIDI, PAS le thread audio) --------
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;

    /// Piste qui reçoit les notes d'un clavier MIDI branché (la piste
    /// sélectionnée dans l'application). Lue depuis le thread MIDI, écrite
    /// depuis l'UI -> atomique.
    ///
    /// ELLE NE SERT QUE S'IL N'Y A AUCUNE PISTE ARMÉE : armer une piste, c'est
    /// dire « c'est celle-là qui écoute mon clavier », et il serait absurde de
    /// jouer sur une piste et d'enregistrer sur une autre.
    void setLiveInputTrack(size_t trackIndex) { liveInputTrack_.store(trackIndex, std::memory_order_release); }
    /// D11.7 — S'ENTENDRE : l'entrée audio recopiée vers la sortie, en direct,
    /// à la latence du périphérique (aller-retour mesurable par « Mesurer la
    /// latence »). Rien n'est traité : c'est le son qui entre, tel quel.
    void setInputMonitoring(bool on) { inputMonitoring_.store(on, std::memory_order_release); }
    bool inputMonitoring() const { return inputMonitoring_.load(std::memory_order_acquire); }
    /// D11.7 — LE CLAVIER D'ORDINATEUR joue comme un clavier MIDI : même
    /// chemin que `handleIncomingMidiMessage` (piste choisie ou armées,
    /// capture si l'enregistrement tourne).
    /// LA SAISIE PAS À PAS (D13.5). Armée, chaque note reçue -- d'un clavier
    /// MIDI comme du clavier d'ordinateur, qui passe par le même chemin -- est
    /// AUSSI postée au fil d'interface, où le piano roll l'écrit. Postée, pas
    /// appelée : on est sur le thread MIDI ici, et le projet ne s'y touche pas.
    void setStepInputArmed(bool armed) { stepInputArmed_.store(armed, std::memory_order_relaxed); }
    bool stepInputArmed() const { return stepInputArmed_.load(std::memory_order_relaxed); }
    std::function<void(uint8_t note, uint8_t velocity)> onStepInputNote;

    void playComputerKey(uint8_t note, uint8_t velocity, bool on) {
        handleIncomingMidiMessage(nullptr, on ? juce::MidiMessage::noteOn(1, note, velocity)
                                              : juce::MidiMessage::noteOff(1, note));
    }

    // --- Enregistrement MIDI temps réel (D3.3) ----------------------------
    //
    // CE QUI SE PASSE ICI, ET POURQUOI CE N'EST PAS QU'UNE FILE DE PLUS. Les
    // messages d'un clavier arrivent sur le THREAD MIDI, daté par le pilote
    // sur l'horloge du système ; le morceau, lui, est daté par l'horloge du
    // TRANSPORT, que seul le thread audio fait avancer. Enregistrer, c'est
    // traduire l'une dans l'autre.
    //
    // La traduction naïve -- lire `currentSeconds()` au moment où le message
    // arrive -- donnerait à toutes les notes d'un même bloc la même date, soit
    // une quantification involontaire à la taille de bloc : 10,7 ms à 512
    // échantillons, largement audible sur une double croche. Le thread audio
    // publie donc, à chaque bloc, une ANCRE (heure système, position du
    // transport) et le thread MIDI interpole entre deux ancres. La précision
    // devient celle de l'horodatage du pilote, pas celle du découpage en blocs.

    /// Arme la capture. Ce qui est joué avant le point d'entrée est écarté par
    /// `MidiRecorder`, pas ici : le moteur date, il ne juge pas.
    void setRecording(bool on) { recording_.store(on, std::memory_order_release); }
    bool isRecording() const { return recording_.load(std::memory_order_acquire); }

    /// Les pistes armées, publiées d'un coup depuis le thread UI. Elles
    /// reçoivent les notes du clavier -- à l'écoute comme à l'enregistrement.
    void setArmedTracks(std::vector<size_t> tracks);

    /// Vide la file de capture dans `out` (thread UI). Renvoie le nombre
    /// d'événements ajoutés.
    size_t drainRecordedEvents(std::vector<vsm::sequencer::RecordedNoteEvent>& out);

    /// Notes perdues faute de place dans la file. Doit rester à zéro ; toute
    /// autre valeur est une note qu'on a jouée et qui n'est pas dans la prise.
    uint64_t droppedRecordedEvents() const { return droppedRecorded_.load(std::memory_order_relaxed); }

    /// Le décalage retranché à chaque note enregistrée, en secondes.
    ///
    /// C'EST UNE LATENCE DÉCLARÉE, PAS MESURÉE, et la nuance est écrite ici
    /// pour qu'on ne l'oublie pas : c'est le chiffre que le pilote annonce pour
    /// sa sortie. On le retranche parce qu'on joue en réaction à ce qu'on
    /// ENTEND, et que ce qu'on entend a déjà pris ce retard -- sans correction,
    /// toute prise serait systématiquement en retard d'une dizaine de
    /// millisecondes. La mesure réelle, par boucle physique, est l'objet de
    /// D3.6 ; elle remplacera cette déclaration sans rien changer d'autre.
    double declaredLatencySeconds() const { return declaredLatency_.load(std::memory_order_acquire); }

    /// La position du transport correspondant à une heure système, par
    /// interpolation depuis la dernière ancre publiée par le thread audio.
    /// Publique pour être testable sans carte son.
    /// `passe`, s'il est fourni, reçoit le nombre de rebouclages au moment de
    /// l'ancre : c'est ce qui range chaque note dans la bonne passe de boucle.
    double transportSecondsAtClock(double clockSeconds, uint64_t* passe = nullptr) const;

    /// Les bornes de la capture MIDI. Au-delà du point de sortie, on entend ce
    /// qui était déjà là et on n'écrit plus rien -- c'est le « punch out ».
    /// L'infini (le défaut) veut dire « jusqu'à ce qu'on arrête ».
    void setRecordPunchOut(double seconds) { punchOutSeconds_.store(seconds, std::memory_order_release); }

    // --- Enregistrement AUDIO en flux sur disque (D3.4) --------------------
    //
    // Le pendant audio de ce qui précède, et il pose le problème inverse : le
    // MIDI est un filet de données qu'on rattrape à l'arrêt, l'audio est un
    // flot continu qu'il faut écrire PENDANT. Voir `DiskRecorder` pour la
    // séparation thread audio / thread d'écriture.

    /// Thread UI. Ouvre le fichier et arme la capture. `punchSeconds` est le
    /// point d'entrée : rien n'est écrit avant, et le premier échantillon du
    /// fichier est EXACTEMENT celui de ce point -- pas celui du début du bloc
    /// qui le contient, sans quoi chaque prise commencerait avec un décalage
    /// aléatoire pouvant aller jusqu'à une taille de bloc.
    bool startAudioRecording(const juce::File& fichier, double punchSeconds, juce::String& erreur);
    /// Thread UI. Ferme le fichier ; rend le nombre de trames écrites.
    int64_t stopAudioRecording();
    bool isRecordingAudio() const { return recordingAudio_.load(std::memory_order_acquire); }
    const DiskRecorder& diskRecorder() const { return diskRecorder_; }

    // --- Mesure de la latence par boucle physique (D3.6) -------------------
    //
    // Les pilotes ANNONCENT une latence ; elle est souvent fausse, presque
    // toujours sous-estimée, et jamais vérifiable de l'intérieur. Le seul moyen
    // de la connaître est d'émettre un signal et de l'entendre revenir --
    // câble de la sortie vers l'entrée, ou micro devant un haut-parleur.
    //
    // Le rappel audio ne fait ici que DEUX choses : poser le balayage dans la
    // sortie, et recopier l'entrée dans un tampon PRÉ-ALLOUÉ. La corrélation,
    // elle, tourne sur le thread de l'interface une fois la capture finie.

    /// Lance une mesure. Rend faux si la carte n'a pas d'entrée -- il n'y a
    /// alors rien à mesurer, et le dire vaut mieux que de faire semblant.
    bool startLatencyMeasurement();
    /// Vrai tant que la mesure est en cours.
    bool latencyMeasurementRunning() const { return probeState_.load(std::memory_order_acquire) != ProbeState::Idle; }
    /// Analyse la capture (thread UI) et, si elle est nette, adopte le
    /// résultat. Rend le résultat brut, nettete comprise, pour que l'interface
    /// puisse REFUSER un chiffre peu convaincant plutôt que de l'appliquer.
    vsm::audio::engine::LatencyProbe::Resultat finishLatencyMeasurement();

    /// La latence d'aller-retour retenue, en secondes. Zéro = jamais mesurée.
    double measuredRoundTripSeconds() const { return measuredRoundTrip_.load(std::memory_order_acquire); }
    /// Impose une valeur (celle conservée d'une exécution à l'autre).
    void setMeasuredRoundTripSeconds(double secondes) {
        measuredRoundTrip_.store(secondes, std::memory_order_release);
    }

    // --- MIDI Learn --------------------------------------------------------
    // Arme l'apprentissage : le PROCHAIN CC reçu sera lié à `target`.
    void armMidiLearn(const vsm::audio::engine::MidiLearnTarget& target);
    void cancelMidiLearn();
    bool isMidiLearnArmed() const { return learnArmed_.load(std::memory_order_acquire); }
    void clearMidiLearn(); // efface toutes les associations
    /// Défait UNE association. `clearAll` n'y répond pas : perdre les quinze
    /// autres pour en corriger une seule n'est pas une correction (D10.2).
    void clearMidiLearnController(uint8_t controller);
    size_t midiLearnMappingCount() const;
    /// Copie de la carte, pour l'afficher. Une COPIE et non une référence :
    /// le thread MIDI peut la modifier à tout instant.
    vsm::audio::engine::MidiLearnMap midiLearnMap() const;
    /// Remplace toute la carte (relecture des préférences au démarrage).
    void setMidiLearnMap(vsm::audio::engine::MidiLearnMap map);

    /// CE QUI NE PEUT PAS ÊTRE APPLIQUÉ DEPUIS LE THREAD MIDI (D10.2).
    ///
    /// Un paramètre de machine se règle par un `std::atomic` : le thread MIDI
    /// l'écrit directement, comme il l'a toujours fait. Le volume, le
    /// panoramique, le muet, les départs et le transport vivent dans le
    /// PROJET, que seul le thread de l'interface a le droit de modifier --
    /// c'est pour cela que le MIDI learn ne les atteignait pas, et la réponse
    /// n'est pas de forcer la frontière mais de la traverser proprement : une
    /// file sans verrou, vidée par la minuterie de l'interface.
    struct LearnedControl {
        vsm::audio::engine::MidiLearnTarget target;
        float value = 0.0f;      ///< déjà mise à l'échelle de la cible
        uint8_t rawValue = 0;    ///< la valeur brute du CC, pour les bascules
    };
    /// Thread UI. Vide la file dans `out` ; renvoie le nombre d'éléments.
    size_t drainLearnedControls(std::vector<LearnedControl>& out);

private:
    vsm::audio::engine::ProcessGraph graph_;
    juce::AudioDeviceManager deviceManager_;
    std::atomic<double> currentSampleRate_{48000.0};
    std::atomic<int> currentBlockSize_{512};
    std::atomic<int> currentInputChannels_{0};
    std::atomic<float> inputPeak_{0.0f};
    juce::String lastError_;

    // MIDI Learn : accédé par le thread MIDI (handleIncomingMidiMessage) ET
    // le thread UI (arm/cancel/clear) -> protégé par un mutex. Ce mutex n'est
    // JAMAIS pris sur le thread audio (le chemin audio n'y touche pas), donc
    // il ne viole pas la contrainte temps réel.
    mutable std::mutex learnMutex_;
    vsm::audio::engine::MidiLearnMap learnMap_;
    vsm::audio::engine::MidiLearnTarget pendingLearnTarget_;
    std::atomic<bool> learnArmed_{false};
    /// Les commandes apprises qui doivent être appliquées par l'interface.
    /// Un seul producteur (le thread MIDI), un seul consommateur (l'UI) :
    /// c'est exactement le contrat de `LockFreeRingBuffer`.
    static constexpr size_t kLearnQueueCapacity = 256;
    vsm::audio::util::LockFreeRingBuffer<LearnedControl, kLearnQueueCapacity> learnQueue_;
    std::atomic<size_t> liveInputTrack_{0};
    std::atomic<bool> inputMonitoring_{false};
    std::vector<juce::String> enabledMidiInputs_;
    std::atomic<bool> stepInputArmed_{false};

    // --- Capture MIDI ------------------------------------------------------
    std::atomic<bool> recording_{false};
    std::atomic<std::shared_ptr<const std::vector<size_t>>> armedTracks_{nullptr};
    static constexpr size_t kRecordQueueCapacity = 1024;
    vsm::audio::util::LockFreeRingBuffer<vsm::sequencer::RecordedNoteEvent, kRecordQueueCapacity> recordQueue_;
    std::atomic<uint64_t> droppedRecorded_{0};
    std::atomic<double> declaredLatency_{0.0};

    // L'ANCRE, publiée par le thread audio et lue par le thread MIDI. Deux
    // valeurs qui doivent être VUES ENSEMBLE : une heure système et la position
    // du transport à cette heure-là. Les lire séparément donnerait, une fois
    // sur des millions, une paire dépareillée -- et donc une note posée
    // n'importe où dans le morceau.
    //
    // D'où le compteur de version (un « seqlock ») : le rédacteur l'incrémente
    // avant et après son écriture, le lecteur relit et recommence si le
    // compteur a bougé. Un mutex ferait le même travail mais est INTERDIT ici :
    // le rédacteur est le thread audio temps réel, qui n'a le droit ni
    // d'attendre ni de faire attendre.
    std::atomic<uint32_t> anchorVersion_{0};
    std::atomic<double> anchorClockSeconds_{0.0};
    std::atomic<double> anchorTransportSeconds_{0.0};
    /// Le nombre de rebouclages au moment de l'ancre. Il fait partie de l'ancre
    /// et non d'une lecture séparée : la position et la passe doivent être vues
    /// ENSEMBLE, sinon une note se retrouverait à la bonne date dans la
    /// mauvaise prise.
    std::atomic<uint64_t> anchorLoopWraps_{0};
    /// Thread audio uniquement. `positionTransport` est la position du DÉBUT du
    /// bloc, lue une seule fois par le rappel et partagée avec l'écriture sur
    /// disque -- la relire donnerait deux réponses différentes.
    void publishTransportAnchor(double positionTransport);

    // La mesure traverse le rappel audio, d'où un petit automate atomique :
    // l'interface arme, le rappel émet puis capture, l'interface analyse.
    enum class ProbeState { Idle, Emitting, Capturing, Done };
    std::atomic<ProbeState> probeState_{ProbeState::Idle};
    std::vector<float> probeSignal_;    ///< pré-calculé, jamais fabriqué dans le rappel
    std::vector<float> probeCapture_;   ///< pré-alloué, jamais redimensionné dans le rappel
    std::atomic<int> probeEmitted_{0};
    std::atomic<int> probeCaptured_{0};
    std::atomic<double> measuredRoundTrip_{0.0};

    DiskRecorder diskRecorder_;
    std::atomic<bool> recordingAudio_{false};
    std::atomic<double> audioPunchSeconds_{0.0};
    std::atomic<double> punchOutSeconds_{std::numeric_limits<double>::infinity()};

    // Repli pour un device de sortie mono (rare, mais ne doit jamais
    // crasher) : jamais alloué dans le callback, seulement ici à la
    // préparation du device (audioDeviceAboutToStart).
    std::vector<float> monoFallbackBuffer_;
};
