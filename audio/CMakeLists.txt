add_library(vsm_audio STATIC
    src/dsp/WaveTable.cpp
    src/io/WavFileWriter.cpp
    src/io/WavFileReader.cpp
    src/io/WavStreamReader.cpp
    src/io/AudioTrackLoader.cpp
    src/io/WaveformPeaks.cpp
    src/engine/AudioTrackSource.cpp
    src/engine/SampleStore.cpp
    src/engine/ProcessGraph.cpp
    src/engine/Transport.cpp
    src/engine/RenderThreadPool.cpp
    src/effect/EffectFactory.cpp
    src/engine/OfflineRenderer.cpp
    src/plugin/BuiltInPlugins.cpp
    src/plugin/MultisampleSampleCache.cpp
    plugins/testtone/TestToneSynth.cpp
    plugins/minimoog/MinimoogSynth.cpp
    plugins/tb303/TB303Synth.cpp
    plugins/juno106/Juno106Synth.cpp
    plugins/tr808/TR808Synth.cpp
    plugins/tr909/TR909Synth.cpp
    plugins/sh101/SH101Synth.cpp
    plugins/prophet/ProphetSynth.cpp
    plugins/jupiter8/Jupiter8Synth.cpp
    plugins/arpodyssey/ArpOdysseySynth.cpp
    plugins/ms20/MS20Synth.cpp
    plugins/dx7/DX7Synth.cpp
    plugins/sampler/SamplerSynth.cpp
    plugins/epiano/EPianoSynth.cpp
    plugins/obx/ObxSynth.cpp
    plugins/supersaw/SupersawSynth.cpp
    plugins/wavetable/WavetableSynth.cpp
    plugins/pcmhybrid/PcmHybridSynth.cpp
    plugins/tonewheel/TonewheelOrgan.cpp
    plugins/generic/GenericSynth.cpp
    plugins/string/StringSynth.cpp
    plugins/piano/PianoSynth.cpp
    plugins/drums/DrumsSynth.cpp
    plugins/wind/WindSynth.cpp
    plugins/perc/PercSynth.cpp
    plugins/additive/AdditiveSynth.cpp
    plugins/westcoast/WestCoastSynth.cpp
    plugins/fmdrums/FmDrumsSynth.cpp
    plugins/vocal/VocalSynth.cpp
    plugins/phasedist/PhaseDistSynth.cpp
    plugins/divider/DividerSynth.cpp
    plugins/psg/PsgSynth.cpp
    plugins/stochastic/StochasticSynth.cpp
    plugins/cone/ConeSynth.cpp
    plugins/vector/VectorSynth.cpp
    plugins/granular/GranularSynth.cpp
    plugins/cs80/Cs80Synth.cpp
    plugins/modal/ModalSynth.cpp
    plugins/chebyshev/ChebyshevSynth.cpp
    plugins/scanned/ScannedSynth.cpp
    plugins/mellotron/MellotronSynth.cpp
    plugins/multisample/MultisampleSynth.cpp
)

target_include_directories(vsm_audio PUBLIC include)
target_include_directories(vsm_audio PRIVATE plugins) # *.h des plugins inclus par leurs propres .cpp et par BuiltInPlugins.cpp
target_compile_features(vsm_audio PUBLIC cxx_std_20)

# NB: TestToneSynth.cpp est compilé directement comme source de vsm_audio
# (pas dans une bibliothèque plugins séparée), ET référencé explicitement
# par src/plugin/BuiltInPlugins.cpp -- les deux sont nécessaires pour que
# son registrar VSM_REGISTER_SYNTH_PLUGIN survive à l'édition de liens
# (voir PluginRegistry.h et BuiltInPlugins.h pour le pourquoi).

find_package(Threads REQUIRED)
target_link_libraries(vsm_audio PUBLIC vsm_core Threads::Threads)

if (MSVC)
    target_compile_options(vsm_audio PRIVATE /W4)
