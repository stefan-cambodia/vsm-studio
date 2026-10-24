#!/usr/bin/env bash
# D588.2 — LA LARGEUR DE LA TÊTE DE LECTURE (Affichage ▸ Largeur de la tête de lecture ; le *Cursor Width* de Cubase),
# MESURÉE PAR UN RELEVÉ QUI LIT CE QUE LA PEINTURE LIT ET PAR LE COMPTE DES COLONNES DE SA COULEUR SUR LA PHOTO.
#
# LES RÈGLES GARDÉES. Une piste, un clip de huit mesures ; la vue enregistrée (0,1 px/tick, défilée à 0, dans les deux
# vues) ; la tête posée à la mesure 3, temps 3 (`VSM_POSITION=3.3`), loin des bords et hors des barres de mesure. Sur la
# photo, une ligne de pixels sous les pistes (l'arrangement) ou dans la grille (le piano roll) ; une colonne est PLEINE
# quand son pixel est la couleur de la tête (`Palette::accentAmber`, 227,162,77) à 3 près. `relever-largeur-tete` dit
# la largeur que chaque vue peindra (`VSM_LARGEUR_TETE`).
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D588) :
#   (1) « Tête de 3 px » : les six vues disent 3 ; la tête de l'arrangement et celle de la grille du piano roll couvrent
#       3 colonnes pleines, contiguës ;
#   (2) « Tête de 5 px » : 5, et 5 colonnes ;
#   (3) d'origine : les pixels autour de la tête IDENTIQUES à ceux du témoin (le binaire de D587) — jugé seulement quand
#       `VSM_TEMOIN=<binaire>` est donné, et DIT sinon ;
#   (4) relancé dans le même HOME : 5 retenu ; en anglais, « Playhead width ».
#
#   [VSM_TEMOIN=build/temoin-D587] tools/largeur-tete.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
TEMOIN="${VSM_TEMOIN:-}"
PY="$racine/analyse/.venv/bin/python"
[ -x "$PY" ] || { echo "REFUS : $PY absent (PIL) — voir analyse/requirements.txt"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-largeur-tete.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie" + b"\x00\x90\x3c\x5a\x83\x60\x80\x3c\x00" + b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
clip = {"sourceStart": 0, "sourceLength": 15360, "start": 0, "length": 15360, "color": "#FF6B9BFF"}
json.dump({"format": "vsm-project", "version": 1, "title": "largeur-tete", "midi": {"file": "midi/arrangement.mid"},
           "view": {"pixelsPerTick": 0.1, "scrollTick": 0, "pianoRollPixelsPerTick": 0.1, "pianoRollScrollTick": 0,
                    "pianoRollTopNote": 72, "pianoRollNoteHeight": 16},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "clips": [clip], "instrument": {"preferredPlugin": "vsm.minimoog"}}]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = HOME (vide : un neuf) ; $3 = la vue ; $4 = binaire ; le reste : variables
    local nom="$1" maison="$2" vue="$3" bin="$4"
    shift 4
    [ -n "$maison" ] || maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,$vue" VSM_POSITION=3.3 \
        "$@" VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$bin" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|GESTE|POSITION) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
