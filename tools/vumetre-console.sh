#!/usr/bin/env bash
# La garde de D344 : LE VUMÈTRE D'UNE TRANCHE SE VOIT, SE GRADUE, ET GARDE SON ÉCRÊTAGE.
#
# RÈGLES GARDÉES (18/09/2026) :
#   1. AU REPOS, la fente du mètre se distingue du fond de la tranche. Avant
#      D344, elle était peinte en `pianoKeyBlack` (0x1a1a1f) sur un fond `panel`
#      (0x1f1f24) : **contraste 1,06**, c'est-à-dire rien — on ne savait ni où
#      était le mètre ni jusqu'où il pouvait monter. Règle : **>= 1,40** ;
#   2. la barre MONTE au bon endroit : une consigne en dBFS se relit à la même
#      valeur sur le relevé (`VSM_VUMETRES=1`) ;
#   3. le TÉMOIN D'ÉCRÊTAGE s'arme au-delà de 0 dBFS, **reste allumé quand la
#      crête est retombée** (c'est toute sa raison d'être : une crête dure un
#      buffer et passe entre deux rafraîchissements), et **s'efface d'un clic**.
#
# COMMENT. `VSM_MIXEUR_NIVEAU=piste:dBFS[!]` pose une crête par le MÊME chemin
# que le minuteur de l'application (`setMeasurement`) ; le « ! » la pose UNE
# SEULE FOIS, sans quoi la consigne reposée à chaque tour réarmerait le témoin
# aussitôt effacé et l'effacement serait invérifiable. Le clic passe par
# `cliquer:mixeur.vumetre` (D344 : le clic de banc atteint aussi un composant qui
# n'est pas un bouton). Projet de démarrage, aucune donnée extérieure.
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire manque.
#
#   tools/vumetre-console.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-vumetre.XXXXXX")"
trap 'rm -rf "$brouillon"' EXIT

lancer() {   # $1 nom de la course ; $2... variables supplémentaires
    local nom="$1"; shift
    local maison; maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF
    env HOME="$maison" VSM_TAILLE="1280x742" VSM_VUMETRES=1 VSM_DELAI=2500 \
        VSM_CAPTURE="$brouillon/$nom.png" "$@" \
        timeout 40 "$BIN" > "$brouillon/$nom.txt" 2>&1
}

rates=0
verdict() { if [ "$2" -ne 0 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
echo "=== D344 : le vumètre de la console ==="

# (a) au repos : la fente se voit
lancer repos VSM_MIXEUR=1
# LE RELEVÉ DIT OÙ, LA PHOTO DIT DE QUELLE COULEUR. Sans la position, la garde
# cherchait « le mètre » au jugé sur une rangée du fader : elle rendait 5,66 de
# contraste AVANT comme APRÈS, c'est-à-dire qu'elle mesurait le capuchon ambre du
# fader et validait le défaut qu'elle devait attraper.
zone="$(sed -n 's/.*mètre \([0-9]*\)x\([0-9]*\) @\([0-9]*\),\([0-9]*\).*/\1 \2 \3 \4/p' "$brouillon/repos.txt" | head -1)"
if [ -z "$zone" ]; then
    verdict "le relevé VSM_MIXEUR donne la position du mètre" 0
    zone="0 0 0 0"
fi
contraste="$(python3 - "$brouillon/repos.png" $zone <<'PY'
import sys
try:
    from PIL import Image
except ImportError:
    print("0.00"); raise SystemExit(0)


def lum(c):
    f = [(v / 255) / 12.92 if v / 255 <= 0.03928 else ((v / 255 + 0.055) / 1.055) ** 2.4 for v in c]
    return 0.2126 * f[0] + 0.7152 * f[1] + 0.0722 * f[2]


im = Image.open(sys.argv[1]).convert("RGB")
px = im.load()
lw, lh, lx, ly = (int(v) for v in sys.argv[2:6])
if lw <= 0 or lh <= 0:
    print("0.00")
    raise SystemExit(0)
# Le fond de la tranche se lit À GAUCHE du mètre, sur la même rangée ; le mètre
# se lit dans ses propres colonnes. Une rangée au milieu de sa hauteur, loin du
# capuchon du fader et de la bande de corrélation du bas.
y = ly + lh // 2
fond = px[max(0, lx - 6), y]
colonnes = [px[x, y] for x in range(lx, lx + lw)]
pire = max(colonnes, key=lambda c: abs(lum(c) - lum(fond)))
a, b = sorted((lum(fond), lum(pire)), reverse=True)
print("%.2f" % ((a + 0.05) / (b + 0.05)))
PY
)"
verdict "au repos, la fente du mètre tranche sur le fond (contraste $contraste, règle : >= 1,40)" \
        "$(awk -v c="$contraste" 'BEGIN { print (c >= 1.40) ? 1 : 0 }')"

# (b) la barre monte au bon endroit
lancer barre VSM_MIXEUR_NIVEAU="0:-12.0"
lu="$(sed -n 's/^VSM_VUMETRE : tranche 0 : \(-\?[0-9.]*\) dBFS.*/\1/p' "$brouillon/barre.txt" | head -1)"
verdict "une consigne de -12,00 dBFS se relit -12,00 (relevé : $lu)" \
        "$([ "$lu" = "-12.00" ] && echo 1 || echo 0)"

# (c) le témoin d'écrêtage : armé, RETENU, puis effacé d'un clic
lancer ecrete VSM_MIXEUR_NIVEAU="0:0.5!"
arme="$(sed -n 's/^VSM_VUMETRE : tranche 0 : .*écrête \([01]\).*/\1/p' "$brouillon/ecrete.txt" | head -1)"
niveau="$(sed -n 's/^VSM_VUMETRE : tranche 0 : \(-\?[a-z0-9.]*\) dBFS.*/\1/p' "$brouillon/ecrete.txt" | head -1)"
verdict "une crête d'un seul buffer à +0,5 dBFS arme le témoin et le RETIENT (crête retombée à $niveau, témoin $arme)" \
        "$([ "$arme" = "1" ] && [ "$niveau" = "-inf" ] && echo 1 || echo 0)"
lancer efface VSM_MIXEUR_NIVEAU="0:0.5!" VSM_GESTE_APRES="1200:cliquer:mixeur.vumetre"
efface="$(sed -n 's/^VSM_VUMETRE : tranche 0 : .*écrête \([01]\).*/\1/p' "$brouillon/efface.txt" | head -1)"
clic="$(grep -c 'VSM_CLIC : mixeur.vumetre — cliqué' "$brouillon/efface.txt")"
verdict "le clic sur le mètre atteint le composant et efface le témoin (clic $clic, témoin $efface)" \
        "$([ "$clic" -ge 1 ] && [ "$efface" = "0" ] && echo 1 || echo 0)"

echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
