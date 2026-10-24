#!/usr/bin/env bash
# D587.3 — LE RÉTICULE PENDANT UN GESTE (Affichage ▸ Réticule pendant un geste ; le *Cross-hair Cursor* de Cubase),
# MESURÉ PAR UN RELEVÉ QUI LIT CE QUE LA PEINTURE LIT, PENDANT PUIS APRÈS UN GLISSÉ TENU, LE TÉMOIN DÉCOCHÉ À CÔTÉ.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 4/4). Une piste, un clip de huit mesures. Le geste tenu passe par
# `glisser:<vue>:fx0,fy0:fx1,fy1:lent` : l'appui, huit mouvements espacés de 50 ms, le relâchement 50 ms après le
# dernier (450 ms en tout) ; `relever-reticule` dit pour chaque vue « montré à x,y » ou « caché ». À mi-geste, le minuteur
# a du jeu : le point relevé doit tomber sur L'UN des huit points du trajet (à 1 px près), pas sur un point deviné.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D587) :
#   (1) un glissé tenu sur un clip de l'arrangement, le relevé à mi-geste : montré, sur le trajet ;
#   (2) après le relâchement : caché ;
#   (3) le même dans la grille du piano roll : montré, sur le trajet, puis caché ;
#   (4) décochée : caché à mi-geste ;
#   (5) relancé dans le même HOME : décochée retenue ; en anglais, « Cross-hair while dragging » ;
#   et la photo prise à mi-geste (`VSM_GARDER=dossier`, `arr-photo.png`) montre les deux traits — REGARDÉE, pas jugée ici.
#
#   tools/reticule.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-reticule.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie"
corps += vlq(0) + b"\x90\x3c\x5a" + vlq(480) + b"\x80\x3c\x00"
corps += vlq(14400) + b"\x90\x3e\x5a" + vlq(480) + b"\x80\x3e\x00"
corps += b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
clip = {"sourceStart": 0, "sourceLength": 15360, "start": 0, "length": 15360, "color": "#FF6B9BFF"}
json.dump({"format": "vsm-project", "version": 1, "title": "reticule", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "clips": [clip], "instrument": {"preferredPlugin": "vsm.minimoog"}}]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = HOME (vide : un neuf) ; $3 = la vue ; le reste : variables
    local nom="$1" maison="$2" vue="$3"
    shift 3
    [ -n "$maison" ] || maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,$vue" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|GESTE|GLISSE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
# « montré », « caché » ou « ? » pour une vue, au relevé n (1 = le premier) ; et le point, s'il est montré.
etat() { grep -h "^VSM_RETICULE : " "$brouillon/$1.txt" | sed -n "${3}p" | sed -n "s/.*$2 \(montré\|caché\).*/\1/p"; }
point() { grep -h "^VSM_RETICULE : " "$brouillon/$1.txt" | sed -n "${3}p" | sed -n "s/.*$2 montré à \([0-9.-]*,[0-9.-]*\).*/\1/p"; }
# Le point est-il l'un des huit points du trajet ? Le trajet se lit sur la ligne `VSM_GLISSE : de (x, y) à (x, y) px`.
sur_le_trajet() {   # $1 = course ; $2 = « x,y »
    python3 - "$brouillon/$1.txt" "$2" <<'PY2'
import re, sys
texte = open(sys.argv[1], encoding="utf-8", errors="replace").read()
m = re.search(r"VSM_GLISSE : de \(([0-9.-]+), ([0-9.-]+)\) à \(([0-9.-]+), ([0-9.-]+)\) px", texte)
if not m or not sys.argv[2]:
    sys.exit(1)
x0, y0, x1, y1 = map(float, m.groups())
px, py = map(float, sys.argv[2].split(","))
ok = any(abs(px - (x0 + (x1 - x0) * k / 8)) <= 1.0 and abs(py - (y0 + (y1 - y0) * k / 8)) <= 1.0 for k in range(9))
sys.exit(0 if ok else 1)
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
OPTION="menu:Affichage > Réticule pendant un geste"
# Sur le clip de la piste 0 (y 50 sur 574, comme tete-posee.sh), tiré vers la droite ; dans la grille du piano roll, un
# lasso. « 0.087 » est écrit en fraction à l'arrivée aussi, pour que `VSM_GLISSE` dise le trajet en pixels.
ARR="glisser:arrangement:0.400,0.087:0.600,0.087:lent"
PR="glisser:pianoroll:0.300,0.300:0.500,0.500:lent"
APRES_ARR="1120:relever-reticule;1600:relever-reticule"

echo "=== D587.3 : le réticule pendant un geste ==="
maison="$(mktemp -d "$brouillon/home.XXXX")"
course arr "" arrangement VSM_DELAI=1900 VSM_GESTE_APRES="900:$ARR;$APRES_ARR"
course pr "" pianoroll VSM_DELAI=1900 VSM_GESTE_APRES="900:$PR;$APRES_ARR"
course decochee "$maison" arrangement VSM_DELAI=1900 VSM_GESTE_APRES="600:$OPTION;900:$ARR;$APRES_ARR"
course retenu "$maison" arrangement VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"
course anglais "" arrangement VSM_LANGUE=en VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"
course arr-photo "" arrangement VSM_DELAI=1120 VSM_GESTE_APRES="900:$ARR"

for n in arr pr decochee; do
    echo "       $n : mi-geste — $(grep -h '^VSM_RETICULE : ' "$brouillon/$n.txt" | sed -n 1p | cut -c16-) ; après — $(grep -h '^VSM_RETICULE : ' "$brouillon/$n.txt" | sed -n 2p | cut -c16-)"
done
grep -h "^VSM_GLISSE : de " "$brouillon"/arr.txt "$brouillon"/pr.txt | sed 's/^/       /'
decoche="$(grep -h "^VSM_MENU_LISTE : Affichage > Réticule pendant un geste" "$brouillon/retenu.txt" | grep -vc "\[cochée\]")"
en="$(grep -ch "^VSM_MENU_LISTE : View > Cross-hair while dragging" "$brouillon/anglais.txt")"
echo "       relancé : $decoche entrée décochée ; en anglais : $en entrée(s)"

verdict "(1) l'arrangement, à mi-geste : montré, sur le trajet" \
    "$([ "$(etat arr arrangement 1)" = montré ] && sur_le_trajet arr "$(point arr arrangement 1)" && echo 1 || echo 0)"
verdict "(2) après le relâchement : caché" "$([ "$(etat arr arrangement 2)" = caché ] && echo 1 || echo 0)"
verdict "(3) le piano roll : montré sur le trajet, puis caché" \
    "$([ "$(etat pr 'piano roll' 1)" = montré ] && sur_le_trajet pr "$(point pr 'piano roll' 1)" \
        && [ "$(etat pr 'piano roll' 2)" = caché ] && echo 1 || echo 0)"
verdict "(4) décochée : caché à mi-geste" "$([ "$(etat decochee arrangement 1)" = caché ] && echo 1 || echo 0)"
verdict "(5) retenu au poste ; en anglais" "$([ "$decoche" = 1 ] && [ "$en" = 1 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "RETICULE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "RETICULE : deux traits suivent le pointeur pendant le geste, et s'effacent au relâchement"
