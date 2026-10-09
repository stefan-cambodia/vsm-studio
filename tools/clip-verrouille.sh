#!/usr/bin/env bash
# D546.3 — VERROUILLER LA POSITION D'UN CLIP, MESURÉ PAR LE PROJET RELU.
#
# LA RÈGLE GARDÉE. Une piste MIDI à deux clips (aux ticks 0 et 1920, 480 par noire).
#   (1) « Verrouiller la position » sur le premier : le projet relu porte son verrou, et lui seul ;
#   (2) les DEUX choisis, décalés d'une mesure : le premier reste à 0, le second passe à 3840 ; le refus est
#       DIT (la boîte « Montage refusé », un clip) — le TÉMOIN, sans verrou, les décale tous les deux ;
#   (3) un pas : verrouillé puis Ctrl+Z → plus de verrou au projet relu ;
#   (4) le cadenas se dessine : la photo de l'arrangement (regardée à la main).
#
#   tools/clip-verrouille.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-verrou.XXXXXX")"
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
for debut in (0, 1920):
    corps += vlq(debut - t) + bytes([0x90, 60, 100]); corps += vlq(480) + bytes([0x80, 60, 0]); t = debut + 480
corps += b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                 + b"MTrk" + struct.pack(">I", len(corps)) + corps)
clips = [{"start": 0, "length": 960, "sourceStart": 0, "sourceLength": 960, "name": "A"},
         {"start": 1920, "length": 960, "sourceStart": 1920, "sourceLength": 960, "name": "B"}]
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "clips": clips,
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8},
                       "name": "Partie"}]}, open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2" maison
    shift 2
    maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
    env HOME="$maison" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU_CONTEXTE|VUE|TOUCHE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
clips() {   # « nom:début:verrou » des clips de la piste MIDI d'un project.json, ou « ABSENT »
    python3 -c "
import json, sys
try:
    p = json.load(open(sys.argv[1]))
except Exception:
    print('ABSENT'); sys.exit(0)
c = [c for t in p['tracks'] for c in t.get('clips', [])]
print(' '.join(f\"{x.get('name', '?')}:{int(x.get('start', 0))}:{'V' if x.get('locked') else '-'}\" for x in c) or 'AUCUN')
" "$1"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D546.3 : verrouiller la position d'un clip ==="
for x in verrou temoin annule; do cp -r "$brouillon/base" "$brouillon/$x"; done
course verrouiller "$brouillon/verrou" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" \
    VSM_MENU_CONTEXTE="clip-midi:Verrouiller la position" VSM_GESTE_APRES="1500:enregistrer:$brouillon/verrou"
cv="$(clips "$brouillon/verrou/project.json")"
cp -r "$brouillon/verrou" "$brouillon/deplace"
course deplacer "$brouillon/deplace" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement,tout-choisir,deplacer-clips" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/deplace"
course temoin "$brouillon/temoin" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement,tout-choisir,deplacer-clips" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/temoin"
course annuler "$brouillon/annule" VSM_DELAI=3000 VSM_VUE="sans-rapport,arrangement" \
    VSM_MENU_CONTEXTE="clip-midi:Verrouiller la position" VSM_GESTE_APRES="1300:touche:ctrl + Z;2000:enregistrer:$brouillon/annule"
course photo "$brouillon/verrou" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement"

cd_="$(clips "$brouillon/deplace/project.json")"; ct="$(clips "$brouillon/temoin/project.json")"; ca="$(clips "$brouillon/annule/project.json")"
echo "       clips (nom:début:verrou) : verrouillé [$cv] ; décalés [$cd_] ; témoin décalé [$ct] ; Ctrl+Z [$ca]"
verdict "(1) le premier clip verrouillé, et lui seul" "$([ "$cv" = "A:0:V B:1920:-" ] && echo 1 || echo 0)"
verdict "(2) décalés d'une mesure : le verrouillé reste, l'autre part, le refus est dit ; le témoin décale les deux" \
    "$([ "$cd_" = "A:0:V B:3840:-" ] && [ "$ct" = "A:1920:- B:3840:-" ] \
        && grep -q "^VSM_BOITE : Montage refusé : 1 clip verrouillé n'a pas bougé" "$brouillon/deplacer.txt" && echo 1 || echo 0)"
verdict "(3) un pas : verrouillé puis Ctrl+Z → plus de verrou" "$([ "$ca" = "A:0:- B:1920:-" ] && echo 1 || echo 0)"
verdict "(4) le cadenas : la photo de l'arrangement est prise (à regarder)" "$([ -s "$brouillon/photo.png" ] && echo 1 || echo 0)"
echo "       photo : $brouillon/photo.png (VSM_GARDER pour la garder)"

echo
if [ "$rates" -gt 0 ]; then echo "CLIP-VERROUILLE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "CLIP-VERROUILLE : un clip verrouillé ne bouge plus, le refus est dit, en un pas"
