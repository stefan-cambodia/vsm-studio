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
# D373 (25/09/2026) — LE COMPTE DEMANDÉ NE PROUVAIT RIEN SEUL. À 1 920 px, le
# relevé disait « 2 rangées » pendant que la photo montrait la seconde VIDE : les
# deux témoins (« SANS SON », craquements), cachés, comptaient dans la largeur, et
# la hauteur n'était jamais redemandée quand ils disparaissaient. La garde
# confronte donc les rangées DEMANDÉES aux rangées OCCUPÉES par des composants
# visibles qui MONTRENT quelque chose (`occupees=` ; une étiquette visible mais
# vide est comptée à part, `vides=` — le premier relevé la comptait occupante,
# et la garde, rejouée sur le code d'avant, restait verte sur ce point), à chaque largeur, et juge une troisième largeur, 1 920,
# entre les deux bornes : c'est là que le défaut vivait.
#
# CE QUE CHAQUE CONTRÔLE GARDE (vus rouges sur le code d'avant, D373) : le défaut
# avait deux visages. Tôt, les commandes sur une rangée et la seconde VIDE — c'est
# le contrôle d'occupation qui le voit. Plus tard, la fréquence poussée SEULE sur
# la seconde rangée (`derniere=etiquette:48.0_kHz@8,50`) — la rangée est occupée,
# et seul le contrôle de LARGEUR à 1 920 le voit.
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire manque.
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
        | sed 's/.*rangees=\([0-9]*\).*occupees=\([0-9]*\) vides=\([0-9]*\).*/\1 \2 \3/'
}

rates=0
verdict() { if [ "$2" -ne 0 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
echo "=== D364 : la barre de transport et sa largeur de repli ==="

juger() {   # $1 = taille, $2 = rangées attendues
    set -- "$1" "$2" $(rangees_a "$1")
    echo "       $1 : ${3:-?} rangée(s) demandée(s), ${4:-?} occupée(s), ${5:-?} étiquette(s) visible(s) mais vide(s)"
    verdict "à $1, la barre prend $2 rangée(s)" "$([ "${3:-0}" = "$2" ] && echo 1 || echo 0)"
    verdict "à $1, chaque rangée demandée porte une commande (D373)" \
            "$([ -n "${4:-}" ] && [ "${3:-0}" = "${4:-x}" ] && echo 1 || echo 0)"
}
juger 1280x742 2     # le repli marche encore
juger 1920x1200 1    # D373 : une seule suffit, et une seule est réservée
juger 2240x1400 1    # il n'est plus nécessaire

echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
