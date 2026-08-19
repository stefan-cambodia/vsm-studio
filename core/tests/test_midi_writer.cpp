#include "TestFramework.h"
#include "fixtures/TestMidiFixtures.h"
#include "vsm/midi/MidiFileParser.h"
#include "vsm/midi/MidiFileWriter.h"
#include "vsm/sequencer/Project.h"

using namespace vsm::midi;
using namespace vsm::sequencer;
using namespace vsm::test::fixtures;

VSM_TEST(project_from_parsed_file_pairs_notes_correctly) {
    auto bytes = buildTestSmf(480);
    ParsedFile parsed = MidiFileParser::parse(bytes);
    Project project = Project::fromParsedFile(parsed);

    VSM_ASSERT_EQ(project.tracks.size(), static_cast<size_t>(2));
    VSM_ASSERT_EQ(project.tracks[0].notes.size(), static_cast<size_t>(0)); // conductor : pas de notes

    const auto& bass = project.tracks[1];
    VSM_ASSERT_EQ(bass.name, std::string("Bass"));
    VSM_ASSERT_EQ(bass.notes.size(), static_cast<size_t>(2));

    VSM_ASSERT_EQ(bass.notes[0].number, static_cast<uint8_t>(36));
    VSM_ASSERT_EQ(bass.notes[0].startTick, static_cast<Tick>(0));
    VSM_ASSERT_EQ(bass.notes[0].endTick, static_cast<Tick>(480));
    VSM_ASSERT_EQ(bass.notes[0].velocity, static_cast<uint8_t>(100));

    VSM_ASSERT_EQ(bass.notes[1].number, static_cast<uint8_t>(40));
    VSM_ASSERT_EQ(bass.notes[1].startTick, static_cast<Tick>(480));
    VSM_ASSERT_EQ(bass.notes[1].endTick, static_cast<Tick>(720));

    VSM_ASSERT_EQ(bass.controlChanges.size(), static_cast<size_t>(1));
    VSM_ASSERT_EQ(bass.pitchBends.size(), static_cast<size_t>(1));

    VSM_ASSERT_NEAR(project.tempoMap.bpmAt(0), 120.0, 0.001);
    VSM_ASSERT_EQ(project.timeSignatureMap.numeratorAt(0), static_cast<uint8_t>(4));
}

VSM_TEST(full_roundtrip_preserves_musical_content) {
    auto originalBytes = buildTestSmf(480);
    ParsedFile parsed1 = MidiFileParser::parse(originalBytes);
    Project project1 = Project::fromParsedFile(parsed1);

    // export -> reparse -> reconstruit un second Project
    ParsedFile exported = project1.toParsedFile();
    std::vector<uint8_t> exportedBytes = MidiFileWriter::write(exported);
    ParsedFile parsed2 = MidiFileParser::parse(exportedBytes);
    Project project2 = Project::fromParsedFile(parsed2);

    VSM_ASSERT_EQ(project1.tracks.size(), project2.tracks.size());

    const auto& bass1 = project1.tracks[1];
    const auto& bass2 = project2.tracks[1];
    VSM_ASSERT_EQ(bass1.name, bass2.name);
    VSM_ASSERT_EQ(bass1.notes.size(), bass2.notes.size());

    for (size_t i = 0; i < bass1.notes.size(); ++i) {
        VSM_ASSERT_EQ(bass1.notes[i].number, bass2.notes[i].number);
        VSM_ASSERT_EQ(bass1.notes[i].startTick, bass2.notes[i].startTick);
        VSM_ASSERT_EQ(bass1.notes[i].endTick, bass2.notes[i].endTick);
        VSM_ASSERT_EQ(bass1.notes[i].velocity, bass2.notes[i].velocity);
    }

    VSM_ASSERT_EQ(bass1.controlChanges.size(), bass2.controlChanges.size());
    VSM_ASSERT_EQ(bass1.pitchBends.size(), bass2.pitchBends.size());
    if (!bass1.pitchBends.empty())
        VSM_ASSERT_EQ(bass1.pitchBends[0].value, bass2.pitchBends[0].value);

    VSM_ASSERT_NEAR(project1.tempoMap.bpmAt(0), project2.tempoMap.bpmAt(0), 0.001);
    VSM_ASSERT_EQ(project1.timeSignatureMap.numeratorAt(0), project2.timeSignatureMap.numeratorAt(0));
    VSM_ASSERT_EQ(project1.timeSignatureMap.denominatorAt(0), project2.timeSignatureMap.denominatorAt(0));
}

VSM_TEST(export_omits_muted_notes) {
    // Le format SMF n'a aucun moyen de dire "présente mais silencieuse" :
    // exporter une note muette produirait un fichier qui joue autre chose que
    // ce qu'on entend dans l'application. Elles sont donc omises (voir
    // Note::muted) -- et les autres notes restent intactes.
    auto bytes = buildTestSmf(480);
    Project project = Project::fromParsedFile(MidiFileParser::parse(bytes));
    VSM_ASSERT(!project.tracks[1].notes.empty());

    const size_t eventsBefore = project.toParsedFile().tracks[1].events.size();
    project.tracks[1].notes[0].muted = true;
    const size_t eventsAfter = project.toParsedFile().tracks[1].events.size();
    VSM_ASSERT_EQ(eventsAfter, eventsBefore - 2); // NoteOn + NoteOff en moins
}

VSM_TEST(writer_rejects_smpte_export_in_phase1) {
    ParsedFile file;
    file.isSmpteTiming = true;
    file.smpteFramesPerSecond = -25;
    file.smpteTicksPerFrame = 80;
    bool threw = false;
    try {
        MidiFileWriter::write(file);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    VSM_ASSERT(threw);
}