# « n colonnes pleines, contiguës|éparses, de x0 à x1 » sur la ligne y de la photo, entre x min et x max.
colonnes() {   # $1 = photo ; $2 = y ; $3 = x min ; $4 = x max
    "$PY" -I - "$1" "$2" "$3" "$4" <<'PY2'
import sys
from PIL import Image
try:
    im = Image.open(sys.argv[1]).convert("RGB")
except Exception:
    print("ABSENT"); sys.exit(0)
y, x0, x1 = int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
pleines = [x for x in range(x0, x1) if all(abs(c - r) <= 3 for c, r in zip(im.getpixel((x, y)), (227, 162, 77)))]
if not pleines:
    print("0"); sys.exit(0)
contigues = pleines == list(range(pleines[0], pleines[-1] + 1))
print(f"{len(pleines)} {'contiguës' if contigues else 'éparses'} {pleines[0]}..{pleines[-1]}")
PY2
}
# Les pixels d'une bande de la photo autour de x (±12), de y0 à y1 : une empreinte.
empreinte() {   # $1 = photo ; $2 = x ; $3 = y0 ; $4 = y1
    "$PY" -I - "$1" "$2" "$3" "$4" <<'PY3'
import hashlib, sys
from PIL import Image
try:
    im = Image.open(sys.argv[1]).convert("RGB")
except Exception:
    print("ABSENT"); sys.exit(0)
x, y0, y1 = int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
print(hashlib.sha256(im.crop((x - 12, y0, x + 13, y1)).tobytes()).hexdigest()[:16])
PY3
}
largeur() { grep -h "^VSM_LARGEUR_TETE : " "$brouillon/$1.txt" | tail -1 | cut -c20-; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
CHOIX="menu:Affichage > Largeur de la tête de lecture"

echo "=== D588.2 : la largeur de la tête de lecture ==="
course arr3 "" arrangement "$BIN" VSM_DELAI=1300 VSM_GESTE_APRES="400:$CHOIX > Tête de 3 px;800:relever-largeur-tete"
course pr3 "" pianoroll "$BIN" VSM_DELAI=1300 VSM_GESTE_APRES="400:$CHOIX > Tête de 3 px;800:relever-largeur-tete"
maison="$(mktemp -d "$brouillon/home.XXXX")"
course arr5 "$maison" arrangement "$BIN" VSM_DELAI=1300 VSM_GESTE_APRES="400:$CHOIX > Tête de 5 px;800:relever-largeur-tete"
course pr5 "" pianoroll "$BIN" VSM_DELAI=1300 VSM_GESTE_APRES="400:$CHOIX > Tête de 5 px;800:relever-largeur-tete"
course retenu "$maison" arrangement "$BIN" VSM_DELAI=1200 VSM_GESTE_APRES="600:relever-largeur-tete;800:lister-menus"
course anglais "" arrangement "$BIN" VSM_LANGUE=en VSM_DELAI=1200 VSM_GESTE_APRES="800:lister-menus"
course arr0 "" arrangement "$BIN" VSM_DELAI=1000
[ -n "$TEMOIN" ] && course arr0-temoin "" arrangement "$TEMOIN" VSM_DELAI=1000

# Où regarder : la zone des clips de l'arrangement (sous la piste, y 600) et la grille du piano roll (y 500), en
# coordonnées de la fenêtre ; la tête à la mesure 3, temps 3, tombe vers x 937 et x 850 (0,1 px/tick).
for n in arr3 arr5 arr0; do echo "       $n : $(colonnes "$brouillon/$n.png" 600 460 1190) ; relevé — $(largeur $n)"; done
for n in pr3 pr5; do echo "       $n : $(colonnes "$brouillon/$n.png" 500 372 1188) ; relevé — $(largeur $n)"; done
retenu_l="$(largeur retenu)"
en="$(grep -c "^VSM_MENU_LISTE : View > Playhead width > Thin playhead (default) \[cochée\]" "$brouillon/anglais.txt")"
echo "       relancé : $retenu_l ; en anglais : $en entrée cochée"

verdict "(1) 3 px : les six vues disent 3 ; 3 colonnes pleines dans l'arrangement et la grille" \
    "$(largeur arr3 | grep -q "réglage 3 ; arrangement 3.0 ; piano roll 3.0 ; règle 3.0 ; automation 3.0 ; contrôleurs 3.0 ; tempo 3.0" \
        && colonnes "$brouillon/arr3.png" 600 460 1190 | grep -q "^3 contiguës" \
        && colonnes "$brouillon/pr3.png" 500 372 1188 | grep -q "^3 contiguës" && echo 1 || echo 0)"
verdict "(2) 5 px : 5, et 5 colonnes pleines" \
    "$(largeur arr5 | grep -q "réglage 5 ; arrangement 5.0" \
        && colonnes "$brouillon/arr5.png" 600 460 1190 | grep -q "^5 contiguës" \
        && colonnes "$brouillon/pr5.png" 500 372 1188 | grep -q "^5 contiguës" && echo 1 || echo 0)"
if [ -n "$TEMOIN" ]; then
    # La bande se centre sur la tête MESURÉE (le milieu des trois colonnes de la course à 3 px), pas sur un x deviné.
    xt="$(colonnes "$brouillon/arr3.png" 600 460 1190 | sed -n 's/^3 contiguës \([0-9]*\)\.\.\([0-9]*\)$/\1/p')"
    xt=$(( ${xt:-936} + 1 ))
    e0="$(empreinte "$brouillon/arr0.png" "$xt" 150 700)"; et="$(empreinte "$brouillon/arr0-temoin.png" "$xt" 150 700)"
    echo "       d'origine (bande centrée sur x $xt) : empreinte $e0, témoin $et"
    verdict "(3) d'origine : la bande autour de la tête identique au témoin" "$([ "$e0" != ABSENT ] && [ "$e0" = "$et" ] && echo 1 || echo 0)"
else
    echo "  —    (3) d'origine contre le témoin : non jugé, VSM_TEMOIN non donné"
fi
verdict "(4) relancé : 5 retenu ; en anglais" \
    "$(echo "$retenu_l" | grep -q "^réglage 5 " && [ "$en" = 1 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "LARGEUR-TETE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "LARGEUR-TETE : la tête se peint de la largeur choisie, calée sur les pixels, dans les six vues"
