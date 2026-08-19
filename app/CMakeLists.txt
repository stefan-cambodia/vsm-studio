# Coquille JUCE de la Phase 1 (Piano Roll, Transport bar, Track list).
# Récupère le SDK JUCE via FetchContent : nécessite une connexion réseau au
# premier build, ainsi que les bibliothèques système habituelles
# (ALSA/X11/FreeType sous Linux, Xcode sous macOS, Visual Studio sous
# Windows). Voir la note en tête de Source/Main.cpp.

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(VintageSynthMidiStudio
    PRODUCT_NAME "Vintage Synth MIDI Studio"
    COMPANY_NAME "VSM Studio"
    BUNDLE_ID    com.vsmstudio.vintagesynthmidistudio
    VERSION      0.1.0
)

target_sources(VintageSynthMidiStudio PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/audio/AudioEngine.cpp
    Source/ui/PanelWindow.cpp
    Source/ui/TransportBarComponent.cpp
    Source/ui/TrackListComponent.cpp
    Source/ui/PianoRollComponent.cpp
    Source/ui/PianoRollRulerComponent.cpp
    Source/ui/PianoRollToolbar.cpp
    Source/ui/VelocityLaneComponent.cpp
    Source/ui/SynthRackComponent.cpp
    Source/ui/machines/MachinePanelComponent.cpp
    Source/ui/machines/StepSequencerComponent.cpp
    Source/ui/machines/HardwareLookAndFeel.cpp
    Source/ui/MixerComponent.cpp
    Source/ui/AutomationComponent.cpp
    Source/ui/EffectChainComponent.cpp
    Source/ui/LookAndFeel/VsmLookAndFeel.cpp
)

target_compile_definitions(VintageSynthMidiStudio PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
)

target_link_libraries(VintageSynthMidiStudio
    PRIVATE
        vsm_core
        vsm_audio
        vsm_panels
        # interop : lecture des dossiers de projet (project.json + MIDI +
        # presets + échantillons). L'application n'en dépend que pour
        # OUVRIR ; elle ne connaît toujours pas le format en interne.
        vsm_interchange
        juce::juce_gui_extra
        juce::juce_audio_utils
        juce::juce_audio_devices
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

juce_generate_juce_header(VintageSynthMidiStudio)

# --- Outil de prévisualisation des façades (rendu PNG hors écran) ----------
# Sert à REGARDER les façades sans lancer l'application et sans serveur
# graphique : les tests garantissent la cohérence des données (aucune commande
# orpheline, aucun chevauchement), jamais la lisibilité du résultat.
juce_add_console_app(vsm-panel-preview PRODUCT_NAME "vsm-panel-preview")
target_sources(vsm-panel-preview PRIVATE
    Source/tools/PanelPreviewMain.cpp
    Source/ui/machines/MachinePanelComponent.cpp
    Source/ui/machines/StepSequencerComponent.cpp
    Source/ui/machines/HardwareLookAndFeel.cpp
    Source/ui/machines/StepSequencerComponent.cpp
)
target_compile_definitions(vsm-panel-preview PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
)
target_link_libraries(vsm-panel-preview PRIVATE
    vsm_core vsm_audio vsm_panels
    juce::juce_gui_basics juce::juce_graphics
)
juce_generate_juce_header(vsm-panel-preview)

# Aperçu hors écran du piano roll : même raison d'être que celui des façades --
# un marquage qu'on ne regarde jamais est un marquage qu'on ne peut pas juger.
juce_add_console_app(vsm-pianoroll-preview PRODUCT_NAME "vsm-pianoroll-preview")
target_sources(vsm-pianoroll-preview PRIVATE
    Source/tools/PianoRollPreviewMain.cpp
    Source/ui/PianoRollComponent.cpp
)
target_compile_definitions(vsm-pianoroll-preview PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
)
target_link_libraries(vsm-pianoroll-preview PRIVATE
    vsm_core vsm_audio
    juce::juce_gui_basics juce::juce_graphics
)
juce_generate_juce_header(vsm-pianoroll-preview)

