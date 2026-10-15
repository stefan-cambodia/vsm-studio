#!/usr/bin/env bash
# D556.2 — LA TÊTE D'UN ÉVÉNEMENT À L'AUTRE (Transport ▸ Tête : événement suivant / précédent, N et B ; les *Locate Next
# / Previous Event* de Cubase), MESURÉ PAR LE RELEVÉ DE LA TÊTE APRÈS CHAQUE TOUCHE.
#
# LES RÈGLES GARDÉES. La piste choisie : un clip A [0, 1 920) avec une note [480, 960), un clip B [3 840, 5 760) — sa
# matière à sa place — avec une note [3 840, 4 320) ; une note [2 400, 2 880) ENTRE les deux clips, que la lecture
# n'entend pas, et une note [1 200, 1 440) dans A, rendue MUETTE au début de chaque course par le geste réel (la seule
# de vélocité < 64, choisie par « Notes plus faibles que 64 (1) », puis « Rendre muet / audible » : le format de projet
# n'écrit pas les notes muettes).
#   (1) N, sept fois depuis 0 : 480, 960, 1 920, 3 840, 4 320, 5 760, puis 5 760 encore — ni la note hors clip (2 400),
#       ni la muette (1 200) ; la septième dit « aucun après la tête » ;
#   (2) B, deux fois depuis 5 760 : 4 320, 3 840 ;
#   (3) depuis 0, B : la tête reste à 0, et c'est dit.
#
#   tools/tete-evenement.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-tete-evenement.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
notes = [(480, 960, 60, 100), (1200, 1440, 62, 30), (2400, 2880, 64, 100), (3840, 4320, 65, 100)]
ev = []
for d, f, h, v in notes:
    ev.append((d, 1, bytes([0x90, h, v]))); ev.append((f, 0, bytes([0x80, h, 0])))
ev.sort(key=lambda e: (e[0], e[1]))
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie"
t = 0
for tick, _, m in ev:
    corps += vlq(tick - t) + m; t = tick
corps += b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
clip = lambda n, d: {"start": d, "length": 1920, "sourceStart": d, "sourceLength": 1920, "name": n}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "clips": [clip("A", 0), clip("B", 3840)]}]}, open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {
    local nom="$1"
    shift
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,pianoroll,piste:0" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|POSITION) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
tetes() { grep -h "^VSM_TETE : tick " "$brouillon/$1.txt" | sed 's/^VSM_TETE : tick \([0-9]*\),.*/\1/' | tr '\n' ' ' | sed 's/ $//'; }
MUETTE="500:menu:Édition > Sélection > Notes plus faibles que 64 (1);700:menu:Édition > Rendre muet / audible"
suite() { local s="$MUETTE" ms=1000 k; for k in $(seq 1 "$2"); do s="$s;$ms:touche:$1;$((ms + 150)):relever-tete"; ms=$((ms + 300)); done; echo "$s"; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D556.2 : la tête d'un événement à l'autre ==="
course avant VSM_DELAI=4000 VSM_GESTE_APRES="$(suite N 7)"
course arriere VSM_DELAI=2500 VSM_POSITION=4 VSM_GESTE_APRES="$(suite B 2)"
course debut VSM_DELAI=2000 VSM_GESTE_APRES="$(suite B 1)"

a="$(tetes avant)"; r="$(tetes arriere)"; d="$(tetes debut)"
grep -h "^VSM_TETE_EVENEMENT : aucun" "$brouillon/avant.txt" "$brouillon/debut.txt" | sed 's/^/       /'
grep -h "^VSM_POSITION" "$brouillon/arriere.txt" | head -2 | sed 's/^/       /'
echo "       N ×7 depuis 0 : [$a] ; B ×2 [$r] ; B depuis 0 [$d]"

verdict "(1) N : les bords des clips et des notes entendues, rien d'autre ; la septième dit qu'il n'y a plus rien" \
    "$([ "$a" = "480 960 1920 3840 4320 5760 5760" ] && grep -q "^VSM_TETE_EVENEMENT : aucun après la tête" "$brouillon/avant.txt" && echo 1 || echo 0)"
verdict "(2) B : le chemin inverse" "$([ "$r" = "4320 3840" ] && echo 1 || echo 0)"
verdict "(3) B depuis 0 : la tête reste, et c'est dit" \
    "$([ "$d" = "0" ] && grep -q "^VSM_TETE_EVENEMENT : aucun avant la tête" "$brouillon/debut.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "TETE-EVENEMENT : $rates contrôle(s) raté(s)"; exit 1; fi
echo "TETE-EVENEMENT : la tête va d'un bord de note ou de clip au suivant"
