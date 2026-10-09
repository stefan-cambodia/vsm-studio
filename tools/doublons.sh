#!/usr/bin/env bash
# D546.1 — SUPPRIMER LES DOUBLONS, MESURÉ PAR LE .MID EXPORTÉ.
#
# LA RÈGLE GARDÉE. Une partie de huit notes (480 ticks par noire) dont trois doublons :
#   do  à 0 (vél. 80) et à 0 (vél. 100)              → reste celui à 100 ;
#   ré  à 480 (vél. 90) et à 490 (vél. 70)           → reste celui à 480 ;
#   mi  à 960 (vél. 60, 120 ticks) et à 985 (vél. 60, 240 ticks) → reste le plus long ;
#   fa et sol à 1440, deux hauteurs                  → restent tous les deux.
#   (1) tout choisi, « Supprimer les doublons » : le journal dit « 3 retirée(s) sur 8 », et le .mid exporté
#       a les CINQ notes attendues (début, hauteur, vélocité, durée) ;
#   (2) le témoin, sans le geste : huit notes ;
#   (3) un pas : Ctrl+Z → huit notes.
#
#   tools/doublons.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-doublons.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$brouillon"/*.mid "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
notes = [(0, 60, 80, 120), (0, 60, 100, 120), (480, 62, 90, 120), (490, 62, 70, 120),
         (960, 64, 60, 120), (985, 64, 60, 240), (1440, 65, 90, 120), (1440, 67, 90, 120)]
evts = []
for i, (t, h, v, d) in enumerate(notes):
    evts.append((t, 1, i, bytes([0x90, h, v])))
    evts.append((t + d, 0, i, bytes([0x80, h, 0])))
evts.sort()
corps, t0 = b"\x00\xff\x51\x03\x07\xa1\x20", 0
for t, _, _, e in evts:
    corps += vlq(t - t0) + e; t0 = t
corps += b"\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                 + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8},
                       "name": "Partie"}]}, open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; le reste : variables
    local nom="$1" maison
    shift
    maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
    env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,pianoroll" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU_CONTEXTE|TOUCHE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
notes() {   # « tick:hauteur:vélocité:durée » de chaque note d'un .mid, triées — ou « ABSENT »
    python3 - "$1" <<'PY'
import sys
try:
    data = open(sys.argv[1], "rb").read()
except Exception:
    print("ABSENT"); sys.exit(0)
if data[:4] != b"MThd":
    print("ABSENT"); sys.exit(0)
division = int.from_bytes(data[12:14], "big")
i, ouvertes, finies = 14, {}, []
while i + 8 <= len(data):
    n = int.from_bytes(data[i + 4:i + 8], "big"); corps = data[i + 8:i + 8 + n]; i += 8 + n
    j, st, t = 0, 0, 0
    while j < len(corps):
        dt = 0
        while corps[j] & 0x80: dt = (dt << 7) | (corps[j] & 0x7F); j += 1
        dt = (dt << 7) | corps[j]; j += 1; t += dt
        if corps[j] in (0xFF, 0xF0, 0xF7):
            j += 2 if corps[j] == 0xFF else 1
            L = 0
            while corps[j] & 0x80: L = (L << 7) | (corps[j] & 0x7F); j += 1
            L = (L << 7) | corps[j]; j += 1 + L
            continue
        if corps[j] & 0x80: st = corps[j]; j += 1
        kind = st & 0xF0
        if kind == 0x90 and corps[j + 1] > 0:
            ouvertes.setdefault((st & 0x0F, corps[j]), []).append((t, corps[j + 1]))
        elif kind in (0x80, 0x90):
            pile = ouvertes.get((st & 0x0F, corps[j]))
            if pile:
                debut, vel = pile.pop(0)
                finies.append((debut, corps[j], vel, t - debut))
        j += 1 if kind in (0xC0, 0xD0) else 2
k = 480 / division
print(" ".join(f"{round(a * k)}:{h}:{v}:{round(d * k)}" for a, h, v, d in sorted(finies)) or "AUCUNE")
PY
}
compte() { [ "$1" = ABSENT ] && echo ABSENT || echo "$1" | wc -w; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D546.1 : supprimer les doublons ==="
MENU="pianoroll:Supprimer les doublons"
course doublons VSM_DELAI=2500 VSM_TOUCHE="pianoroll:ctrl + A" VSM_MENU_CONTEXTE="$MENU" \
    VSM_GESTE_APRES="1500:exporter-midi:$brouillon/doublons.mid"
course temoin VSM_DELAI=2500 VSM_TOUCHE="pianoroll:ctrl + A" VSM_GESTE_APRES="1500:exporter-midi:$brouillon/temoin.mid"
course annuler VSM_DELAI=3000 VSM_TOUCHE="pianoroll:ctrl + A" VSM_MENU_CONTEXTE="$MENU" \
    VSM_GESTE_APRES="1300:touche:ctrl + Z;2000:exporter-midi:$brouillon/annule.mid"

grep -h "^VSM_DOUBLONS" "$brouillon/doublons.txt" | sed 's/^/       /'
nd="$(notes "$brouillon/doublons.mid")"; nt="$(notes "$brouillon/temoin.mid")"; na="$(notes "$brouillon/annule.mid")"
echo "       .mid après le geste : [$nd]"
echo "       notes : après $(compte "$nd") ; témoin $(compte "$nt") ; Ctrl+Z $(compte "$na")"
verdict "(1) trois retirées sur huit, et les cinq notes attendues dans le .mid" \
    "$(grep -q "^VSM_DOUBLONS : 3 retirée(s) sur 8 choisie(s)" "$brouillon/doublons.txt" \
        && [ "$nd" = "0:60:100:120 480:62:90:120 985:64:60:240 1440:65:90:120 1440:67:90:120" ] && echo 1 || echo 0)"
verdict "(2) le témoin, sans le geste : huit notes" "$([ "$(compte "$nt")" = 8 ] && echo 1 || echo 0)"
verdict "(3) un pas : Ctrl+Z → huit notes" "$([ "$(compte "$na")" = 8 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "DOUBLONS : $rates contrôle(s) raté(s)"; exit 1; fi
echo "DOUBLONS : les notes doubles se retirent, la plus forte et la plus longue restent, en un pas"