else()
    target_compile_options(vsm_audio PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Banc de mesure CPU (Phase 6). Cible séparée des tests : les tests VÉRIFIENT
# (et doivent rester rapides et déterministes), le banc MESURE (et prend
# volontairement plusieurs secondes). Ne jamais interpréter ses chiffres
# depuis un build non optimisé -- le binaire le signale lui-même.
if (VSM_BUILD_BENCH)
    add_executable(vsm_audio_bench bench/bench_main.cpp)
    target_link_libraries(vsm_audio_bench PRIVATE vsm_audio)
    if (MSVC)
        target_compile_options(vsm_audio_bench PRIVATE /W4)
    else()
        target_compile_options(vsm_audio_bench PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if (VSM_BUILD_TESTS)
    add_executable(vsm_audio_tests
        tests/test_main.cpp
        tests/test_oscillator.cpp
        tests/test_envelope.cpp
        tests/test_filter.cpp
        tests/test_ladder_filter.cpp
        tests/test_mono_voice_allocator.cpp
        tests/test_analog_drift.cpp
        tests/test_parameter_smoother.cpp
        tests/test_ring_buffer.cpp
        tests/test_voice_manager.cpp
        tests/test_automation_lane.cpp
        tests/test_mixer.cpp
        tests/test_plugin_registry.cpp
        tests/test_testtone_synth.cpp
        tests/test_minimoog_synth.cpp
        tests/test_tb303_synth.cpp
        tests/test_chorus.cpp
        tests/test_juno106_synth.cpp
        tests/test_master_dsp.cpp
        tests/test_master_bus.cpp
        tests/test_oversampler.cpp
        tests/test_audio_track.cpp
        tests/test_metronome.cpp
        tests/test_count_in.cpp
        tests/test_latency_probe.cpp
        tests/test_channel_strip.cpp
        tests/test_waveform_peaks.cpp
        tests/test_control_events.cpp
        tests/test_effects.cpp
        tests/test_tr808_synth.cpp
        tests/test_tr909_synth.cpp
        tests/test_sh101_synth.cpp
        tests/test_prophet_synth.cpp
        tests/test_jupiter8_synth.cpp
        tests/test_arpodyssey_synth.cpp
        tests/test_ms20_synth.cpp
        tests/test_dx7_synth.cpp
        tests/test_sampler_synth.cpp
        tests/test_epiano_synth.cpp
        tests/test_obx_synth.cpp
        tests/test_supersaw_synth.cpp
        tests/test_wavetable.cpp
        tests/test_reference_track.cpp
        tests/test_wavetable_synth.cpp
        tests/test_pcmhybrid_synth.cpp
        tests/test_tonewheel_organ.cpp
        tests/test_generic_synth.cpp
        tests/test_string_synth.cpp
        tests/test_piano_synth.cpp
        tests/test_drums_synth.cpp
        tests/test_wind_synth.cpp
        tests/test_perc_synth.cpp
        tests/test_additive_synth.cpp
        tests/test_westcoast_synth.cpp
        tests/test_fmdrums_synth.cpp
        tests/test_vocal_synth.cpp
        tests/test_phasedist_synth.cpp
        tests/test_divider_synth.cpp
        tests/test_psg_synth.cpp
        tests/test_stochastic_synth.cpp
        tests/test_cone_synth.cpp
        tests/test_vector_synth.cpp
        tests/test_granular_synth.cpp
        tests/test_cs80_synth.cpp
        tests/test_modal_synth.cpp
        tests/test_chebyshev_synth.cpp
        tests/test_scanned_synth.cpp
        tests/test_mellotron_synth.cpp
        tests/test_multisample_synth.cpp
        tests/test_midi_learn.cpp
        tests/test_effect_routing.cpp
        tests/test_more_effects.cpp
        tests/test_effect_factory.cpp
        tests/test_process_graph_determinism.cpp
        tests/test_render_thread_pool.cpp
        tests/test_streamed_audio.cpp
        tests/test_transport_unifie.cpp
        tests/test_banc_de_charge.cpp
        tests/test_no_allocation_in_process.cpp
        tests/test_wav_writer.cpp
        tests/test_wav_reader.cpp
        tests/test_simd_ladder.cpp
        tests/test_audio_regression.cpp
    )
    target_link_libraries(vsm_audio_tests PRIVATE vsm_audio)
    target_include_directories(vsm_audio_tests PRIVATE tests)

    enable_testing()
    add_test(NAME vsm_audio_tests COMMAND vsm_audio_tests)
endif()
