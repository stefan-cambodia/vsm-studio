# Coquille JUCE de la Phase 1 (Piano Roll, Transport bar, Track list).
# Récupère le SDK JUCE via FetchContent : nécessite une connexion réseau au
# premier build, ainsi que les bibliothèques système habituelles
# (ALSA/X11/FreeType sous Linux, Xcode sous macOS, Visual Studio sous
# Windows). Voir la note en tête de Source/Main.cpp.

# JUCE est récupéré par le CMakeLists racine (voir `vsm_fetch_juce`) : la
# couche VST3 en a besoin elle aussi, et elle est définie AVANT ce dossier.
# Deux FetchContent pour la même dépendance se marcheraient dessus.

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
    Source/audio/DiskRecorder.cpp
    Source/audio/ReferenceAudioLoader.cpp
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
    Source/ui/ArrangementComponent.cpp
    Source/ui/AutomationComponent.cpp
    Source/ui/MidiCcComponent.cpp
    Source/ui/TempoLaneComponent.cpp
    Source/ui/EffectChainComponent.cpp
    Source/ui/ImportReportComponent.cpp
    Source/ui/LookAndFeel/VsmLookAndFeel.cpp
    Source/ui/UiScale.cpp
    Source/plugins/PluginScanner.cpp
    Source/reconstruction/ReconstructionRunner.cpp
    Source/project/AutosaveService.cpp
    Source/ui/ReconstructionWindow.cpp
    Source/ui/MidiLearnWindow.cpp
    Source/ui/ShortcutsWindow.cpp
    Source/ui/HistoryWindow.cpp
    Source/ui/PreferencesWindow.cpp
    Source/ui/BrowserComponent.cpp
)

target_compile_definitions(VintageSynthMidiStudio PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    # DÉCODEUR MP3 de JUCE, pour la piste de référence A/B.
    #
    # POURQUOI IL FAUT LE DEMANDER. `registerBasicFormats()` inscrit WAV et
    # AIFF toujours, FLAC et Ogg Vorbis par défaut, mais PAS le MP3 : JUCE le
    # laisse désactivé et l'assortit d'un avertissement (« at your own risk »)
    # à propos de brevets tiers. C'est un avertissement de 2012 ; les derniers
    # brevets MP3 ont expiré en 2017, et le décodeur est du code source livré
    # avec JUCE, sans binaire ni service extérieur.
    #
    # POURQUOI ON LE VEUT. La chaîne d'analyse part de fichiers du commerce,
    # qui sont des MP3 -- refuser le format obligerait à convertir chaque
    # morceau à la main avant de pouvoir le comparer à sa reconstruction.
    # Décoder ici ne coûte AUCUNE dépendance nouvelle : le dépôt se compile
    # toujours hors ligne, JUCE mis à part, et le moteur audio ignore tout de
    # cette histoire.
    JUCE_USE_MP3AUDIOFORMAT=1
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

# D7.1 : L'HÔTE CLAP EST BRANCHÉ QUAND IL EXISTE. Sans lui, l'application se
# compile et fonctionne à l'identique -- l'entrée de menu n'apparaît simplement
# pas, plutôt que d'apparaître grisée pour une raison qu'on ne saurait pas
# expliquer. Le SDK CLAP reste facultatif, et `audio/` continue de ne rien en
# savoir : c'est `installClapResolver()` qui se pose dans le registre, jamais le
# registre qui va chercher CLAP.
if (TARGET vsm_clap_host)
    target_link_libraries(VintageSynthMidiStudio PRIVATE vsm_clap_host)
    target_compile_definitions(VintageSynthMidiStudio PRIVATE VSM_WITH_CLAP=1)
    # D7.4 : la façade native, qui vit dans une cible à part parce qu'elle est
    # la seule moitié de l'hôte CLAP à avoir besoin de JUCE.
    if (TARGET vsm_clap_window)
        target_link_libraries(VintageSynthMidiStudio PRIVATE vsm_clap_window)
    endif()
endif()

# D7.2 : et l'hôte VST3, quand il est compilé. Même règle que pour CLAP -- sans
# lui, l'application se compile et fonctionne à l'identique.
if (TARGET vsm_vst3_host)
    target_link_libraries(VintageSynthMidiStudio PRIVATE vsm_vst3_host)
    target_compile_definitions(VintageSynthMidiStudio PRIVATE VSM_WITH_VST3=1)
endif()

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
    Source/ui/PianoRollRulerComponent.cpp
)
target_compile_definitions(vsm-pianoroll-preview PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
)
target_link_libraries(vsm-pianoroll-preview PRIVATE
    # `vsm_interchange` depuis D10.3 : le piano roll ne décide plus quelle
    # touche fait quoi, il lit la table des raccourcis, qui vit là.
    vsm_core vsm_audio vsm_interchange
    juce::juce_gui_basics juce::juce_graphics
)
juce_generate_juce_header(vsm-pianoroll-preview)


