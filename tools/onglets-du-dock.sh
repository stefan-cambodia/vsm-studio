#!/usr/bin/env bash
# La garde de D32.2 et D349 : UN VERBE D'ONGLET OUVRE L'ONGLET QU'IL NOMME.
#
# RÈGLE GARDÉE (18/09/2026). Les six onglets du dock du bas se désignent par
# leur NOM, jamais par un numéro. D32.2 l'avait écrit — « insérer "Liste" avant
# "Tempo" a décalé ce dernier d'un rang, et `VSM_VUE=tempo` ouvrait la liste :
# un numéro en dur est un piège qui se referme au premier onglet ajouté » — et
# n'avait corrigé que les DEUX onglets qu'il ajoutait. Les quatre autres
# (`mixer`, `automation`, `effets`, `midi-cc`) sont restés sur
# `setCurrentTabIndex(0..3)` pendant seize phases : le piège nommé était encore
# armé quatre fois sur six, et invisible, parce que les numéros étaient JUSTES
# ce jour-là.
#
# UNE GARDE NE PEUT PAS LIRE LE CODE : elle lit l'ÉCART entre ce qu'on demande
# et ce qu'on obtient. Le relevé dit les deux (« "Effets" demandé, "Effets"
# obtenu »), et c'est cet écart qu'un numéro en dur finirait par produire.
#
# LES DEUX LANGUES, parce que le nom cherché passe par `tr()` : « Effets »
# devient « Effects » et « Liste » devient « List » (la leçon de D301, où un
# libellé écrit en dur ne trouvait plus son onglet en anglais).
#
# ET LA LISTE DES VERBES DE VUE (`VSM_VUE=?`) EST REJOUÉE : chacun de ceux qui
# n'attendent pas d'argument est lancé, et aucun ne doit répondre « commande
# inconnue ». Une liste qu'on ne rejoue pas dérive.
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire manque.
#
#   tools/onglets-du-dock.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-onglets.XXXXXX")"
trap 'rm -rf "$brouillon"' EXIT

rates=0
verdict() { if [ "$2" -ne 0 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
echo "=== D32.2/D349 : les six onglets du dock, par leur nom ==="

for langue in fr en; do
    for verbe in mixer automation effets midi-cc liste tempo; do
        maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF
        env VSM_TAILLE="1280x742" HOME="$maison" VSM_LANGUE="$langue" VSM_DELAI=2000 \
            VSM_VUE="sans-rapport,arrangement,$verbe" \
            VSM_CAPTURE="$brouillon/$langue-$verbe.png" \
            timeout 45 "$BIN" > "$brouillon/$langue-$verbe.txt" 2>&1
        ligne="$(grep -m1 '^VSM_VUE : onglet ' "$brouillon/$langue-$verbe.txt")"
        if [ -z "$ligne" ]; then
            verdict "$langue · $verbe : le verbe est reconnu et dit ce qu'il ouvre" 0
            continue
        fi
        demande="$(sed -n 's/.*« \(.*\) » demandé.*/\1/p' <<<"$ligne")"
        obtenu="$(sed -n 's/.*demandé, « \(.*\) » obtenu.*/\1/p' <<<"$ligne")"
        verdict "$langue · $verbe : « $demande » demandé, « $obtenu » obtenu" \
                "$([ -n "$demande" ] && [ "$demande" = "$obtenu" ] && echo 1 || echo 0)"
    done
done

# ET LES SIX SONT DISTINCTS : six verbes qui ouvriraient le même onglet
# passeraient le contrôle ci-dessus sans rien garder.
rangs="$(for v in mixer automation effets midi-cc liste tempo; do
             sed -n 's/.*obtenu (rang \([0-9]*\) sur.*/\1/p' "$brouillon/fr-$v.txt" | head -1
         done | sort -n | uniq | wc -l)"
verdict "les six verbes ouvrent six onglets DIFFÉRENTS (rangs distincts : $rangs)" \
        "$([ "$rangs" = "6" ] && echo 1 || echo 0)"

# D349 : ET LA LISTE DES VERBES NE MENT PAS. `VSM_VUE=?` les énumère ; chacun
# de ceux qui n'attendent pas d'argument est ensuite JOUÉ, et aucun ne doit
# répondre « commande inconnue ». Une liste qu'on ne rejoue pas dérive — c'est
# ainsi que la garde du thème a demandé « navigateur » et « mixeur » pendant
# deux phases, en photographiant l'onglet par défaut sans le savoir.
maison="$(mktemp -d "$brouillon/home.XXXX")"
env VSM_TAILLE="1280x742" HOME="$maison" VSM_VUE="?" VSM_DELAI=1500 VSM_CAPTURE="$brouillon/verbes.png" \
    timeout 45 "$BIN" > "$brouillon/verbes.txt" 2>&1
liste="$(sed -n 's/^VSM_VUE : verbes — //p' "$brouillon/verbes.txt")"
simples="$(tr ',' '\n' <<<"$liste" | tr -d ' ' | grep -v ':' | grep -v '^$' | paste -sd,)"
verdict "VSM_VUE=? énumère les verbes ($(tr ',' '\n' <<<"$simples" | grep -c .) sans argument)" \
        "$([ -n "$simples" ] && echo 1 || echo 0)"
if [ -n "$simples" ]; then
    maison2="$(mktemp -d "$brouillon/home.XXXX")"
    env VSM_TAILLE="1280x742" HOME="$maison2" VSM_VUE="$simples" VSM_DELAI=2500 \
        VSM_CAPTURE="$brouillon/tous.png" timeout 45 "$BIN" > "$brouillon/tous.txt" 2>&1
    inconnus="$(grep -c 'VSM_VUE : commande inconnue' "$brouillon/tous.txt")"
    verdict "tous les verbes énumérés sont reconnus (inconnus : $inconnus)" \
            "$([ "$inconnus" -eq 0 ] && echo 1 || echo 0)"
    [ "$inconnus" -eq 0 ] || grep 'commande inconnue' "$brouillon/tous.txt" | sed 's/^/       /'
fi

echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
