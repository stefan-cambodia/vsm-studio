#!/usr/bin/env bash
# D596 — L'EXPANDEUR ET LE DE-ESSER, ATTEINTS PAR LES DEUX CHEMINS DE RENDU (D21) : le son lui-même se mesure dans le
# moteur (test_expander_deesser.cpp) ; ce banc dit que l'insert se POSE depuis l'application et qu'il SONNE dans l'export
# de l'application comme dans le rendu hors-ligne.
#
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D596, (4)) : un projet d'une piste MIDI ; le même avec un expandeur réglé
# pour tout baisser (seuil 0 dB, rapport 8, plage −80) : son export plus faible que celui du témoin de PLUS de 20 dB, par
# `vsm-render` comme par l'application, les deux à 0,5 dB l'un de l'autre ; « Expander » et « De-esser » posés par la liste
# « ajouter un effet » s'écrivent dans project.json sous « expander » et « deesser ».
#
#   tools/expandeur-deesser.sh [binaire de l'application] [vsm-render]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
RENDU="${2:-./build/tools/vsm-render}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
[ -x "$RENDU" ] || { echo "REFUS : $RENDU absent — compiler vsm-render d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"
[ -x "$PY" ] || { echo "REFUS : $PY absent (numpy, soundfile) — voir analyse/requirements.txt"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-expandeur.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import copy, json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
evs = []
for debut in range(0, 1921, 480):
    evs.append((debut, 1, bytes([0x90, 57, 100]))); evs.append((debut + 400, 0, bytes([0x80, 57, 0])))
evs.sort(key=lambda e: (e[0], e[1]))
corps, t = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x03\x06Partie", 0
for tick, _, o in evs:
    corps += vlq(tick - t) + o; t = tick
corps += b"\x00\xff\x2f\x00"
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
projet = {"format": "vsm-project", "version": 1, "title": "expandeur", "midi": {"file": "midi/arrangement.mid"},
          "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                        "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
          "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                      "instrument": {"preferredPlugin": "vsm.minimoog"}}]}
for nom, effets in (("temoin", []),
                    ("exp", [{"type": "expander", "parameters": {"effect.expander.threshold": 0.0,
                                                                 "effect.expander.ratio": 8.0,
                                                                 "effect.expander.range": -80.0}}]),
                    ("pose", [])):
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(
        b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    p = copy.deepcopy(projet)
    p["tracks"][0]["effects"] = effets
    json.dump(p, open(f"{b}/{nom}/project.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2" n
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    # D147 : ce que l'application avertit se compte et se montre AVANT de conclure.
    local motif="VSM_(GESTE_APRES|MENU|MENU_CONTEXTE|EXPORT) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    avertis=$((avertis + n))
    grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2
}
rms() { "$PY" -c "
import sys
import numpy as np
import soundfile as sf
try:
    x, _ = sf.read(sys.argv[1], dtype='float64', always_2d=True)
    print('%.3f' % (10 * np.log10(max(np.mean(x ** 2), 1e-30))))
except Exception:
    print('ABSENT')" "$1"; }
ecart() { python3 -c "import sys
try: print('%.2f' % (float(sys.argv[1]) - float(sys.argv[2])))
except Exception: print('ABSENT')" "$1" "$2"; }
types() { python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
    print(','.join(e['type'] for e in d['tracks'][0].get('effects', [])) or 'aucun')
except Exception:
    print('ABSENT')" "$1"; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D596 : l'expandeur et le de-esser, par les deux chemins ==="
for x in temoin exp; do "$RENDU" "$brouillon/$x" "$brouillon/r-$x.wav" --sample-rate 48000 --quiet > "$brouillon/rendu-$x.txt" 2>&1; done
course a-temoin "$brouillon/temoin" VSM_DELAI=1500 VSM_EXPORT="$brouillon/a-temoin.wav"
course a-exp "$brouillon/exp" VSM_DELAI=1500 VSM_EXPORT="$brouillon/a-exp.wav"
course pose "$brouillon/pose" VSM_DELAI=2500 VSM_MENU_CONTEXTE="ajout-effet:Expander;ajout-effet:De-esser" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/pose"

er="$(ecart "$(rms "$brouillon/r-exp.wav")" "$(rms "$brouillon/r-temoin.wav")")"
ea="$(ecart "$(rms "$brouillon/a-exp.wav")" "$(rms "$brouillon/a-temoin.wav")")"
echo "       l'expandeur sous le témoin : vsm-render ${er} dB, l'application ${ea} dB"
echo "       posés depuis la liste : [$(types "$brouillon/pose")]"

verdict "(4) vsm-render : plus de 20 dB sous le témoin" "$(python3 -c "import sys; sys.exit(0 if float(sys.argv[1]) < -20 else 1)" "$er" 2>/dev/null && echo 1 || echo 0)"
verdict "(4) l'application : plus de 20 dB sous le témoin" "$(python3 -c "import sys; sys.exit(0 if float(sys.argv[1]) < -20 else 1)" "$ea" 2>/dev/null && echo 1 || echo 0)"
verdict "(4) les deux chemins à 0,5 dB l'un de l'autre" "$(python3 -c "import sys; sys.exit(0 if abs(float(sys.argv[1]) - float(sys.argv[2])) <= 0.5 else 1)" "$er" "$ea" 2>/dev/null && echo 1 || echo 0)"
verdict "(4) posés depuis la liste : expander,deesser" "$([ "$(types "$brouillon/pose")" = "expander,deesser" ] && echo 1 || echo 0)"
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "EXPANDEUR-DEESSER : $rates contrôle(s) raté(s)"; exit 1; fi
echo "EXPANDEUR-DEESSER : les deux inserts se posent depuis la liste et sonnent par les deux chemins de rendu"
