#pragma once
#include "vsm/sequencer/PlaybackScheduler.h"
#include "vsm/sequencer/Project.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace vsm::sequencer {

enum class TransportState { Stopped, Playing, Paused };

/// Reçoit les événements MIDI émis pendant la lecture. En Phase 1, une
/// implémentation typique journalise les événements ou les envoie à une
/// sortie MIDI système. En Phase 2, le Synth Rack implémentera cette
/// interface pour router chaque événement vers l'instrument de la piste.
class IMidiEventSink {
public:
    virtual ~IMidiEventSink() = default;
    virtual void onMidiEvent(size_t trackIndex, const midi::MidiEventData& data) = 0;
};

/// Transport de lecture temps réel.
///
/// Contrainte du cahier des charges : "Évite les problèmes de timing causés
/// par le thread UI." -> toute la logique de scheduling tourne sur un
/// thread dédié (jamais le thread d'interface), avec une horloge basée sur
/// std::chrono::steady_clock et une correction de dérive : chaque événement
/// est déclenché par rapport à une origine temporelle fixe
/// (playbackStartWallClock_ + positionSeconds), jamais par accumulation de
/// sleep() successifs, ce qui éliminerait toute dérive progressive.
///
/// En Phase 2, ce thread sera remplacé/complété par le callback du moteur
/// audio (sample-accurate, cf. AudioEngine) pour la lecture avec synthèse ;
/// RealtimeTransport reste utile tel quel pour le monitoring MIDI-out pur.
class RealtimeTransport {
public:
    explicit RealtimeTransport(IMidiEventSink& sink);
    ~RealtimeTransport();

    RealtimeTransport(const RealtimeTransport&) = delete;
    RealtimeTransport& operator=(const RealtimeTransport&) = delete;

    void loadProject(const Project& project);

    void play();
    void pause();
    void stop();

    void seekToTick(midi::Tick tick);
    void setLoopRegion(midi::Tick startTick, midi::Tick endTick, bool enabled);

    TransportState state() const { return state_.load(std::memory_order_acquire); }
    midi::Tick currentTick() const;
    double currentSeconds() const { return positionSeconds_.load(std::memory_order_acquire); }

private:
    void threadLoop();

    IMidiEventSink& sink_;

    mutable std::mutex mutex_;               // protège project_/schedule_/loop settings (pas le chemin chaud)
    Project project_;
    std::vector<ScheduledEvent> schedule_;

    std::thread thread_;
    std::condition_variable cv_;
    std::atomic<TransportState> state_{TransportState::Stopped};
    std::atomic<bool> shouldStop_{false};
    std::atomic<bool> seekRequested_{false}; // interrompt la passe en cours (seek/loop mis à jour en direct)

    std::atomic<double> positionSeconds_{0.0};
    midi::Tick loopStartTick_ = 0;
    midi::Tick loopEndTick_ = 0;
    bool loopEnabled_ = false;
};

} // namespace vsm::sequencer
