#!/usr/bin/env bash
#
# Passe TOUS les garde-fous du dépôt, et dit ce qu'il n'a pas pu passer.
#
#     ./verifier.sh            les suites déjà compilées, puis Python, lint, types
#     ./verifier.sh --compiler compile d'abord les cibles de test (deux travaux)
#     ./verifier.sh --gardes   seulement les gardes des sources de tools/ (D378)
#     ./verifier.sh --bancs    seulement les bancs qui LANCENT l'application (D504)
#
# POURQUOI CE FICHIER. Les garde-fous existaient tous — cinq suites C++, une
# suite Python, `ruff check .` déclaré par `ruff.toml`, `mypy` déclaré par
# `mypy.ini` — et RIEN ne les lançait ensemble. Il n'y a pas d'intégration
# continue dans ce dépôt : la vérification est un geste, et un geste qui demande
# huit commandes n'est pas fait. Celui-ci en demande une.
#
# CE QU'IL NE FAIT PAS, ET C'EST DÉLIBÉRÉ. Il ne compile rien sans qu'on le lui
# demande. Remplacer `build/tools/vsm-render` pendant qu'une reconstruction
# tourne tue la course (règle du dépôt) ; et à `-j` élevé une compilation JUCE
# pendant une séparation demucs se fait tuer par le manque de mémoire. Avec
# `--compiler`, il compile à DEUX travaux, ce qui est la limite écrite.
#
# LES GARDES DE tools/ (D378). La règle du dépôt (D150) range dans tools/ ce
# qui doit empêcher une régression -- et ce fichier n'en lançait AUCUNE : une
# garde que rien ne rejoue n'empêche rien. Ne passent ici que celles qui lisent
# les SOURCES seules (ni application lancée, ni image, ni audio) et rendent 1 à
# la faute ; les bancs qui lancent l'application se jouent par `--bancs` (D504). `--gardes`
# ne passe qu'elles : elles tiennent en quelques secondes et peuvent tourner
# pendant une campagne, ce que la suite Python entière ne peut pas.
#
# Le code de sortie vaut 0 si tout ce qui a pu être passé est vert.

set -u
cd "$(dirname "$0")"

VENV=analyse/.venv/bin/python
ECHECS=0
SAUTES=0

titre()  { printf '\n\033[1m== %s\033[0m\n' "$1"; }
vert()   { printf '   \033[32m✓\033[0m %s\n' "$1"; }
rouge()  { printf '   \033[31m✗\033[0m %s\n' "$1"; ECHECS=$((ECHECS + 1)); }
saute()  { printf '   \033[33m—\033[0m %s\n' "$1"; SAUTES=$((SAUTES + 1)); }

GARDES_SEULES=0
[ "${1:-}" = "--gardes" ] && GARDES_SEULES=1

