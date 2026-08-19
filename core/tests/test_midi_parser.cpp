#include "TestFramework.h"
#include "fixtures/TestMidiFixtures.h"
#include "vsm/midi/MidiFileParser.h"
#include "vsm/midi/VariableLengthQuantity.h"

using namespace vsm::midi;
using namespace vsm::test::fixtures;

VSM_TEST(vlq_roundtrip_basic_values) {
    for (uint32_t value : {0u, 1u, 127u, 128u, 8192u, 16383u, 2097151u, 268435455u}) {
        std::vector<uint8_t> buf;
        writeVLQ(buf, value);
        const uint8_t* p = buf.data();
        uint32_t decoded = readVLQ(p, buf.data() + buf.size());
        VSM_ASSERT_EQ(decoded, value);
        VSM_ASSERT_EQ(p, buf.data() + buf.size()); // tout consommé, rien de plus
    }
}

VSM_TEST(parses_header_correctly) {
    auto bytes = buildTestSmf(480);
    ParsedFile file = MidiFileParser::parse(bytes);
    VSM_ASSERT(file.format == SmfFormat::Type1);
    VSM_ASSERT_EQ(file.ticksPerQuarterNote, 480);
    VSM_ASSERT(!file.isSmpteTiming);
    VSM_ASSERT_EQ(file.tracks.size(), static_cast<size_t>(2));
}

VSM_TEST(extracts_track_name) {
    auto bytes = buildTestSmf(480);
    ParsedFile file = MidiFileParser::parse(bytes);
    VSM_ASSERT_EQ(file.tracks[1].name, std::string("Bass"));
}

VSM_TEST(parses_conductor_meta_events) {
    auto bytes = buildTestSmf(480);
    ParsedFile file = MidiFileParser::parse(bytes);
    const auto& events = file.tracks[0].events;
    VSM_ASSERT_EQ(events.size(), static_cast<size_t>(3)); // tempo, timesig, end-of-track

    auto* tempo = std::get_if<TempoEvent>(&events[0].data);
    VSM_ASSERT(tempo != nullptr);
    VSM_ASSERT_EQ(tempo->microsecondsPerQuarterNote, static_cast<uint32_t>(500000));

    auto* timeSig = std::get_if<TimeSignatureEvent>(&events[1].data);
    VSM_ASSERT(timeSig != nullptr);
    VSM_ASSERT_EQ(timeSig->numerator, static_cast<uint8_t>(4));
    VSM_ASSERT_EQ(timeSig->denominatorPow2, static_cast<uint8_t>(2)); // 2^2 = 4
}

VSM_TEST(parses_notes_with_correct_ticks_and_velocity) {
    auto bytes = buildTestSmf(480);
    ParsedFile file = MidiFileParser::parse(bytes);
    const auto& events = file.tracks[1].events;

    // Ordre attendu : TrackName, CC1, NoteOn(36), NoteOff(36)@480,
    // PitchBend, NoteOn(40), NoteOff(40)@720, EndOfTrack
    VSM_ASSERT_EQ(events.size(), static_cast<size_t>(8));

    auto* noteOn1 = std::get_if<NoteOnEvent>(&events[2].data);
    VSM_ASSERT(noteOn1 != nullptr);
    VSM_ASSERT_EQ(events[2].tick, static_cast<Tick>(0));
    VSM_ASSERT_EQ(noteOn1->note, static_cast<uint8_t>(36));
    VSM_ASSERT_EQ(noteOn1->velocity, static_cast<uint8_t>(100));

    auto* noteOff1 = std::get_if<NoteOffEvent>(&events[3].data);
    VSM_ASSERT(noteOff1 != nullptr);
    VSM_ASSERT_EQ(events[3].tick, static_cast<Tick>(480));

    auto* bend = std::get_if<PitchBendEvent>(&events[4].data);
    VSM_ASSERT(bend != nullptr);
    VSM_ASSERT_EQ(events[4].tick, static_cast<Tick>(480));
}

VSM_TEST(rejects_truncated_file) {
    std::vector<uint8_t> bytes = {'M', 'T', 'h'}; // trop court, même pas un header complet
    bool threw = false;
    try {
        MidiFileParser::parse(bytes);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    VSM_ASSERT(threw);
}

VSM_TEST(rejects_file_without_mthd) {
    std::vector<uint8_t> bytes = {'X', 'X', 'X', 'X', 0, 0, 0, 6, 0, 1, 0, 1, 1, 0xE0};
    bool threw = false;
    try {
        MidiFileParser::parse(bytes);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    VSM_ASSERT(threw);
}
