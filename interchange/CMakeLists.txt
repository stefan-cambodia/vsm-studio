# vsm_interchange : couche d'interopérabilité (Phase 7).
#
# C'est la SEULE bibliothèque du projet autorisée à connaître JSON. Elle
# dépend de vsm_audio (pour lire les ParameterList réelles des machines), mais
# l'inverse est FAUX et doit le rester : ni core/, ni audio/, ni le chemin
# temps réel n'incluent quoi que ce soit d'ici (docs/ROADMAP-interop.md § 0).
add_library(vsm_interchange STATIC
    src/Json.cpp
    src/ParameterDescriptor.cpp
    src/SearchProfile.cpp
    src/ReconstructionReport.cpp
    src/SynthPreset.cpp
    src/SoundFont.cpp
    src/MultisampleProfile.cpp
    src/EffectDescription.cpp
    src/ProjectDocument.cpp
    src/ProjectBundle.cpp
    src/OfflineReconstruction.cpp
    src/ClapParameterIds.cpp
    src/PatchRenderService.cpp
)

target_include_directories(vsm_interchange PUBLIC include)
target_compile_features(vsm_interchange PUBLIC cxx_std_20)
target_link_libraries(vsm_interchange PUBLIC vsm_audio vsm_core)

if (MSVC)
    target_compile_options(vsm_interchange PRIVATE /W4)
else()
    target_compile_options(vsm_interchange PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (VSM_BUILD_TESTS)
    add_executable(vsm_interchange_tests
        tests/test_main.cpp
        tests/test_json.cpp
        tests/test_semantic_parameters.cpp
        tests/test_synth_preset.cpp
        tests/test_multisample_profile.cpp
        tests/test_soundfont.cpp
        tests/test_effect_description.cpp
        tests/test_project_document.cpp
        tests/test_offline_reconstruction.cpp
        tests/test_clap_ids.cpp
        tests/test_patch_render_service.cpp
        tests/test_search_profile.cpp
        tests/test_preset_samples.cpp
        tests/test_reconstruction_report.cpp
    )
    target_link_libraries(vsm_interchange_tests PRIVATE vsm_interchange)
    target_include_directories(vsm_interchange_tests PRIVATE tests)

    enable_testing()
    add_test(NAME vsm_interchange_tests COMMAND vsm_interchange_tests)
endif()
