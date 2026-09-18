#!/usr/bin/env bash
# La garde de D352 : ON CRÉE UN ÉVÉNEMENT DEPUIS LA LISTE.
#
# RÈGLES GARDÉES (18/09/2026) :
#   1. les six familles se créent, et l'événement créé survit à l'ALLER-RETOUR
#      par le fichier MIDI — c'est là qu'on vérifie, pas dans la case affichée ;
#   2. **le changement de programme et la pression polyphonique ne se posent
#      nulle part ailleurs** dans le logiciel : le piano roll fait des notes, la
#      lane MIDI CC fait des contrôleurs, des plis et des pressions de canal.
#      C'est le manque que cette phase comble, et c'est lui qu'on garde ;
#   3. une nature inconnue est REFUSÉE et le DIT ;
#   4. la création est ANNULABLE (« Ajouter un événement » à l'historique) ;
#   5. le bouton « + » existe dans l'en-tête de la liste, avec son infobulle.
#
# Natures : 0 note, 1 contrôleur, 2 pli, 3 pression polyphonique, 4 pression de
# canal, 5 programme.
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire ou mido manquent.
#
#   tools/liste-ajouter.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="analyse/.venv/bin/python"
[ -x "$PY" ] || PY=python3
"$PY" -c "import mido" 2>/dev/null || { echo "REFUS : mido est requis pour relire le .mid"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-liste-ajouter.XXXXXX")"
trap 'rm -rf "$brouillon"' EXIT
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
for i in range(4):
    h = 48 + i
    evs += [(0, bytes([0x90, h, 100])), (480, bytes([0x80, h, 0]))]
corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
open(d + "/midi/arrangement.mid", "wb").write(
    b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "ajouter",
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
echo "=== D352 : créer un événement depuis la liste ==="

maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF
# D351 : la fenêtre est bornée — une course ne prend pas l'écran de l'utilisateur.
env HOME="$maison" VSM_TAILLE="1280x742" VSM_PROJET="$brouillon/projet" VSM_DELAI=2500 \
    VSM_VUE="sans-rapport,arrangement,liste" VSM_TEXTES_LISTE=1 \
    VSM_LISTE_AJOUTER="5:960;3:1440;1:480;2:720;4:1200;0:1920;9:0" \
    VSM_EXPORT_MIDI="$brouillon/apres.mid" \
    VSM_CAPTURE="$brouillon/liste.png" \
    timeout 45 "$BIN" > "$brouillon/journal.txt" 2>&1
rc=$?
j="$(cat "$brouillon/journal.txt")"

compte="$("$PY" - "$brouillon/apres.mid" <<'PY'
import sys
from collections import Counter
import mido
c = Counter()
for tr in mido.MidiFile(sys.argv[1]).tracks:
    for e in tr:
        c[e.type] += 1
print(f"{c['note_on']} {c['control_change']} {c['pitchwheel']} {c['polytouch']} "
      f"{c['aftertouch']} {c['program_change']}")
PY
)"
set -- $compte
echo "       .mid exporté : notes $1, CC $2, pli $3, pression poly $4, pression canal $5, programme $6"
verdict "les six familles créées se retrouvent dans le .mid (5 notes, 1 de chaque)" \
        "$([ "$1" = "5" ] && [ "$2" = "1" ] && [ "$3" = "1" ] && [ "$4" = "1" ] && [ "$5" = "1" ] && [ "$6" = "1" ] && echo 1 || echo 0)"
verdict "le programme et la pression POLYPHONIQUE, que rien d'autre ne crée, sont là" \
        "$([ "$6" = "1" ] && [ "$4" = "1" ] && echo 1 || echo 0)"
verdict "une nature inconnue est refusée et le dit" \
        "$(grep -c 'VSM_LISTE : nature 9 inconnue' <<<"$j")"
verdict "la création entre dans l'historique (annulable)" \
        "$(grep -c 'VSM_HISTORIQUE : Ajouter un événement' <<<"$j")"
verdict "le bouton « + » et son infobulle sont dans l'en-tête de la liste" \
        "$([ "$(grep -c '^VSM_TEXTE : bouton : +$' <<<"$j")" -ge 1 ] \
          && [ "$(grep -c 'VSM_TEXTE : infobulle : Ajouter un événement de la nature choisie' <<<"$j")" -ge 1 ] \
          && echo 1 || echo 0)"
verdict "code de sortie 0 (relevé : $rc)" "$([ "$rc" -eq 0 ] && echo 1 || echo 0)"

echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
