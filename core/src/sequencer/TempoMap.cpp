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

double bpmOf(uint32_t usPerQuarterNote) {
    return usPerQuarterNote > 0 ? 60'000'000.0 / static_cast<double>(usPerQuarterNote) : 120.0;
}

// LA RAMPE, EN FORME CLOSE (D15.5). Sur un tronçon de `fullSpan` ticks, le
// tempo va linéairement de b0 à b1 BPM : b(x) = b0 + k·x, k = (b1 - b0)/fullSpan.
// Une noire dure 60/b(x) secondes et vaut ppq ticks, donc
//   s(x) = ∫ 60 / (ppq · b(u)) du = 60 / (ppq · k) · ln(b(x) / b0),
// et l'inverse x(s) = b0 · (exp(s · ppq · k / 60) - 1) / k. Aucun pas fixe :
// le milieu d'une rampe se calcule aussi juste que son bout.
double secondsForRampSpan(Tick span, Tick fullSpan, uint32_t us0, uint32_t us1, uint16_t ppq) {
    if (ppq == 0 || fullSpan <= 0) return 0.0;
    const double b0 = bpmOf(us0), b1 = bpmOf(us1);
    const double k = (b1 - b0) / static_cast<double>(fullSpan);
    if (std::abs(k) < 1e-12) return secondsForTickSpan(span, us0, ppq);
    const double bx = b0 + k * static_cast<double>(span);
    return 60.0 / (static_cast<double>(ppq) * k) * std::log(bx / b0);
}

