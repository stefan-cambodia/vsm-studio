#!/usr/bin/env bash
# D569.3 — CHOISIR LES NOTES PAR LA PLAGE (piano roll, Sélection ▸ Les notes entre les locateurs / du début à la tête /
# de la tête à la fin ; les *Select In Loop / From Start to Cursor / From Cursor to End* de Cubase), MESURÉ PAR LE GESTE
# QUI SUIT — Suppr — ET LE .mid EXPORTÉ.
#
# LES RÈGLES GARDÉES (celles des clips, `choisir-par-plage.sh`). Une piste : A 60 [0, 480), B 62 [1 800, 2 000),
# C 64 [2 400, 2 880), D 65 [3 840, 4 320), E 67 [1 440, 1 920) ; locateurs [1 920, 3 840) ; la tête à 1 920.
#   (1) entre les locateurs : B et C (celles qui TOUCHENT la plage ; E y finit, D y commence : dehors) — restent A D E ;
#   (2) du début à la tête : A, B, E (celles qui COMMENCENT avant) — restent C D ;
#   (3) de la tête à la fin : B, C, D (celles qui FINISSENT après) — restent A E ;
#   (4) le témoin : Suppr sans rien choisir — les cinq restent ; et le journal dit chaque compte (`VSM_SELECTION`).
#
#   tools/notes-par-plage.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-notes-plage.XXXXXX")"
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
notes = [(0, 480, 60), (1800, 2000, 62), (2400, 2880, 64), (3840, 4320, 65), (1440, 1920, 67)]
evs = sorted([(a, 1, bytes([0x90, h, 100])) for a, b, h in notes] + [(b, 0, bytes([0x80, h, 0])) for a, b, h in notes])
corps, t = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie", 0
for at, _, o in evs:
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

course() {   # $1 = nom ; $2 = geste de sélection (vide : aucun)
    local nom="$1" choix="$2" gestes
    gestes="1200:touche:pianoroll:delete;1500:exporter-midi:$brouillon/$nom.mid"
    [ -n "$choix" ] && gestes="900:menu:Édition > Sélection > $choix;$gestes"
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" \
        VSM_VUE="sans-rapport,pianoroll,piste:0" VSM_POSITION=2 VSM_DELAI=2500 VSM_GESTE_APRES="$gestes" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU|POSITION|TOUCHE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|AMBIGU|GRISÉE)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
hauteurs() {   # les hauteurs jouées par le .mid, triées — ou « ABSENT »
    "$racine/analyse/.venv/bin/python" - "$1" <<'PY'
import sys
import mido
try:
    m = mido.MidiFile(sys.argv[1])
except Exception:
    print("ABSENT"); sys.exit(0)
print(" ".join(str(h) for h in sorted(msg.note for t in m.tracks for msg in t if msg.type == "note_on" and msg.velocity > 0)) or "AUCUNE")
PY
}
compte() { grep -h "^VSM_SELECTION : " "$brouillon/$1.txt" | tail -1 | sed 's/^VSM_SELECTION : \([0-9]*\).*/\1/'; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D569.3 : choisir les notes par la plage ==="
course entre "Les notes entre les locateurs"
course debut "Les notes du début à la tête"
course fin "Les notes de la tête à la fin"
course temoin ""
for x in entre debut fin temoin; do eval "h_$x=\"\$(hauteurs \"\$brouillon/\$x.mid\")\""; done
echo "       restent — entre : [$h_entre] (choisies $(compte entre)) ; début : [$h_debut] ($(compte debut)) ; fin : [$h_fin] ($(compte fin)) ; témoin : [$h_temoin]"

verdict "(1) entre les locateurs : B et C choisies et supprimées — restent A D E" \
    "$([ "$h_entre" = "60 65 67" ] && [ "$(compte entre)" = 0 ] && grep -q "^VSM_SELECTION : 2 note(s)" "$brouillon/entre.txt" && echo 1 || echo 0)"
verdict "(2) du début à la tête : A B E — restent C D" \
    "$([ "$h_debut" = "64 65" ] && grep -q "^VSM_SELECTION : 3 note(s)" "$brouillon/debut.txt" && echo 1 || echo 0)"
verdict "(3) de la tête à la fin : B C D — restent A E" \
    "$([ "$h_fin" = "60 67" ] && grep -q "^VSM_SELECTION : 3 note(s)" "$brouillon/fin.txt" && echo 1 || echo 0)"
verdict "(4) le témoin : Suppr sans rien choisir, les cinq restent" "$([ "$h_temoin" = "60 62 64 65 67" ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "NOTES-PAR-PLAGE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "NOTES-PAR-PLAGE : les notes se choisissent par la plage, avec les règles des clips"
