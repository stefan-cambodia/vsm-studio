#include "vsm/audio/engine/Transport.h"
#include <algorithm>
#include <chrono>

namespace vsm::audio::engine {

using vsm::midi::Tick;

Transport::Transport(ProcessGraph& graph) : graph_(graph) {}

Transport::~Transport() { stopFallbackClock(); }

void Transport::setProject(const vsm::sequencer::Project& project) {
    std::lock_guard<std::mutex> verrou(mutex_);
    project_ = project;
    // LA FIN DU MORCEAU EST CELLE DE TOUT CE QUI SONNE, clips audio compris.
    // Ne compter que les notes faisait qu'un projet uniquement audio s'arrêtait
    // avant d'avoir commencé -- le planning était vide, donc « fini ».
    const Tick fin = project_.lastSoundingTick() + project_.ticksPerQuarterNote;
    endSeconds_.store(project_.ticksToSeconds(fin), std::memory_order_release);
}

Tick Transport::currentTick() const {
    std::lock_guard<std::mutex> verrou(mutex_);
    // LA POSITION PEUT ÊTRE NÉGATIVE pendant un décompte (voir
    // `ProcessGraph::seekSeconds`) ; un tick négatif ne veut rien dire pour
    // le reste de l'application, qui compte à partir de zéro.
    return project_.secondsToTicks(std::max(0.0, graph_.currentSeconds()));
}

double Transport::endOfSongSeconds() const { return endSeconds_.load(std::memory_order_acquire); }

void Transport::play() {
    if (state_.load(std::memory_order_acquire) == TransportState::Playing) return;
    state_.store(TransportState::Playing, std::memory_order_release);
    graph_.setPlaying(true);
}

void Transport::pause() {
    state_.store(TransportState::Paused, std::memory_order_release);
    graph_.setPlaying(false);
}

void Transport::stop() {
    state_.store(TransportState::Stopped, std::memory_order_release);
    graph_.setPlaying(false);
    graph_.seekSeconds(0.0);
}

void Transport::seekToTick(Tick tick) {
    double secondes = 0.0;
    {
        std::lock_guard<std::mutex> verrou(mutex_);
        secondes = project_.ticksToSeconds(tick);
    }
    graph_.seekSeconds(secondes);
}

void Transport::seekSeconds(double seconds) { graph_.seekSeconds(seconds); }

void Transport::setLoopRegion(Tick startTick, Tick endTick, bool enabled) {
    double debut = 0.0, fin = 0.0;
    {
        std::lock_guard<std::mutex> verrou(mutex_);
        debut = project_.ticksToSeconds(startTick);
        fin = project_.ticksToSeconds(endTick);
    }
    graph_.setLoopRegion(debut, fin, enabled);
}

void Transport::poll() {
    if (state_.load(std::memory_order_acquire) != TransportState::Playing) return;
    // UNE BOUCLE NE FINIT JAMAIS, et c'est bien tout son intérêt.
    if (graph_.isLoopActive()) return;
    const double fin = endSeconds_.load(std::memory_order_acquire);
    if (fin <= 0.0) return;
    if (graph_.currentSeconds() >= fin) stop();
}

void Transport::setAudioDeviceOpen(bool open, double sampleRate, int blockSize) {
    if (open) stopFallbackClock();
    else startFallbackClock(sampleRate, blockSize);
}

void Transport::startFallbackClock(double sampleRate, int blockSize) {
    if (fallbackRunning_.load(std::memory_order_acquire)) return;
    fallbackShouldStop_.store(false, std::memory_order_release);
    fallbackRunning_.store(true, std::memory_order_release);
    fallbackThread_ = std::thread([this, sampleRate, blockSize] {
        fallbackLoop(sampleRate > 0.0 ? sampleRate : 48000.0, blockSize > 0 ? blockSize : 512);
    });
}

void Transport::stopFallbackClock() {
    fallbackShouldStop_.store(true, std::memory_order_release);
    if (fallbackThread_.joinable()) fallbackThread_.join();
    fallbackRunning_.store(false, std::memory_order_release);
}

void Transport::fallbackLoop(double sampleRate, int blockSize) {
    using horloge = std::chrono::steady_clock;
    std::vector<float> gauche(static_cast<size_t>(blockSize), 0.0f);
    std::vector<float> droite(static_cast<size_t>(blockSize), 0.0f);
    const auto duree = std::chrono::duration<double>(static_cast<double>(blockSize) / sampleRate);

    // L'ÉCHÉANCE SE CALCULE DEPUIS L'ORIGINE, jamais bloc par bloc : additionner
    // des attentes courtes accumule l'erreur de chaque réveil, et une heure de
    // défilement finirait sensiblement en retard. C'est la même règle que celle
    // du rendu hors ligne au pas du temps réel (D6.5).
    auto origine = horloge::now();
    int64_t blocs = 0;

    while (!fallbackShouldStop_.load(std::memory_order_acquire)) {
        if (state_.load(std::memory_order_acquire) != TransportState::Playing) {
            // À l'arrêt, on ne rend RIEN : l'horloge de secours n'existe que
            // pour faire avancer le temps, et le temps ne bouge pas.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            origine = horloge::now();
            blocs = 0;
            continue;
        }
        graph_.processBlock(gauche.data(), droite.data(), blockSize);
        ++blocs;
        const auto echeance = origine + std::chrono::duration_cast<horloge::duration>(duree * blocs);
        std::this_thread::sleep_until(echeance);
    }
}

} // namespace vsm::audio::engine
