#!/usr/bin/env bash
# D568.2 — EXPORTER LE MIDI ENTRE LES LOCATEURS (Fichier ▸ Exporter le MIDI entre les locateurs… ; l'*Export ▸ MIDI
# Loop* de Cubase), MESURÉ PAR LE .mid RELU, L'EXPORT DU MORCEAU ENTIER À CÔTÉ.
#
# LES RÈGLES GARDÉES. Une piste, 120 BPM : do 60 à 0, ré 62 à 1 920, sol 67 à 3 360 (960 ticks : il déborde la
# mesure 2), mi 64 à 3 840 ; CC 7 à 100 au tick 0 puis 40 à 1 000 ; CC 10 à 20 au tick 2 000. Locateurs [1 920, 3 840).
#   (1) le témoin : l'export du morceau entier a ses quatre notes et le CC 7 à 100 au tick 0 ;
#   (2) la boucle : ré à 0 (480 ticks), sol à 1 440 COUPÉ à 1 920 ; ni do (avant), ni mi (après) ;
#   (3) les états : CC 7 à 40 — celui en vigueur au locateur gauche — au tick 0, CC 10 à 80 ; le tempo de 120 au tick 0 ;
#   (4) la fin de piste à 1 920 ; le journal le dit.
#
#   tools/midi-locateurs.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-midi-locateurs.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

mkdir -p "$brouillon/base/midi"
python3 - "$brouillon/base" <<'PY'
import json, struct, sys
d = sys.argv[1]
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
abs_ = [(0, b"\xff\x51\x03\x07\xa1\x20"), (0, b"\xff\x03\x06Partie"), (0, bytes([0xB0, 7, 100])),
        (0, bytes([0x90, 60, 100])), (480, bytes([0x80, 60, 0])), (1000, bytes([0xB0, 7, 40])),
        (1920, bytes([0x90, 62, 100])), (2000, bytes([0xB0, 10, 20])), (2400, bytes([0x80, 62, 0])),
        (3360, bytes([0x90, 67, 100])), (3840, bytes([0x90, 64, 100])), (4320, bytes([0x80, 67, 0])),
        (4320, bytes([0x80, 64, 0]))]
t, corps = 0, b""
for at, o in abs_:
    corps += vlq(at - t) + o; t = at
corps += b"\x00\xff\x2f\x00"
open(f"{d}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                              + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}],
                         "loop": {"enabled": False, "startTick": 1920, "endTick": 3840}},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie",
                       "instrument": {"preferredPlugin": "vsm.minimoog"},
                       "mix": {"muted": False, "pan": 0.0, "sends": [], "solo": False, "volume": 0.8}}]},
          open(f"{d}/project.json", "w"), indent=1)
PY

lire() {   # « on:tick:hauteur:durée », « cc:tick:n:valeur », « tempo:tick:µs », « fin:tick », triés — ou « ABSENT »
    "$racine/analyse/.venv/bin/python" - "$1" <<'PY'
import sys
import mido
try:
    m = mido.MidiFile(sys.argv[1])
except Exception:
    print("ABSENT"); sys.exit(0)
sortie, fin = [], 0
for piste in m.tracks:
    t, ouvertes = 0, {}
    for msg in piste:
        t += msg.time
        if msg.type == "note_on" and msg.velocity > 0: ouvertes[(msg.channel, msg.note)] = t
        elif msg.type in ("note_off", "note_on"):
            d = ouvertes.pop((msg.channel, msg.note), None)
            if d is not None: sortie.append(f"on:{d}:{msg.note}:{t - d}")
        elif msg.type == "control_change": sortie.append(f"cc:{t}:{msg.control}:{msg.value}")
        elif msg.type == "set_tempo": sortie.append(f"tempo:{t}:{msg.tempo}")
        elif msg.type == "end_of_track": fin = max(fin, t)
print(" ".join(sorted(sortie, key=lambda s: (int(s.split(":")[1]), s))) + f" fin:{fin}")
PY
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D568.2 : exporter le MIDI entre les locateurs ==="
env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" \
    VSM_DELAI=2500 VSM_GESTE_APRES="900:lister-menus;1200:exporter-midi-locateurs:$brouillon/boucle.mid;1600:exporter-midi:$brouillon/tout.mid" \
    VSM_CAPTURE="$brouillon/export.png" timeout 60 "$BIN" > "$brouillon/export.txt" 2>&1
grep -E "VSM_(GESTE_APRES|MENU) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU)" "$brouillon/export.txt" | sed 's/^/        journal : /' >&2
b="$(lire "$brouillon/boucle.mid")"; t="$(lire "$brouillon/tout.mid")"
grep -h "^VSM_EXPORT_MIDI_LOCATEURS\|^VSM_MENU_LISTE : Fichier > Exporter le MIDI entre" "$brouillon/export.txt" | sed 's/^/       /'
echo "       morceau : $t"
echo "       boucle  : $b"
notes_de() { printf '%s\n' "$1" | tr ' ' '\n' | grep '^on:' | tr '\n' ' ' | sed 's/ $//'; }

verdict "(1) le témoin : le morceau entier, quatre notes, CC 7 à 100 au tick 0" \
    "$([ "$(notes_de "$t")" = "on:0:60:480 on:1920:62:480 on:3360:67:960 on:3840:64:480" ] && printf '%s' "$t" | grep -q "cc:0:7:100" && echo 1 || echo 0)"
verdict "(2) la boucle : ré à 0, sol à 1 440 coupé à 1 920 ; ni do ni mi" \
    "$([ "$(notes_de "$b")" = "on:0:62:480 on:1440:67:480" ] && echo 1 || echo 0)"
verdict "(3) les états chassés : CC 7 à 40 et le tempo au tick 0, CC 10 à 80" \
    "$(printf '%s' "$b" | grep -q "cc:0:7:40" && ! printf '%s' "$b" | grep -q "cc:0:7:100" && printf '%s' "$b" | grep -q "cc:80:10:20" \
       && printf '%s' "$b" | grep -q "tempo:0:500000" && echo 1 || echo 0)"
verdict "(4) la fin à 1 920, et le journal le dit" \
    "$(printf '%s' "$b" | grep -q "fin:1920$" && grep -q "^VSM_EXPORT_MIDI_LOCATEURS : \[1920, 3840) — 2 note(s)" "$brouillon/export.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "MIDI-LOCATEURS : $rates contrôle(s) raté(s)"; exit 1; fi
echo "MIDI-LOCATEURS : la boucle s'exporte au tick 0, ses notes coupées, ses états repris"
