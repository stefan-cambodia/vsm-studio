# vsm_panels : description des façades « façon hardware », machine par machine.
#
# Ne dépend NI de JUCE NI du moteur audio : ce sont des données (quelles
# commandes, de quel type, à quelle place). C'est ce qui permet de les
# vérifier par des tests, sans serveur graphique -- notamment qu'aucune
# commande ne pointe vers un paramètre inexistant et qu'aucun paramètre ne
# reste inatteignable.
add_library(vsm_panels STATIC
    src/MachinePanels.cpp
)

target_include_directories(vsm_panels PUBLIC include)
target_compile_features(vsm_panels PUBLIC cxx_std_20)

if (MSVC)
    target_compile_options(vsm_panels PRIVATE /W4)
else()
    target_compile_options(vsm_panels PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (VSM_BUILD_TESTS)
    add_executable(vsm_panels_tests
        tests/test_main.cpp
        tests/test_machine_panels.cpp
    )
    # Les tests lient vsm_audio pour confronter chaque façade aux VRAIS
    # paramètres de sa machine : une façade juste "cohérente avec elle-même"
    # ne prouverait rien.
    target_link_libraries(vsm_panels_tests PRIVATE vsm_panels vsm_audio)
    target_include_directories(vsm_panels_tests PRIVATE tests)

    enable_testing()
    add_test(NAME vsm_panels_tests COMMAND vsm_panels_tests)
endif()
