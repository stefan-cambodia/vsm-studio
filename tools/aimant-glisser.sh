#!/usr/bin/env bash
# D547.4 — L'AIMANT AU GLISSER DE LA SOURIS, MESURÉ PAR LE PROJET RELU.
#
# LA RÈGLE GARDÉE. Le geste de souris aimante le BORD qu'il déplace, pas le pointeur : un clip saisi n'importe
# où se pose sur la grille. Une piste MIDI, trois clips d'une mesure (480 ticks par noire) : A [0, 1920) ·
# B [3840, 5760) · C [7680, 9600) ; aimant à la mesure (le défaut).
# Le pointeur parcourt UNE MESURE ET QUART (2 400 ticks) : un pas qui ne tombe pas sur la grille, sans quoi
# aimant et pas d'aimant donneraient la même chose.
#   (1) B saisi un temps après son début (4 320) : B commence à 5 760, sur la grille ; le TÉMOIN, le même geste
#       aimant éteint (Affichage ▸ Aimantation dans l'arrangement), à 6 240 (± 2 ticks d'arrondi du pixel).
#       L'ancien geste, aimant allumé, le posait à 5 281 (mesuré le 10/10) — hors de la grille de l'écart
#       saisi (H12) ;
#   (2) le bord droit de B saisi 2 px en deçà : B finit à 7 680 (longueur 3 840), sur la grille ; le témoin sans
#       aimant, à 8 160 (longueur 4 320, ± 2). L'ancien geste le finissait à 7 708.
# La géométrie vient de `relever-clips` (VSM_CLIP_ECRAN), pas d'une photo.
#
#   tools/aimant-glisser.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
AIMANT="Aimantation dans l'arrangement"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-aimant.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
corps, t = b"\x00\xff\x51\x03\x07\xa1\x20", 0
for debut in (0, 3840, 7680):
    corps += vlq(debut - t) + bytes([0x90, 60, 100]); corps += vlq(1440) + bytes([0x80, 60, 0]); t = debut + 1440
corps += b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                 + b"MTrk" + struct.pack(">I", len(corps)) + corps)
clip = lambda nom, d: {"start": d, "length": 1920, "sourceStart": d, "sourceLength": 1920, "name": nom}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie",
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8},
                       "clips": [clip("A", 0), clip("B", 3840), clip("C", 7680)]}]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|GLISSE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
clipB() {   # « début:longueur » de B dans un project.json, ou « ABSENT »
    python3 -c "
import json, sys
try:
    p = json.load(open(sys.argv[1]))
except Exception:
    print('ABSENT'); sys.exit(0)
c = [x for t in p['tracks'] for x in t.get('clips', []) if x.get('name') == 'B']
print(f\"{int(c[0]['start'])}:{int(c[0]['length'])}\" if c else 'ABSENT')
" "$1"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D547.4 : l'aimant au glisser de la souris ==="
course geometrie "$brouillon/base" VSM_DELAI=2000 VSM_VUE="sans-rapport,arrangement" VSM_GESTE_APRES="1200:relever-clips"
ligne="$(grep '^VSM_CLIP_ECRAN : piste 0 "B"' "$brouillon/geometrie.txt" | tail -1)"
read -r corps bord < <(python3 -c "
import re, sys
m = re.search(r'x ([-0-9.]+)\.\.([-0-9.]+) y ([0-9]+)\.\.([0-9]+) sur ([0-9]+)x([0-9]+)', sys.argv[1])
if not m: print('AUCUNE AUCUNE'); sys.exit(0)
x1, x2, y1, y2, w, h = (float(v) for v in m.groups())
parTick = (x2 - x1) / 1920.0
fy = (y1 + y2) / 2 / h
saisi = x1 + 480 * parTick                     # un temps après le début de B
pas = 2400 * parTick                           # une mesure et quart
print(f'arrangement:{saisi / w:.5f},{fy:.5f}:{(saisi + pas) / w:.5f}',
      f'arrangement:{(x2 - 2) / w:.5f},{fy:.5f}:{(x2 - 2 + pas) / w:.5f}')
" "$ligne")
echo "       B à l'écran : ${ligne#VSM_CLIP_ECRAN : }"
for x in deplace deplace-libre bord bord-libre; do cp -r "$brouillon/base" "$brouillon/$x"; done
if [ "$corps" != AUCUNE ]; then
    course deplacer "$brouillon/deplace" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" \
        VSM_GESTE_APRES="1200:glisser:$corps;1700:enregistrer:$brouillon/deplace"
    course deplacer-libre "$brouillon/deplace-libre" VSM_DELAI=2500 VSM_MENU="$AIMANT" VSM_VUE="sans-rapport,arrangement" \
        VSM_GESTE_APRES="1200:glisser:$corps;1700:enregistrer:$brouillon/deplace-libre"
    course bord "$brouillon/bord" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" \
        VSM_GESTE_APRES="1200:glisser:$bord;1700:enregistrer:$brouillon/bord"
    course bord-libre "$brouillon/bord-libre" VSM_DELAI=2500 VSM_MENU="$AIMANT" VSM_VUE="sans-rapport,arrangement" \
        VSM_GESTE_APRES="1200:glisser:$bord;1700:enregistrer:$brouillon/bord-libre"
fi
grep -h "^VSM_ARRANGEMENT : aimant" "$brouillon/deplacer-libre.txt" 2>/dev/null | head -1 | sed 's/^/       témoin : /'

d="$(clipB "$brouillon/deplace/project.json")"; dl="$(clipB "$brouillon/deplace-libre/project.json")"
b="$(clipB "$brouillon/bord/project.json")"; bl="$(clipB "$brouillon/bord-libre/project.json")"
echo "       B (début:longueur) : déplacé aimanté [$d] ; sans aimant [$dl] ; bord aimanté [$b] ; sans aimant [$bl]"
proche() { python3 -c "
import sys
try:
    v = int(sys.argv[1].split(':')[int(sys.argv[2])]); sys.exit(0 if abs(v - int(sys.argv[3])) <= 2 else 1)
except Exception:
    sys.exit(1)" "$1" "$2" "$3"; }
verdict "(1) déplacé, saisi un temps après son début : B à 5 760 ; sans aimant, à 6 240" \
    "$([ "$d" = "5760:1920" ] && proche "$dl" 0 6240 && echo 1 || echo 0)"
verdict "(2) le bord droit saisi 2 px en deçà : B finit à 7 680 ; sans aimant, à 8 160" \
    "$([ "$b" = "3840:3840" ] && proche "$bl" 1 4320 && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "AIMANT-GLISSER : $rates contrôle(s) raté(s)"; exit 1; fi
echo "AIMANT-GLISSER : le bord déplacé se pose sur la grille, où que le clip soit saisi"
