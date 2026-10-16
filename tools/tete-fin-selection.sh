#!/usr/bin/env bash
# D558.2 — LA TÊTE À LA FIN DE LA SÉLECTION (Transport ▸ Tête à la fin de la sélection, Maj+L ; le *Locate Selection
# End* de Cubase), MESURÉ PAR LE RELEVÉ DE LA TÊTE.
#
# LES RÈGLES GARDÉES. Une piste : trois notes (0-480, 1 920-2 400, 4 320-4 800) et deux clips, A [0, 1 920) et
# B [3 840, 5 760).
#   (1) les clips choisis (tout l'arrangement), Maj+L : la tête à 5 760, la fin jouée de B — « (clips) » au journal ;
#   (2) les notes choisies au piano roll, rien dans l'arrangement, par le MENU : la tête à 4 800, la fin de la dernière
#       note — « (notes) » ;
#   (3) rien de choisi : dit, et la tête reste où elle était (960) ;
#   (4) le TÉMOIN, sa sœur L sur les mêmes clips, la tête partie de 960 : au DÉBUT (0) — Maj+L n'est pas L.
# Les courses (1) et (4) partent de 960 : une tête qui finirait à 0 sans avoir bougé ne prouverait rien (D145).
#
#   tools/tete-fin-selection.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-tete-fin.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
ev = []
for d, f, h in ((0, 480, 60), (1920, 2400, 64), (4320, 4800, 67)):
    ev.append((d, 1, bytes([0x90, h, 100]))); ev.append((f, 0, bytes([0x80, h, 0])))
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
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.juno106"},
                       "clips": [{"start": 0, "length": 1920, "sourceStart": 0, "sourceLength": 1920, "name": "A"},
                                 {"start": 3840, "length": 1920, "sourceStart": 3840, "sourceLength": 1920, "name": "B"}]}]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {
    local nom="$1"
    shift
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS|AUCUNE)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
tete() { grep -h "^VSM_TETE : tick " "$brouillon/$1.txt" | tail -1 | sed -n 's/^VSM_TETE : tick \([0-9-]*\).*/\1/p'; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
CLIPS="menu:Édition > Tout sélectionner dans l'arrangement"
NOTES="menu:Édition > Sélection > Tout sélectionner"

echo "=== D558.2 : la tête à la fin de la sélection ==="
course clips VSM_DELAI=2400 VSM_VUE="sans-rapport,arrangement,piste:0,tete-temps:2" \
    VSM_GESTE_APRES="1000:$CLIPS;1300:touche:shift + L;1700:relever-tete"
course notes VSM_DELAI=2400 VSM_VUE="sans-rapport,pianoroll,piste:0" \
    VSM_GESTE_APRES="1000:$NOTES;1300:menu:Transport > Tête à la fin de la sélection;1700:relever-tete"
course rien VSM_DELAI=2200 VSM_VUE="sans-rapport,arrangement,piste:0,tete-temps:2" \
    VSM_GESTE_APRES="1000:touche:shift + L;1500:relever-tete"
course temoin VSM_DELAI=2400 VSM_VUE="sans-rapport,arrangement,piste:0,tete-temps:2" \
    VSM_GESTE_APRES="1000:$CLIPS;1300:touche:L;1700:relever-tete"

c="$(tete clips)"; n="$(tete notes)"; r="$(tete rien)"; t="$(tete temoin)"
grep -h "^VSM_TETE_FIN_SELECTION" "$brouillon/clips.txt" "$brouillon/notes.txt" "$brouillon/rien.txt" | sed 's/^/       /'
echo "       tête : clips [$c] ; notes [$n] ; rien [$r] ; témoin L [$t]"

verdict "(1) les clips choisis, Maj+L : la fin jouée du dernier (5 760)" \
    "$([ "$c" = 5760 ] && grep -q "^VSM_TETE_FIN_SELECTION : 5760 ticks (clips)" "$brouillon/clips.txt" && echo 1 || echo 0)"
verdict "(2) les notes choisies, par le menu : la fin de la dernière (4 800)" \
    "$([ "$n" = 4800 ] && grep -q "^VSM_TETE_FIN_SELECTION : 4800 ticks (notes)" "$brouillon/notes.txt" && echo 1 || echo 0)"
verdict "(3) rien de choisi : dit, la tête reste à 960" \
    "$([ "$r" = 960 ] && grep -q "^VSM_TETE_FIN_SELECTION : rien de choisi, la tête reste à 960" "$brouillon/rien.txt" && echo 1 || echo 0)"
verdict "(4) le témoin L, parti de 960 : le début (0), pas la fin" "$([ "$t" = 0 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "TETE-FIN-SELECTION : $rates contrôle(s) raté(s)"; exit 1; fi
echo "TETE-FIN-SELECTION : la tête va à la fin de ce qui est choisi, et le dit quand rien ne l'est"
