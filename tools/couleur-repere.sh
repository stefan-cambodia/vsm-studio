#!/usr/bin/env bash
# D570.1 — LA COULEUR D'UN REPÈRE (clic droit sur un repère de la règle ▸ Couleur du repère… ; les couleurs de la piste
# de repères de Cubase), MESURÉE PAR LE PROJET RELU ET LA PHOTO.
#
# LES RÈGLES GARDÉES. Deux repères, « Couplet » à 0 et « Refrain » à 1 920 ; une note qui tient tout.
#   (1) la règle de l'arrangement propose « Couleur du repère… » ;
#   (2) « Refrain » en rouge (le verbe `couleur-repere:`, par la fonction même du sélecteur) : le projet relu porte
#       `color` sur lui seul, « Couplet » n'en écrit pas ;
#   (3) la photo : des pixels rouges dans la règle de l'arrangement ; le témoin sans couleur, aucun ;
#   (4) un pas : Ctrl+Z retire la couleur, après un geste qui a eu lieu.
#
#   tools/couleur-repere.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-couleur-repere.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

mkdir -p "$brouillon/base/midi"
python3 - "$brouillon/base" <<'PY'
import json, struct, sys
d = sys.argv[1]
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie" + bytes([0x00, 0x90, 60, 100, 0xbc, 0x00, 0x80, 60, 0]) + b"\x00\xff\x2f\x00"
open(f"{d}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                              + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "markers": [{"tick": 0, "name": "Couplet"}, {"tick": 1920, "name": "Refrain"}],
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie",
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}}]},
          open(f"{d}/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = VSM_GESTE_APRES ; le reste : variables
    local nom="$1" gestes="$2"
    shift 2
    cp -r "$brouillon/base" "$brouillon/$nom"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/$nom" VSM_TAILLE="1600x1000" \
        VSM_VUE="sans-rapport,arrangement" VSM_DELAI=2500 VSM_GESTE_APRES="$gestes" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU|MENU_CONTEXTE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
couleurs() { python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
print(' '.join(f\"{m['name']}:{m.get('color', '-')}\" for m in d.get('markers', [])))" "$1"; }
rouges() {   # pixels rouges dans la bande de la règle de l'arrangement, sur la photo
    local o; o="$(grep -h "^VSM_CLIP_ECRAN" "$brouillon/$1.txt" | head -1 | sed -n 's/.*fenêtre \([0-9]*\)x[0-9]* à \([0-9]*\),\([0-9]*\).*/\1 \2 \3/p')"
    "$PY" - "$brouillon/$1.png" $o <<'PY2'
import sys
try:
    from PIL import Image
    im = Image.open(sys.argv[1]).convert("RGB")
    fw, ox, oy = (int(v) for v in sys.argv[2:5])
except Exception:
    print("ILLISIBLE"); sys.exit(0)
k = im.width / fw
n = 0
for y in range(int(oy * k), int((oy + 24) * k)):
    for x in range(int(ox * k), im.width):
        r, g, b = im.getpixel((x, y))
        if r > 180 and g < 70 and b < 70: n += 1
print(n)
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D570.1 : la couleur d'un repère ==="
course rouge "1000:couleur-repere:1:FF0000;1300:relever-clips;1700:enregistrer:$brouillon/rouge" VSM_MENU_CONTEXTE="regle:?"
course temoin "1300:relever-clips"
course annule "1000:couleur-repere:1:FF0000;1400:touche:ctrl + Z;1800:enregistrer:$brouillon/annule"
cr="$(couleurs "$brouillon/rouge")"; cz="$(couleurs "$brouillon/annule")"
pr="$(rouges rouge)"; pt="$(rouges temoin)"
menu="$(grep -h "^VSM_MENU_CONTEXTE : regle = " "$brouillon/rouge.txt" | head -1)"
grep -h "^VSM_REPERE_COULEUR" "$brouillon/rouge.txt" | sed 's/^/       /'
echo "       couleurs : rouge [$cr] ; Ctrl+Z [$cz] ; pixels rouges dans la règle : $pr (témoin $pt)"

verdict "(1) la règle propose « Couleur du repère… »" "$(echo "$menu" | grep -q "Couleur du repère…" && echo 1 || echo 0)"
verdict "(2) Refrain rouge au projet, Couplet sans couleur écrite" "$([ "$cr" = "Couplet:- Refrain:#FFFF0000" ] && echo 1 || echo 0)"
verdict "(3) la photo : du rouge dans la règle, aucun sur le témoin" \
    "$([ "$pt" = 0 ] && [ "$pr" != "ILLISIBLE" ] && [ "${pr:-0}" -gt 20 ] && echo 1 || echo 0)"
verdict "(4) un pas : Ctrl+Z retire la couleur, après un geste qui a eu lieu" \
    "$([ "$cz" = "Couplet:- Refrain:-" ] && grep -q "^VSM_REPERE_COULEUR : 1" "$brouillon/annule.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "COULEUR-REPERE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "COULEUR-REPERE : un repère porte sa couleur, au projet comme sur la règle"
