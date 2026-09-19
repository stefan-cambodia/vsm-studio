#!/usr/bin/env bash
# La garde de D364 : LA BARRE DE TRANSPORT TIENT SUR UNE RANGÉE SUR CET ÉCRAN.
#
# RÈGLE GARDÉE (19/09/2026) : la barre de transport se replie sur DEUX rangées
# tant que la fenêtre est étroite, et sur UNE dès 2 240 px de large. L'écran de
# cette machine fait **3 200 × 2 000** : le régime normal est donc UNE rangée,
# et 56 px au lieu de 100.
#
# POURQUOI CETTE GARDE EXISTE. A6 — le seul point resté ouvert de la section A de
# `docs/INDEX.md` — disait : « la barre prend deux rangées SUR CET ÉCRAN (100 px
# au lieu de 56) parce qu'il lui faut ~1 400 px pour une seule, et que LE PLAFOND
# EST 1 280 ». Ce plafond de 1 280 est celui des BANCS (`VSM_TAILLE=1280x742`),
# pas celui de l'écran. Mesuré à la taille réelle, la barre tient sur une rangée.
# A6 décrivait donc une fenêtre que l'utilisateur n'a jamais.
#
# CE QUE LA GARDE EMPÊCHE : qu'une commande de plus dans la barre repousse le
# seuil au-delà de la largeur de l'écran sans que personne ne s'en aperçoive —
# le défaut reviendrait alors pour de bon, et cette fois sur la vraie machine.
# Les deux bornes sont donc gardées : deux rangées à 1 280 (le repli marche
# encore) et une seule à 2 240 (il n'est plus nécessaire).
#
# Rend 0 si les deux tiennent, 1 sinon, 2 si le binaire manque.
#
#   tools/barre-transport.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-barre.XXXXXX")"
trap 'rm -rf "$brouillon"' EXIT

rangees_a() {   # $1 = taille de fenêtre -> le nombre de rangées
    maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_TAILLE="$1" VSM_DELAI=2200 VSM_VUE="sans-rapport,arrangement" \
        VSM_TRANSPORT_ZONES=1 VSM_CAPTURE="$brouillon/$1.png" \
        timeout 60 "$BIN" > "$brouillon/$1.txt" 2>&1
    grep "VSM_TRANSPORT_ZONES" "$brouillon/$1.txt" | tail -1 \
        | sed 's/.*rangees=\([0-9]*\).*/\1/'
}

rates=0
verdict() { if [ "$2" -ne 0 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
echo "=== D364 : la barre de transport et sa largeur de repli ==="

etroite="$(rangees_a 1280x742)"
large="$(rangees_a 2240x1400)"
echo "       1 280 px : ${etroite:-?} rangée(s)   |   2 240 px : ${large:-?} rangée(s)"
verdict "à 1 280 px, la barre se replie sur deux rangées" \
        "$([ "${etroite:-0}" = "2" ] && echo 1 || echo 0)"
verdict "à 2 240 px, elle tient sur une seule" \
        "$([ "${large:-0}" = "1" ] && echo 1 || echo 0)"

echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
