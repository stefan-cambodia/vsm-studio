#!/usr/bin/env bash
# D553.1 — SUPPRIMER L'AUTOMATION D'UNE PISTE (Piste ▸ Supprimer l'automation de la piste), MESURÉ PAR LE PROJET RELU
# ET PAR L'EXPORT.
#
# LES RÈGLES GARDÉES (120 BPM). Une piste : une note tenue de 8 s, une courbe `mix.volume` qui descend de 1 à 0.
#   (1) le TÉMOIN : l'export d'origine s'éteint — le niveau efficace de 6 à 7 s vaut moins de 40 % de celui de 1 à 2 s ;
#   (2) le geste : la courbe partie du projet relu, « 1 courbe(s), 2 point(s) » dit, et l'export ne descend plus
#       (de 6 à 7 s, entre 80 et 125 % de 1 à 2 s) ;
#   (2 bis) le MENU, lu avant et après le geste : « (1 courbe(s)) » active, puis « (0 courbe(s)) » grisée. Le fichier
#       relu ne suffit pas, et c'est mesuré : une courbe VIDÉE mais gardée (le mutant qui efface les points sans
#       retirer les courbes) n'est pas écrite dans `project.json`, si bien que (2) restait vert pendant que la
#       séance montrait encore une courbe ;
#   (3) un pas : Ctrl+Z → la courbe revient (deux points).
#
#   tools/automation-retiree.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-automation-retiree.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie" + bytes([0x00, 0x90, 60, 100]) \
    + bytes([0xbc, 0x00]) + bytes([0x80, 60, 0]) + b"\x00\xff\x2f\x00"   # do4 de 0 à 7 680 ticks (VLQ de 7 680 : bc 00)
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "automation": [{"parameter": "mix.volume",
                                       "points": [{"tick": 0, "value": 1.0}, {"tick": 7680, "value": 0.0}]}]}]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|EXPORT) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
courbes() {   # « courbes:points » de la piste 0, ou « ABSENT »
    python3 -c "
import json, sys
try:
    p = json.load(open(sys.argv[1]))
except Exception:
    print('ABSENT'); sys.exit(0)
a = p['tracks'][0].get('automation', [])
print(f\"{len(a)}:{sum(len(c.get('points', [])) for c in a)}\")
" "$1"
}
rapport() {   # niveau efficace de [6 ; 7] s sur celui de [1 ; 2] s, ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    x, sr = sf.read(sys.argv[1], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
rms = lambda a, b: float(np.sqrt(np.mean(x[int(a * sr):int(b * sr), 0] ** 2)))
r = rms(1, 2)
print(f"{rms(6, 7) / r:.3f}" if r > 0 else "SILENCE")
PY2
}
dans() { python3 -c "
import sys
try:
    sys.exit(0 if float(sys.argv[2]) <= float(sys.argv[1]) <= float(sys.argv[3]) else 1)
except ValueError:
    sys.exit(1)" "$1" "$2" "$3"; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
GESTE="menu:Supprimer l'automation de la piste"

echo "=== D553.1 : supprimer l'automation d'une piste ==="
for x in retiree annulee; do cp -r "$brouillon/base" "$brouillon/$x"; done
course retirer "$brouillon/retiree" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement,piste:0" \
    VSM_GESTE_APRES="900:lister-menus;1200:$GESTE;1500:lister-menus;1700:enregistrer:$brouillon/retiree"
course annuler "$brouillon/annulee" VSM_DELAI=3000 VSM_VUE="sans-rapport,arrangement,piste:0" \
    VSM_GESTE_APRES="1200:$GESTE;1600:touche:ctrl + Z;2100:enregistrer:$brouillon/annulee"
course export-base "$brouillon/base" VSM_DELAI=1500 VSM_EXPORT="$brouillon/base.wav"
course export-retiree "$brouillon/retiree" VSM_DELAI=1500 VSM_EXPORT="$brouillon/retiree.wav"

c="$(courbes "$brouillon/retiree/project.json")"; z="$(courbes "$brouillon/annulee/project.json")"
rb="$(rapport "$brouillon/base.wav")"; rr="$(rapport "$brouillon/retiree.wav")"
grep -h "^VSM_AUTOMATION_RETIREE" "$brouillon/retirer.txt" | sed 's/^/       /'
echo "       courbes:points après [$c], après Ctrl+Z [$z] ; export, fin/début : témoin $rb, sans automation $rr"

verdict "(1) le témoin : la rampe s'entend, l'export s'éteint" "$(dans "$rb" 0 0.4 && echo 1 || echo 0)"
verdict "(2) la courbe est partie, c'est dit, et l'export ne descend plus" \
    "$([ "$c" = "0:0" ] && grep -q "^VSM_AUTOMATION_RETIREE : 1 courbe(s), 2 point(s)" "$brouillon/retirer.txt" && dans "$rr" 0.8 1.25 && echo 1 || echo 0)"
mapfile -t m < <(grep -h "^VSM_MENU_LISTE : Piste > Supprimer l.automation de la piste" "$brouillon/retirer.txt" | sed 's/^VSM_MENU_LISTE : //')
for x in "${m[@]}"; do echo "       menu : $x"; done
verdict "(2 bis) le menu : une courbe avant, zéro et grisée après" \
    "$([ "${#m[@]}" = 2 ] && echo "${m[0]}" | grep -q "(1 courbe(s))$" && echo "${m[1]}" | grep -q "(0 courbe(s)) \[grisée\]$" && echo 1 || echo 0)"
verdict "(3) un pas : Ctrl+Z rend la courbe" "$([ "$z" = "1:2" ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "AUTOMATION-RETIREE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "AUTOMATION-RETIREE : les courbes de la piste partent, et l'export ne les entend plus"
