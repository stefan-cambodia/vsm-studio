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

# UNE BIBLIOTHÈQUE **INTERFACE**, ET C'EST UNE NÉCESSITÉ, PAS UN GOÛT.
#
# Un module JUCE n'est pas une bibliothèque compilée à part : c'est une cible
# INTERFACE qui AJOUTE SES SOURCES à chaque cible qui la lie. Une bibliothèque
# statique qui lierait `juce_audio_processors` en emporterait donc une copie
# complète -- et l'application, qui lie `juce_audio_utils` (lequel dépend du
# même module), en emporterait une seconde. L'éditeur de liens refuse alors la
# fusion : « définitions multiples de juce::PluginListComponent::scanFor »,
# répété sur des centaines de symboles.
#
# En INTERFACE, notre source est compilée UNE FOIS, par la cible finale, avec
# la seule copie de JUCE qu'elle possède déjà. C'est le montage habituel pour
# du code qui s'appuie sur un module JUCE sans être lui-même une application.
add_library(vsm_vst3_host INTERFACE)
target_sources(vsm_vst3_host INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/host/Vst3PluginHost.cpp)
target_include_directories(vsm_vst3_host INTERFACE host)
target_link_libraries(vsm_vst3_host INTERFACE vsm_audio juce::juce_audio_processors)
target_compile_definitions(vsm_vst3_host INTERFACE
    # L'HÔTE VST3 DE JUCE EST OPTIONNEL À LA COMPILATION et désactivé par
    # défaut : sans cette définition, `VST3PluginFormat` existe mais ne trouve
    # jamais rien, ce qui est la pire des pannes -- silencieuse. Un `#error`
    # dans le .cpp refuse de compiler sans elle.
    JUCE_PLUGINHOST_VST3=1
)
target_compile_features(vsm_vst3_host INTERFACE cxx_std_20)

# PAS DE `-Wpedantic` ICI. Les options d'une cible INTERFACE se propagent à
# celle qui la consomme, et donc à tout JUCE : on imposerait à l'application
# des avertissements sur du code qui n'est pas le nôtre. Le code de ce dossier
# est compilé sous les options de son consommateur, qui les a déjà strictes.

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

    # Et un EFFET (D7.3), pour la moitié du critère qui parle d'entrées audio.
    # Séparé de l'instrument : un `.vst3` déclare ce qu'il est, et un plugin qui
    # serait les deux ne prouverait justement pas que la distinction est lue.
    juce_add_plugin(vsm_vst3_test_effect
        PRODUCT_NAME "VSM Test Effect"
        COMPANY_NAME "VSM Studio"
        PLUGIN_MANUFACTURER_CODE Vsms
        PLUGIN_CODE Vtef
        FORMATS VST3
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        VST3_CATEGORIES Fx
    )
    target_sources(vsm_vst3_test_effect PRIVATE tests/TestEffect.cpp)
    target_compile_definitions(vsm_vst3_test_effect PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
    )
    target_link_libraries(vsm_vst3_test_effect PRIVATE juce::juce_audio_utils)

    add_executable(vsm_vst3_tests
        tests/test_main.cpp
        tests/test_vst3_host.cpp
    )
    target_link_libraries(vsm_vst3_tests PRIVATE vsm_vst3_host vsm_interchange)
    target_include_directories(vsm_vst3_tests PRIVATE tests)
    add_dependencies(vsm_vst3_tests vsm_vst3_test_instrument vsm_vst3_test_effect)
    # LE CHEMIN EST NORMALISÉ PAR LE TEST, pas ici. `TARGET_FILE_DIR` désigne le
    # dossier du binaire, deux niveaux sous le `.vst3` ; remonter avec « /../.. »
    # donne un chemin qui ne se TERMINE plus par « .vst3 », et JUCE refuse alors
    # de le reconnaître comme un plugin -- sans rien dire, ce qui a coûté un bon
    # moment. Le test le normalise avec `std::filesystem`, qui sait le faire.
    target_compile_definitions(vsm_vst3_tests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        VSM_VST3_TEST_INSTRUMENT_BINARY_DIR="$<TARGET_FILE_DIR:vsm_vst3_test_instrument_VST3>"
        VSM_VST3_TEST_EFFECT_BINARY_DIR="$<TARGET_FILE_DIR:vsm_vst3_test_effect_VST3>")

    enable_testing()
    add_test(NAME vsm_vst3_tests COMMAND vsm_vst3_tests)
endif()