# --- Vérification du chargement d'un enregistrement d'origine ---------------
# Le chargement de la référence A/B est du code d'interface, et l'interface ne
# se teste pas sans écran -- mais ce qu'on veut savoir de lui (ce fichier
# est-il décodé, à quelle fréquence, sur quelle durée) ne demande aucune
# fenêtre. Cet outil appelle EXACTEMENT le code du menu Fichier, hors écran,
# sur les fichiers qu'on lui donne.
juce_add_console_app(vsm-audio-import-check PRODUCT_NAME "vsm-audio-import-check")
target_sources(vsm-audio-import-check PRIVATE
    Source/tools/AudioImportCheckMain.cpp
    Source/audio/ReferenceAudioLoader.cpp
)
target_include_directories(vsm-audio-import-check PRIVATE Source)
target_compile_definitions(vsm-audio-import-check PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
    # Même décodeur que l'application : vérifier autre chose que ce qui tourne
    # dans l'interface ne vérifierait rien.
    JUCE_USE_MP3AUDIOFORMAT=1
)
target_link_libraries(vsm-audio-import-check PRIVATE
    vsm_audio
    juce::juce_audio_formats
)
juce_generate_juce_header(vsm-audio-import-check)


# --- Vérification de l'enregistrement audio en flux (D3.4) ------------------
# Le critère de D3.4 est « 10 minutes s'enregistrent sans décrochage ; le
# fichier est relu tel quel ». Ce n'est pas une propriété qu'on peut lire dans
# le code : elle dépend du disque et du thread d'écriture. Elle ne demande en
# revanche ni micro ni écran, puisqu'on peut fabriquer le signal d'entrée.
juce_add_console_app(vsm-disk-record-check PRODUCT_NAME "vsm-disk-record-check")
target_sources(vsm-disk-record-check PRIVATE
    Source/tools/DiskRecordCheckMain.cpp
    Source/audio/DiskRecorder.cpp
)
target_include_directories(vsm-disk-record-check PRIVATE Source)
target_compile_definitions(vsm-disk-record-check PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
)
target_link_libraries(vsm-disk-record-check PRIVATE
    juce::juce_audio_formats
)
juce_generate_juce_header(vsm-disk-record-check)


# --- Vérification de la mesure de latence (D3.6) ----------------------------
# Le critère est « une boucle physique enregistre à l'échantillon près ». La
# boucle demande un câble ; tout ce qui se trouve entre le câble et le résultat,
# non. On branche donc un faux périphérique qui renvoie la sortie dans l'entrée
# avec un retard connu, et on regarde si l'application le retrouve.
juce_add_console_app(vsm-latency-check PRODUCT_NAME "vsm-latency-check")
target_sources(vsm-latency-check PRIVATE
    Source/tools/LatencyCheckMain.cpp
    Source/audio/AudioEngine.cpp
    Source/audio/DiskRecorder.cpp
)
target_include_directories(vsm-latency-check PRIVATE Source)
target_compile_definitions(vsm-latency-check PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
)
target_link_libraries(vsm-latency-check PRIVATE
    vsm_audio
    vsm_core
    juce::juce_audio_devices
    juce::juce_audio_formats
)
juce_generate_juce_header(vsm-latency-check)


# --- Mesure d'un fichier avec le code du mixeur (D4.7) ----------------------
# Le critère de la phase D4 veut que l'application et `analyse/` donnent le même
# LUFS à 0,1 près sur les mêmes stems. Deux moitiés qui mesurent différemment ne
# peuvent pas se comparer. Cet outil emploie LE code du mixeur -- pas une redite
# écrite pour l'occasion, qui ne prouverait que sa propre justesse.
juce_add_console_app(vsm-measure PRODUCT_NAME "vsm-measure")
target_sources(vsm-measure PRIVATE Source/tools/MeasureMain.cpp)
target_include_directories(vsm-measure PRIVATE Source)
target_compile_definitions(vsm-measure PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
)
target_link_libraries(vsm-measure PRIVATE
    vsm_audio
    juce::juce_audio_formats
)
juce_generate_juce_header(vsm-measure)


