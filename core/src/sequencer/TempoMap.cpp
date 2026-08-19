#include "vsm/sequencer/TempoMap.h"
#include <algorithm>
#include <cmath>

namespace vsm::sequencer {

namespace {
constexpr uint32_t kDefaultUsPerQuarterNote = 500000; // 120 BPM

double secondsForTickSpan(Tick tickSpan, uint32_t usPerQuarterNote, uint16_t ppq) {
    if (ppq == 0) return 0.0;
    return (static_cast<double>(tickSpan) * static_cast<double>(usPerQuarterNote)) /
           (static_cast<double>(ppq) * 1'000'000.0);
}
} // namespace

TempoMap::TempoMap() {
    changes_.push_back({0, kDefaultUsPerQuarterNote});
}

TempoMap::TempoMap(bool smpte, double fps, uint16_t tpf)
    : isSmpte_(smpte), smpteFramesPerSecond_(fps), smpteTicksPerFrame_(tpf) {
    changes_.push_back({0, kDefaultUsPerQuarterNote});
}

TempoMap TempoMap::smpte(double framesPerSecond, uint16_t ticksPerFrame) {
    return TempoMap(true, framesPerSecond, ticksPerFrame);
}

void TempoMap::addTempoChange(Tick tick, uint32_t microsecondsPerQuarterNote) {
    if (isSmpte_) return;

    auto it = std::find_if(changes_.begin(), changes_.end(),
                            [tick](const TempoChange& c) { return c.tick == tick; });
    if (it != changes_.end()) {
        it->microsecondsPerQuarterNote = microsecondsPerQuarterNote;
        return;
    }
    changes_.push_back({tick, microsecondsPerQuarterNote});
    std::sort(changes_.begin(), changes_.end(),
              [](const TempoChange& a, const TempoChange& b) { return a.tick < b.tick; });
}

void TempoMap::clearTempoChanges() {
    changes_.clear();
    changes_.push_back({0, kDefaultUsPerQuarterNote});
}

double TempoMap::ticksToSeconds(Tick tick, uint16_t ppq) const {
    if (isSmpte_) {
        if (smpteFramesPerSecond_ <= 0.0 || smpteTicksPerFrame_ == 0) return 0.0;
        return static_cast<double>(tick) / (smpteFramesPerSecond_ * static_cast<double>(smpteTicksPerFrame_));
    }

    double accumSeconds = 0.0;
    Tick lastTick = 0;
    uint32_t lastUsPerQn = changes_.front().microsecondsPerQuarterNote;

    for (size_t i = 1; i < changes_.size() && changes_[i].tick <= tick; ++i) {
        accumSeconds += secondsForTickSpan(changes_[i].tick - lastTick, lastUsPerQn, ppq);
        lastTick = changes_[i].tick;
        lastUsPerQn = changes_[i].microsecondsPerQuarterNote;
    }
    accumSeconds += secondsForTickSpan(tick - lastTick, lastUsPerQn, ppq);
    return accumSeconds;
}

Tick TempoMap::secondsToTicks(double seconds, uint16_t ppq) const {
    if (isSmpte_) {
        if (smpteFramesPerSecond_ <= 0.0 || smpteTicksPerFrame_ == 0) return 0;
        return static_cast<Tick>(std::llround(seconds * smpteFramesPerSecond_ * smpteTicksPerFrame_));
    }

    double remainingSeconds = seconds;
    Tick lastTick = 0;
    uint32_t lastUsPerQn = changes_.front().microsecondsPerQuarterNote;

    for (size_t i = 1; i < changes_.size(); ++i) {
        Tick segmentTicks = changes_[i].tick - lastTick;
        double segmentSeconds = secondsForTickSpan(segmentTicks, lastUsPerQn, ppq);
        if (segmentSeconds > remainingSeconds) break;
        remainingSeconds -= segmentSeconds;
        lastTick = changes_[i].tick;
        lastUsPerQn = changes_[i].microsecondsPerQuarterNote;
    }

    if (ppq == 0 || lastUsPerQn == 0) return lastTick;
    double extraTicks = (remainingSeconds * ppq * 1'000'000.0) / static_cast<double>(lastUsPerQn);
    return lastTick + static_cast<Tick>(std::llround(extraTicks));
}

double TempoMap::bpmAt(Tick tick) const {
    if (isSmpte_) return 0.0;
    uint32_t usPerQn = changes_.front().microsecondsPerQuarterNote;
    for (const auto& c : changes_) {
        if (c.tick > tick) break;
        usPerQn = c.microsecondsPerQuarterNote;
    }
    if (usPerQn == 0) return 0.0;
    return 60'000'000.0 / static_cast<double>(usPerQn);
}

} // namespace vsm::sequencer
