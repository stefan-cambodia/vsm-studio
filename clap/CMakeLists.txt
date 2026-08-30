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

# --- D7.4 : la façade native, qui a besoin de JUCE -------------------------
# SÉPARÉE DE `vsm_clap_host`, et pour la même raison que du côté VST3 : l'hôte
# ne parle que d'`ISynthPlugin`, ce qui permet à `vsm-render` et aux tests de
# l'employer sans emporter une bibliothèque graphique. Une façade est du
# dessin ; elle n'a de sens que dans l'application.
if (TARGET juce::juce_gui_basics)
    add_library(vsm_clap_window STATIC host/ClapPluginWindow.cpp)
    target_include_directories(vsm_clap_window PUBLIC host)
    target_link_libraries(vsm_clap_window PUBLIC vsm_clap_host juce::juce_gui_basics PRIVATE clap)
    target_compile_features(vsm_clap_window PUBLIC cxx_std_20)
    if (NOT MSVC)
        target_compile_options(vsm_clap_window PRIVATE -Wall -Wextra -Wpedantic)
    endif()
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

    # --- Un INSTRUMENT CLAP à interface X11, pour éprouver la façade (D7.4) -
    # Aucun plugin CLAP tiers à interface n'est installé sur la machine de
    # développement, et en exiger un rendrait la vérification dépendante de
    # l'ordinateur -- exactement ce que l'effet d'essai ci-dessus refusait
    # déjà. Construit uniquement là où X11 existe : sur les autres systèmes,
    # la façade CLAP se vérifie avec leur propre API de fenêtrage, et ce
    # fichier n'aurait rien à dire.
    find_package(X11 QUIET)
    if (X11_FOUND)
        add_library(vsm_clap_test_gui MODULE tests/TestGuiPlugin.cpp)
        set_target_properties(vsm_clap_test_gui PROPERTIES
            OUTPUT_NAME "vsm-test-gui"
            PREFIX ""
            SUFFIX ".clap"
        )
        target_link_libraries(vsm_clap_test_gui PRIVATE clap ${X11_LIBRARIES})
        target_include_directories(vsm_clap_test_gui PRIVATE ${X11_INCLUDE_DIR})
        target_compile_features(vsm_clap_test_gui PRIVATE cxx_std_20)
    endif()

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
    if (TARGET vsm_clap_test_gui)
        add_dependencies(vsm_clap_tests vsm_clap_test_gui)
        target_compile_definitions(vsm_clap_tests PRIVATE
            VSM_CLAP_TEST_GUI_PATH="$<TARGET_FILE:vsm_clap_test_gui>")
    endif()

    enable_testing()
    add_test(NAME vsm_clap_tests COMMAND vsm_clap_tests)
endif()
