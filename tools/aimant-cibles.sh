#!/usr/bin/env bash
# D574.1, D574.2 — L'AIMANTATION AUX CLIPS ET AUX REPÈRES ET LE CURSEUR MAGNÉTIQUE (Affichage ▸ Aimanter aux clips et
# aux repères ; Affichage ▸ Aimanter à la tête de lecture (curseur magnétique) ; les *Snap Types* « Events » et
# « Magnetic Cursor » de Cubase), MESURÉS PAR DE VRAIS GLISSERS (`glisser:arrangement:…`) PUIS LE PROJET RELU.
#
# LES RÈGLES GARDÉES. L'aimant à la mesure (1 920), son défaut. Piste 0 : un clip « A » de 0 à 1 440 et un clip « C » de
# 6 000 à 7 200 — leurs bords hors de la grille ; piste 1 : un clip « B » de 3 840 à 5 760. Chaque glisser lâche la
# position visée À 60 TICKS d'une cible (sous les 10 px de l'aimant à tout zoom raisonnable, et plus près d'elle que de
# la grille) ; L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D574) :
#   (a) B tiré à gauche, son début lâché à 1 440 + 60 : **1 440** case « clips » cochée, **1 920** sans elle ;
#   (b) la tête à 2 400 (mesure 1 + un temps), B lâché à 2 400 + 60 : **2 400** curseur magnétique coché, **1 920**
#       sans lui ;
#   (c) B tiré à droite, sa FIN lâchée à 6 000 − 60 (le début de C) : B commence **à 4 080** case « clips » cochée,
#       **à 3 840** sans elle ;
#   (d) la case « clips » cochée, B lâché loin de toute cible (début à 2 700) : la grille, **1 920**, comme sans elle ;
#   (e) les cases sont retenues : (c) et (d) se jouent dans le HOME de (a) sans toucher au menu, qui les dit cochées.
#
#   tools/aimant-cibles.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-aimant-cibles.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def piste(nom, debut, fin):
    corps = b"\x00\xff\x03" + bytes([len(nom)]) + nom.encode()
    t = 0
    for d in range(debut, fin, 480):
        dt = d - t
        v = [dt & 0x7F]; dt >>= 7
        while dt: v.insert(0, (dt & 0x7F) | 0x80); dt >>= 7
        corps += bytes(v) + bytes([0x90, 60, 100]) + bytes([0x83, 0x60]) + bytes([0x80, 60, 0])
        t = d + 480
    return corps + b"\x00\xff\x2f\x00"
pistes = [b"\x00\xff\x51\x03\x07\xa1\x20" + piste("Haut", 0, 7680), piste("Bas", 0, 7680)]   # le tempo en tête de la première
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, len(pistes), 480)
    + b"".join(b"MTrk" + struct.pack(">I", len(c)) + c for c in pistes))
