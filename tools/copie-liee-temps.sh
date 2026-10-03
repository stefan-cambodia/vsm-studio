#!/usr/bin/env bash
# D539 — UNE ÉDITION DE TEMPS DANS UN MOTIF NE CHANGE PAS SA COPIE LIÉE, MESURÉ PAR L'EXPORT MIDI.
#
# LA RÈGLE GARDÉE. Un motif d'une mesure (do à 0, sol à 960) et sa COPIE LIÉE à 1 920 (D34.2 : la
# même fenêtre sur le même matériau) ; les locateurs sur [480, 960), dans le premier motif.
#   (1) le témoin : do, sol, do, sol à 0, 960, 1 920, 2 880 ;
#   (2) « Insérer du silence entre les locateurs » : le motif coupé (sol à 1 440), la copie
#       reculée de 480 et INTACTE (do 2 400, sol 3 360). Avant D539, le sol de la copie arrivait
#       à 3 840 : le matériau partagé avait glissé sous elle ;
#   (3) « Supprimer le temps entre les locateurs » : sol du motif à 480, la copie à 1 440 et
#       2 400. Avant D539, le sol de la copie arrivait à 1 920.
#
#   tools/copie-liee-temps.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-copie-liee.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

mkdir -p "$brouillon/motif/midi"
python3 - "$brouillon/motif" <<'PY2'
import json, struct, sys
d = sys.argv[1]
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
evs = [(0, b"\xff\x51\x03\x07\xa1\x20"), (0, b"\xff\x03\x05Motif"),
       (0, bytes([0x90, 60, 100])), (240, bytes([0x80, 60, 0])),
       (720, bytes([0x90, 67, 100])), (240, bytes([0x80, 67, 0]))]
corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
open(f"{d}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                              + b"MTrk" + struct.pack(">I", len(corps)) + corps)
clip = lambda debut: {"sourceStart": 0, "sourceLength": 1920, "start": debut, "length": 1920, "color": "#FF6B9BFF"}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}],
                         "loop": {"enabled": False, "startTick": 480, "endTick": 960}},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "clips": [clip(0), clip(1920)],
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8},
                       "name": "Motif"}]}, open(f"{d}/project.json", "w"), indent=1)
PY2

course() {   # $1 = nom ; $2 = VSM_GESTE_APRES
    local nom="$1" maison
    maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
    env HOME="$maison" VSM_PROJET="$brouillon/motif" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" \
        VSM_DELAI=3000 VSM_GESTE_APRES="$2" VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|grisée)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
notes() {   # « canal:tick:hauteur » de chaque note d'un .mid, triées — ou « ABSENT »
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
        if (st & 0xF0) == 0x90 and corps[j + 1] > 0: jouees.append((st & 0x0F, t, corps[j]))
        j += 1 if (st & 0xF0) in (0xC0, 0xD0) else 2
print(" ".join(f"{c + 1}:{t}:{h}" for c, t, h in sorted(jouees)) or "AUCUNE")
PY
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D539 : une édition de temps dans un motif ne change pas sa copie liée ==="
course temoin "1500:exporter-midi:$brouillon/temoin.mid"
course inserer "1200:menu:Insérer du silence entre les locateurs;1600:exporter-midi:$brouillon/insere.mid"
course supprimer "1200:menu:Supprimer le temps entre les locateurs;1600:exporter-midi:$brouillon/supprime.mid"
t="$(notes "$brouillon/temoin.mid")"; i="$(notes "$brouillon/insere.mid")"; s="$(notes "$brouillon/supprime.mid")"
echo "       témoin {$t} ; inséré {$i} ; supprimé {$s}"
verdict "(1) le témoin : do, sol, do, sol" "$([ "$t" = "1:0:60 1:960:67 1:1920:60 1:2880:67" ] && echo 1 || echo 0)"
verdict "(2) insérer [480, 960) : le motif coupé, la copie reculée et intacte" \
    "$([ "$i" = "1:0:60 1:1440:67 1:2400:60 1:3360:67" ] && echo 1 || echo 0)"
verdict "(3) supprimer [480, 960) : le motif raccourci, la copie avancée et intacte" \
    "$([ "$s" = "1:0:60 1:480:67 1:1440:60 1:2400:67" ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "COPIE LIÉE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "COPIE LIÉE : le temps se coupe dans les fenêtres, le matériau partagé ne bouge pas"
