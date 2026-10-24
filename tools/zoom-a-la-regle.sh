#!/usr/bin/env bash
# D585.2 — LE ZOOM EN GLISSANT SUR LA RÈGLE (Transport ▸ Zoomer en glissant sur la règle ; le *Zoom while Locating in
# Time Scale* de Cubase), MESURÉ PAR LE RELEVÉ DU ZOOM ET DE LA TÊTE APRÈS UN GLISSÉ, LE TÉMOIN DÉCOCHÉ À CÔTÉ.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 4/4, 120 BPM). Une piste, un clip de huit mesures [0, 15 360). Le glissé passe
# par `glisser:arrangement:fx,fy:fx,+100px` — l'appui à 12 px du haut, DANS la règle (22 px), l'arrivée 100 px plus
# bas (D585.2 : des pixels, pas une fraction). L'abscisse de la mesure 5 se lit d'abord sur le clip (`relever-clips`) :
# l'appui y tombe sans dépendre du cadrage d'ouverture.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D585) :
#   (1) 100 px vers le BAS sans bouger en x → zoom ×2,00 (à 1 % près), le tick saisi (dans la mesure 5) toujours sous le
#       pointeur (à 1 px près), la tête au tick saisi (à un pixel près) ;
#   (2) 100 px vers le HAUT → ×0,50 ;
#   (3) un glissé horizontal pur → le zoom inchangé, la tête au tick sous le point d'arrivée ;
#   (4) un glissé vers le bas puis « Annuler le zoom » → le zoom d'avant, en UN pas ;
#   (5) le TÉMOIN décoché → le zoom inchangé, la tête qui suit ;
#   (6) relancé dans le même HOME : décochée ; en anglais, « Zoom by dragging on the ruler ».
#
#   tools/zoom-a-la-regle.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-zoom-regle.XXXXXX")"
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
corps += vlq(0) + b"\x90\x3c\x5a" + vlq(480) + b"\x80\x3c\x00"        # une noire au tick 0
corps += vlq(14400) + b"\x90\x3e\x5a" + vlq(480) + b"\x80\x3e\x00"    # une noire au tick 14 880, la dernière
corps += b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
clip = {"sourceStart": 0, "sourceLength": 15360, "start": 0, "length": 15360, "color": "#FF6B9BFF"}
json.dump({"format": "vsm-project", "version": 1, "title": "zoom-regle", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "clips": [clip], "instrument": {"preferredPlugin": "vsm.minimoog"}}]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = HOME (vide : un neuf) ; le reste : variables
    local nom="$1" maison="$2"
    shift 2
    [ -n "$maison" ] || maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,arrangement" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|GESTE|GLISSE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
tete() { grep -h "^VSM_TETE : tick " "$brouillon/$1.txt" | tail -1 | sed 's/^VSM_TETE : tick \([0-9-]*\).*/\1/'; }
glisse() { grep -h "^VSM_ZOOM : glissé sur la règle" "$brouillon/$1.txt" | tail -1; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
OPTION="menu:Transport > Zoomer en glissant sur la règle"

echo "=== D585.2 : le zoom en glissant sur la règle ==="
# LE REPÉRAGE : le clip [0, 15 360) à l'écran donne la correspondance x ↔ tick à l'ouverture.
course reperage "" VSM_DELAI=1500 VSM_GESTE_APRES="900:relever-clips"
ligne="$(grep -h "^VSM_CLIP_ECRAN : piste 0" "$brouillon/reperage.txt" | head -1)"
read -r x0 x1 largeur hauteur <<< "$(echo "$ligne" | sed -n 's/.* x \([0-9.]*\)\.\.\([0-9.]*\) y .* sur \([0-9]*\)x\([0-9]*\).*/\1 \2 \3 \4/p')"
if [ -z "${x0:-}" ] || [ -z "${hauteur:-}" ]; then echo "REFUS : le clip n'a pas été relevé à l'écran ($ligne)"; exit 2; fi
eval "$(python3 - "$x0" "$x1" "$largeur" "$hauteur" <<'PY'
import sys
x0, x1, w, h = float(sys.argv[1]), float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4])
ppt = (x1 - x0) / 15360.0
xm5 = x0 + (7680 + 480) * ppt          # un temps après le début de la mesure 5 : franchement dedans
xm7 = x0 + (11520 + 480) * ppt
print(f"PPT0={ppt:.6f} X5={xm5:.1f} FX5={xm5 / w:.5f} FX7={xm7 / w:.5f} X7={xm7:.1f} X0={x0:.1f} FY0={12.0 / h:.5f}")
PY
)"
echo "       repérage : clip x $x0..$x1 sur $largeur px ; ${PPT0} px/tick ; la mesure 5 (+1 temps) à x $X5"

