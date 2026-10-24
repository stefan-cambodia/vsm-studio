#!/usr/bin/env bash
# D586.1 — LE ZOOM EN GLISSANT SUR LA RÈGLE DU PIANO ROLL (Transport ▸ Zoomer en glissant sur la règle, la préférence de
# D585.2 ; le *Zoom while Locating in Time Scale* de Cubase), MESURÉ PAR LE RELEVÉ DU ZOOM DU PIANO ROLL APRÈS UN GLISSÉ,
# LE TÉMOIN DÉCOCHÉ À CÔTÉ.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 4/4, 120 BPM). Une piste de huit mesures, la vue du piano roll. Le point de la
# mesure 5 (+ un temps, tick 8 160) se lit d'abord sur le relevé du piano roll (`zoom=`, `defilement=`, `clavier=`,
# `largeur=`) : sa règle a la même abscisse et la même largeur. Le glissé passe par `glisser:pianoroll.regle:fx,0.5:fx,+100px`
# — l'appui au milieu de la règle, en fraction (lisible par un binaire d'avant D585.2), l'arrivée 100 px plus bas.
# Un banc à part de celui de D585.2 (l'en-tête de D586 l'annonçait « étendu ») : l'arrangement et le piano roll ne
# s'ouvrent pas dans la même vue, et un banc qui raterait sur l'un ne doit pas cacher l'autre.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D586) :
#   (1) 100 px vers le BAS → ×2,00 (à 1 % près), le tick saisi sous le pointeur (à 1 px près) ;
#   (2) 100 px vers le HAUT → ×0,50 ;
#   (3) un glissé horizontal pur → le zoom inchangé ;
#   (4) Maj+glissé → la boucle dessinée, le zoom inchangé ;
#   (5) le TÉMOIN décoché → le zoom inchangé.
#
#   tools/zoom-regle-pianoroll.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-zoom-pianoroll.XXXXXX")"
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
corps += vlq(14400) + b"\x90\x3e\x5a" + vlq(480) + b"\x80\x3e\x00"   # la dernière noire au tick 14 880
corps += b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
clip = {"sourceStart": 0, "sourceLength": 15360, "start": 0, "length": 15360, "color": "#FF6B9BFF"}
json.dump({"format": "vsm-project", "version": 1, "title": "zoom-pianoroll", "midi": {"file": "midi/arrangement.mid"},
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
    env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,pianoroll" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|GESTE|GLISSE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
champ() { grep -h "^VSM_PIANOROLL_RANG : " "$brouillon/$1.txt" | tail -1 | sed -n "s/.* $2=\([^ ]*\).*/\1/p"; }
glisse() { grep -h "^VSM_ZOOM_PIANOROLL : glissé sur la règle" "$brouillon/$1.txt" | tail -1; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
OPTION="menu:Transport > Zoomer en glissant sur la règle"

echo "=== D586.1 : le zoom en glissant sur la règle du piano roll ==="
course reperage "" VSM_DELAI=1500 VSM_GESTE_APRES="900:relever-pianoroll"
zoom0="$(champ reperage zoom)"; def0="$(champ reperage defilement)"; clav="$(champ reperage clavier)"; larg="$(champ reperage largeur)"
if [ -z "$zoom0" ] || [ -z "$clav" ] || [ -z "$larg" ]; then
    echo "REFUS : le relevé du piano roll ne dit pas zoom, clavier et largeur ($(grep -h '^VSM_PIANOROLL_RANG' "$brouillon/reperage.txt" | tail -1))"
    exit 2
fi
eval "$(python3 - "$zoom0" "$def0" "$clav" "$larg" <<'PY'
import sys
ppt, defil, clav, larg = float(sys.argv[1]), float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4])
x5 = clav + (8160 - defil) * ppt
x7 = clav + (12000 - defil) * ppt
print(f"X5={x5:.1f} FX5={x5 / larg:.5f} FX7={min(0.98, x7 / larg):.5f}")
PY
)"
echo "       repérage : zoom $zoom0 px/tick, défilement $def0, clavier $clav px sur $larg ; le tick 8 160 à x $X5"

