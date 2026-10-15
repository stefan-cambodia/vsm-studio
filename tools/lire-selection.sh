#!/usr/bin/env bash
# D553.2 — LIRE LA SÉLECTION (Transport ▸ Lire la sélection, Maj+Espace ; le *Play Selection Range* de Cubase), MESURÉ
# PAR LE RELEVÉ DU TRANSPORT PENDANT ET APRÈS.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 120 BPM : une mesure = 2 s). Une piste, A [1 920, 3 840) — de 2 à 4 s — et
# B [5 760, 7 680) ; A choisi.
#   (1) Maj+Espace : 0,8 s plus tard, la lecture court et la tête est dans A (entre 2 et 4 s) ;
#   (2) 3,4 s plus tard : le transport est ARRÊTÉ et la tête posée EXACTEMENT à la fin de A (4,000 s) — et non à la
#       fin du morceau, ni à 2 s de B ;
#   (3) la préférence « Revenir au point de départ » (écrite dans le HOME du banc), par le MENU Transport : après la
#       fin, la tête revient au début de A (2,000 s) ;
#   (4) rien de choisi : « Lire la sélection : Rien n'est choisi… » dit, le transport reste arrêté ;
#   (5) LA BOUCLE allumée sur [1 920, 2 880), à l'intérieur de A : elle ramène la tête avant la fin de A — la lecture
#       de la sélection est ABANDONNÉE et c'est dit (« abandonnée, la tête a reculé »), la lecture continue dans la
#       boucle (3,4 s après le départ : en lecture, la tête entre 2 et 3 s), et rien ne l'arrête à 4 s.
#
#   tools/lire-selection.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-lire-selection.XXXXXX")"
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
t = 0
for debut in (1920, 2880, 5760, 6720):
    corps += vlq(debut - t) + bytes([0x90, 60, 100]); corps += vlq(480) + bytes([0x80, 60, 0]); t = debut + 480
corps += b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
clip = lambda n, d: {"start": d, "length": 1920, "sourceStart": d, "sourceLength": 1920, "name": n}
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.minimoog"}, "clips": [clip("A", 1920), clip("B", 5760)]}]},
          open(f"{b}/base/project.json", "w"), indent=1)
# LE PROJET DE (5) : le même, la boucle allumée sur [1920, 2880).
os.makedirs(f"{b}/boucle", exist_ok=True)
import shutil
shutil.copytree(f"{b}/base/midi", f"{b}/boucle/midi")
p = json.load(open(f"{b}/base/project.json"))
p["transport"]["loop"] = {"enabled": True, "startTick": 1920, "endTick": 2880}
json.dump(p, open(f"{b}/boucle/project.json", "w"), indent=1)
# LE HOME DE (3) : la préférence « Revenir au point de départ » posée dans le fichier de réglages, comme D372.
os.makedirs(f"{b}/home-retour/VintageSynthMidiStudio", exist_ok=True)
open(f"{b}/home-retour/VintageSynthMidiStudio/VintageSynthMidiStudio.settings", "w").write(
    '<?xml version="1.0" encoding="UTF-8"?>\n<PROPERTIES>\n  <VALUE name="retourAuDepartALArret" val="1"/>\n</PROPERTIES>\n')
PY

course() {   # $1 = nom ; $2 = HOME (vide : un neuf) ; le reste : variables
    local nom="$1" maison="$2"
    shift 2
    [ -n "$maison" ] || maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
releves() { grep -h "^VSM_DECOMPTE : " "$brouillon/$1.txt" | sed 's/^VSM_DECOMPTE : //'; }
tete() { echo "$1" | sed -n 's/.*tête \([-0-9.]*\) s.*/\1/p'; }
etat() { echo "$1" | sed -n 's/.*transport \(.*\)$/\1/p'; }
dans() { python3 -c "
import sys
try:
    sys.exit(0 if float(sys.argv[2]) <= float(sys.argv[1]) <= float(sys.argv[3]) else 1)
except ValueError:
    sys.exit(1)" "$1" "$2" "$3"; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D553.2 : lire la sélection ==="
course lire "" VSM_DELAI=5200 VSM_VUE="sans-rapport,arrangement,premier-clip:0" \
    VSM_GESTE_APRES="1200:touche:shift + spacebar;2000:relever-decompte;4600:relever-decompte"
course retour "$brouillon/home-retour" VSM_DELAI=5200 VSM_VUE="sans-rapport,arrangement,premier-clip:0" \
    VSM_GESTE_APRES="1200:menu:Lire la sélection;4600:relever-decompte"
course boucle "" VSM_PROJET="$brouillon/boucle" VSM_DELAI=5200 VSM_VUE="sans-rapport,arrangement,premier-clip:0" \
    VSM_GESTE_APRES="1200:touche:shift + spacebar;4600:relever-decompte"
course rien "" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" \
    VSM_GESTE_APRES="1200:touche:shift + spacebar;1800:relever-decompte"

mapfile -t l < <(releves lire); mapfile -t r < <(releves retour); mapfile -t n < <(releves rien)
mapfile -t o < <(releves boucle)
for x in "${l[@]}"; do echo "       lire    : $x"; done
for x in "${r[@]}"; do echo "       retour  : $x"; done
for x in "${n[@]}"; do echo "       rien    : $x"; done
for x in "${o[@]}"; do echo "       boucle  : $x"; done
grep -h "^VSM_LIRE_SELECTION" "$brouillon/lire.txt" "$brouillon/rien.txt" "$brouillon/boucle.txt" | sed 's/^/       /'

verdict "(1) pendant : la lecture court, la tête dans A" \
    "$([ "${#l[@]}" = 2 ] && [ "$(etat "${l[0]}")" = "en lecture" ] && dans "$(tete "${l[0]}")" 2.0 4.0 && echo 1 || echo 0)"
verdict "(2) après : arrêté, la tête exactement à la fin de A" \
    "$([ "${#l[@]}" = 2 ] && [ "$(etat "${l[1]}")" = "arrêté" ] && [ "$(tete "${l[1]}")" = "4.000" ] && echo 1 || echo 0)"
verdict "(3) avec le retour au départ, par le menu : la tête revient au début de A" \
    "$([ "${#r[@]}" = 1 ] && [ "$(etat "${r[0]}")" = "arrêté" ] && [ "$(tete "${r[0]}")" = "2.000" ] && echo 1 || echo 0)"
verdict "(4) rien de choisi : dit, le transport reste arrêté" \
    "$([ "${#n[@]}" = 1 ] && [ "$(etat "${n[0]}")" = "arrêté" ] && grep -q "^VSM_BOITE : Lire la sélection : Rien n'est choisi" "$brouillon/rien.txt" && echo 1 || echo 0)"
verdict "(5) la boucle ramène la tête : la lecture de la sélection est abandonnée, dit, et la boucle continue" \
    "$([ "${#o[@]}" = 1 ] && [ "$(etat "${o[0]}")" = "en lecture" ] && dans "$(tete "${o[0]}")" 2.0 3.0 \
       && grep -q "^VSM_LIRE_SELECTION : abandonnée, la tête a reculé" "$brouillon/boucle.txt" \
       && ! grep -q "^VSM_LIRE_SELECTION : arrêt" "$brouillon/boucle.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "LIRE-SELECTION : $rates contrôle(s) raté(s)"; exit 1; fi
echo "LIRE-SELECTION : la sélection se joue et la lecture s'arrête à sa fin"
