#include "vsm/sequencer/RealtimeTransport.h"
#include <algorithm>

namespace vsm::sequencer {

RealtimeTransport::RealtimeTransport(IMidiEventSink& sink) : sink_(sink) {}

RealtimeTransport::~RealtimeTransport() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shouldStop_.store(true, std::memory_order_release);
    }
    cv_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

void RealtimeTransport::loadProject(const Project& project) {
    std::lock_guard<std::mutex> lock(mutex_);
    project_ = project;
    positionSeconds_.store(0.0, std::memory_order_release);
}

void RealtimeTransport::play() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_.load() == TransportState::Playing) return;
        state_.store(TransportState::Playing, std::memory_order_release);
        if (!thread_.joinable()) {
            shouldStop_.store(false, std::memory_order_release);
            thread_ = std::thread(&RealtimeTransport::threadLoop, this);
        }
    }
    cv_.notify_all();
}

void RealtimeTransport::pause() {
    state_.store(TransportState::Paused, std::memory_order_release);
    cv_.notify_all();
}

void RealtimeTransport::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.store(TransportState::Stopped, std::memory_order_release);
        positionSeconds_.store(0.0, std::memory_order_release);
    }
    cv_.notify_all();
}

void RealtimeTransport::seekToTick(midi::Tick tick) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        positionSeconds_.store(project_.ticksToSeconds(tick), std::memory_order_release);
        seekRequested_.store(true, std::memory_order_release);
    }
    cv_.notify_all();
}

void RealtimeTransport::setLoopRegion(midi::Tick startTick, midi::Tick endTick, bool enabled) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        loopStartTick_ = startTick;
        loopEndTick_ = endTick;
        loopEnabled_ = enabled;
        seekRequested_.store(true, std::memory_order_release); // applique le changement immédiatement
    }
    cv_.notify_all();
}

midi::Tick RealtimeTransport::currentTick() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return project_.secondsToTicks(positionSeconds_.load(std::memory_order_acquire));
}

void RealtimeTransport::threadLoop() {
    using clock = std::chrono::steady_clock;

    while (true) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&] {
            return state_.load(std::memory_order_acquire) == TransportState::Playing ||
                   shouldStop_.load(std::memory_order_acquire);
        });
        if (shouldStop_.load(std::memory_order_acquire)) return;

        midi::Tick startTick = project_.secondsToTicks(positionSeconds_.load());
        midi::Tick endTick = loopEnabled_
            ? loopEndTick_
            : (project_.lastUsedTick() + project_.ticksPerQuarterNote); // marge finale
        if (loopEnabled_ && startTick < loopStartTick_) startTick = loopStartTick_;

        std::vector<ScheduledEvent> pass = PlaybackScheduler::build(project_, startTick, endTick);
        double originSeconds = positionSeconds_.load();
        lock.unlock();

        auto wallOrigin = clock::now();
        bool completedNaturally = true;

        for (const auto& ev : pass) {
            auto deadline = wallOrigin + std::chrono::duration_cast<clock::duration>(
                std::chrono::duration<double>(ev.timeSeconds - originSeconds));

            std::unique_lock<std::mutex> waitLock(mutex_);
            cv_.wait_until(waitLock, deadline, [&] {
                return shouldStop_.load(std::memory_order_acquire) ||
                       state_.load(std::memory_order_acquire) != TransportState::Playing ||
                       seekRequested_.load(std::memory_order_acquire);
            });
            waitLock.unlock();

            if (shouldStop_.load(std::memory_order_acquire)) return;
            if (state_.load(std::memory_order_acquire) != TransportState::Playing ||
                seekRequested_.load(std::memory_order_acquire)) {
                completedNaturally = false;
                break;
            }

            positionSeconds_.store(ev.timeSeconds, std::memory_order_release);
            sink_.onMidiEvent(ev.trackIndex, ev.data);
        }

        std::lock_guard<std::mutex> endLock(mutex_);
        if (shouldStop_.load(std::memory_order_acquire)) return;

        if (!completedNaturally) {
            // Interrompu par pause/seek/changement de boucle : on relance
            // simplement une passe fraîche depuis la position à jour.
            seekRequested_.store(false, std::memory_order_release);
            continue;
        }
        if (state_.load(std::memory_order_acquire) != TransportState::Playing)
            continue;

        if (loopEnabled_) {
            positionSeconds_.store(project_.ticksToSeconds(loopStartTick_), std::memory_order_release);
            continue;
        }

        // Fin de lecture naturelle (plus d'événements à jouer, pas de boucle).
        state_.store(TransportState::Stopped, std::memory_order_release);
        positionSeconds_.store(0.0, std::memory_order_release);
        return;
    }
}

} // namespace vsm::sequencer
