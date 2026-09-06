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

Tick TimeSignatureMap::tickAtBarBeat(int64_t bar, int64_t beat, uint16_t ppq) const {
    bar = std::max<int64_t>(0, bar);
    beat = std::max<int64_t>(0, beat);
    // Même parcours que `barBeatAt`, dans l'autre sens : on avance segment par
    // segment tant que la mesure demandée est au-delà du segment.
    int64_t barsBefore = 0;
    Tick segmentStartTick = 0;
    for (size_t i = 0; i < changes_.size(); ++i) {
        Tick barTicks = ticksPerBar(changes_[i].tick, ppq);
        if (barTicks <= 0) barTicks = static_cast<Tick>(ppq) * 4;
        const Tick beatTicks = std::max<Tick>(1, ticksPerBeat(changes_[i].tick, ppq));
        if (i + 1 < changes_.size()) {
            const Tick segmentTicks = changes_[i + 1].tick - segmentStartTick;
            const int64_t barsInSegment = segmentTicks / barTicks;
            if (bar - barsBefore >= barsInSegment) {
                barsBefore += barsInSegment;
                segmentStartTick = changes_[i + 1].tick;
                continue;
            }
        }
        return segmentStartTick + (bar - barsBefore) * barTicks + beat * beatTicks;
    }
    return segmentStartTick;
}

bool parseBarBeat(const std::string& text, int64_t& bar, int64_t& beat) {
    // Deux nombres au plus, séparés par un point, deux-points ou une espace ;
    // rien d'autre n'est toléré, pour qu'une faute de frappe soit refusée
    // plutôt que lue de travers.
    std::string mesure, temps;
    bool separateur = false;
    for (const char c : text) {
        if (c >= '0' && c <= '9') { (separateur ? temps : mesure) += c; continue; }
        if (c == ' ' && !separateur && mesure.empty()) continue;   // espaces de tête
        if ((c == '.' || c == ':' || c == ' ' || c == ',') && !separateur && !mesure.empty()) {
            separateur = true;
            continue;
        }
        if (c == ' ' && separateur && temps.empty()) continue;      // « 17 . 3 »
        if (c == ' ' && !temps.empty()) break;                       // espaces de queue
        return false;
    }
    if (mesure.empty() || mesure.size() > 9 || temps.size() > 9) return false;
    if (separateur && temps.empty()) return false;
    const int64_t m = std::stoll(mesure);
    const int64_t t = temps.empty() ? 1 : std::stoll(temps);
    if (m < 1 || t < 1) return false;
    bar = m - 1;
    beat = t - 1;
    return true;
}

} // namespace vsm::sequencer
