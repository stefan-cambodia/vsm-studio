#!/usr/bin/env bash
# D572.1 — L'AIMANTATION RELATIVE À LA GRILLE (Affichage ▸ Aimantation relative à la grille (garder l'écart) ; le *Grid
# Relative* de Cubase), MESURÉE PAR UN VRAI GLISSER (`glisser:arrangement:…`) PUIS LE PROJET RELU.
#
# LES RÈGLES GARDÉES. Une piste MIDI, un clip « B » posé à 2 400 (la mesure 2 et un temps), long d'une mesure (1 920) ;
# l'aimant à la mesure, son défaut. Le corps de B est tiré de 4 140 ticks (deux mesures et un peu plus).
#   (1) le témoin, l'aimant absolu (un HOME neuf) : l'ancre se pose sur la ligne la plus proche — 6 540 → 5 760 ;
#   (2) relatif (coché au menu) : B avance de deux mesures entières, son écart gardé — 2 400 + 3 840 = 6 240 ;
#   (3) relatif, le BORD droit de B tiré de 1 000 : il se pose sur la grille (5 760), la longueur devient 3 360 ;
#   (4) la case est retenue : relancé dans le même HOME sans la toucher, le glisser reste relatif (6 240), et le menu
#       la dit cochée.
#
#   tools/aimant-relatif.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-aimant-relatif.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie"
for i in range(16):
    corps += bytes([0x00, 0x90, 60, 100]) + bytes([0x83, 0x60]) + bytes([0x80, 60, 0])
corps += b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie",
                       "mix": {"muted": False, "pan": 0.0, "solo": False, "volume": 1.0},
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "clips": [{"start": 2400, "length": 1920, "sourceStart": 2400, "sourceLength": 1920, "name": "B"}]}]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = HOME ; $3 = projet ; le reste : variables
    local nom="$1" maison="$2" projet="$3"
    shift 3
    env HOME="$maison" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|GLISSE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
clipB() {   # « début:longueur » de B, ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
    c = [c for c in d['tracks'][0]['clips'] if c.get('name') == 'B'][0]
except Exception:
    print('ABSENT'); sys.exit(0)
print(f\"{c['start']}:{c.get('length', 0)}\")" "$1"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
RELATIF="menu:Affichage > Aimantation relative à la grille (garder l'écart)"

echo "=== D572.1 : l'aimantation relative à la grille ==="
course geometrie "$(mktemp -d "$brouillon/home.XXXX")" "$brouillon/base" VSM_DELAI=2000 VSM_VUE="sans-rapport,arrangement" \
    VSM_GESTE_APRES="1200:relever-clips"
ligne="$(grep '^VSM_CLIP_ECRAN : piste 0 "B"' "$brouillon/geometrie.txt" | tail -1)"
read -r corps bord < <(python3 -c "
import re, sys
m = re.search(r'x ([-0-9.]+)\.\.([-0-9.]+) y ([0-9]+)\.\.([0-9]+) sur ([0-9]+)x([0-9]+)', sys.argv[1])
if not m: print('AUCUNE AUCUNE'); sys.exit(0)
x1, x2, y1, y2, w, h = (float(v) for v in m.groups())
parTick = (x2 - x1) / 1920.0
fy = (y1 + y2) / 2 / h
xc = (x1 + x2) / 2
print(f'arrangement:{xc / w:.5f},{fy:.5f}:{(xc + 4140 * parTick) / w:.5f}',
      f'arrangement:{(x2 - 2) / w:.5f},{fy:.5f}:{(x2 - 2 + 1000 * parTick) / w:.5f}')
" "$ligne")
echo "       B à l'écran : ${ligne#VSM_CLIP_ECRAN : }"
[ "$corps" != AUCUNE ] || { echo "  RATÉ la géométrie : B introuvable (relever-clips)"; exit 1; }
for x in absolu relatif bord retenu; do cp -r "$brouillon/base" "$brouillon/$x"; done
course absolu "$(mktemp -d "$brouillon/home.XXXX")" "$brouillon/absolu" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" \
    VSM_GESTE_APRES="1200:glisser:$corps;1700:enregistrer:$brouillon/absolu"
maison="$(mktemp -d "$brouillon/home.XXXX")"
course relatif "$maison" "$brouillon/relatif" VSM_DELAI=2800 VSM_VUE="sans-rapport,arrangement" \
    VSM_GESTE_APRES="1000:$RELATIF;1400:glisser:$corps;1900:enregistrer:$brouillon/relatif"
course retenu "$maison" "$brouillon/retenu" VSM_DELAI=2800 VSM_VUE="sans-rapport,arrangement" \
    VSM_GESTE_APRES="1000:lister-menus;1400:glisser:$corps;1900:enregistrer:$brouillon/retenu"
course bord "$maison" "$brouillon/bord" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" \
    VSM_GESTE_APRES="1200:glisser:$bord;1700:enregistrer:$brouillon/bord"

ea="$(clipB "$brouillon/absolu")"; er="$(clipB "$brouillon/relatif")"; et="$(clipB "$brouillon/retenu")"; eb="$(clipB "$brouillon/bord")"
case_menu="$(grep -h "^VSM_MENU_LISTE : Affichage > Aimantation relative" "$brouillon/retenu.txt" | head -1 | sed 's/^VSM_MENU_LISTE : //')"
grep -h "relatif à la grille" "$brouillon/relatif.txt" | head -1 | sed 's/^/       /'
echo "       B (début:longueur) : absolu [$ea] ; relatif [$er] ; relancé [$et] ; bord tiré [$eb]"
echo "       menu au relancement : $case_menu"

verdict "(1) le témoin absolu : l'ancre se pose sur la ligne la plus proche (5 760)" "$([ "$ea" = "5760:1920" ] && echo 1 || echo 0)"
verdict "(2) relatif : deux mesures entières, l'écart gardé (6 240)" "$([ "$er" = "6240:1920" ] && echo 1 || echo 0)"
verdict "(3) relatif, le bord se pose sur la grille (fin à 5 760, longueur 3 360)" "$([ "$eb" = "2400:3360" ] && echo 1 || echo 0)"
verdict "(4) retenue : relancé sans y toucher, toujours relatif, la case cochée" \
    "$([ "$et" = "6240:1920" ] && echo "$case_menu" | grep -q "\[cochée\]" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "AIMANT-RELATIF : $rates contrôle(s) raté(s)"; exit 1; fi
echo "AIMANT-RELATIF : le clip déplacé garde son écart à la grille, ses bords s'y posent"
