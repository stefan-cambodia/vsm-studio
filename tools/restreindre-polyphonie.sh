#!/usr/bin/env bash
# D554.2 — RESTREINDRE LA POLYPHONIE (piano roll ▸ Temps et durée ▸ Restreindre la polyphonie ▸ N voix ; le *Restrict
# Polyphony* de Cubase), MESURÉ PAR LE .mid DE CE QUI EST JOUÉ.
#
# LES RÈGLES GARDÉES. Une piste : un accord de quatre [0, 1 920) — do4 60, mi 64, sol 67, si 71 —, sol 55 [480, 1 920)
# qui entre ensuite, puis ré 62 [960, 1 440). Aucune paire de même hauteur ne se chevauche : l'appariement des fins du
# .mid reste sûr.
#   (1) tout choisi, « 3 voix » : 60 RETIRÉ (le plus grave d'un accord de quatre) ; quand 55 entre, 64 finit à 480 (à
#       départ égal, le plus grave des plus anciens) ; quand 62 entre, 67 finit à 960 — le plus ANCIEN, et non 55, le
#       plus récent (le premier état du banc, sans 55, ne distinguait pas les deux, et le mutant qui coupe la plus
#       récente y passait) ; 71, 55 et 62 intacts ; « 2 raccourcie(s), 1 retirée(s) » ;
#   (2) le TÉMOIN, sans le geste : les six notes telles quelles ;
#   (3) un pas : Ctrl+Z → le témoin ;
#   (4) « 8 voix » : rien à faire — « aucune » dit dans la ligne d'état, AUCUN pas.
#
#   tools/restreindre-polyphonie.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-polyphonie.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
notes = [(0, 1920, 60), (0, 1920, 64), (0, 1920, 67), (0, 1920, 71), (480, 1920, 55), (960, 1440, 62)]
ev = []
for d, f, h in notes:
    ev.append((d, 1, bytes([0x90, h, 100]))); ev.append((f, 0, bytes([0x80, h, 0])))
ev.sort(key=lambda e: (e[0], e[1]))   # à tick égal, les fins d'abord
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
                       "instrument": {"preferredPlugin": "vsm.juno106"}}]}, open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {
    local nom="$1"
    shift
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,pianoroll" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU_CONTEXTE|TOUCHE|EXPORT_MIDI) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
notes() {   # « début-fin:hauteur » des notes d'un .mid, dans l'ordre, ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
try:
    import mido
    m = mido.MidiFile(sys.argv[1])
except Exception:
    print("ABSENT"); sys.exit(0)
out = []
for tr in m.tracks:
    t, ouvertes = 0, {}
    for x in tr:
        t += x.time
        if x.type == "note_on" and x.velocity > 0:
            ouvertes.setdefault(x.note, []).append(t)
        elif x.type in ("note_off", "note_on") and ouvertes.get(x.note):
            out.append((ouvertes[x.note].pop(0), t, x.note))
print(" ".join(f"{d}-{f}:{h}" for d, f, h in sorted(out)) or "AUCUNE")
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D554.2 : restreindre la polyphonie ==="
course trois VSM_DELAI=2500 VSM_TOUCHE="pianoroll:ctrl + A" VSM_MENU_CONTEXTE="pianoroll:3 voix" \
    VSM_GESTE_APRES="1500:exporter-midi:$brouillon/trois.mid"
course temoin VSM_DELAI=2500 VSM_TOUCHE="pianoroll:ctrl + A" VSM_GESTE_APRES="1500:exporter-midi:$brouillon/temoin.mid"
course annuler VSM_DELAI=3000 VSM_TOUCHE="pianoroll:ctrl + A" VSM_MENU_CONTEXTE="pianoroll:3 voix" \
    VSM_GESTE_APRES="1300:touche:ctrl + Z;2000:exporter-midi:$brouillon/annule.mid"
# (4) EN DIFFÉRÉ, par le menu de la barre : la ligne d'état se refait quand la sélection change, et le Ctrl+A du
# démarrage, rendu après un geste joué au démarrage lui aussi, recouvrait son message (vu au premier passage).
course huit VSM_DELAI=2800 VSM_TOUCHE="pianoroll:ctrl + A" \
    VSM_GESTE_APRES="1200:menu:Édition > Temps et durée > Restreindre la polyphonie > 8 voix;1700:relever-historique;1900:relever-etat"

m="$(notes "$brouillon/trois.mid")"; t="$(notes "$brouillon/temoin.mid")"; z="$(notes "$brouillon/annule.mid")"
hist="$(grep -h "^VSM_HISTORIQUE_PAS : " "$brouillon/huit.txt" | tail -1)"; etat="$(grep -h "^VSM_ETAT_PIANOROLL" "$brouillon/huit.txt" | tail -1)"
grep -h "^VSM_POLYPHONIE" "$brouillon/trois.txt" "$brouillon/huit.txt" | sed 's/^/       /'
echo "       3 voix [$m] ; témoin [$t] ; Ctrl+Z [$z]"
echo "       8 voix : ${hist:-historique non relevé} ; ${etat:-état non relevé}"

verdict "(1) trois voix : le plus grave de l'accord retiré, les plus anciens coupés quand sol et ré entrent, et c'est dit" \
    "$([ "$m" = "0-480:64 0-960:67 0-1920:71 480-1920:55 960-1440:62" ] && grep -q "^VSM_POLYPHONIE : 3 voix, 2 raccourcie(s), 1 retirée(s)" "$brouillon/trois.txt" && echo 1 || echo 0)"
verdict "(2) le témoin : les six notes telles quelles" \
    "$([ "$t" = "0-1920:60 0-1920:64 0-1920:67 0-1920:71 480-1920:55 960-1440:62" ] && echo 1 || echo 0)"
verdict "(3) un pas : Ctrl+Z rend le témoin" "$([ "$z" = "$t" ] && [ "$t" != ABSENT ] && echo 1 || echo 0)"
verdict "(4) huit voix : rien à faire, dit, aucun pas" \
    "$([ "$(echo "$hist" | cut -d' ' -f3)" = 0 ] && echo "$etat" | grep -q "aucune" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "RESTREINDRE-POLYPHONIE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "RESTREINDRE-POLYPHONIE : jamais plus de N notes à la fois, les plus aiguës gardées"
