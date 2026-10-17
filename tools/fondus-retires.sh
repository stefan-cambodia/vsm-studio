#!/usr/bin/env bash
# D560.2 — RETIRER LES FONDUS (clic droit sur un clip audio ▸ Retirer les fondus ; le *Remove Fades* de Cubase),
# MESURÉ PAR L'EXPORT.
#
# LES RÈGLES GARDÉES. Un la constant de deux secondes, importé, fondu sur 0,5 s aux deux bouts (écrit au projet).
#   (1) le TÉMOIN : l'export s'éteint aux bords — la première et la dernière tranche de 100 ms plus de 6 dB sous
#       le milieu ;
#   (2) retirés, enregistré, rouvert et exporté : les deux tranches au niveau du milieu (à 0,3 dB) ; le projet ne
#       porte plus ni « fadeIn » ni « fadeOut », et garde sa FORME de fondu ;
#   (3) un pas : Ctrl+Z rend les deux fondus, après un geste qui a eu lieu ;
#   (4) après : l'entrée est GRISÉE — aucun clip n'a plus de fondu.
#
#   tools/fondus-retires.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-fondus.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
sr = 44100
la = [0.5 * math.sin(2 * math.pi * 440 * n / sr) for n in range(2 * sr)]
with wave.open(f"{b}/la.wav", "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(b"".join(struct.pack("<hh", int(v * 32767), int(v * 32767)) for v in la))
vide = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                   + b"MTrk" + struct.pack(">I", len(vide)) + vide)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": []}, open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 120 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU_CONTEXTE|MENU|TOUCHE|EXPORT|IMPORT) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS|AUCUNE)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
tranches() {   # « début milieu fin » : dBFS efficaces des 100 ms du début, du milieu (1,0 s) et de la fin du clip (2 s)
    "$PY" - "$1" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    x, sr = sf.read(sys.argv[1], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
if len(x) < 2 * sr:
    print("ABSENT"); sys.exit(0)
db = lambda a, b: 20 * np.log10(max(float(np.sqrt(np.mean(x[int(a * sr):int(b * sr)] ** 2))), 1e-12))
print(f"{db(0.0, 0.1):.2f} {db(0.95, 1.05):.2f} {db(1.9, 2.0):.2f}")
PY2
}
fondus() {   # « fadeIn fadeOut forme » du clip audio d'un project.json, ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1], encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
c = [c for t in d['tracks'] if t.get('audio') for c in t.get('clips', [])]
print(f\"{c[0].get('fadeIn', 0)} {c[0].get('fadeOut', 0)} {c[0].get('fadeShape', 'aucune')}\" if c else 'ABSENT')" "$1/project.json"
}
poser_fondus() {   # 0,5 s aux deux bouts, forme « égale puissance » — ce que les poignées auraient posé
    python3 -c "
import json, sys
p = sys.argv[1] + '/project.json'
d = json.load(open(p, encoding='utf-8'))
for t in d['tracks']:
    for c in t.get('clips', []):
        if t.get('audio'): c['fadeIn'] = 0.5; c['fadeOut'] = 0.5; c['fadeShape'] = 'equalPower'
json.dump(d, open(p, 'w', encoding='utf-8'), indent=1)" "$1"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
MENU="clip-audio-tous:Retirer les fondus"

echo "=== D560.2 : retirer les fondus ==="
course import "$brouillon/base" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/la.wav" VSM_GESTE_APRES="1500:enregistrer:$brouillon/base"
poser_fondus "$brouillon/base"
for x in r z; do cp -r "$brouillon/base" "$brouillon/$x"; done
course retirer "$brouillon/r" VSM_DELAI=2500 VSM_MENU_CONTEXTE="$MENU" VSM_GESTE_APRES="1500:enregistrer:$brouillon/r"
course annuler "$brouillon/z" VSM_DELAI=2800 VSM_MENU_CONTEXTE="$MENU" \
    VSM_GESTE_APRES="1300:touche:ctrl + Z;1900:enregistrer:$brouillon/z"
course export-temoin "$brouillon/base" VSM_DELAI=1500 VSM_EXPORT="$brouillon/temoin.wav"
course export-r "$brouillon/r" VSM_DELAI=1500 VSM_EXPORT="$brouillon/r.wav"
course apres "$brouillon/r" VSM_DELAI=1800 VSM_MENU_CONTEXTE="clip-audio:?"

t="$(tranches "$brouillon/temoin.wav")"; r="$(tranches "$brouillon/r.wav")"
fb="$(fondus "$brouillon/base")"; fr="$(fondus "$brouillon/r")"; fz="$(fondus "$brouillon/z")"
entree="$(grep -h "^VSM_MENU_CONTEXTE : clip-audio = " "$brouillon/apres.txt" | head -1 | tr '|' '\n' | grep "Retirer les fondus" | sed 's/^ *//; s/ *$//')"
grep -h "^VSM_FONDUS_RETIRES" "$brouillon/retirer.txt" "$brouillon/annuler.txt" | sed 's/^/       /'
echo "       témoin (début milieu fin, dBFS) : [$t] ; retirés, rouvert : [$r]"
echo "       fondus (entrée sortie forme) — posés : [$fb] ; retirés : [$fr] ; Ctrl+Z : [$fz] ; après : [$entree]"

juge() { python3 -c "
import sys
try:
    d, m, f = map(float, sys.argv[1].split())
except ValueError:
    sys.exit(1)
if sys.argv[2] == 'eteint': sys.exit(0 if d < m - 6 and f < m - 6 and m > -20 else 1)
sys.exit(0 if abs(d - m) < 0.3 and abs(f - m) < 0.3 and m > -20 else 1)" "$1" "$2"; }
verdict "(1) le témoin : l'export s'éteint aux deux bords" "$(juge "$t" eteint && [ "$fb" = "0.5 0.5 equalPower" ] && echo 1 || echo 0)"
verdict "(2) retirés et rouvert : les bords au niveau du milieu ; plus de fondu au projet, la forme gardée" \
    "$(juge "$r" plein && [ "$fr" = "0 0 equalPower" ] && echo 1 || echo 0)"
verdict "(3) un pas : Ctrl+Z rend les deux fondus" \
    "$([ "$fz" = "0.5 0.5 equalPower" ] && grep -q "^VSM_FONDUS_RETIRES : 1 clip" "$brouillon/annuler.txt" && echo 1 || echo 0)"
verdict "(4) après : l'entrée grisée" "$(echo "$entree" | grep -q "^Retirer les fondus \[grisee\]" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "FONDUS-RETIRES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "FONDUS-RETIRES : les fondus d'un clip se retirent d'un geste, et l'export sonne dès la première tranche"
