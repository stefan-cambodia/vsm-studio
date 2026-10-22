#!/usr/bin/env bash
# D570.3 — LA COULEUR DES NOTES (piano roll, Couleur des notes ▸ Selon la vélocité / la hauteur / le canal MIDI ; les
# *Event Colors* de Cubase), MESURÉE SUR LA PHOTO : la teinte au centre de chaque note (`relever-notes-ecran` dit où).
#
# LES RÈGLES GARDÉES. Une piste : do4 (60), do5 (72) et sol4 (67), même vélocité, canal 1.
#   (1) selon la piste (le défaut, le témoin) : les trois de la même teinte ;
#   (2) selon la hauteur : do4 et do5 de la MÊME teinte (même nom), sol4 d'une autre (au moins 0,2 du cercle) ;
#   (3) selon le canal : les trois de la même teinte (un seul canal), et AUTRE que celle de la piste ;
#   (4) retenue : relancée sous le même HOME, l'application la retrouve (« selon le canal » au démarrage).
#
#   tools/couleur-notes.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-couleur-notes.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

mkdir -p "$brouillon/base/midi"
python3 - "$brouillon/base" <<'PY'
import json, struct, sys
d = sys.argv[1]
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie"
for h in (60, 72, 67):
    corps += bytes([0x00, 0x90, h, 100, 0x8f, 0x00, 0x80, h, 0])   # 1 920 ticks chacune, l'une après l'autre
corps += b"\x00\xff\x2f\x00"
open(f"{d}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                              + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie",
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}}]},
          open(f"{d}/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = HOME ; $3 = mode à choisir (vide : aucun)
    local nom="$1" maison="$2" mode="$3" gestes="1600:relever-notes-ecran"
    [ -n "$mode" ] && gestes="900:menu:Édition > Couleur des notes > $mode;$gestes"
    env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,pianoroll,piste:0" \
        VSM_DELAI=2500 VSM_GESTE_APRES="$gestes" VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|GRISÉE)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
teintes() {   # « hauteur:teinte » au centre de chaque note relevée, sur la photo — ou « ILLISIBLE »
    grep -h "^VSM_NOTE_ECRAN : " "$brouillon/$1.txt" > "$brouillon/$1.notes"
    "$PY" - "$brouillon/$1.png" "$brouillon/$1.notes" <<'PY2'
import colorsys, re, sys
try:
    from PIL import Image
    im = Image.open(sys.argv[1]).convert("RGB")
except Exception:
    print("ILLISIBLE"); sys.exit(0)
out = []
for ligne in open(sys.argv[2], encoding="utf-8"):
    m = re.search(r"hauteur (\d+) canal \d+ centre (-?\d+),(-?\d+) ; fen.tre (\d+)x(\d+)", ligne)
    if not m: continue
    h, x, y, fw = int(m[1]), int(m[2]), int(m[3]), int(m[4])
    k = im.width / fw
    r, g, b = im.getpixel((int(round(x * k)), int(round(y * k))))
    out.append(f"{h}:{colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)[0]:.3f}")
print(" ".join(sorted(out)) or "ILLISIBLE")
PY2
}
juger() {   # $1 = relevé « h:teinte… » ; $2 = règle — 1 si tenue
    python3 -c "
import sys
t = {int(a): float(b) for a, b in (x.split(':') for x in sys.argv[1].split())} if ':' in sys.argv[1] else {}
d = lambda a, b: min(abs(a - b), 1 - abs(a - b))
regle = sys.argv[2]
if len(t) != 3: print(0); sys.exit(0)
if regle == 'egales': print(1 if d(t[60], t[72]) < 0.03 and d(t[60], t[67]) < 0.03 else 0)
elif regle == 'hauteur': print(1 if d(t[60], t[72]) < 0.03 and d(t[60], t[67]) >= 0.2 else 0)
" "$1" "$2"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D570.3 : la couleur des notes ==="
maison="$(mktemp -d "$brouillon/home.XXXX")"
course piste "$(mktemp -d "$brouillon/home.XXXX")" ""
course hauteur "$(mktemp -d "$brouillon/home.XXXX")" "Selon la hauteur (une teinte par nom de note)"
course canal "$maison" "Selon le canal MIDI"
course retenue "$maison" ""
tp="$(teintes piste)"; th="$(teintes hauteur)"; tc="$(teintes canal)"
echo "       teintes (hauteur:teinte) — piste : $tp ; hauteur : $th ; canal : $tc"
grep -h "^VSM_COULEUR_NOTES" "$brouillon/retenue.txt" | head -1 | sed 's/^/       relancé : /'

verdict "(1) selon la piste : les trois de la même teinte" "$(juger "$tp" egales)"
verdict "(2) selon la hauteur : do4 = do5, sol4 autre" "$(juger "$th" hauteur)"
verdict "(3) selon le canal : les trois de la même teinte, autre que celle de la piste" \
    "$([ "$(juger "$tc" egales)" = 1 ] && python3 -c "
import sys
a = float(sys.argv[1].split()[0].split(':')[1]); b = float(sys.argv[2].split()[0].split(':')[1])
print(1 if min(abs(a - b), 1 - abs(a - b)) >= 0.05 else 0)" "$tc" "$tp" | grep -q 1 && echo 1 || echo 0)"
verdict "(4) retenue au relancement" "$(grep -q "^VSM_COULEUR_NOTES : selon le canal" "$brouillon/retenue.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "COULEUR-NOTES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "COULEUR-NOTES : les notes prennent la teinte de leur piste, de leur nom ou de leur canal"
