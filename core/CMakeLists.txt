add_library(vsm_core STATIC
    src/midi/MidiFileParser.cpp
    src/midi/MidiFileWriter.cpp
    src/sequencer/TempoMap.cpp
    src/sequencer/TimeSignatureMap.cpp
    src/sequencer/Track.cpp
    src/sequencer/Project.cpp
    src/sequencer/Quantizer.cpp
    src/sequencer/NoteEdit.cpp
    src/sequencer/StepPattern.cpp
    src/sequencer/MidiRecorder.cpp
    src/sequencer/PlaybackScheduler.cpp
    src/sequencer/RealtimeTransport.cpp
)

target_include_directories(vsm_core PUBLIC include)
target_compile_features(vsm_core PUBLIC cxx_std_20)

find_package(Threads REQUIRED)
target_link_libraries(vsm_core PUBLIC Threads::Threads)

if (MSVC)
    target_compile_options(vsm_core PRIVATE /W4)
else()
    target_compile_options(vsm_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (VSM_BUILD_TESTS)
    add_executable(vsm_core_tests
        tests/test_main.cpp
        tests/test_midi_parser.cpp
        tests/test_midi_writer.cpp
        tests/test_tempo_map.cpp
        tests/test_quantizer.cpp
        tests/test_note_edit.cpp
        tests/test_step_pattern.cpp
        tests/test_edit_history.cpp
        tests/test_clips.cpp
        tests/test_project_history.cpp
        tests/test_transport.cpp
        tests/test_midi_recorder.cpp
    )
    target_link_libraries(vsm_core_tests PRIVATE vsm_core)
    target_include_directories(vsm_core_tests PRIVATE tests)

    enable_testing()
    add_test(NAME vsm_core_tests COMMAND vsm_core_tests)
endif()