double ticksForRampSeconds(double seconds, Tick fullSpan, uint32_t us0, uint32_t us1, uint16_t ppq) {
    if (ppq == 0 || fullSpan <= 0) return 0.0;
    const double b0 = bpmOf(us0), b1 = bpmOf(us1);
    const double k = (b1 - b0) / static_cast<double>(fullSpan);
    if (std::abs(k) < 1e-12) return seconds * ppq * 1'000'000.0 / static_cast<double>(us0);
    return b0 * (std::exp(seconds * static_cast<double>(ppq) * k / 60.0) - 1.0) / k;
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

void TempoMap::setRampToNext(size_t index, bool rampToNext) {
    if (index < changes_.size()) changes_[index].rampToNext = rampToNext;
}

bool TempoMap::hasRamps() const {
    for (size_t i = 0; i + 1 < changes_.size(); ++i)
        if (changes_[i].rampToNext) return true;
    return false;
}

std::vector<TempoChange> TempoMap::flattened(uint16_t ppq, Tick stepTicks) const {
    std::vector<TempoChange> plate;
    if (stepTicks <= 0 || ppq == 0) stepTicks = std::max<Tick>(1, ppq);
    for (size_t i = 0; i < changes_.size(); ++i) {
        const auto& c = changes_[i];
        if (!c.rampToNext || i + 1 >= changes_.size()) {
            plate.push_back({c.tick, c.microsecondsPerQuarterNote, false});
            continue;
        }
        const auto& suivant = changes_[i + 1];
        const Tick fullSpan = suivant.tick - c.tick;
        for (Tick x = 0; x < fullSpan; x += stepTicks) {
            const Tick fin = std::min(fullSpan, x + stepTicks);
            // Le tempo qui fait durer ce palier exactement ce que la rampe lui donne.
            const double secondes = secondsForRampSpan(fin, fullSpan, c.microsecondsPerQuarterNote,
                                                       suivant.microsecondsPerQuarterNote, ppq)
                                  - secondsForRampSpan(x, fullSpan, c.microsecondsPerQuarterNote,
                                                       suivant.microsecondsPerQuarterNote, ppq);
            const double us = secondes * 1'000'000.0 * static_cast<double>(ppq) / static_cast<double>(fin - x);
            plate.push_back({c.tick + x, static_cast<uint32_t>(std::llround(us)), false});
        }
    }
    return plate;
}

void TempoMap::addTempoChange(Tick tick, uint32_t microsecondsPerQuarterNote, bool rampToNext) {
    if (isSmpte_) return;

    auto it = std::find_if(changes_.begin(), changes_.end(),
                            [tick](const TempoChange& c) { return c.tick == tick; });
    if (it != changes_.end()) {
        it->microsecondsPerQuarterNote = microsecondsPerQuarterNote;
        it->rampToNext = rampToNext;
        return;
    }
    changes_.push_back({tick, microsecondsPerQuarterNote, rampToNext});
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
    // Le tronçon i va de changes_[i] à changes_[i+1] (ou à l'infini) : en
    // palier, ou en rampe si le changement le dit (D15.5).
    for (size_t i = 0; i < changes_.size(); ++i) {
        const auto& c = changes_[i];
        if (c.tick > tick) break;
        const bool dernier = i + 1 >= changes_.size();
        const Tick finTroncon = dernier ? tick : std::min(tick, changes_[i + 1].tick);
        const Tick span = finTroncon - c.tick;
        if (span <= 0) continue;
        if (!dernier && c.rampToNext)
            accumSeconds += secondsForRampSpan(span, changes_[i + 1].tick - c.tick, c.microsecondsPerQuarterNote,
                                               changes_[i + 1].microsecondsPerQuarterNote, ppq);
        else
            accumSeconds += secondsForTickSpan(span, c.microsecondsPerQuarterNote, ppq);
    }
    return accumSeconds;
}

Tick TempoMap::secondsToTicks(double seconds, uint16_t ppq) const {
    if (isSmpte_) {
        if (smpteFramesPerSecond_ <= 0.0 || smpteTicksPerFrame_ == 0) return 0;
        return static_cast<Tick>(std::llround(seconds * smpteFramesPerSecond_ * smpteTicksPerFrame_));
    }

    double remainingSeconds = seconds;
    for (size_t i = 0; i < changes_.size(); ++i) {
        const auto& c = changes_[i];
        const bool dernier = i + 1 >= changes_.size();
        const bool rampe = !dernier && c.rampToNext;
        const Tick fullSpan = dernier ? 0 : changes_[i + 1].tick - c.tick;
        if (!dernier) {
            const double segmentSeconds =
                rampe ? secondsForRampSpan(fullSpan, fullSpan, c.microsecondsPerQuarterNote,
                                           changes_[i + 1].microsecondsPerQuarterNote, ppq)
                      : secondsForTickSpan(fullSpan, c.microsecondsPerQuarterNote, ppq);
            if (segmentSeconds <= remainingSeconds) { remainingSeconds -= segmentSeconds; continue; }
        }
        if (ppq == 0 || c.microsecondsPerQuarterNote == 0) return c.tick;
        const double extraTicks =
            rampe ? ticksForRampSeconds(remainingSeconds, fullSpan, c.microsecondsPerQuarterNote,
                                        changes_[i + 1].microsecondsPerQuarterNote, ppq)
                  : (remainingSeconds * ppq * 1'000'000.0) / static_cast<double>(c.microsecondsPerQuarterNote);
        return c.tick + static_cast<Tick>(std::llround(extraTicks));
    }
    return changes_.back().tick;
}

double TempoMap::bpmAt(Tick tick) const {
    if (isSmpte_) return 0.0;
    size_t i = 0;
    for (size_t j = 0; j < changes_.size(); ++j) {
        if (changes_[j].tick > tick) break;
        i = j;
    }
    const auto& c = changes_[i];
    if (c.microsecondsPerQuarterNote == 0) return 0.0;
    const double b0 = 60'000'000.0 / static_cast<double>(c.microsecondsPerQuarterNote);
    if (!c.rampToNext || i + 1 >= changes_.size() || tick < c.tick) return b0;
    // Sur une rampe : linéaire en BPM entre les deux changements (D15.5).
    const auto& s = changes_[i + 1];
    if (s.microsecondsPerQuarterNote == 0 || s.tick <= c.tick) return b0;
    const double b1 = 60'000'000.0 / static_cast<double>(s.microsecondsPerQuarterNote);
    const double t = static_cast<double>(tick - c.tick) / static_cast<double>(s.tick - c.tick);
    return b0 + (b1 - b0) * std::min(1.0, t);
}

} // namespace vsm::sequencer
