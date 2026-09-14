#!/usr/bin/env bash
# La garde de D301 : LA GRILLE DES NOTES REÇOIT AU MOINS DEUX FOIS LA LANE DE VÉLOCITÉ.
#
# RÈGLE. Dans le piano roll, la lane de vélocité est bornée au tiers de ce qui
# reste sous la barre d'outils, la règle et la barre d'état (poignée déduite) :
# la grille des notes -- ce qu'on est venu voir -- reçoit toujours au moins deux
# fois la lane. Plancher de 36 px pour la lane, sauf quand le tiers lui-même
# passe dessous. Le 14/09/2026, à 1366 × 768, la lane gardait 110 px fixes et la
# grille en avait 58 (quatre rangées).
#
# COMMENT. L'application est lancée sous un HOME de brouillon (jamais celui de
# l'utilisateur, D77), sur le projet de banc, à trois tailles de fenêtre et dans
# les deux langues ; le relevé `VSM_PIANOROLL_ZONES=1` imprime la géométrie de
# la dernière disposition, et c'est CE relevé qui est jugé, pas une photo.
#
# Rend 0 si toutes les dispositions tiennent la règle, 1 sinon, 2 si l'application
# manque ou ne répond pas.
#
#   tools/pianoroll-zones.sh                 # 1366x768, 1600x900, 2117x1317 × fr, en
#   tools/pianoroll-zones.sh 1280x720        # une taille de plus
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
projet="reconstruction/travail/cdl"
tailles=(1366x768 1600x900 2117x1317 "$@")
brouillon="${TMPDIR:-/tmp}/vsm-zones-$$"
mkdir -p "$brouillon"
rates=0; releves=0
echo "=== D301 : grille ≥ 2 × lane, lane ≥ 36 px (projet $projet) ==="
for langue in fr en; do
  for taille in "${tailles[@]}"; do
    maison="$brouillon/h-$langue-$taille"; mkdir -p "$maison"
    ligne=$(timeout 90 env HOME="$maison" VSM_LANGUE=$langue VSM_TAILLE="$taille" VSM_PROJET="$projet" \
              VSM_VUE="sans-rapport,pianoroll" VSM_PIANOROLL_ZONES=1 VSM_DELAI=1500 "$BIN" 2>&1 \
            | grep "VSM_PIANOROLL_ZONES" | tail -1)
    if [ -z "$ligne" ]; then
        printf '  RATÉ %-3s %-10s aucun relevé (VSM_PIANOROLL_ZONES muet)\n' "$langue" "$taille"
        rates=$((rates + 1)); continue
    fi
    releves=$((releves + 1))
    notes=$(sed -E 's/.* notes=([0-9]+).*/\1/' <<<"$ligne")
    lane=$(sed -E 's/.* lane=([0-9]+).*/\1/' <<<"$ligne")
    poignee=$(sed -E 's/.* poignee=([0-9]+).*/\1/' <<<"$ligne")
    tiers=$(( (notes + lane) / 3 ))
    verdict=OK
    [ "$notes" -ge $((2 * lane)) ] || verdict="RATÉ (grille < 2 × lane)"
    if [ "$tiers" -ge 36 ] && [ "$lane" -lt 36 ]; then verdict="RATÉ (lane sous 36 px)"; fi
    printf '  %-24s %-3s %-10s notes=%-4s lane=%-4s poignée=%s\n' "$verdict" "$langue" "$taille" "$notes" "$lane" "$poignee"
    [ "$verdict" = OK ] || rates=$((rates + 1))
  done
done
# D338 : LE CADRAGE AUTOMATIQUE NE DESCEND PAS SOUS LE RANG QUI PORTE UN NOM.
# « Zoom : tout voir » sur une piste de six octaves donnait des rangs de 9 px et
# des noms de touche de 6 pt ; la règle : rang ≥ 15 px après ce geste, police 12,
# et toutes les touches nommées. Piste 1 (« guitar ») du projet d'écoute, qui est
# celle où le défaut a été vu.
projet_large="reconstruction/travail/b4wuzthen"
if [ -d "$projet_large" ]; then
  maison="$brouillon/h-d338"; mkdir -p "$maison"
  ligne=$(timeout 90 env HOME="$maison" VSM_PROJET="$projet_large" VSM_VUE="sans-rapport,piste:1" \
            VSM_MENU_CONTEXTE="pianoroll:Zoom : tout voir" VSM_PIANOROLL_ZONES=1 VSM_DELAI=1500 \
            VSM_CAPTURE="$maison/c.png" "$BIN" 2>&1 | grep "VSM_PIANOROLL_RANG" | tail -1)
  if [ -z "$ligne" ]; then
    printf '  RATÉ D338 aucun relevé (VSM_PIANOROLL_RANG muet)\n'; rates=$((rates + 1))
  else
    releves=$((releves + 1))
    rang=$(sed -E 's/.* rang=([0-9]+).*/\1/' <<<"$ligne")
    police=$(sed -E 's/.* police=([0-9]+).*/\1/' <<<"$ligne")
    verdict=OK
    [ "$rang" -ge 15 ] && [ "$police" -ge 12 ] && grep -q "touches-nommees=toutes" <<<"$ligne" || verdict="RATÉ (rang < 15 px ou police < 12 après « Zoom : tout voir »)"
    printf '  %-24s D338 « Zoom : tout voir » piste 1 : rang=%s px police=%s pt\n' "$verdict" "$rang" "$police"
    [ "$verdict" = OK ] || rates=$((rates + 1))
  fi
fi
rm -rf "$brouillon"
echo "--- $releves relevé(s), $rates raté(s)"
[ "$releves" -gt 0 ] || exit 2
[ "$rates" -eq 0 ]
