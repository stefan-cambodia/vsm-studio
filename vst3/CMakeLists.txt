# Couche VST3 (D7.2) : héberger les instruments des autres.
#
# DÉSACTIVÉE PAR DÉFAUT, comme CLAP, parce qu'elle exige JUCE -- donc un
# téléchargement. Elle n'en exige toutefois AUCUN de plus dès que l'application
# est compilée : JUCE 8 embarque le SDK VST3, et `juce_audio_processors` sait
# déjà héberger. Ni core/, ni audio/, ni interchange/ ne connaissent ce dossier.
#
# POURQUOI VST3 ALORS QUE CLAP EST PLUS PROPRE : parce que le marché est en
# VST3. Un hôte qui n'accepterait que le format qu'il préfère n'hébergerait
# personne.

add_library(vsm_vst3_host STATIC
    host/Vst3PluginHost.cpp
)
target_include_directories(vsm_vst3_host PUBLIC host)
target_link_libraries(vsm_vst3_host
    PUBLIC vsm_audio
    PRIVATE juce::juce_audio_processors
)
target_compile_definitions(vsm_vst3_host PRIVATE
    # L'HÔTE VST3 DE JUCE EST OPTIONNEL À LA COMPILATION et désactivé par
    # défaut : sans cette définition, `VST3PluginFormat` existe mais ne trouve
    # jamais rien, ce qui est la pire des pannes -- silencieuse.
    JUCE_PLUGINHOST_VST3=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0
)
target_compile_features(vsm_vst3_host PUBLIC cxx_std_20)

if (MSVC)
    target_compile_options(vsm_vst3_host PRIVATE /W4)
else()
    target_compile_options(vsm_vst3_host PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (VSM_BUILD_TESTS)
    # --- Un instrument VST3 construit ICI, pour se tester soi-même ---------
    #
    # MÊME CIRCUIT FERMÉ QUE POUR CLAP, et pour la même raison : l'hôte charge
    # un `.vst3` que ce dépôt vient de construire. Aucun plugin tiers n'est
    # nécessaire pour prouver que l'hôte fonctionne -- et le jour où un plugin
    # tiers pose problème, on saura que le défaut vient de lui, puisque ce
    # circuit est vert.
    #
    # Ce plugin de test n'est PAS une machine du parc : il ne s'enregistre nulle
    # part, ne sort pas de ce dossier, et n'existe que pour être hébergé.
    juce_add_plugin(vsm_vst3_test_instrument
        PRODUCT_NAME "VSM Test Instrument"
        COMPANY_NAME "VSM Studio"
        PLUGIN_MANUFACTURER_CODE Vsms
        PLUGIN_CODE Vtin
        FORMATS VST3
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        VST3_CATEGORIES Instrument Synth
    )
    target_sources(vsm_vst3_test_instrument PRIVATE tests/TestInstrument.cpp)
    target_compile_definitions(vsm_vst3_test_instrument PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
    )
    target_link_libraries(vsm_vst3_test_instrument PRIVATE juce::juce_audio_utils)

    add_executable(vsm_vst3_tests
        tests/test_main.cpp
        tests/test_vst3_host.cpp
    )
    target_link_libraries(vsm_vst3_tests PRIVATE vsm_vst3_host vsm_interchange)
    target_include_directories(vsm_vst3_tests PRIVATE tests)
    add_dependencies(vsm_vst3_tests vsm_vst3_test_instrument)
    # LE CHEMIN EST NORMALISÉ PAR LE TEST, pas ici. `TARGET_FILE_DIR` désigne le
    # dossier du binaire, deux niveaux sous le `.vst3` ; remonter avec « /../.. »
    # donne un chemin qui ne se TERMINE plus par « .vst3 », et JUCE refuse alors
    # de le reconnaître comme un plugin -- sans rien dire, ce qui a coûté un bon
    # moment. Le test le normalise avec `std::filesystem`, qui sait le faire.
    target_compile_definitions(vsm_vst3_tests PRIVATE
        VSM_VST3_TEST_INSTRUMENT_BINARY_DIR="$<TARGET_FILE_DIR:vsm_vst3_test_instrument_VST3>")

    enable_testing()
    add_test(NAME vsm_vst3_tests COMMAND vsm_vst3_tests)
endif()
