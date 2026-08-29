# Couche CLAP (Phase 7, P5-P6).
#
# DÉSACTIVÉE PAR DÉFAUT, et ce n'est pas de la frilosité : c'est la seule
# partie du projet qui exige un téléchargement (le SDK CLAP, via FetchContent).
# Le reste du dépôt doit continuer à se compiler et se tester hors ligne, sur
# une machine neuve, sans rien récupérer -- règle qui a déjà décidé du parseur
# MIDI, du framework de tests et du lecteur JSON maison.
#
#     cmake -B build -DVSM_BUILD_CLAP=ON
#
# Le SDK CLAP est un ensemble d'en-têtes C, sans bibliothèque à lier : il
# n'introduit aucune dépendance à l'exécution, et rien n'en dépend en dehors de
# ce dossier. Ni core/, ni audio/, ni interchange/ ne le connaissent.

include(FetchContent)

FetchContent_Declare(
    clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG        1.2.10
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(clap)

# --- P5 : adaptateur -- les machines VSM vues comme un plugin CLAP ---------
# Construit un module `vsm-instruments.clap` : une bibliothèque partagée dont
# le point d'entrée est `clap_entry`. L'extension du fichier fait partie du
# format, d'où le SUFFIX explicite.
add_library(vsm_clap_adapter MODULE
    adapter/VsmClapAdapter.cpp
)
set_target_properties(vsm_clap_adapter PROPERTIES
    OUTPUT_NAME "vsm-instruments"
    PREFIX ""
    SUFFIX ".clap"
)
target_link_libraries(vsm_clap_adapter PRIVATE vsm_interchange clap)
target_compile_features(vsm_clap_adapter PRIVATE cxx_std_20)

# --- P6 : hôte -- charger un plugin CLAP externe ---------------------------
# Bibliothèque statique : c'est l'application (et les tests) qui l'utilisent.
add_library(vsm_clap_host STATIC
    host/ClapPluginHost.cpp
)
target_include_directories(vsm_clap_host PUBLIC host)
target_link_libraries(vsm_clap_host PUBLIC vsm_audio vsm_interchange PRIVATE clap)
target_compile_features(vsm_clap_host PUBLIC cxx_std_20)
if (UNIX)
    target_link_libraries(vsm_clap_host PUBLIC ${CMAKE_DL_LIBS}) # dlopen
endif()

if (MSVC)
    target_compile_options(vsm_clap_adapter PRIVATE /W4)
    target_compile_options(vsm_clap_host PRIVATE /W4)
else()
    target_compile_options(vsm_clap_adapter PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(vsm_clap_host PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (VSM_BUILD_TESTS)
    # --- Un EFFET CLAP construit ici, pour tester l'hôte d'effets (D7.3) ----
    # L'adaptateur ci-dessus n'expose que des INSTRUMENTS : l'hôte d'effets
    # n'avait rien à héberger. Dépendre d'un effet CLAP installé sur la machine
    # rendrait le test vert ou rouge selon l'ordinateur.
    add_library(vsm_clap_test_effect MODULE tests/TestEffectPlugin.cpp)
    set_target_properties(vsm_clap_test_effect PROPERTIES
        OUTPUT_NAME "vsm-test-effect"
        PREFIX ""
        SUFFIX ".clap"
    )
    target_link_libraries(vsm_clap_test_effect PRIVATE clap)
    target_compile_features(vsm_clap_test_effect PRIVATE cxx_std_20)

    add_executable(vsm_clap_tests
        tests/test_main.cpp
        tests/test_clap_roundtrip.cpp
    )
    target_link_libraries(vsm_clap_tests PRIVATE vsm_clap_host)
    target_include_directories(vsm_clap_tests PRIVATE tests)
    # Les tests chargent l'adaptateur que l'on vient de construire : ils
    # valident les deux moitiés l'une par l'autre, sans plugin tiers.
    add_dependencies(vsm_clap_tests vsm_clap_adapter vsm_clap_test_effect)
    target_compile_definitions(vsm_clap_tests PRIVATE
        VSM_CLAP_ADAPTER_PATH="$<TARGET_FILE:vsm_clap_adapter>"
        VSM_CLAP_TEST_EFFECT_PATH="$<TARGET_FILE:vsm_clap_test_effect>")

    enable_testing()
    add_test(NAME vsm_clap_tests COMMAND vsm_clap_tests)
endif()
