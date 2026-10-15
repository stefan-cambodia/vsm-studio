#!/usr/bin/env bash
# D557.1 — QUANTIFIER LES FINS (piano roll ▸ Temps et durée ▸ Quantifier les fins ; le *Quantize MIDI Event Ends* de
# Cubase), ET H22 — « Quantifier début ET fin » réduisait à UN TICK une note plus courte qu'une demi-case. MESURÉ PAR
# LE .mid JOUÉ.
#
# LES RÈGLES GARDÉES. Une piste, grille du piano roll à la double croche (120) : 0-200 (62), 125-365 (60), 130-170 (64),
# 240-290 (65). Toutes choisies par « Tout sélectionner », sans souris.
#   (1) « Quantifier les fins » : les débuts ne bougent pas, les fins vont à la grille — 0-240, 125-360 ; la fin de
#       130-170 tomberait AVANT le début (120) et va à la première ligne après lui (240), celle de 240-290 tomberait SUR
#       lui et va à 360 ;
#   (2) H22, « Quantifier début ET fin » : 0-240, 120-360, et les deux notes courtes à 120-240 et 240-360 — et non
#       120-121 et 240-241, ce que rendait le binaire d'avant ;
#   (3) le TÉMOIN sans geste : le .mid rend les notes d'origine (le geste a donc changé quelque chose) ;
#   (4) Ctrl+Z après (1) : les notes d'origine ;
#   (5) rien de choisi : l'entrée est grisée.
#
#   tools/quantifier-fins.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-quantifier-fins.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
notes = [(0, 200, 62), (125, 365, 60), (130, 170, 64), (240, 290, 65)]
ev = []
for d, f, h in notes:
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
                       "instrument": {"preferredPlugin": "vsm.juno106"}}]}, open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {
    local nom="$1"
    shift
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport,pianoroll" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU|TOUCHE|EXPORT_MIDI) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
notes() {   # « début-fin:hauteur » des notes d'un .mid, triées, ou « ABSENT »
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
print(" ".join(f"{d}-{f}:{h}" for d, f, h in sorted(out)) or "RIEN")
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
TOUT="menu:Édition > Sélection > Tout sélectionner"
FINS="menu:Édition > Temps et durée > Quantifier les fins"
DEUX="menu:Édition > Temps et durée > Quantifier début ET fin"

echo "=== D557.1 : quantifier les fins (et H22, quantifier début ET fin) ==="
course fins VSM_DELAI=2600 VSM_GESTE_APRES="1000:$TOUT;1300:$FINS;1800:exporter-midi:$brouillon/fins.mid"
course deux VSM_DELAI=2600 VSM_GESTE_APRES="1000:$TOUT;1300:$DEUX;1800:exporter-midi:$brouillon/deux.mid"
course temoin VSM_DELAI=2000 VSM_GESTE_APRES="1200:exporter-midi:$brouillon/temoin.mid"
course annuler VSM_DELAI=3000 VSM_GESTE_APRES="1000:$TOUT;1300:$FINS;1700:touche:ctrl + Z;2200:exporter-midi:$brouillon/annuler.mid"
course rien VSM_DELAI=1800 VSM_MENU_LISTE=1

ORIGINE="0-200:62 125-365:60 130-170:64 240-290:65"
f="$(notes "$brouillon/fins.mid")"; d="$(notes "$brouillon/deux.mid")"; t="$(notes "$brouillon/temoin.mid")"; z="$(notes "$brouillon/annuler.mid")"
g="$(grep -h "^VSM_MENU_LISTE : Édition > Temps et durée > Quantifier les fins" "$brouillon/rien.txt" | head -1)"
grep -h "^VSM_QUANTIFIER : " "$brouillon/fins.txt" "$brouillon/deux.txt" | sed 's/^/       /'
echo "       fins seules : [$f]"
echo "       début ET fin : [$d]"
echo "       témoin sans geste : [$t] ; annulé : [$z]"
echo "       rien de choisi : [${g#VSM_MENU_LISTE : }]"

verdict "(1) les fins sur la grille, les débuts immobiles, aucune fin sur ou avant son début" \
    "$([ "$f" = "0-240:62 125-360:60 130-240:64 240-360:65" ] && echo 1 || echo 0)"
verdict "(2) H22 : début ET fin ne réduit plus une note courte à un tick" \
    "$([ "$d" = "0-240:62 120-240:64 120-360:60 240-360:65" ] && echo 1 || echo 0)"
verdict "(3) le témoin sans geste rend les notes d'origine" "$([ "$t" = "$ORIGINE" ] && echo 1 || echo 0)"
verdict "(4) Ctrl+Z rend les notes d'origine, après un geste qui a eu lieu" \
    "$([ "$z" = "$ORIGINE" ] && grep -q "^VSM_QUANTIFIER : 4 note(s) quantifi.es (fins seules)" "$brouillon/annuler.txt" && echo 1 || echo 0)"
verdict "(5) rien de choisi : l'entrée grisée" "$(echo "$g" | grep -q "Quantifier les fins \[grisée\]$" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "QUANTIFIER-FINS : $rates contrôle(s) raté(s)"; exit 1; fi
echo "QUANTIFIER-FINS : les fins vont à la grille, les débuts restent, et aucune note ne tombe à un tick"