# D504 : LES BANCS QUI LANCENT L'APPLICATION. Ils se jouaient « à part » (D378),
# c'est-à-dire à la main, quatre ou cinq à la fois : rien ne rejouait les trente et un (trente-deux depuis D505),
# et un script que rien ne rejoue n'est pas une garde (D150). Liste FERMÉE, comme
# celle des gardes : un banc neuf n'y entre qu'écrit ici.
# HORS LISTE, ET POURQUOI : `reconstruction-annuler.sh` lance une vraie séparation
# demucs (son en-tête le réserve aux changements de la chaîne) ; `apres-campagne`,
# `comparer-rendus` et `garder-batterie` ne sont pas des gardes.
if [ "${1:-}" = "--bancs" ]; then
    titre "Bancs qui lancent l'application (D504)"
    # PAS PENDANT UNE CAMPAGNE : certains bancs exportent, et un export pendant une
    # course la triple (13/09). Le motif est dans CE fichier, pas dans la ligne de
    # commande d'un shell : `pgrep -f` ne se trouve pas lui-même ici.
    if course=$(pgrep -af "reconstruire\.py|corpus\.py|demucs" | head -1) && [ -n "$course" ]; then
        rouge "une campagne tourne — bancs NON passés : $course"
        exit 1
    fi
    BIN="./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio"
    [ -x "$BIN" ] || { rouge "binaire absent — ./verifier.sh --compiler, ou compiler la cible de l'application"; exit 1; }
    journaux="${TMPDIR:-/tmp}/vsm-verifier-bancs"
    mkdir -p "$journaux"
    # LES PRÉFÉRENCES DE L'UTILISATEUR, copiées JUSTE AVANT la série (D77 : jamais
    # contre une copie ancienne — l'application sert pendant qu'on travaille).
    prefs="$HOME/VintageSynthMidiStudio/VintageSynthMidiStudio.settings"
    [ -f "$prefs" ] && cp "$prefs" "$journaux/preferences-avant.settings"
    debut_serie=$(date +%s)
    passes=0
    for banc in arret-tete.sh automation-echelle.sh autosauvegarde-vue.sh balayer-facades.sh banc-fumee.sh \
                barre-transport.sh bascules-retenues.sh cadrage-ouverture.sh clavier-emprunte.sh \
                fader-console.sh grille-gamme-projet.sh liste-ajouter.sh liste-editer.sh \
                metronome-projet.sh miniature-clips.sh onglets-du-dock.sh ouvrir-midi.sh \
                pas-a-pas.sh pianoroll-zones.sh police-plancher.sh portes-de-l-arrangement.sh \
                portes-des-outils.sh portes-des-pistes.sh portes-du-transport.sh quantifier.sh \
                theme-sombre.sh tout-voir.sh transport-au-repos.sh volet-anglais.sh \
                vue-du-morceau.sh vumetre-console.sh zoom-reassigne.sh; do
        t0=$(date +%s)
        passes=$((passes + 1))
        if [ "$banc" = "balayer-facades.sh" ]; then
            "tools/$banc" "$journaux/facades.tsv" > "$journaux/$banc.log" 2>&1
        else
            "tools/$banc" > "$journaux/$banc.log" 2>&1
        fi
        rc=$?
        duree=$(( $(date +%s) - t0 ))
        if [ "$rc" -eq 0 ]; then vert "$banc (${duree} s) : $(grep -v '^\s*$' "$journaux/$banc.log" | tail -1 | cut -c1-90)"
        else rouge "$banc (${duree} s, code $rc) : $(grep -v '^\s*$' "$journaux/$banc.log" | tail -1 | cut -c1-90)"
             grep -E 'RATÉ|REFUS|rouge' "$journaux/$banc.log" | head -4 | sed 's/^/       /'
        fi
    done
    if [ -f "$journaux/preferences-avant.settings" ]; then
        if cmp -s "$prefs" "$journaux/preferences-avant.settings"
        then vert "préférences de l'utilisateur identiques après la série"
        else rouge "préférences de l'utilisateur MODIFIÉES pendant la série — lire clé par clé avant de conclure (D77) : $journaux/preferences-avant.settings"
        fi
    fi
    titre "Bilan"
    printf '   %d banc(s) en %d min, journaux dans %s\n' "$passes" $(( ($(date +%s) - debut_serie) / 60 )) "$journaux"
    if [ "$ECHECS" -eq 0 ]; then printf '   \033[32mTous les bancs sont verts.\033[0m\n'; exit 0; fi
    printf '   \033[31m%d en échec.\033[0m\n' "$ECHECS"
    exit 1
fi

if [ "${1:-}" = "--compiler" ]; then
    titre "Compilation des cibles de test (deux travaux)"
    # D387 : ET LES BANCS. Deux d'entre eux ne se compilaient plus depuis D364,
    # vingt-deux phases, parce que rien ne les bâtissait. Toutes les cibles
    # `vsm-*` que CMake connaît, SAUF `vsm-render` : c'est celui qu'une course
    # emploie, et le remplacer la tue (règle du dépôt).
    bancs=$(cmake --build build --target help 2>/dev/null | grep -o 'vsm-[a-z0-9-]*' \
            | sort -u | grep -vx 'vsm-render' | tr '\n' ' ')
    if cmake --build build -j 2 --target vsm_core_tests vsm_audio_tests \
             vsm_interchange_tests vsm_clap_tests vsm_panels_tests $bancs > /tmp/vsm-verifier-build.log 2>&1
    then vert "cinq cibles de test et $(echo $bancs | wc -w) bancs compilés ($bancs)"
    else rouge "compilation en échec — voir /tmp/vsm-verifier-build.log"
         grep 'Error' /tmp/vsm-verifier-build.log | grep -o 'CMakeFiles/[a-z0-9_-]*\.dir' | sort -u | sed 's/^/       /'
         exit 1
    fi
