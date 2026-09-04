#include "TestFramework.h"
#include "vsm/sequencer/TempoMap.h"
#include "vsm/sequencer/TimeSignatureMap.h"
#include <cmath>
#include <cstdlib>

using namespace vsm::sequencer;

VSM_TEST(default_tempo_is_120bpm) {
    TempoMap map;
    VSM_ASSERT_NEAR(map.bpmAt(0), 120.0, 0.0001);
}

VSM_TEST(constant_tempo_ticks_to_seconds) {
    TempoMap map; // 120 BPM constant
    uint16_t ppq = 480;
    // À 120 BPM, une noire = 0.5s -> 480 ticks = 0.5s
    VSM_ASSERT_NEAR(map.ticksToSeconds(480, ppq), 0.5, 0.0001);
    VSM_ASSERT_NEAR(map.ticksToSeconds(1920, ppq), 2.0, 0.0001); // 1 mesure de 4/4
    VSM_ASSERT_NEAR(map.ticksToSeconds(0, ppq), 0.0, 0.0001);
}

VSM_TEST(tempo_change_affects_only_subsequent_ticks) {
    TempoMap map;
    uint16_t ppq = 480;
    // Changement à 60 BPM (1 000 000 us/qn) au tick 1920 (bar 2 en 4/4)
    map.addTempoChange(1920, 1000000);

    VSM_ASSERT_NEAR(map.ticksToSeconds(1920, ppq), 2.0, 0.0001); // inchangé avant le changement
    VSM_ASSERT_NEAR(map.ticksToSeconds(1920 + 480, ppq), 3.0, 0.0001); // +1 noire à 60 BPM = +1.0s
    VSM_ASSERT_NEAR(map.bpmAt(1920), 60.0, 0.0001);
    VSM_ASSERT_NEAR(map.bpmAt(1919), 120.0, 0.0001);
}

VSM_TEST(seconds_to_ticks_is_inverse_of_ticks_to_seconds) {
    TempoMap map;
    uint16_t ppq = 480;
    map.addTempoChange(1920, 1000000);

    for (Tick t : {0, 240, 480, 1920, 2400, 4800}) {
        double seconds = map.ticksToSeconds(t, ppq);
        Tick recovered = map.secondsToTicks(seconds, ppq);
        VSM_ASSERT(std::abs(static_cast<long long>(recovered - t)) <= 1); // tolérance d'arrondi
    }
}

VSM_TEST(smpte_tempo_map_uses_constant_tick_rate) {
    TempoMap map = TempoMap::smpte(25.0, 80); // 25 fps, 80 ticks/frame -> 2000 ticks/s
    VSM_ASSERT(map.isSmpte());
    VSM_ASSERT_NEAR(map.ticksToSeconds(2000, 480), 1.0, 0.0001);
    VSM_ASSERT_NEAR(map.ticksToSeconds(1000, 480), 0.5, 0.0001);
}

VSM_TEST(time_signature_default_is_4_4) {
    TimeSignatureMap map;
    VSM_ASSERT_EQ(map.numeratorAt(0), static_cast<uint8_t>(4));
    VSM_ASSERT_EQ(map.denominatorAt(0), static_cast<uint32_t>(4));
}

VSM_TEST(time_signature_ticks_per_bar) {
    TimeSignatureMap map; // 4/4
    uint16_t ppq = 480;
    VSM_ASSERT_EQ(map.ticksPerBeat(0, ppq), static_cast<Tick>(480));
    VSM_ASSERT_EQ(map.ticksPerBar(0, ppq), static_cast<Tick>(1920));

    map.addChange(0, 6, 3); // 6/8
    VSM_ASSERT_EQ(map.ticksPerBeat(0, ppq), static_cast<Tick>(240)); // ppq*4/8
    VSM_ASSERT_EQ(map.ticksPerBar(0, ppq), static_cast<Tick>(1440)); // 6 * 240
}

VSM_TEST(bar_beat_at_tracks_position_across_bars) {
    TimeSignatureMap map; // 4/4, 480 ppq
    uint16_t ppq = 480;
    BarBeat bb0 = map.barBeatAt(0, ppq);
    VSM_ASSERT_EQ(bb0.bar, static_cast<int64_t>(0));
    VSM_ASSERT_EQ(bb0.beat, static_cast<int64_t>(0));

    BarBeat bb1 = map.barBeatAt(1920, ppq); // exactement 1 mesure plus tard
    VSM_ASSERT_EQ(bb1.bar, static_cast<int64_t>(1));
    VSM_ASSERT_EQ(bb1.beat, static_cast<int64_t>(0));

    BarBeat bb2 = map.barBeatAt(1920 + 480 * 2, ppq); // mesure 1, temps 2
    VSM_ASSERT_EQ(bb2.bar, static_cast<int64_t>(1));
    VSM_ASSERT_EQ(bb2.beat, static_cast<int64_t>(2));
}

