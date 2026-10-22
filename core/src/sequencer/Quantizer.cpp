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

    // Swing : décale une case de grille sur deux ("off-beat") de swing × pas / 3.
    // swing=0 -> droit ; swing=1 -> triolet (croches : 240 -> 320 ticks). D415.
    if (settings.swing != 0.0f && (index % 2 != 0)) {
        target += static_cast<midi::Tick>(std::llround(settings.swing * static_cast<double>(grid) / 3.0));
    }

    double blended = static_cast<double>(tick) +
                      static_cast<double>(settings.strength) *
                      (static_cast<double>(target) - static_cast<double>(tick));
    return static_cast<midi::Tick>(std::llround(blended));
}

bool quantizeLeavesAlone(midi::Tick tick, const QuantizeSettings& settings, uint16_t ppq) {
    if (settings.nonQuantizeZone <= 0.0f && settings.catchRange <= 0.0f) return false;   // le chemin d'avant
    const midi::Tick pas = gridResolutionToTicks(settings.grid, ppq);
    if (pas <= 0) return false;
    // LA CIBLE PLEINE, swing compris, la force mise à part : la distance dit où est la note, pas où elle irait.
    QuantizeSettings plein = settings;
    plein.strength = 1.0f;
    const double ecart = std::abs(static_cast<double>(quantizeTick(tick, plein, ppq) - tick));
    if (settings.nonQuantizeZone > 0.0f && ecart <= settings.nonQuantizeZone * static_cast<double>(pas)) return true;
    if (settings.catchRange > 0.0f && ecart > settings.catchRange * static_cast<double>(pas)) return true;
    return false;
}

void quantizeNotes(std::vector<Note>& notes, const QuantizeSettings& settings, uint16_t ppq) {
    for (auto& note : notes) {
        midi::Tick duration = note.durationTicks();
        const midi::Tick debutAvant = note.startTick, finAvant = note.endTick;   // D566.1
        // D575.1, D575.2 : la zone et le rayon laissent la note où elle est — son début, sa fin, chacun pour soi.
        if (settings.quantizeNoteStart && !quantizeLeavesAlone(note.startTick, settings, ppq))
            note.startTick = quantizeTick(note.startTick, settings, ppq);
        if (settings.quantizeNoteEnd && quantizeLeavesAlone(finAvant, settings, ppq)) {
            note.endTick = finAvant;
        } else if (settings.quantizeNoteEnd) {
            note.endTick = quantizeTick(note.endTick, settings, ppq);
            // H22 (D557.1) : UNE FIN QUI TOMBE SUR OU AVANT LE DÉBUT VA À LA PREMIÈRE LIGNE APRÈS LUI. Le rattrapage
            // d'un tick ci-dessous réduisait une note plus courte qu'une demi-case à une milliseconde : 100-150 sur
            // une grille de 120 rendait [120, 121). La ligne passe par `quantizeTick`, pour garder swing et force.
            const midi::Tick pas = gridResolutionToTicks(settings.grid, ppq);
            if (note.endTick <= note.startTick && pas > 0)
                note.endTick = std::max(note.startTick + 1,
                                        quantizeTick((note.startTick / pas + 1) * pas, settings, ppq));
        } else {
            note.endTick = note.startTick + duration; // conserve la durée d'origine
        }

        if (note.endTick <= note.startTick)
            note.endTick = note.startTick + 1; // jamais de durée nulle/négative
        // D566.1 : LE DÉPLACEMENT SE CUMULE, pour qu'« Annuler la quantification » le retire.
        note.quantizeShiftStart += note.startTick - debutAvant;
        note.quantizeShiftEnd += note.endTick - finAvant;
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