BAS="glisser:pianoroll.regle:$FX5,0.5:$FX5,+100px"
HAUT="glisser:pianoroll.regle:$FX5,0.5:$FX5,-100px"
COTE="glisser:pianoroll.regle:$FX5,0.5:$FX7,+0px"
MAJ="glisser:pianoroll.regle:$FX5,0.5:$FX7,+100px:maj"
maison="$(mktemp -d "$brouillon/home.XXXX")"
course bas "" VSM_DELAI=1800 VSM_GESTE_APRES="900:$BAS;1200:relever-pianoroll"
course haut "" VSM_DELAI=1800 VSM_GESTE_APRES="900:$HAUT;1200:relever-pianoroll"
course cote "" VSM_DELAI=1800 VSM_GESTE_APRES="900:$COTE;1200:relever-pianoroll;1300:relever-tete"
course maj "" VSM_DELAI=1800 VSM_GESTE_APRES="900:$MAJ;1200:relever-pianoroll;1300:relever-boucle"
course temoin "$maison" VSM_DELAI=1800 VSM_GESTE_APRES="700:$OPTION;900:$BAS;1200:relever-pianoroll"

for n in bas haut cote maj temoin; do echo "       $n : zoom $(champ $n zoom) ; $(glisse $n | cut -c1-160)"; done
grep -h "^VSM_BOUCLE" "$brouillon/maj.txt" | sed 's/^/       /'
eval "$(python3 - "$(glisse bas)" "$(glisse haut)" "$zoom0" "$(champ cote zoom)" "$(champ maj zoom)" "$(champ temoin zoom)" <<'PY'
import re, sys
bas, haut, z0, zc, zm, zt = sys.argv[1:7]
def lire(l):
    m = re.search(r"\(×([0-9.]+)\), le tick saisi ([0-9]+) à x ([0-9.-]+) pour un appui à x ([0-9.-]+)", l)
    return m.groups() if m else None
b, h = lire(bas), lire(haut)
ok1 = int(b is not None and abs(float(b[0]) - 2.0) <= 0.02 and abs(float(b[2]) - float(b[3])) <= 1.0)
ok2 = int(h is not None and abs(float(h[0]) - 0.5) <= 0.005)
egal = lambda z: z != "" and abs(float(z) - float(z0)) < 1e-9
print(f"OK1={ok1} OK2={ok2} OK3={int(egal(zc))} OK4Z={int(egal(zm))} OK5={int(egal(zt))}")
PY
)"
nb_cote="$(grep -c "^VSM_ZOOM_PIANOROLL" "$brouillon/cote.txt")"; nb_maj="$(grep -c "^VSM_ZOOM_PIANOROLL" "$brouillon/maj.txt")"
nb_temoin="$(grep -c "^VSM_ZOOM_PIANOROLL" "$brouillon/temoin.txt")"
boucle="$(grep -h "^VSM_BOUCLE : projet" "$brouillon/maj.txt" | tail -1 | sed -n 's/^VSM_BOUCLE : projet [a-z]* \[\([0-9]*\),\([0-9]*\)\].*/\1 \2/p')"
read -r b0 b1 <<< "${boucle:-0 0}"

verdict "(1) 100 px vers le bas : ×2,00, le tick saisi sous le pointeur" "$OK1"
verdict "(2) 100 px vers le haut : ×0,50" "$OK2"
verdict "(3) de côté : le zoom inchangé" "$([ "$OK3" = 1 ] && [ "$nb_cote" = 0 ] && echo 1 || echo 0)"
verdict "(4) Maj+glissé : la boucle dessinée, le zoom inchangé" \
    "$([ "$OK4Z" = 1 ] && [ "$nb_maj" = 0 ] && [ "$b1" -gt "$b0" ] && echo 1 || echo 0)"
verdict "(5) le témoin décoché : le zoom inchangé" "$([ "$OK5" = 1 ] && [ "$nb_temoin" = 0 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "ZOOM-REGLE-PIANOROLL : $rates contrôle(s) raté(s)"; exit 1; fi
echo "ZOOM-REGLE-PIANOROLL : glisser sur la règle du piano roll zoome autour du point saisi ; Maj y dessine toujours la boucle"
