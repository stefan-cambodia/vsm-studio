# Outils en ligne de commande de la couche d'interopérabilité (Phase 7).
# `vsm-render` rend un dossier de projet en WAV sans interface ni carte son :
# c'est le point d'entrée du « Mode A », celui qu'un script Python appelle.
add_executable(vsm-render vsm_render_main.cpp)
target_link_libraries(vsm-render PRIVATE vsm_interchange)

if (MSVC)
    target_compile_options(vsm-render PRIVATE /W4)
else()
    target_compile_options(vsm-render PRIVATE -Wall -Wextra -Wpedantic)
endif()
