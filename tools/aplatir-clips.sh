#!/usr/bin/env bash
# D537 — « APLATIR L'ORDRE DE JEU » SUR UNE PISTE À CLIPS, MESURÉ PAR L'EXPORT MIDI.
#
# LA RÈGLE GARDÉE. Une piste, deux sections (repères A à 0, B à 1 920), deux clips à fenêtre
# identité — ceux que pose l'ouverture d'un MIDI (D333) : do dans A, sol dans B.
#   (1) le témoin, sans aplatir : le .mid exporté joue do à 0 et sol à 1 920 ;
#   (2) aplati {B, A} (`VSM_VUE=aplatir:1:0`) : sol à 0 et do à 480 — B dure 480 ticks.
#       Avant D537, la fenêtre de A, posée à 480, lisait les DEUX notes déplacées : sol à 480,
#       do à 960 (0,5 s et 1 s au lieu de 0 et 0,5 s), et l'ordre des hauteurs, seul, était juste ;
#   (3) aplati {A, B} : rien ne change.
# Le .mid est relu en couples (tick, hauteur) — les TEMPS, pas seulement l'ordre : c'est
# l'ordre seul qui avait absous le défaut à sa première mesure.
#
#   tools/aplatir-clips.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-aplatir.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

mkdir -p "$brouillon/ab/midi"
python3 - "$brouillon/ab" <<'PY'
import json, struct, sys
d = sys.argv[1]
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
evs = [(0, b"\xff\x51\x03\x07\xa1\x20"), (0, b"\xff\x03\x05Piano"),
       (0, bytes([0x90, 60, 100])), (480, bytes([0x80, 60, 0])),
       (1440, bytes([0x90, 67, 100])), (480, bytes([0x80, 67, 0]))]
corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
open(f"{d}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                              + b"MTrk" + struct.pack(">I", len(corps)) + corps)
clip = lambda s, l: {"sourceStart": s, "sourceLength": l, "start": s, "length": l, "color": "#FF6B9BFF"}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "markers": [{"tick": 0, "name": "A"}, {"tick": 1920, "name": "B"}],
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "clips": [clip(0, 1920), clip(1920, 480)],
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8},
                       "name": "Piano"}]}, open(f"{d}/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = VSM_VUE
    local nom="$1" maison
    maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
    env HOME="$maison" VSM_PROJET="$brouillon/ab" VSM_TAILLE="1600x1000" VSM_VUE="$2" VSM_DELAI=2500 \
        VSM_GESTE_APRES="1500:exporter-midi:$brouillon/$nom.mid" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
notes() {   # « tick:hauteur » de chaque note jouée d'un .mid, dans l'ordre du temps — ou « ABSENT »
    python3 - "$1" <<'PY'
import sys
try:
    data = open(sys.argv[1], "rb").read()
except Exception:
    print("ABSENT"); sys.exit(0)
i, jouees = 14, []
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
        if (st & 0xF0) == 0x90 and corps[j + 1] > 0: jouees.append((t, corps[j]))
        j += 1 if (st & 0xF0) in (0xC0, 0xD0) else 2
print(" ".join(f"{t}:{h}" for t, h in sorted(jouees)) or "AUCUNE")
PY
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D537 : aplatir l'ordre de jeu sur une piste à clips ==="
course temoin "sans-rapport"
course ba "sans-rapport,aplatir:1:0"
course ab "sans-rapport,aplatir:0:1"
t="$(notes "$brouillon/temoin.mid")"; ba="$(notes "$brouillon/ba.mid")"; ab="$(notes "$brouillon/ab.mid")"
echo "       témoin {$t} ; {B, A} {$ba} ; {A, B} {$ab}"
verdict "(1) le témoin : do à 0, sol à 1 920" "$([ "$t" = "0:60 1920:67" ] && echo 1 || echo 0)"
verdict "(2) {B, A} : sol à 0, do à 480" "$([ "$ba" = "0:67 480:60" ] && echo 1 || echo 0)"
verdict "(3) {A, B} : rien ne change" "$([ "$ab" = "$t" ] && [ "$ab" != "ABSENT" ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "APLATIR : $rates contrôle(s) raté(s)"; exit 1; fi
echo "APLATIR : chaque créneau fait entendre sa section, à sa place"
