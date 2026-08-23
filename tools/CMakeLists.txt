# Outils en ligne de commande de la couche d'interopérabilité (Phase 7).
# `vsm-render` rend un dossier de projet en WAV sans interface ni carte son :
# c'est le point d'entrée du « Mode A », celui qu'un script Python appelle.
add_executable(vsm-render vsm_render_main.cpp)
target_link_libraries(vsm-render PRIVATE vsm_interchange)

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
