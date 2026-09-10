#!/usr/bin/env bash
#
# Passe TOUS les garde-fous du dépôt, et dit ce qu'il n'a pas pu passer.
#
#     ./verifier.sh            les suites déjà compilées, puis Python, lint, types
#     ./verifier.sh --compiler compile d'abord les cibles de test (deux travaux)
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

if [ "${1:-}" = "--compiler" ]; then
    titre "Compilation des cibles de test (deux travaux)"
    if cmake --build build -j 2 --target vsm_core_tests vsm_audio_tests \
             vsm_interchange_tests vsm_clap_tests vsm_panels_tests > /tmp/vsm-verifier-build.log 2>&1
    then vert "cinq cibles compilées"
    else rouge "compilation en échec — voir /tmp/vsm-verifier-build.log"; exit 1
    fi
fi

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