# --- Aperçu de la vue d'arrangement, sans écran (D5.1) ----------------------
# Même raison que vsm-pianoroll-preview : ce qu'on ne peut pas regarder, on ne
# peut pas le juger. Une vue d'arrangement se juge d'abord à l'oeil.
juce_add_console_app(vsm-arrangement-preview PRODUCT_NAME "vsm-arrangement-preview")
target_sources(vsm-arrangement-preview PRIVATE
    Source/tools/ArrangementPreviewMain.cpp
    Source/ui/ArrangementComponent.cpp
    Source/ui/LookAndFeel/VsmLookAndFeel.cpp
)
target_include_directories(vsm-arrangement-preview PRIVATE Source)
target_compile_definitions(vsm-arrangement-preview PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
)
target_link_libraries(vsm-arrangement-preview PRIVATE
    vsm_core
    vsm_audio
    juce::juce_gui_basics
    juce::juce_graphics
)
juce_generate_juce_header(vsm-arrangement-preview)

# --- Aperçu hors écran des panneaux de l'application (D10) -------------------
# Même raison d'être que `vsm-panel-preview` : une fenêtre qu'on ne regarde
# jamais est une fenêtre qu'on ne peut pas juger. Les tests disent que la
# logique est juste ; ils ne diront jamais qu'une colonne déborde.
juce_add_console_app(vsm-ui-preview PRODUCT_NAME "vsm-ui-preview")
target_sources(vsm-ui-preview PRIVATE
    Source/tools/UiPreviewMain.cpp
    Source/ui/ShortcutsWindow.cpp
    Source/ui/HistoryWindow.cpp
    Source/ui/MidiLearnWindow.cpp
    Source/ui/PreferencesWindow.cpp
    Source/ui/BrowserComponent.cpp
    Source/ui/ReconstructionWindow.cpp
    Source/ui/ImportReportComponent.cpp
    Source/ui/LookAndFeel/VsmLookAndFeel.cpp
    Source/ui/UiScale.cpp
    Source/reconstruction/ReconstructionRunner.cpp
)
target_compile_definitions(vsm-ui-preview PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
)
target_link_libraries(vsm-ui-preview PRIVATE
    vsm_core vsm_audio vsm_interchange
    juce::juce_gui_basics juce::juce_graphics
)
juce_generate_juce_header(vsm-ui-preview)

# --- Ouvrir la façade native d'un plugin CLAP, pour de bon (D7.4) -----------
# D7.4 avait différé cette façade en écrivant qu'il ne fallait pas la déclarer
# faite sans l'avoir vue tourner. Cet outil est ce qui le permet : il emprunte
# EXACTEMENT le chemin du menu de l'application, sur n'importe quel `.clap`.
if (TARGET vsm_clap_window)
    juce_add_gui_app(vsm-clap-gui-check PRODUCT_NAME "vsm-clap-gui-check")
    target_sources(vsm-clap-gui-check PRIVATE Source/tools/ClapGuiCheckMain.cpp)
    target_compile_definitions(vsm-clap-gui-check PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        # CET OUTIL A BESOIN DE TOURNER LA BOUCLE DE MESSAGES À LA MAIN : il
        # ouvre une fenêtre, la laisse vivre quelques secondes, puis rend la
        # main. C'est ce que JUCE appelle une boucle modale, et il faut le lui
        # demander. L'application, elle, ne l'active pas : une boucle modale
        # dans un DAW est un rappel audio qu'on fait attendre.
        JUCE_MODAL_LOOPS_PERMITTED=1
    )
    target_link_libraries(vsm-clap-gui-check PRIVATE
        vsm_clap_window vsm_clap_host
        juce::juce_gui_basics juce::juce_graphics
    )
    juce_generate_juce_header(vsm-clap-gui-check)
endif()

# `NumberText.h` est un en-tête SEUL : ces outils y accèdent sans lier
# `vsm_interchange`, dont ils n'ont besoin de rien d'autre. La règle qu'il porte
# -- un nombre qui traverse une frontière se lit en locale C -- vaut pour un
# argument de ligne de commande comme pour un fichier.
# `if(TARGET)` parce que `vsm-clap-gui-check` n'existe que si CLAP est activé.
foreach(outil vsm-panel-preview vsm-ui-preview vsm-clap-gui-check
              vsm-latency-check vsm-disk-record-check)
    if(TARGET ${outil})
        target_include_directories(${outil} PRIVATE ${CMAKE_SOURCE_DIR}/interchange/include)
    endif()
endforeach()
