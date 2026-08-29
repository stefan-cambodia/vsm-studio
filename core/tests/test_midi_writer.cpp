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
    // ce qu'on entend dans l'application. Elles sont donc omises DU FLUX JOUÉ
    // (voir Note::muted) -- et les autres notes restent intactes.
    //
    // LE COMPTE PORTE SUR LES NOTES, PLUS SUR LES ÉVÉNEMENTS. Depuis D6.3, une
    // note muette fait apparaître un bloc privé 0x7F qui la décrit sans la
    // faire sonner : compter les événements bruts mesurerait ce bloc en même
    // temps que l'omission, et confondrait deux choses opposées.
    auto bytes = buildTestSmf(480);
    Project project = Project::fromParsedFile(MidiFileParser::parse(bytes));
    VSM_ASSERT(!project.tracks[1].notes.empty());

    auto compteNotes = [](const ParsedFile& parsed) {
        size_t n = 0;
        for (const auto& ev : parsed.tracks[1].events)
            if (std::holds_alternative<NoteOnEvent>(ev.data)
                || std::holds_alternative<NoteOffEvent>(ev.data)) ++n;
        return n;
    };
    const size_t eventsBefore = compteNotes(project.toParsedFile());
    project.tracks[1].notes[0].muted = true;
    const size_t eventsAfter = compteNotes(project.toParsedFile());
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

// --- D6.3 : l'export MIDI ne perd plus `muted` ni `confidence` --------------
//
// Le critère de l'étape se lit en deux moitiés. La première -- « relu ailleurs
// sans perte de tempo ni de signature » -- porte sur ce que le SMF sait dire ;
// la seconde sur ce qu'il ne sait pas, et que le bloc privé 0x7F transporte.

VSM_TEST(a_midi_round_trip_keeps_muted_notes_and_their_confidence) {
    Project project;
    project.ticksPerQuarterNote = 480;
    Track piste;
    piste.name = "Transcription";
    uint64_t ids = 1;
    piste.addNote(0, 240, 60, 100, 0, ids);
    piste.addNote(480, 720, 64, 90, 0, ids);
    piste.addNote(960, 1200, 67, 80, 0, ids);
    piste.notes[1].muted = true;          // tue dans l'éditeur
    piste.notes[1].confidence = 0.25f;
    piste.notes[2].confidence = 0.5f;     // douteuse mais sonnante
    project.tracks.push_back(piste);

    const Project relu = Project::fromParsedFile(
        MidiFileParser::parse(MidiFileWriter::write(project.toParsedFile())));

    VSM_ASSERT_EQ(relu.tracks.size(), static_cast<size_t>(1));
    const auto& notes = relu.tracks[0].notes;
    VSM_ASSERT_EQ(notes.size(), static_cast<size_t>(3));

    size_t muettes = 0;
    for (const auto& note : notes) {
        if (!note.muted) continue;
        ++muettes;
        VSM_ASSERT_EQ(note.number, static_cast<uint8_t>(64));
        VSM_ASSERT_EQ(note.startTick, static_cast<Tick>(480));
        VSM_ASSERT_EQ(note.endTick, static_cast<Tick>(720));
        VSM_ASSERT_EQ(note.velocity, static_cast<uint8_t>(90));
        VSM_ASSERT_NEAR(note.confidence, 0.25f, 1e-4);
    }
    VSM_ASSERT_EQ(muettes, static_cast<size_t>(1));

    for (const auto& note : notes)
        if (!note.muted && note.number == 67) VSM_ASSERT_NEAR(note.confidence, 0.5f, 1e-4);
    for (const auto& note : notes)
        if (!note.muted && note.number == 60) VSM_ASSERT_NEAR(note.confidence, 1.0f, 1e-6);
}