# L'APPUI EN FRACTION (12 px de haut), lisible aussi par un binaire d'avant D585.2 — le témoin appuie au même point ;
# l'arrivée en pixels relatifs, que ce binaire-là lit comme un glissé sans course verticale.
BAS="glisser:arrangement:$FX5,$FY0:$FX5,+100px"
HAUT="glisser:arrangement:$FX5,$FY0:$FX5,-100px"
COTE="glisser:arrangement:$FX5,$FY0:$FX7,+0px"
maison="$(mktemp -d "$brouillon/home.XXXX")"
course bas "" VSM_DELAI=1800 VSM_GESTE_APRES="900:$BAS;1200:relever-tete"
course haut "" VSM_DELAI=1800 VSM_GESTE_APRES="900:$HAUT;1200:relever-tete"
course cote "" VSM_DELAI=1800 VSM_GESTE_APRES="900:$COTE;1200:relever-tete;1300:relever-arrangement"
course annule "" VSM_DELAI=2000 VSM_GESTE_APRES="900:$BAS;1200:menu:Affichage > Annuler le zoom"
course temoin "$maison" VSM_DELAI=2000 VSM_GESTE_APRES="700:$OPTION;900:$BAS;1200:relever-tete"
course retenu "$maison" VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"
course anglais "" VSM_LANGUE=en VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"

for n in bas haut cote annule temoin; do echo "       $n : $(glisse $n | cut -c1-170)"; done
grep -h "^VSM_ZOOM : annulé" "$brouillon/annule.txt" | sed 's/^/       /'
eval "$(python3 - "$(glisse bas)" "$(glisse haut)" "$(tete bas)" "$(tete cote)" "$(tete temoin)" "$PPT0" "$X0" "$X7" \
    "$(grep -h '^VSM_ZOOM : annulé' "$brouillon/annule.txt" | tail -1)" <<'PY'
import re, sys
bas, haut, tbas, tcote, ttemoin, ppt0, x0, x7, annule = sys.argv[1:10]
ppt0, x0, x7 = float(ppt0), float(x0), float(x7)
def lire(l):
    m = re.search(r"zoom ([0-9.]+) → ([0-9.]+) px/tick \(×([0-9.]+)\), le tick saisi ([0-9]+) .* à x ([0-9.-]+) pour un appui à x ([0-9.-]+) \(([0-9]+) à annuler", l)
    return m.groups() if m else None
b, h = lire(bas), lire(haut)
ok1 = ok2 = ok3 = ok4 = ok5 = 0
if b:
    avant, apres, facteur, saisi, xs, xa, pas = float(b[0]), float(b[1]), float(b[2]), int(b[3]), float(b[4]), float(b[5]), int(b[6])
    un_px = 1.0 / apres   # ticks par pixel après le zoom
    ok1 = int(abs(facteur - 2.0) <= 0.02 and abs(xs - xa) <= 1.0 and 7680 <= saisi < 9600
              and tbas.lstrip("-").isdigit() and abs(int(tbas) - saisi) <= un_px + 1)
    m = re.search(r"zoom ([0-9.]+) px/tick.*\(([0-9]+) à annuler", annule)
    ok4 = int(m is not None and abs(float(m.group(1)) - avant) < 1e-6 and int(m.group(2)) == 0 and pas == 1)
if h:
    ok2 = int(abs(float(h[2]) - 0.5) <= 0.005)
attendu_cote = (x7 - x0) / ppt0
ok3 = int(tcote.lstrip("-").isdigit() and abs(int(tcote) - attendu_cote) <= 1.0 / ppt0 + 1)
attendu_temoin = int(b[3]) if b else None
ok5 = int(attendu_temoin is not None and ttemoin.lstrip("-").isdigit() and abs(int(ttemoin) - attendu_temoin) <= 1.0 / ppt0 + 1)
print(f"OK1={ok1} OK2={ok2} OK3={ok3} OK4={ok4} OK5={ok5} ATT_COTE={attendu_cote:.0f}")
PY
)"
nb_cote="$(grep -c "^VSM_ZOOM : glissé sur la règle" "$brouillon/cote.txt")"
nb_temoin="$(grep -c "^VSM_ZOOM : glissé sur la règle" "$brouillon/temoin.txt")"
echo "       têtes : bas $(tete bas) ; de côté $(tete cote) (attendu ~$ATT_COTE, $nb_cote zoom) ; témoin $(tete temoin) ($nb_temoin zoom)"
decoche="$(grep -h "^VSM_MENU_LISTE : Transport > Zoomer en glissant sur la règle" "$brouillon/retenu.txt" | grep -vc "\[cochée\]")"
en="$(grep -ch "^VSM_MENU_LISTE : Transport > Zoom by dragging on the ruler" "$brouillon/anglais.txt")"
echo "       relancé : $decoche entrée décochée ; en anglais : $en entrée(s)"

verdict "(1) 100 px vers le bas : ×2,00, le tick saisi sous le pointeur, la tête au tick saisi" "$OK1"
verdict "(2) 100 px vers le haut : ×0,50" "$OK2"
verdict "(3) de côté : le zoom inchangé, la tête sous le point d'arrivée" "$([ "$OK3" = 1 ] && [ "$nb_cote" = 0 ] && echo 1 || echo 0)"
verdict "(4) « Annuler le zoom » : le zoom d'avant, en un pas" "$OK4"
verdict "(5) le témoin décoché : le zoom inchangé, la tête qui suit" "$([ "$OK5" = 1 ] && [ "$nb_temoin" = 0 ] && echo 1 || echo 0)"
verdict "(6) retenu au poste ; en anglais" "$([ "$decoche" = 1 ] && [ "$en" = 1 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "ZOOM-A-LA-REGLE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "ZOOM-A-LA-REGLE : glisser sur la règle zoome autour du point saisi, et la tête suit"
