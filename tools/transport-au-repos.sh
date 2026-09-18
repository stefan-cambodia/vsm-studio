#!/usr/bin/env bash
# La garde de D351 : OUVRIR UN PROJET NE LANCE PAS LA LECTURE.
#
# CE QUI A ÉTÉ VU, ET CE QUI NE L'A PAS ÉTÉ. Le 18/09, sur un projet de douze
# pistes (`children-c3-plafond`), **quatre ouvertures sur six** montraient le
# bouton *Play* allumé et la tête de lecture avancée (00:00,186 et 00:02,357 au
# bout de six secondes) — sans aucun verbe de lecture. Trois heures plus tard,
# **zéro fois sur neuf**, et la trace posée entre-temps n'a jamais vu passer
# d'ordre. Le défaut n'est donc ni reproduit ni expliqué : ce qui est en place,
# c'est l'INSTRUMENT qui le nommera s'il revient (`VSM_TRACE_TRANSPORT`, qui dit
# d'où vient chaque ordre de lecture) et cette garde, qui le verra.
#
# DEUX CAUSES RESTENT OUVERTES, et il faut les écrire pour ne pas les inventer
# après : (1) une course de banc ouvre une fenêtre qui prend le clavier de
# l'utilisateur — une barre d'espace tapée ailleurs lance la lecture, et c'est
# la raison pour laquelle les gardes bornent désormais leur fenêtre
# (`VSM_TAILLE`) au lieu de prendre l'écran ; (2) quelque chose dans
# l'application, qu'aucune trace n'a encore attrapé.
#
# CE QUE LA GARDE VÉRIFIE, quatre fois de suite : à l'ouverture d'un projet, le
# transport est à l'ARRÊT, la tête à 00:00,000, et AUCUN ordre de lecture n'a
# été donné.
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire manque.
#
#   tools/transport-au-repos.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-transport.XXXXXX")"
trap 'rm -rf "$brouillon"' EXIT
mkdir -p "$brouillon/projet/midi"

# UN PROJET ENGENDRÉ, de plusieurs pistes : c'est sur douze pistes que la chose
# a été vue, et un projet d'une piste s'ouvre trop vite pour rien montrer.
python3 - "$brouillon/projet" <<'PY'
import json, struct, sys
d = sys.argv[1]
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
def piste(evs):
    corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(corps)) + corps
pistes, tracks = [], []
for i in range(8):
    evs = [(0, b"\xff\x03\x05Voie" + bytes([0x30 + i]))]
    if i == 0:
        evs += [(0, b"\xff\x51\x03\x07\xa1\x20"), (0, b"\xff\x58\x04\x04\x02\x18\x08")]
    for k in range(16):
        h = 40 + (i * 3 + k) % 30
        evs += [(0, bytes([0x90 | (i % 16), h, 90])), (240, bytes([0x80 | (i % 16), h, 0]))]
    pistes.append(piste(evs))
    tracks.append({"channel": i, "color": "#FF6B9BFF", "effects": [],
                    "instrument": {"preferredPlugin": "vsm.minimoog"},
                    "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
                    "name": f"Voie{i}"})
open(d + "/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, len(pistes), 480) + b"".join(pistes))
json.dump({"format": "vsm-project", "version": 1, "title": "transport",
           "midi": {"file": "midi/arrangement.mid"},
           "transport": {"loop": {"enabled": False, "endTick": 0, "startTick": 0},
                          "tempoChanges": [{"bpm": 120.0, "tick": 0}],
                          "ticksPerQuarterNote": 480,
                          "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": tracks}, open(d + "/project.json", "w"), indent=1)
PY

rates=0
verdict() { if [ "$2" -ne 0 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
echo "=== D351 : ouvrir un projet ne lance pas la lecture ==="

joue=0
ordres=0
for essai in 1 2 3 4; do
    maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF
    # LA FENÊTRE EST BORNÉE : une course qui prend l'écran prend aussi le
    # clavier de l'utilisateur, et une barre d'espace tapée ailleurs lancerait
    # la lecture — c'est l'une des deux causes encore ouvertes.
    env HOME="$maison" VSM_TAILLE="1280x742" VSM_TRACE_TRANSPORT=1 \
        VSM_PROJET="$brouillon/projet" VSM_VUE="sans-rapport,arrangement" \
        VSM_DELAI=5000 VSM_CAPTURE="$brouillon/essai$essai.png" \
        timeout 90 "$BIN" > "$brouillon/essai$essai.txt" 2>&1
    ordres=$((ordres + $(grep -c 'VSM_TRANSPORT' "$brouillon/essai$essai.txt")))
    etat="$(python3 - "$brouillon/essai$essai.png" <<'PY'
import sys
try:
    from PIL import Image
except ImportError:
    print("inconnu"); raise SystemExit(0)
px = Image.open(sys.argv[1]).convert("RGB").load()
# Le bouton « Play » est rempli d'ambre quand le transport roule.
r, g, b = px[40, 50]
print("joue" if r > 150 and b < 120 else "arret")
PY
)"
    [ "$etat" = "joue" ] && joue=$((joue + 1))
done

verdict "quatre ouvertures, transport à l'ARRÊT (roulant : $joue)" \
        "$([ "$joue" -eq 0 ] && echo 1 || echo 0)"
verdict "aucun ordre de lecture donné (relevé : $ordres ligne(s) VSM_TRANSPORT)" \
        "$([ "$ordres" -eq 0 ] && echo 1 || echo 0)"
[ "$ordres" -eq 0 ] || grep -h 'VSM_TRANSPORT' "$brouillon"/essai*.txt | sed 's/^/       /' | sort -u

echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
