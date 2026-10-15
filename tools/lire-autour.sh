#!/usr/bin/env bash
# D555.1 — LIRE JUSQU'AU DÉBUT DE LA SÉLECTION, LIRE DEPUIS SA FIN (Transport ; les *Play until Selection Start* et
# *Play from Selection End* de Cubase), MESURÉ PAR LE RELEVÉ DU TRANSPORT PENDANT ET APRÈS.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 4/4, 120 BPM : une mesure = 2 s). Une piste, A [3 840, 5 760) — de 4 à 6 s
# — choisi, et C [7 680, 9 600) après lui ; B [0, 1 920) seul dans un second projet ; A seul dans un troisième.
#   (1) « Lire jusqu'au début de la sélection », par Maj+I : 0,8 s après, en lecture, la tête entre 2 et 4 s (la mesure d'AVANT A) ;
#       2,6 s après : arrêté, la tête EXACTEMENT au début de A (4,000 s) ;
#   (2) « Lire depuis la fin de la sélection », par le menu : 0,8 s après, en lecture, la tête entre 6 et 8 s ; 2,6 s après :
#       arrêté, la tête exactement une mesure après la fin de A (8,000 s) ;
#   (3) une sélection qui commence au début du morceau (B) : « il n'y a rien à lire avant elle » dit, rien ne part ;
#   (4) rien de choisi : dit, rien ne part ;
#   (5) une sélection qui FINIT avec le morceau (A seul) : « il n'y a rien à lire après elle » dit, rien ne part. Le
#       premier passage du banc, sans C, a vu la lecture partir de 6 s et s'arrêter aussitôt, rembobinée à 0, muette.
#
#   tools/lire-autour.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-lire-autour.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
def ecrire(nom, debuts):
    corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie"
    t = 0
    for d in sorted(x + k for x in debuts for k in (0, 960)):
        corps += vlq(d - t) + bytes([0x90, 60, 100]); corps += vlq(480) + bytes([0x80, 60, 0]); t = d + 480
    corps += b"\x00\xff\x2f\x00"
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                           "instrument": {"preferredPlugin": "vsm.minimoog"},
                           "clips": [{"start": d, "length": 1920, "sourceStart": d, "sourceLength": 1920, "name": f"C{k}"}
                                     for k, d in enumerate(debuts)]}]},
              open(f"{b}/{nom}/project.json", "w"), indent=1)
ecrire("A", [3840, 7680])
ecrire("B", [0])
ecrire("Z", [3840])
PY

course() {   # $1 = nom ; $2 = projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
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
AVANT="menu:Lire jusqu'au début de la sélection"
APRES="menu:Lire depuis la fin de la sélection"
VUE="sans-rapport,arrangement,premier-clip:0"

echo "=== D555.1 : lire autour de la sélection ==="
# (1) PAR LA TOUCHE (Maj+I), (2) PAR LE MENU : les deux portes, une course chacune.
course avant "$brouillon/A" VSM_DELAI=4500 VSM_VUE="$VUE" VSM_GESTE_APRES="1200:touche:shift + I;2000:relever-decompte;3800:relever-decompte"
course apres "$brouillon/A" VSM_DELAI=4500 VSM_VUE="$VUE" VSM_GESTE_APRES="1200:$APRES;2000:relever-decompte;3800:relever-decompte"
course debut "$brouillon/B" VSM_DELAI=2500 VSM_VUE="$VUE" VSM_GESTE_APRES="1200:$AVANT;1800:relever-decompte"
course fin "$brouillon/Z" VSM_DELAI=2500 VSM_VUE="$VUE" VSM_GESTE_APRES="1200:$APRES;1800:relever-decompte"
course rien "$brouillon/A" VSM_DELAI=2500 VSM_VUE="sans-rapport,arrangement" VSM_GESTE_APRES="1200:$APRES;1800:relever-decompte"

mapfile -t a < <(releves avant); mapfile -t p < <(releves apres); mapfile -t d < <(releves debut); mapfile -t n < <(releves rien)
mapfile -t f < <(releves fin)
for x in "${a[@]}"; do echo "       avant   : $x"; done
for x in "${p[@]}"; do echo "       après   : $x"; done
for x in "${d[@]}"; do echo "       début   : $x"; done
for x in "${n[@]}"; do echo "       rien    : $x"; done
for x in "${f[@]}"; do echo "       fin     : $x"; done
grep -h "^VSM_LIRE_AUTOUR\|^VSM_LIRE_SELECTION\|^VSM_BOITE" "$brouillon/avant.txt" "$brouillon/apres.txt" "$brouillon/debut.txt" "$brouillon/rien.txt" "$brouillon/fin.txt" | sed 's/^/       /'

verdict "(1) jusqu'au début : la mesure d'avant, puis l'arrêt exactement sur le début" \
    "$([ "${#a[@]}" = 2 ] && [ "$(etat "${a[0]}")" = "en lecture" ] && dans "$(tete "${a[0]}")" 2.0 4.0 \
       && [ "$(etat "${a[1]}")" = "arrêté" ] && [ "$(tete "${a[1]}")" = "4.000" ] && echo 1 || echo 0)"
verdict "(2) depuis la fin : la mesure d'après, puis l'arrêt une mesure plus loin" \
    "$([ "${#p[@]}" = 2 ] && [ "$(etat "${p[0]}")" = "en lecture" ] && dans "$(tete "${p[0]}")" 6.0 8.0 \
       && [ "$(etat "${p[1]}")" = "arrêté" ] && [ "$(tete "${p[1]}")" = "8.000" ] && echo 1 || echo 0)"
verdict "(3) une sélection au début du morceau : rien avant, dit, rien ne part" \
    "$([ "${#d[@]}" = 1 ] && [ "$(etat "${d[0]}")" = "arrêté" ] \
       && grep -q "^VSM_BOITE : Lire jusqu'au début de la sélection : La sélection commence au début du morceau" "$brouillon/debut.txt" && echo 1 || echo 0)"
verdict "(4) rien de choisi : dit, rien ne part" \
    "$([ "${#n[@]}" = 1 ] && [ "$(etat "${n[0]}")" = "arrêté" ] \
       && grep -q "^VSM_BOITE : Lire depuis la fin de la sélection : Rien n'est choisi" "$brouillon/rien.txt" && echo 1 || echo 0)"
verdict "(5) une sélection qui finit avec le morceau : rien après, dit, rien ne part" \
    "$([ "${#f[@]}" = 1 ] && [ "$(etat "${f[0]}")" = "arrêté" ] \
       && grep -q "^VSM_BOITE : Lire depuis la fin de la sélection : La sélection finit avec le morceau" "$brouillon/fin.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "LIRE-AUTOUR : $rates contrôle(s) raté(s)"; exit 1; fi
echo "LIRE-AUTOUR : l'entrée et la sortie d'un passage se jouent, une mesure chacune"
