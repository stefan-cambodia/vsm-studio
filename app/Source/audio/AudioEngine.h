#pragma once
#include <JuceHeader.h>
#include "vsm/audio/engine/MidiLearnMap.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include <atomic>
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
    void start();
    void stop();

    vsm::audio::engine::ProcessGraph& processGraph() { return graph_; }
    const vsm::audio::engine::ProcessGraph& processGraph() const { return graph_; }

    juce::AudioDeviceManager& deviceManager() { return deviceManager_; }

    double currentSampleRate() const { return currentSampleRate_.load(std::memory_order_acquire); }
    /// Taille de bloc réelle du périphérique. Les effets se préparent dessus :
    /// un effet préparé pour 512 échantillons et nourri par blocs de 1024
    /// déborderait ses lignes à retard.
    int currentBlockSize() const { return currentBlockSize_.load(std::memory_order_acquire); }
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
    void setLiveInputTrack(size_t trackIndex) { liveInputTrack_.store(trackIndex, std::memory_order_release); }

    // --- MIDI Learn --------------------------------------------------------
    // Arme l'apprentissage : le PROCHAIN CC reçu sera lié à `target`.
    void armMidiLearn(const vsm::audio::engine::MidiLearnTarget& target);
    void cancelMidiLearn();
    bool isMidiLearnArmed() const { return learnArmed_.load(std::memory_order_acquire); }
    void clearMidiLearn(); // efface toutes les associations
    size_t midiLearnMappingCount() const;

private:
    vsm::audio::engine::ProcessGraph graph_;
    juce::AudioDeviceManager deviceManager_;
    std::atomic<double> currentSampleRate_{48000.0};
    std::atomic<int> currentBlockSize_{512};
    juce::String lastError_;

    // MIDI Learn : accédé par le thread MIDI (handleIncomingMidiMessage) ET
    // le thread UI (arm/cancel/clear) -> protégé par un mutex. Ce mutex n'est
    // JAMAIS pris sur le thread audio (le chemin audio n'y touche pas), donc
    // il ne viole pas la contrainte temps réel.
    mutable std::mutex learnMutex_;
    vsm::audio::engine::MidiLearnMap learnMap_;
    vsm::audio::engine::MidiLearnTarget pendingLearnTarget_;
    std::atomic<bool> learnArmed_{false};
    std::atomic<size_t> liveInputTrack_{0};
    std::vector<juce::String> enabledMidiInputs_;

    // Repli pour un device de sortie mono (rare, mais ne doit jamais
    // crasher) : jamais alloué dans le callback, seulement ici à la
    // préparation du device (audioDeviceAboutToStart).
    std::vector<float> monoFallbackBuffer_;
};
