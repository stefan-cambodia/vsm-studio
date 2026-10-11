#!/usr/bin/env bash
# D552.2 — LES CHEVAUCHEMENTS EN MONO (piano roll ▸ Temps et durée ▸ Retirer les chevauchements (mono)), MESURÉ PAR LE
# .mid DE CE QUI EST JOUÉ.
#
# LES RÈGLES GARDÉES. Une piste : (0 → 1 000, fa1 41), (480 → 900, la1 45), (480 → 1 200, mi2 52), (960 → 1 400, mi1 40)
#   — aucune paire de même hauteur ne se chevauche : l'appariement des fins du .mid relu reste sans ambiguïté.
#   (1) tout choisi, le geste : 41 finit à 480 (au départ de la suivante, d'une autre hauteur), 45 est RETIRÉE (même
#       départ que 52, plus aiguë), 52 finit à 960, la dernière reste ; « 2 raccourcie(s), 1 retirée(s) » ;
#   (2) le TÉMOIN, sans le geste : les quatre notes telles quelles ;
#   (3) un pas : Ctrl+Z → le témoin.
#
#   tools/chevauchements-mono.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-chevauchements.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
notes = [(0, 1000, 41), (480, 900, 45), (480, 1200, 52), (960, 1400, 40)]
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
                       "instrument": {"preferredPlugin": "vsm.minimoog"}}]}, open(f"{b}/base/project.json", "w"), indent=1)
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
MENU="pianoroll:Retirer les chevauchements (mono)"

echo "=== D552.2 : les chevauchements en mono ==="
course mono VSM_DELAI=2500 VSM_TOUCHE="pianoroll:ctrl + A" VSM_MENU_CONTEXTE="$MENU" \
    VSM_GESTE_APRES="1500:exporter-midi:$brouillon/mono.mid"
course temoin VSM_DELAI=2500 VSM_TOUCHE="pianoroll:ctrl + A" VSM_GESTE_APRES="1500:exporter-midi:$brouillon/temoin.mid"
course annuler VSM_DELAI=3000 VSM_TOUCHE="pianoroll:ctrl + A" VSM_MENU_CONTEXTE="$MENU" \
    VSM_GESTE_APRES="1300:touche:ctrl + Z;2000:exporter-midi:$brouillon/annule.mid"

m="$(notes "$brouillon/mono.mid")"; t="$(notes "$brouillon/temoin.mid")"; z="$(notes "$brouillon/annule.mid")"
grep -h "^VSM_CHEVAUCHEMENTS_MONO" "$brouillon/mono.txt" | sed 's/^/       /'
echo "       après [$m] ; témoin [$t] ; Ctrl+Z [$z]"

verdict "(1) une ligne : 41 coupée au départ suivant, 45 retirée, 52 coupée, la dernière intacte, et c'est dit" \
    "$([ "$m" = "0-480:41 480-960:52 960-1400:40" ] && grep -q "^VSM_CHEVAUCHEMENTS_MONO : 2 raccourcie(s), 1 retirée(s)" "$brouillon/mono.txt" && echo 1 || echo 0)"
verdict "(2) le témoin : les quatre notes telles quelles" "$([ "$t" = "0-1000:41 480-900:45 480-1200:52 960-1400:40" ] && echo 1 || echo 0)"
verdict "(3) un pas : Ctrl+Z rend le témoin" "$([ "$z" = "$t" ] && [ "$t" != ABSENT ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "CHEVAUCHEMENTS-MONO : $rates contrôle(s) raté(s)"; exit 1; fi
echo "CHEVAUCHEMENTS-MONO : la partie devient une ligne qui ne joue jamais deux notes"