VSM_TEST(a_muted_note_is_still_absent_from_the_played_stream) {
    // LA RÈGLE NE BOUGE PAS : le fichier joue ce qu'on entend. Le bloc privé
    // n'est pas une porte dérobée pour faire sonner ailleurs ce qu'on a tu.
    Project project;
    project.ticksPerQuarterNote = 480;
    Track piste;
    uint64_t ids = 1;
    piste.addNote(0, 240, 60, 100, 0, ids);
    piste.addNote(480, 720, 64, 90, 0, ids);
    piste.notes[1].muted = true;
    project.tracks.push_back(piste);

    const ParsedFile parsed = project.toParsedFile();
    int notesOn = 0;
    for (const auto& ev : parsed.tracks[0].events)
        if (std::holds_alternative<NoteOnEvent>(ev.data)) ++notesOn;
    VSM_ASSERT_EQ(notesOn, 1);
}

VSM_TEST(a_project_without_anything_to_say_writes_no_private_block) {
    Project project;
    project.ticksPerQuarterNote = 480;
    Track piste;
    uint64_t ids = 1;
    piste.addNote(0, 240, 60, 100, 0, ids);
    project.tracks.push_back(piste);

    const ParsedFile parsed = project.toParsedFile();
    for (const auto& ev : parsed.tracks[0].events)
        VSM_ASSERT(!std::holds_alternative<UnknownMetaEvent>(ev.data));
}

VSM_TEST(a_private_block_from_another_program_is_left_alone) {
    // Un 0x7F qui n'est pas le nôtre traverse le logiciel sans être touché --
    // c'est ce que fait déjà le reste du projet pour ce qu'il ne comprend pas,
    // et le manger ici effacerait le travail d'un autre.
    Project project;
    project.ticksPerQuarterNote = 480;
    Track piste;
    uint64_t ids = 1;
    piste.addNote(0, 240, 60, 100, 0, ids);
    piste.miscEvents.push_back({0, UnknownMetaEvent{0x7F, {0x00, 0x21, 0x1D, 0x42}}});
    project.tracks.push_back(piste);

    const Project relu = Project::fromParsedFile(
        MidiFileParser::parse(MidiFileWriter::write(project.toParsedFile())));
    bool retrouve = false;
    for (const auto& ev : relu.tracks[0].miscEvents)
        if (const auto* meta = std::get_if<UnknownMetaEvent>(&ev.data))
            if (meta->metaType == 0x7F && meta->data.size() == 4 && meta->data[3] == 0x42)
                retrouve = true;
    VSM_ASSERT(retrouve);
}

VSM_TEST(a_midi_round_trip_keeps_every_tempo_and_time_signature_change) {
    Project project;
    project.ticksPerQuarterNote = 480;
    project.tempoMap.addTempoChange(0, 500000);      // 120 BPM
    project.tempoMap.addTempoChange(1920, 400000);   // 150 BPM
    project.timeSignatureMap.addChange(0, 3, 2);     // 3/4
    project.timeSignatureMap.addChange(1920, 7, 3);  // 7/8
    Track piste;
    uint64_t ids = 1;
    piste.addNote(0, 240, 60, 100, 0, ids);
    project.tracks.push_back(piste);

    const Project relu = Project::fromParsedFile(
        MidiFileParser::parse(MidiFileWriter::write(project.toParsedFile())));

    VSM_ASSERT_EQ(relu.ticksPerQuarterNote, 480);
    VSM_ASSERT_EQ(relu.tempoMap.changes().size(), static_cast<size_t>(2));
    VSM_ASSERT_EQ(relu.tempoMap.changes()[1].tick, static_cast<Tick>(1920));
    VSM_ASSERT_EQ(relu.tempoMap.changes()[1].microsecondsPerQuarterNote, 400000u);
    VSM_ASSERT_EQ(relu.timeSignatureMap.changes().size(), static_cast<size_t>(2));
    VSM_ASSERT_EQ(relu.timeSignatureMap.changes()[1].numerator, 7);
    VSM_ASSERT_EQ(relu.timeSignatureMap.changes()[1].denominatorPow2, 3);
    // Et la conséquence qui compte vraiment : la même note tombe au même
    // instant. Un tempo relu de travers ne se voit pas, il s'entend.
    VSM_ASSERT_NEAR(relu.ticksToSeconds(2400), project.ticksToSeconds(2400), 1e-9);
}