fi

if [ "$GARDES_SEULES" -eq 0 ]; then
titre "Suites du moteur (C++)"
for suite in core/vsm_core_tests audio/vsm_audio_tests interchange/vsm_interchange_tests \
             clap/vsm_clap_tests panels/vsm_panels_tests; do
    nom=$(basename "$suite")
    if [ ! -x "build/$suite" ]; then
        saute "$nom — pas compilée (relancer avec --compiler)"
        continue
    fi
    if sortie=$("./build/$suite" 2>&1); then vert "$nom : $(echo "$sortie" | tail -1)"
    else rouge "$nom : $(echo "$sortie" | tail -1)"
    fi
done

titre "Suite de la chaîne d'analyse (Python)"
if [ ! -x "$VENV" ]; then
    saute "environnement absent — voir analyse/requirements.txt"
else
    if sortie=$("$VENV" -u analyse/tests/run.py 2>&1); then
        vert "$(echo "$sortie" | tail -1)"
    else
        rouge "$(echo "$sortie" | tail -1)"
        echo "$sortie" | grep -o '^\[ÉCHEC\] [a-z_]*' | sed 's/^/       /'
    fi
fi

titre "Lint (ruff) et types (mypy)"
if [ -x analyse/.venv/bin/ruff ]; then
    if sortie=$(analyse/.venv/bin/ruff check --output-format=concise . 2>&1)
    then vert "ruff : aucun signalement"
    else rouge "ruff : $(echo "$sortie" | tail -1)"; echo "$sortie" | head -8 | sed 's/^/       /'
    fi
else
    saute "ruff absent — analyse/.venv/bin/python -m pip install ruff"
fi

if [ -x "$VENV" ] && "$VENV" -c "import mypy" 2>/dev/null; then
    if sortie=$("$VENV" -m mypy analyse tools 2>&1)
    then vert "mypy : $(echo "$sortie" | tail -1)"
    else rouge "mypy : $(echo "$sortie" | grep -c 'error:') erreur(s)"; echo "$sortie" | head -8 | sed 's/^/       /'
    fi
else
    saute "mypy absent — analyse/.venv/bin/python -m pip install mypy"
fi
fi   # GARDES_SEULES

titre "Gardes des sources (tools/, D378)"
for garde in "accents-francais.py" "clips-numerotes.py" "index-a-jour.py" \
             "inventaire_langue.py --garde" "inventaire_langue.py --doublons" \
             "menus-des-regles.py" "menus-cites.py" "noms-des-gestes.py" "raccourcis-affiches.py" \
             "tables-markdown.py" "pas-hors-glisse.py" "touches-modifiees.py" "virgule-saisie.py" \
             "touches-du-menu-edition.py"; do
    # Le nom et ses options se séparent ici : un seul mot passé à python
    # chercherait un fichier « inventaire_langue.py --garde ».
    read -r script options <<< "$garde"
    if [ ! -x "$VENV" ]; then saute "$garde — environnement absent"; continue; fi
    # shellcheck disable=SC2086
    if sortie=$("$VENV" "tools/$script" $options 2>&1); then vert "$garde : $(echo "$sortie" | tail -1)"
    else rouge "$garde : $(echo "$sortie" | tail -1)"; echo "$sortie" | grep -i 'raté\|écart\|faute' | head -5 | sed 's/^/       /'
    fi
done

titre "Bilan"
if [ "$SAUTES" -gt 0 ]; then
    printf '   %d garde-fou(s) SAUTÉ(S) — un garde-fou sauté ne garde rien.\n' "$SAUTES"
fi
if [ "$ECHECS" -eq 0 ]; then
    printf '   \033[32mTout ce qui a pu être passé est vert.\033[0m\n'
    exit 0
fi
printf '   \033[31m%d garde-fou(s) en échec.\033[0m\n' "$ECHECS"
exit 1