mix = {"muted": False, "pan": 0.0, "solo": False, "volume": 1.0}
clip = lambda nom, debut, long: {"start": debut, "length": long, "sourceStart": debut, "sourceLength": long, "name": nom}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Haut", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "clips": [clip("A", 0, 1440), clip("C", 6000, 1200)]},
                      {"channel": 1, "color": "#FF9B6BFF", "effects": [], "name": "Bas", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "clips": [clip("B", 3840, 1920)]}]},
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
clipB() {   # le début de B, ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
    c = [c for t in d['tracks'] for c in t.get('clips', []) if c.get('name') == 'B'][0]
except Exception:
    print('ABSENT'); sys.exit(0)
print(c['start'])" "$1"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
CLIPS="menu:Affichage > Aimanter aux clips et aux repères"
TETE="menu:Affichage > Aimanter à la tête de lecture (curseur magnétique)"

echo "=== D574.1, D574.2 : l'aimantation aux clips et aux repères, le curseur magnétique ==="
course geometrie "$(mktemp -d "$brouillon/home.XXXX")" "$brouillon/base" VSM_DELAI=2000 VSM_VUE="sans-rapport,arrangement" \
    VSM_GESTE_APRES="1200:relever-clips"
ligne="$(grep '^VSM_CLIP_ECRAN : piste 1 "B"' "$brouillon/geometrie.txt" | tail -1)"
# Le glisser part du milieu de B ; il ARRIVE là où le début de B vaut la cible visée (le milieu est à +960 du début).
read -r ga gb gc gd ppt < <(python3 -c "
import re, sys
m = re.search(r'x ([-0-9.]+)\.\.([-0-9.]+) y ([0-9]+)\.\.([0-9]+) sur ([0-9]+)x([0-9]+)', sys.argv[1])
if not m: print('AUCUNE AUCUNE AUCUNE AUCUNE 0'); sys.exit(0)
x1, x2, y1, y2, w, h = (float(v) for v in m.groups())
parTick = (x2 - x1) / 1920.0
fy = (y1 + y2) / 2 / h
xc = (x1 + x2) / 2
def vers(debut):
    return f'arrangement:{xc / w:.5f},{fy:.5f}:{(xc + (debut - 3840) * parTick) / w:.5f}'
print(vers(1440 + 60), vers(2400 + 60), vers(6000 - 60 - 1920), vers(2700), f'{parTick:.5f}')
" "$ligne")
echo "       B à l'écran : ${ligne#VSM_CLIP_ECRAN : } ; $ppt px/tick, l'aimant des cibles à $(python3 -c "import sys; p = float(sys.argv[1]); print(round(10 / p) if p > 0 else '?')" "$ppt") ticks"
[ "$ga" != AUCUNE ] || { echo "  RATÉ la géométrie : B introuvable (relever-clips)"; exit 1; }
for x in ta ca tb cb tc cc dd; do cp -r "$brouillon/base" "$brouillon/$x"; done
maison="$(mktemp -d "$brouillon/home.XXXX")"
course temoin-a "$(mktemp -d "$brouillon/home.XXXX")" "$brouillon/ta" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" \
    VSM_GESTE_APRES="1200:glisser:$ga;1700:enregistrer:$brouillon/ta"
course clips-a "$maison" "$brouillon/ca" VSM_DELAI=2800 VSM_VUE="sans-rapport,arrangement" \
    VSM_GESTE_APRES="1000:$CLIPS;1400:glisser:$ga;1900:enregistrer:$brouillon/ca"
course temoin-b "$(mktemp -d "$brouillon/home.XXXX")" "$brouillon/tb" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement,tete:1,tete-temps:1" \
    VSM_GESTE_APRES="1200:glisser:$gb;1700:enregistrer:$brouillon/tb"
course tete-b "$(mktemp -d "$brouillon/home.XXXX")" "$brouillon/cb" VSM_DELAI=2800 VSM_VUE="sans-rapport,arrangement,tete:1,tete-temps:1" \
    VSM_GESTE_APRES="1000:$TETE;1400:glisser:$gb;1900:enregistrer:$brouillon/cb"
course temoin-c "$(mktemp -d "$brouillon/home.XXXX")" "$brouillon/tc" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" \
    VSM_GESTE_APRES="1200:glisser:$gc;1700:enregistrer:$brouillon/tc"
course clips-c "$maison" "$brouillon/cc" VSM_DELAI=2800 VSM_VUE="sans-rapport,arrangement" \
    VSM_GESTE_APRES="1000:lister-menus;1400:glisser:$gc;1900:enregistrer:$brouillon/cc"
course loin-d "$maison" "$brouillon/dd" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" \
    VSM_GESTE_APRES="1200:glisser:$gd;1700:enregistrer:$brouillon/dd"

a0="$(clipB "$brouillon/ta")"; a1="$(clipB "$brouillon/ca")"; b0="$(clipB "$brouillon/tb")"; b1="$(clipB "$brouillon/cb")"
c0="$(clipB "$brouillon/tc")"; c1="$(clipB "$brouillon/cc")"; d1="$(clipB "$brouillon/dd")"
cases="$(grep -h "^VSM_MENU_LISTE : Affichage > Aimanter" "$brouillon/clips-c.txt" | sed 's/^VSM_MENU_LISTE : Affichage > //')"
grep -h "aux clips et aux repères\|, à la tête" "$brouillon/clips-a.txt" "$brouillon/tete-b.txt" | head -2 | sed 's/^/       /'
echo "       début de B, sans / avec : (a) [$a0 / $a1] ; (b) [$b0 / $b1] ; (c) [$c0 / $c1] ; (d) avec [$d1]"
echo "$cases" | sed 's/^/       menu relancé : /'

verdict "(a) le début sur la fin d'A : 1 440 case cochée, 1 920 sans elle" "$([ "$a0" = 1920 ] && [ "$a1" = 1440 ] && echo 1 || echo 0)"
verdict "(b) le curseur magnétique : 2 400 coché, 1 920 sans lui" "$([ "$b0" = 1920 ] && [ "$b1" = 2400 ] && echo 1 || echo 0)"
verdict "(c) la FIN sur le début de C : B à 4 080 case cochée, 3 840 sans elle" "$([ "$c0" = 3840 ] && [ "$c1" = 4080 ] && echo 1 || echo 0)"
verdict "(d) loin de toute cible : la grille (1 920)" "$([ "$d1" = 1920 ] && echo 1 || echo 0)"
verdict "(e) retenue : la case « clips » cochée au relancement, la tête non" \
    "$(echo "$cases" | grep -q "^Aimanter aux clips et aux repères \[cochée\]" \
       && echo "$cases" | grep "^Aimanter à la tête" | grep -vq "cochée" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "AIMANT-CIBLES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "AIMANT-CIBLES : un clip tiré se pose sur les bords des autres, les repères et la tête, la grille ailleurs"
