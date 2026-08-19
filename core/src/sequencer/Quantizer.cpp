#include "vsm/sequencer/Quantizer.h"
#include "vsm/util/DeterministicRng.h"
#include <algorithm>
#include <cmath>

namespace vsm::sequencer {

midi::Tick gridResolutionToTicks(GridResolution res, uint16_t ppq) {
    int64_t base;
    switch (res.value) {
        case NoteValue::Whole:               base = static_cast<int64_t>(ppq) * 4; break;
        case NoteValue::Half:                base = static_cast<int64_t>(ppq) * 2; break;
        case NoteValue::Quarter:             base = ppq; break;
        case NoteValue::Eighth:              base = ppq / 2; break;
        case NoteValue::Sixteenth:           base = ppq / 4; break;
        case NoteValue::ThirtySecond:        base = ppq / 8; break;
        case NoteValue::SixtyFourth:         base = ppq / 16; break;
        case NoteValue::HundredTwentyEighth: base = ppq / 32; break;
        default:                             base = ppq / 4;
    }
    if (base < 1) base = 1;

    if (res.triplet)      base = (base * 2) / 3; // 3 notes dans l'espace de 2
    else if (res.dotted)  base = (base * 3) / 2; // durée x1.5

    return base < 1 ? 1 : base;
}

midi::Tick quantizeTick(midi::Tick tick, const QuantizeSettings& settings, uint16_t ppq) {
    midi::Tick grid = gridResolutionToTicks(settings.grid, ppq);
    if (grid <= 0) return tick;

    long long index = std::llround(static_cast<double>(tick) / static_cast<double>(grid));
    midi::Tick target = index * grid;

    // Swing : décale une case de grille sur deux ("off-beat") d'une fraction
    // du pas de grille. swing=0 -> droit ; swing~0.33 -> swing "triolet" classique.
    if (settings.swing != 0.0f && (index % 2 != 0)) {
        target += static_cast<midi::Tick>(std::llround(settings.swing * static_cast<double>(grid) / 3.0));
    }

    double blended = static_cast<double>(tick) +
                      static_cast<double>(settings.strength) *
                      (static_cast<double>(target) - static_cast<double>(tick));
    return static_cast<midi::Tick>(std::llround(blended));
}

void quantizeNotes(std::vector<Note>& notes, const QuantizeSettings& settings, uint16_t ppq) {
    for (auto& note : notes) {
        midi::Tick duration = note.durationTicks();
        if (settings.quantizeNoteStart)
            note.startTick = quantizeTick(note.startTick, settings, ppq);
        if (settings.quantizeNoteEnd)
            note.endTick = quantizeTick(note.endTick, settings, ppq);
        else
            note.endTick = note.startTick + duration; // conserve la durée d'origine

        if (note.endTick <= note.startTick)
            note.endTick = note.startTick + 1; // jamais de durée nulle/négative
    }
}

void humanizeNotes(std::vector<Note>& notes, const HumanizeSettings& settings) {
    for (auto& note : notes) {
        uint64_t seed = vsm::util::deriveSeed(settings.seed, note.id);
        vsm::util::DeterministicRng rng(seed);

        if (settings.timingAmountTicks > 0.0f) {
            midi::Tick duration = note.durationTicks();
            double offset = static_cast<double>(rng.nextBipolar()) * settings.timingAmountTicks;
            midi::Tick newStart = note.startTick + static_cast<midi::Tick>(std::llround(offset));
            note.startTick = std::max<midi::Tick>(0, newStart);
            note.endTick = note.startTick + duration;
        }
        if (settings.velocityAmount > 0.0f) {
            double offset = static_cast<double>(rng.nextBipolar()) * settings.velocityAmount;
            int newVel = static_cast<int>(note.velocity) + static_cast<int>(std::llround(offset));
            note.velocity = static_cast<uint8_t>(std::clamp(newVel, 1, 127));
        }
    }
}

} // namespace vsm::sequencer