// ---------------------------------------------------------------------------
// D15.5 -- LES RAMPES DE TEMPO. Attendu, écrit avant la mesure : une rampe
// de 120 à 60 BPM sur quatre mesures de 4/4 à 480 ppq (7 680 ticks) dure
// 60·7680/(480·(60-120))·ln(60/120) = 16·ln 2 = 11,0904 s (en palier à 120
// elle durerait 8 s, à 60 16 s) ; au milieu, 90 BPM ; l'aller-retour ticks ↔
// secondes est exact au tick, et à la microseconde en secondes ; les paliers
// d'une noire de l'export conservent la durée totale.
// ---------------------------------------------------------------------------

VSM_TEST(a_tempo_ramp_lasts_what_the_closed_form_says_and_is_linear_in_bpm) {
    TempoMap map;
    const uint16_t ppq = 480;
    map.addTempoChange(0, 500000, true);      // 120 BPM, en rampe vers le suivant
    map.addTempoChange(7680, 1000000);        // 60 BPM à la mesure 5
    const double attendu = 16.0 * std::log(2.0);
    VSM_ASSERT_NEAR(map.ticksToSeconds(7680, ppq), attendu, 1e-9);
    VSM_ASSERT_NEAR(map.bpmAt(0), 120.0, 1e-9);
    VSM_ASSERT_NEAR(map.bpmAt(3840), 90.0, 1e-9);
    VSM_ASSERT_NEAR(map.bpmAt(7680), 60.0, 1e-9);
    // Après la rampe, palier à 60 : une noire = 1 s.
    VSM_ASSERT_NEAR(map.ticksToSeconds(7680 + 480, ppq), attendu + 1.0, 1e-9);
    // Sans le drapeau, la même carte est en palier : 8 s.
    TempoMap palier;
    palier.addTempoChange(0, 500000);
    palier.addTempoChange(7680, 1000000);
    VSM_ASSERT_NEAR(palier.ticksToSeconds(7680, ppq), 8.0, 1e-9);
    VSM_ASSERT(map.hasRamps());
    VSM_ASSERT(!palier.hasRamps());
}

VSM_TEST(a_tempo_ramp_converts_back_and_forth_exactly) {
    TempoMap map;
    const uint16_t ppq = 480;
    map.addTempoChange(0, 500000, true);
    map.addTempoChange(7680, 1000000, true);  // puis remonte vers 140
    map.addTempoChange(11520, 428571);
    for (Tick t : {0, 1, 240, 480, 3840, 7679, 7680, 9600, 11519, 11520, 20000}) {
        const double s = map.ticksToSeconds(t, ppq);
        VSM_ASSERT_EQ(map.secondsToTicks(s, ppq), t);
        VSM_ASSERT_NEAR(map.ticksToSeconds(map.secondsToTicks(s, ppq), ppq), s, 1e-6);
    }
}

VSM_TEST(flattening_a_ramp_into_quarter_note_steps_keeps_the_total_duration) {
    TempoMap map;
    const uint16_t ppq = 480;
    map.addTempoChange(0, 500000, true);
    map.addTempoChange(7680, 1000000);
    const auto paliers = map.flattened(ppq, ppq);
    VSM_ASSERT_EQ(paliers.size(), size_t(17));   // 16 noires de rampe + le 60 final
    TempoMap plate;
    plate.clearTempoChanges();
    for (const auto& c : paliers) plate.addTempoChange(c.tick, c.microsecondsPerQuarterNote);
    VSM_ASSERT(!plate.hasRamps());
    // À la microseconde près par palier : 16 arrondis, moins de 20 µs au total.
    VSM_ASSERT_NEAR(plate.ticksToSeconds(7680, ppq), map.ticksToSeconds(7680, ppq), 2e-5);
    // Et la courbe est approchée : le premier palier tourne autour de 118 BPM,
    // le dernier autour de 61 -- descendants.
    VSM_ASSERT(paliers.front().microsecondsPerQuarterNote < paliers[15].microsecondsPerQuarterNote);
}
