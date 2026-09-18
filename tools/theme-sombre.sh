#!/usr/bin/env bash
# La garde de D347 : AUCUNE SURFACE CLAIRE DANS UNE APPLICATION SOMBRE.
#
# RÈGLE GARDÉE (18/09/2026). Un composant JUCE qu'on oublie de colorer garde le
# gris clair de `LookAndFeel_V4`. Cela ne casse rien, ne fait échouer aucun test,
# et ne se voit que sur une photo : l'en-tête du tableau d'événements est resté
# ainsi pendant toute la vie du logiciel — **2 095 × 22 px à 164 de luminance**,
# la seule surface claire de l'application.
#
# La garde photographie les six onglets du dock du bas et les deux vues du
# centre, et passe chaque image à `tools/surfaces-claires.py`, qui cherche une
# SUITE CONTIGUË de pixels clairs et peu saturés (le texte clair, lui, ne donne
# que des suites de quelques pixels ; les clips et les accents sont saturés).
#
# Rend 0 si aucune surface claire, 1 sinon, 2 si le binaire manque.
#
#   tools/theme-sombre.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
python3 -c "import numpy, PIL" 2>/dev/null || { echo "REFUS : numpy et Pillow sont requis"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-theme-sombre.XXXXXX")"
trap 'rm -rf "$brouillon"' EXIT

# UN PROJET ENGENDRÉ : la garde ne dépend d'aucune donnée du poste, et les
# quelques notes suffisent à remplir la liste d'événements et les lanes.
mkdir -p "$brouillon/projet/midi"
python3 - "$brouillon/projet" <<'PY'
import json, struct, sys
d = sys.argv[1]
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
evs = [(0, b"\xff\x03\x04Lead"), (0, b"\xff\x51\x03\x07\xa1\x20"), (0, b"\xff\x58\x04\x04\x02\x18\x08")]
for i in range(24):
    h = 48 + (i % 12)
    evs += [(0, bytes([0x90, h, 64 + i])), (240, bytes([0x80, h, 0]))]
    evs += [(0, bytes([0xb0, 74, 20 + i * 3])), (0, b"")] if False else []
corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
open(d + "/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "theme",
           "midi": {"file": "midi/arrangement.mid"},
           "transport": {"loop": {"enabled": False, "endTick": 0, "startTick": 0},
                          "tempoChanges": [{"bpm": 120.0, "tick": 0}],
                          "ticksPerQuarterNote": 480,
                          "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                        "instrument": {"preferredPlugin": "vsm.minimoog"},
                        "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
                        "name": "Lead"}]},
          open(d + "/project.json", "w"), indent=1)
PY

rates=0
verdict() { if [ "$2" -ne 0 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
echo "=== D347 : aucune surface claire dans une application sombre ==="
for vue in "arrangement,mixeur" "arrangement,liste" "arrangement,automation" \
           "arrangement,midi-cc" "arrangement,tempo" "arrangement,effets" \
           "pianoroll,liste" "arrangement,navigateur"; do
    nom="$(tr -c 'a-z0-9' '-' <<<"$vue")"
    maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
    env HOME="$maison" VSM_PROJET="$brouillon/projet" VSM_DELAI=2500 \
        VSM_VUE="sans-rapport,$vue" VSM_CAPTURE="$brouillon/$nom.png" \
        timeout 45 "$BIN" > "$brouillon/$nom.txt" 2>&1
    if [ ! -f "$brouillon/$nom.png" ]; then
        verdict "$vue : photo prise" 0
        continue
    fi
    sortie="$(python3 tools/surfaces-claires.py "$brouillon/$nom.png")"
    compte="$(sed -n 's/^SURFACES_CLAIRES \([0-9]*\)$/\1/p' <<<"$sortie")"
    verdict "$vue : $compte surface(s) claire(s)" "$([ "${compte:-1}" = "0" ] && echo 1 || echo 0)"
    [ "${compte:-1}" = "0" ] || grep '^SURFACE CLAIRE' <<<"$sortie" | sed 's/^/       /'
done

echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
