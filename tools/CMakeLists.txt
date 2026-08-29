# Outils en ligne de commande de la couche d'interopérabilité (Phase 7).
# `vsm-render` rend un dossier de projet en WAV sans interface ni carte son :
# c'est le point d'entrée du « Mode A », celui qu'un script Python appelle.
add_executable(vsm-render vsm_render_main.cpp)
target_link_libraries(vsm-render PRIVATE vsm_interchange)

# D7.1 : `vsm-render` accepte les plugins tiers quand la couche CLAP est
# compilée. SANS ELLE IL SE COMPILE ET FONCTIONNE À L'IDENTIQUE -- un projet qui
# porte un plugin CLAP signale simplement l'instrument absent, comme pour toute
# machine introuvable. Le rendu de l'application et celui de la ligne de commande
# doivent accepter les mêmes projets ; sans cela, exporter depuis l'un et
# l'autre ne donnerait pas le même fichier.
if (TARGET vsm_clap_host)
    target_link_libraries(vsm-render PRIVATE vsm_clap_host)
    target_compile_definitions(vsm-render PRIVATE VSM_WITH_CLAP=1)
endif()

# D7.2 : même chose pour l'hôte VST3. Les deux familles cohabitent -- leurs
# résolveurs s'enchaînent au lieu de s'écraser.
if (TARGET vsm_vst3_host)
    target_link_libraries(vsm-render PRIVATE vsm_vst3_host)
    target_compile_definitions(vsm-render PRIVATE
        VSM_WITH_VST3=1
        # `vsm-render` n'a par ailleurs rien de JUCE : il lui faut les mêmes
        # renoncements que l'application, sans quoi JUCE va chercher un
        # navigateur et curl dans un outil qui rend des WAV.
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
    )
endif()

# `vsm-sf2` convertit une banque SoundFont en profil pour vsm.multisample. Outil
# d'INSTALLATION, appelé une fois : le DAW, lui, ne charge jamais qu'un profil.
add_executable(vsm-sf2 vsm_sf2_main.cpp)
target_link_libraries(vsm-sf2 PRIVATE vsm_interchange)

if (MSVC)
    target_compile_options(vsm-render PRIVATE /W4)
    target_compile_options(vsm-sf2 PRIVATE /W4)
else()
    target_compile_options(vsm-render PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(vsm-sf2 PRIVATE -Wall -Wextra -Wpedantic)
endif()
