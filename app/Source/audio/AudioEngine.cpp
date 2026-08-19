#include "AudioEngine.h"
#include <algorithm>

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
    stop();
}

void AudioEngine::start() {
    juce::String error = deviceManager_.initialise(0, 2, nullptr, true);
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
    // Notes : jouées immédiatement sur l'instrument de la piste sélectionnée.
    // Elles passent par la file "live" du ProcessGraph (lock-free), jamais par
    // le planning du projet -- on joue, on n'enregistre pas.
    //
    // C'est bien le THREAD MIDI ici, distinct du thread UI : d'où la source
    // MidiInput, qui a sa propre file (LockFreeRingBuffer est strictement un
    // producteur / un consommateur, voir ProcessGraph::LiveNoteSource).
    if (message.isNoteOnOrOff()) {
        const size_t track = liveInputTrack_.load(std::memory_order_acquire);
        const auto note = static_cast<uint8_t>(message.getNoteNumber());
        const auto velocity = static_cast<uint8_t>(message.getVelocity());
        // Un NoteOn de vélocité 0 est un NoteOff déguisé (convention MIDI).
        const bool noteOn = message.isNoteOn() && velocity > 0;
        graph_.sendLiveNote(vsm::audio::engine::ProcessGraph::LiveNoteSource::MidiInput,
                             track, note, velocity, noteOn);
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
    monoFallbackBuffer_.assign(static_cast<size_t>(std::max(bufferSize, 1)), 0.0f);
    graph_.prepare(sampleRate, bufferSize);
}

void AudioEngine::audioDeviceStopped() {
    // Rien d'obligatoire : ProcessGraph reste dans un état valide, il n'est
    // simplement plus alimenté tant que le device n'est pas relancé.
}

void AudioEngine::audioDeviceIOCallbackWithContext(const float* const* /*inputChannelData*/, int /*numInputChannels*/,
                                                     float* const* outputChannelData, int numOutputChannels,
                                                     int numSamples, const juce::AudioIODeviceCallbackContext&) {
    if (numOutputChannels <= 0 || numSamples <= 0) return;

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
