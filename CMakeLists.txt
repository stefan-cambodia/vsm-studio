cmake_minimum_required(VERSION 3.22)
# LANGUAGES CXX C : le C est nécessaire même si tout le code du projet est en
# C++ -- JUCE embarque des sources C tierces (Sheenbidi, pour le texte
# bidirectionnel) dans le module juce_graphics. Sans le C déclaré ici, dès
# le départ, au niveau du project() racine, CMake ne configure jamais les
# règles de compilation C (variable interne CMAKE_C_COMPILE_OBJECT absente),
# ce qui casse la génération des Makefiles pour TOUT le arbre -- y compris
# les cibles C++ qui n'en ont pourtant pas besoin. L'ajouter après coup dans
# un dossier de build déjà configuré ne suffit pas non plus (CMake ne
# réévalue pas fiablement les langages sur un cache existant) : si vous avez
# un ancien build/ configuré avant ce correctif, faites `rm -rf build`.
project(VintageSynthMidiStudio VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# --- vsm_core : moteur MIDI/séquenceur, aucune dépendance externe --------
# Compile et testable partout (y compris headless / CI), c'est la base sur
# laquelle tout le reste (audio, plugins, UI) viendra se greffer.
option(VSM_BUILD_TESTS "Compiler les tests unitaires de vsm_core et vsm_audio" ON)

# --- CLAP (Phase 7, P5-P6) : option déclarée ICI, avant les sous-répertoires.
# Un plugin CLAP est un MODULE PARTAGÉ, et un module partagé ne peut pas lier
# des bibliothèques statiques compilées sans code indépendant de la position.
# Le PIC doit donc être activé avant que core/, audio/ et interchange/ ne
# soient définis -- d'où la déclaration en tête plutôt qu'à côté du
# add_subdirectory(clap). Sans CLAP, rien ne change pour le reste du projet.
option(VSM_BUILD_CLAP "Compiler la couche CLAP (nécessite de télécharger le SDK CLAP)" OFF)
if (VSM_BUILD_CLAP)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()
# Banc de mesure CPU du moteur audio (Phase 6). OFF par défaut : c'est un outil
# de mesure, pas une garantie de correction -- il n'a pas sa place dans un
# build ordinaire ni dans une CI, où il ne ferait qu'ajouter du temps et du
# bruit (chiffres dépendants de la machine et de sa charge).
option(VSM_BUILD_BENCH "Compiler le banc de mesure CPU du moteur audio (Phase 6)" OFF)

# --- VST3 (D7.2) : héberger les instruments des autres -------------------
# OFF par défaut, pour la même raison que CLAP : c'est la seule autre partie du
# projet qui exige un téléchargement. Elle n'en exige toutefois PAS un de plus
# quand l'application est compilée -- JUCE 8 embarque le SDK VST3, et
# `juce_audio_processors` sait déjà héberger. Le marché est en VST3 ; refuser
# de le lire au motif que CLAP est plus propre reviendrait à n'héberger
# personne.
option(VSM_BUILD_VST3 "Compiler l'hôte VST3 (nécessite JUCE — téléchargé depuis GitHub au premier configure, voir VSM_BUILD_APP)" OFF)

# JUCE, RÉCUPÉRÉ UNE SEULE FOIS ET ICI. Il servait jusqu'ici à `app/` seul, qui
# le récupérait lui-même ; la couche VST3 en a besoin AVANT, puisque `tools/` et
# `app/` la lient tous deux. Deux FetchContent pour la même dépendance se
# marcheraient dessus.
if (VSM_BUILD_APP OR VSM_BUILD_VST3)
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

add_subdirectory(core)

# --- vsm_audio : moteur audio temps réel (Phase 2), aucune dépendance ----
# externe hormis vsm_core. Comme vsm_core, compile et se teste partout,
# y compris sans carte son ni serveur d'affichage (aucun juce::AudioDevice
# ici -- ça, c'est le rôle du wiring dans app/, voir app/Source/audio/).
add_subdirectory(audio)

# --- interchange : couche d'interopérabilité (Phase 7) -------------------
# Formats d'échange (*.synth.json, project.json), identités sémantiques des
# paramètres, et plus tard les adaptateurs CLAP. Seule couche autorisée à
# connaître JSON ; le moteur continue de l'ignorer complètement.
add_subdirectory(panels)
add_subdirectory(interchange)
# --- clap : adaptateur et hôte CLAP (Phase 7, P5-P6) ---------------------
# OFF par défaut : seule partie du projet qui exige un téléchargement (le SDK
# CLAP). Tout le reste doit rester compilable et testable hors ligne.
if (VSM_BUILD_CLAP)
    add_subdirectory(clap)
endif()

# --- vst3 : hôte VST3 (D7.2) ---------------------------------------------
if (VSM_BUILD_VST3)
    add_subdirectory(vst3)
endif()

# AVANT `tools` et `app` (D7.1, D7.2) : tous deux lient les hôtes CLAP et VST3
# quand ils existent, et CMake exige que la cible soit déjà définie.
add_subdirectory(tools)


# --- app : coquille JUCE (Piano Roll, Transport bar, Track list) --------
# Désactivée par défaut : nécessite le SDK JUCE (récupéré via
# FetchContent depuis GitHub) ainsi que les bibliothèques de rendu/audio
# du système (X11/ALSA sous Linux, CoreAudio sous macOS, etc.), qui ne sont
# pas disponibles dans tous les environnements (ex: conteneur headless).
# Sur une machine de développement standard : cmake -B build -DVSM_BUILD_APP=ON
option(VSM_BUILD_APP "Compiler l'application desktop JUCE (télécharge JUCE depuis GitHub au premier configure ; hors ligne, désigner une copie locale par -DFETCHCONTENT_SOURCE_DIR_JUCE=...)" OFF)
if (VSM_BUILD_APP)
    add_subdirectory(app)
endif()
