#!/usr/bin/env bash
# LA GARDE DE D509 : L'ÉDITEUR DES NOTES MONTRE LES NOTES DU PROJET OUVERT.
#
# RÈGLE GARDÉE (29/09/2026). L'éditeur des notes du projet n'était rempli qu'à
# l'ouverture de SA fenêtre. Fenêtre ouverte, ouvrir un autre projet y laissait les
# notes du précédent — et la frappe suivante les recopiait dans le nouveau projet.
#
# COMMENT. Deux projets : A porte des notes (« Notes de A »), B aucune ; les
# préférences du HOME de banc mettent B dans « Projets récents ». Une course : A
# ouvert, la fenêtre des notes ouverte (menu Affichage), B ouvert PAR LE MENU
# « Projets récents », ce que l'éditeur montre relevé (`relever-notes`), une frappe
# (`notes-frappe:`, insérée au curseur comme le clavier le fait), B enregistré, ses
# notes relues dans le fichier. Tout en différé, dans l'ordre.
#
# Rend 0 si tout tient, 1 sinon, 2 si le binaire manque.
#
#   tools/notes-du-projet.sh [chemin/du/binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-notes.XXXXXX")"
trap 'rm -rf "$brouillon"' EXIT

for nom in projet-a projet-b; do
    mkdir -p "$brouillon/$nom/midi"
    python3 - "$brouillon/$nom" "$nom" <<'PY'
import json, struct, sys
d, nom = sys.argv[1], sys.argv[2]
def vlq(n):
    b = [n & 0x7F]; n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80); n >>= 7
    return bytes(reversed(b))
evs = [(0, b"\xff\x03\x03une"), (0, bytes([0x90, 60, 100])), (480, bytes([0x80, 60, 0]))]
corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
open(d + "/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                             + b"MTrk" + struct.pack(">I", len(corps)) + corps)
projet = {"format": "vsm-project", "version": 1, "title": nom, "midi": {"file": "midi/arrangement.mid"},
          "transport": {"loop": {"enabled": False, "endTick": 0, "startTick": 0},
                        "tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                        "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
          "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                      "instrument": {"preferredPlugin": "vsm.minimoog"},
                      "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
                      "name": "une"}]}
if nom == "projet-a":
    projet["notes"] = "Notes de A"
json.dump(projet, open(d + "/project.json", "w"), indent=1)
PY
done

h="$(mktemp -d "$brouillon/home.XXXX")"
mkdir -p "$h/VintageSynthMidiStudio"
python3 - "$h/VintageSynthMidiStudio/VintageSynthMidiStudio.settings" "$brouillon/projet-b" <<'PY'
import sys
from xml.sax.saxutils import quoteattr
open(sys.argv[1], "w", encoding="utf-8").write(
    '<?xml version="1.0" encoding="UTF-8"?>\n\n<PROPERTIES>\n  <VALUE name="projetsRecents" val='
    + quoteattr(sys.argv[2]) + '/>\n</PROPERTIES>\n')
PY

env HOME="$h" VSM_TAILLE="1280x800" VSM_PROJET="$brouillon/projet-a" VSM_VUE="sans-rapport" VSM_DELAI=2800 \
    VSM_GESTE_APRES="400:menu:Notes du projet;700:relever-notes;1000:menu:projet-b;1300:relever-notes;1600:notes-frappe:!;1900:enregistrer:$brouillon/projet-b2;2200:relever-notes" \
    VSM_CAPTURE="$brouillon/notes.png" timeout 40 "$BIN" > "$brouillon/notes.txt" 2>&1
grep -E "VSM_(MENU|GESTE_APRES) : .*(aucune|refusé|JAMAIS|grisée)" "$brouillon/notes.txt" | sed 's/^/        journal : /' >&2
mapfile -t releves < <(grep "VSM_NOTES_EDITEUR : " "$brouillon/notes.txt" | sed 's/VSM_NOTES_EDITEUR : //')
ecrit="$(python3 -c "import json,sys; print(json.load(open(sys.argv[1], encoding='utf-8')).get('notes',''))" "$brouillon/projet-b2/project.json" 2>/dev/null || echo "?")"

rates=0
dire() {   # $1 nom ; $2 obtenu ; $3 attendu
    if [ "$2" = "$3" ]; then printf '  OK   %-34s %s\n' "$1" "$2"
    else printf '  RATÉ %-34s %s (attendu %s)\n' "$1" "$2" "$3"; rates=$((rates + 1)); fi
}
echo "=== D509 : l'éditeur des notes suit le projet ouvert ==="
dire "A ouvert, fenêtre ouverte" "${releves[0]:-?}" "[Notes de A] ; projet : [Notes de A]"
dire "B ouvert, fenêtre restée ouverte" "${releves[1]:-?}" "[] ; projet : []"
dire "B : une frappe, puis enregistré" "$ecrit" "!"
echo "--- $rates raté(s)"
[ "$rates" -eq 0 ]
