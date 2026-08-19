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
