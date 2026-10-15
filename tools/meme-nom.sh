#!/usr/bin/env bash
# D556.3 — LES NOTES DE MÊME NOM, À TOUTES LES OCTAVES (piano roll ▸ Sélection ▸ Toutes les notes de même nom (toutes
# octaves) ; le *Select Equal Pitch – all Octaves* de Cubase), MESURÉ PAR LE RELEVÉ DE LA SÉLECTION ET PAR UN GESTE
# APPLIQUÉ À ELLE (le .mid joué).
#
# LES RÈGLES GARDÉES. Une piste : do3 48, do4 60 (vélocité 30, la seule faible), do5 72, do# 61, mi 64. Le do4 est choisi
# par « Notes plus faibles que 64 (1) », sans souris.
#   (1) « Toutes les notes de même nom » : la sélection passe à 3 (« VSM_SELECTION : 3 note(s) ») ; puis « Transposer +1
#       demi-ton » : le .mid joué dit {49, 61, 61, 64, 73} — les trois do montés, do# et mi intacts ;
#   (2) le TÉMOIN, « Toutes les notes de même hauteur » (la même octave) : 1 note, et le .mid {48, 61, 61, 64, 72} ;
#   (3) rien de choisi : l'entrée est grisée (la sélection ne peut que s'étendre).
#
#   tools/meme-nom.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-meme-nom.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
notes = [(0, 48, 100), (480, 60, 30), (960, 72, 100), (1440, 61, 100), (1920, 64, 100)]
ev = []
for d, h, v in notes:
    ev.append((d, 1, bytes([0x90, h, v]))); ev.append((d + 240, 0, bytes([0x80, h, 0])))
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
hauteurs() {   # les hauteurs jouées d'un .mid, triées, ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
try:
    import mido
    m = mido.MidiFile(sys.argv[1])
except Exception:
    print("ABSENT"); sys.exit(0)
print(",".join(str(h) for h in sorted(x.note for tr in m.tracks for x in tr if x.type == "note_on" and x.velocity > 0)) or "RIEN")
PY2
}
dernier_compte() { grep -h "^VSM_SELECTION : " "$brouillon/$1.txt" | tail -1 | sed -n 's/^VSM_SELECTION : \([0-9]*\) note.*/\1/p'; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
FAIBLE="menu:Édition > Sélection > Notes plus faibles que 64 (1)"
NOM="menu:Édition > Sélection > Toutes les notes de même nom (toutes octaves)"
HAUTEUR="menu:Édition > Sélection > Toutes les notes de même hauteur"
MONTER="menu:Édition > Hauteur > Transposer +1 demi-ton"

echo "=== D556.3 : les notes de même nom, à toutes les octaves ==="
course nom VSM_DELAI=3000 VSM_GESTE_APRES="1000:$FAIBLE;1300:$NOM;1600:relever-etat;1800:$MONTER;2300:exporter-midi:$brouillon/nom.mid"
course temoin VSM_DELAI=3000 VSM_GESTE_APRES="1000:$FAIBLE;1300:$HAUTEUR;1600:relever-etat;1800:$MONTER;2300:exporter-midi:$brouillon/temoin.mid"
course rien VSM_DELAI=1800 VSM_MENU_LISTE=1

n="$(hauteurs "$brouillon/nom.mid")"; t="$(hauteurs "$brouillon/temoin.mid")"; cn="$(dernier_compte nom)"; ct="$(dernier_compte temoin)"
g="$(grep -h "^VSM_MENU_LISTE : Édition > Sélection > Toutes les notes de même nom" "$brouillon/rien.txt" | head -1)"
grep -h "^VSM_SELECTION : " "$brouillon/nom.txt" "$brouillon/temoin.txt" | sed 's/^/       /'
echo "       même nom : $cn choisie(s), joué [$n] ; témoin même hauteur : $ct choisie(s), joué [$t]"
echo "       rien de choisi : [${g#VSM_MENU_LISTE : }]"

verdict "(1) les trois do choisis, et montés ensemble" "$([ "$cn" = 3 ] && [ "$n" = "49,61,61,64,73" ] && echo 1 || echo 0)"
verdict "(2) le témoin : la même hauteur ne prend que le do4" "$([ "$ct" = 1 ] && [ "$t" = "48,61,61,64,72" ] && echo 1 || echo 0)"
verdict "(3) rien de choisi : l'entrée grisée" "$(echo "$g" | grep -q "(toutes octaves) \[grisée\]$" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "MEME-NOM : $rates contrôle(s) raté(s)"; exit 1; fi
echo "MEME-NOM : un do choisi appelle tous les do"
