#include "vsm/sequencer/TimeSignatureMap.h"
#include <algorithm>

namespace vsm::sequencer {

namespace {
const TimeSignatureChange& changeAt(const std::vector<TimeSignatureChange>& changes, Tick tick) {
    const TimeSignatureChange* result = &changes.front();
    for (const auto& c : changes) {
        if (c.tick > tick) break;
        result = &c;
    }
    return *result;
}
} // namespace

TimeSignatureMap::TimeSignatureMap() {
    changes_.push_back({0, 4, 2}); // 4/4
}

void TimeSignatureMap::addChange(Tick tick, uint8_t numerator, uint8_t denominatorPow2) {
    auto it = std::find_if(changes_.begin(), changes_.end(),
                            [tick](const TimeSignatureChange& c) { return c.tick == tick; });
    if (it != changes_.end()) {
        it->numerator = numerator;
        it->denominatorPow2 = denominatorPow2;
        return;
    }
    changes_.push_back({tick, numerator, denominatorPow2});
    std::sort(changes_.begin(), changes_.end(),
              [](const TimeSignatureChange& a, const TimeSignatureChange& b) { return a.tick < b.tick; });
}

bool TimeSignatureMap::removeChangeAt(Tick tick) {
    if (tick == 0) return false;
    auto it = std::find_if(changes_.begin(), changes_.end(),
                            [tick](const TimeSignatureChange& c) { return c.tick == tick; });
    if (it == changes_.end()) return false;
    changes_.erase(it);
    return true;
}

void TimeSignatureMap::clear() {
    changes_.clear();
    changes_.push_back({0, 4, 2});
}

uint32_t TimeSignatureMap::denominatorAt(Tick tick) const {
    return 1u << changeAt(changes_, tick).denominatorPow2;
}

uint8_t TimeSignatureMap::numeratorAt(Tick tick) const {
    return changeAt(changes_, tick).numerator;
}

Tick TimeSignatureMap::ticksPerBeat(Tick tick, uint16_t ppq) const {
    // Un "temps" correspond à une noire redimensionnée selon le dénominateur :
    // ticksPerBeat = ppq * 4 / denominator (ex : denom=4 -> 1 temps = 1 noire = ppq ticks)
    uint32_t denom = denominatorAt(tick);
    if (denom == 0) return ppq;
    return static_cast<Tick>((static_cast<int64_t>(ppq) * 4) / static_cast<int64_t>(denom));
}

Tick TimeSignatureMap::ticksPerBar(Tick tick, uint16_t ppq) const {
    return ticksPerBeat(tick, ppq) * numeratorAt(tick);
}

BarBeat TimeSignatureMap::barBeatAt(Tick tick, uint16_t ppq) const {
    int64_t bar = 0;
    Tick segmentStartTick = 0;

    for (size_t i = 0; i < changes_.size(); ++i) {
        Tick nextChangeTick = (i + 1 < changes_.size()) ? changes_[i + 1].tick : (tick + 1);
        Tick barTicks = ticksPerBar(changes_[i].tick, ppq);
        if (barTicks <= 0) barTicks = static_cast<Tick>(ppq) * 4;

        if (nextChangeTick > tick) {
            Tick beatTicks = ticksPerBeat(changes_[i].tick, ppq);
            Tick offsetInSegment = tick - segmentStartTick;
            int64_t barsInSegment = offsetInSegment / barTicks;
            Tick tickInBar = offsetInSegment % barTicks;
            int64_t beatInBar = beatTicks > 0 ? tickInBar / beatTicks : 0;
            Tick tickInBeat = beatTicks > 0 ? tickInBar % beatTicks : tickInBar;
            return BarBeat{bar + barsInSegment, beatInBar, tickInBeat};
        }

        Tick segmentTicks = nextChangeTick - segmentStartTick;
        bar += segmentTicks / barTicks; // suppose que le changement tombe sur une frontière de mesure
        segmentStartTick = nextChangeTick;
    }
    return BarBeat{bar, 0, 0};
}

} // namespace vsm::sequencer
