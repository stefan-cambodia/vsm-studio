#!/usr/bin/env bash
# D556.1 — LIRE JUSQU'AU REPÈRE SUIVANT (Transport ▸ Lire jusqu'au repère suivant, Alt+N ; le *Play until Next Marker*
# de Cubase), MESURÉ PAR LE RELEVÉ DU TRANSPORT PENDANT ET APRÈS.
#
# LES RÈGLES GARDÉES (480 ticks par noire, 4/4, 120 BPM : une mesure = 2 s). Des notes sur six mesures, deux repères :
# « Couplet » à 1 920 (2 s), « Refrain » à 5 760 (6 s).
#   (1) la tête à 0, Alt+N : 0,8 s après, en lecture avant 2 s ; 2,6 s après, arrêté EXACTEMENT sur « Couplet » (2,000 s) ;
#   (2) la tête POSÉE sur « Couplet » (mesure 2), par le menu : la lecture va au SUIVANT — 4,6 s après, arrêté à 6,000 s,
#       et non à 2 s ;
#   (3) la tête sur « Refrain », le dernier : « Aucun repère après la tête » dit, rien ne part.
#
#   tools/repere-suivant.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-repere-suivant.XXXXXX")"
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
for d in range(0, 11520, 960):
    corps += vlq(d - t) + bytes([0x90, 60, 100]); corps += vlq(480) + bytes([0x80, 60, 0]); t = d + 480
corps += b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "markers": [{"tick": 1920, "name": "Couplet"}, {"tick": 5760, "name": "Refrain"}],
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.minimoog"}}]}, open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {
    local nom="$1"
    shift
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,arrangement" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|POSITION) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
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

echo "=== D556.1 : lire jusqu'au repère suivant ==="
course zero VSM_DELAI=4500 VSM_GESTE_APRES="1200:touche:alt + N;2000:relever-decompte;3800:relever-decompte"
course pose VSM_DELAI=6500 VSM_POSITION=2 VSM_GESTE_APRES="1200:menu:Lire jusqu'au repère suivant;5800:relever-decompte"
course dernier VSM_DELAI=2500 VSM_POSITION=4 VSM_GESTE_APRES="1200:touche:alt + N;1800:relever-decompte"

mapfile -t z < <(releves zero); mapfile -t p < <(releves pose); mapfile -t d < <(releves dernier)
for x in "${z[@]}"; do echo "       de 0     : $x"; done
for x in "${p[@]}"; do echo "       posée    : $x"; done
for x in "${d[@]}"; do echo "       dernier  : $x"; done
grep -h "^VSM_LIRE_REPERE\|^VSM_LIRE_SELECTION\|^VSM_BOITE" "$brouillon/zero.txt" "$brouillon/pose.txt" "$brouillon/dernier.txt" | sed 's/^/       /'

verdict "(1) de 0 : la lecture, puis l'arrêt exactement sur « Couplet »" \
    "$([ "${#z[@]}" = 2 ] && [ "$(etat "${z[0]}")" = "en lecture" ] && dans "$(tete "${z[0]}")" 0.0 2.0 \
       && [ "$(etat "${z[1]}")" = "arrêté" ] && [ "$(tete "${z[1]}")" = "2.000" ] && echo 1 || echo 0)"
verdict "(2) posée sur un repère : la lecture va au suivant" \
    "$([ "${#p[@]}" = 1 ] && [ "$(etat "${p[0]}")" = "arrêté" ] && [ "$(tete "${p[0]}")" = "6.000" ] && echo 1 || echo 0)"
verdict "(3) après le dernier repère : dit, rien ne part" \
    "$([ "${#d[@]}" = 1 ] && [ "$(etat "${d[0]}")" = "arrêté" ] \
       && grep -q "^VSM_BOITE : Lire jusqu'au repère suivant : Aucun repère après la tête" "$brouillon/dernier.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "REPERE-SUIVANT : $rates contrôle(s) raté(s)"; exit 1; fi
echo "REPERE-SUIVANT : la lecture va jusqu'au repère suivant, et s'y pose"
