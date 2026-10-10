#!/usr/bin/env bash
# D549.1 — LE FONDU À LA TÊTE DE LECTURE, MESURÉ PAR LE PROJET RELU ET PAR L'EXPORT.
#
# LA RÈGLE GARDÉE. Un sinus de 1 kHz de 2 s (crête 0,5) importé sur une piste audio, 120 BPM : la tête à 1 · 3,
# c'est-à-dire 1,0 s, au milieu du clip.
#   (1) « Fondu d'entrée jusqu'à la tête de lecture » : le projet relu porte un fondu d'entrée de 1,0 s (± 1 ms) ;
#       l'EXPORT : le niveau efficace de 0,05 à 0,25 s vaut moins de 35 % de celui de 1,3 à 1,5 s ; le TÉMOIN,
#       l'export sans le geste, entre 95 et 105 % ;
#   (2) « Fondu de sortie depuis la tête de lecture » : un fondu de sortie de 1,0 s ; l'export, de 1,75 à 1,95 s,
#       moins de 35 % de 0,3 à 0,5 s ;
#   (3) un pas : le fondu d'entrée puis Ctrl+Z → plus de fondu ;
#   (4) la tête hors du clip (mesure 3) : « Aucun fondu posé » dit, aucun pas.
#
#   tools/fondu-a-la-tete.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-fondu-tete.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
sr = 44100
trames = b"".join(struct.pack("<hh", v, v) for v in
                  (int(0.5 * 32767 * math.sin(2 * math.pi * 1000.0 * n / sr)) for n in range(2 * sr)))
with wave.open(f"{b}/sinus.wav", "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(trames)
os.makedirs(f"{b}/base/midi", exist_ok=True)
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"
open(f"{b}/base/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                 + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": []}, open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU_CONTEXTE|EXPORT|IMPORT|TOUCHE|POSITION) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
fondus() {   # « entrée:sortie » du clip audio d'un project.json (0 s'ils ne sont pas écrits), ou « ABSENT »
    python3 -c "
import json, sys
try:
    p = json.load(open(sys.argv[1]))
except Exception:
    print('ABSENT'); sys.exit(0)
c = [c for t in p['tracks'] if t.get('kind') == 'audio' for c in t.get('clips', [])]
print(f\"{c[0].get('fadeIn', 0.0):.3f}:{c[0].get('fadeOut', 0.0):.3f}\" if c else 'PAS-DE-CLIP')
" "$1"
}
rapport() {   # niveau efficace de [a1, a2] / niveau efficace de [b1, b2] dans un .wav, ou « ABSENT »
    "$PY" - "$@" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    x, sr = sf.read(sys.argv[1], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
a1, a2, b1, b2 = (float(v) for v in sys.argv[2:6])
rms = lambda d, f: float(np.sqrt(np.mean(x[int(d * sr):int(f * sr), 0] ** 2)))
r = rms(b1, b2)
print(f"{rms(a1, a2) / r:.3f}" if r > 0 else "SILENCE")
PY2
}
dans() { python3 -c "
import sys
try:
    sys.exit(0 if float(sys.argv[2]) <= float(sys.argv[1]) <= float(sys.argv[3]) else 1)
except ValueError:
    sys.exit(1)" "$1" "$2" "$3"; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D549.1 : le fondu à la tête de lecture ==="
course import "$brouillon/base" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/sinus.wav" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/base"
# LE PROJET DE DÉPART SE VÉRIFIE (10/10) : une fois sur la série du jour, l'import n'a laissé AUCUN clip, et le
# banc a conclu sur la courbe (« deux points… » raté) au lieu de l'import — le journal de l'import n'était pas
# relayé. Il l'est, et un départ sans clip audio arrête tout en le disant.
grep -h "^Import audio" "$brouillon/import.txt" | sed 's/^/       /'
if ! python3 -c "
import json, sys
p = json.load(open(sys.argv[1]))
sys.exit(0 if any(t.get('kind') == 'audio' and t.get('clips') for t in p['tracks']) else 1)" "$brouillon/base/project.json" 2>/dev/null; then
    echo "  RATÉ l'import : le projet de départ n'a pas de clip audio — rien d'autre n'est mesuré"
    grep -E "Import audio|VSM_IMPORT|VSM_ENREGISTRER|erreur|Erreur" "$brouillon/import.txt" | head -5 | sed 's/^/        journal (import) : /'
    exit 1
fi
for x in entree sortie annule rien; do cp -r "$brouillon/base" "$brouillon/$x"; done
ENTREE="clip-audio:Fondu d'entrée jusqu'à la tête de lecture"
SORTIE="clip-audio:Fondu de sortie depuis la tête de lecture"
course entree "$brouillon/entree" VSM_DELAI=2500 VSM_POSITION=1.3 VSM_MENU_CONTEXTE="$ENTREE" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/entree"
course sortie "$brouillon/sortie" VSM_DELAI=2500 VSM_POSITION=1.3 VSM_MENU_CONTEXTE="$SORTIE" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/sortie"
course annuler "$brouillon/annule" VSM_DELAI=3000 VSM_POSITION=1.3 VSM_MENU_CONTEXTE="$ENTREE" \
    VSM_GESTE_APRES="1300:touche:ctrl + Z;2000:enregistrer:$brouillon/annule"
course rien "$brouillon/rien" VSM_DELAI=2500 VSM_POSITION=3 VSM_MENU_CONTEXTE="$ENTREE" \
    VSM_GESTE_APRES="1300:relever-historique;1500:enregistrer:$brouillon/rien"
for x in base entree sortie; do course "export-$x" "$brouillon/$x" VSM_DELAI=1500 VSM_EXPORT="$brouillon/$x.wav"; done

fe="$(fondus "$brouillon/entree/project.json")"; fs="$(fondus "$brouillon/sortie/project.json")"
fa="$(fondus "$brouillon/annule/project.json")"; fr="$(fondus "$brouillon/rien/project.json")"
hist="$(grep -h "^VSM_HISTORIQUE_PAS : " "$brouillon/rien.txt" | tail -1)"
re="$(rapport "$brouillon/entree.wav" 0.05 0.25 1.3 1.5)"; rt="$(rapport "$brouillon/base.wav" 0.05 0.25 1.3 1.5)"
rs="$(rapport "$brouillon/sortie.wav" 1.75 1.95 0.3 0.5)"; rts="$(rapport "$brouillon/base.wav" 1.75 1.95 0.3 0.5)"
echo "       fondus relus (entrée:sortie) : entrée $fe ; sortie $fs ; entrée puis Ctrl+Z $fa ; tête hors du clip $fr"
echo "       export, début / milieu : avec fondu d'entrée $re ; témoin $rt — fin / début : avec fondu de sortie $rs ; témoin $rts"
echo "       ${hist:-historique non relevé}"
verdict "(1) le fondu d'entrée finit à la tête : 1,0 s relu, et l'export monte" \
    "$(dans "${fe%%:*}" 0.999 1.001 && dans "$re" 0 0.35 && dans "$rt" 0.95 1.05 && echo 1 || echo 0)"
verdict "(2) le fondu de sortie commence à la tête : 1,0 s relu, et l'export descend" \
    "$(dans "${fs##*:}" 0.999 1.001 && dans "$rs" 0 0.35 && dans "$rts" 0.95 1.05 && echo 1 || echo 0)"
verdict "(3) un pas : Ctrl+Z rend le clip sans fondu" "$([ "$fa" = "0.000:0.000" ] && echo 1 || echo 0)"
verdict "(4) la tête hors du clip : dit, aucun pas, rien de posé" \
    "$([ "$fr" = "0.000:0.000" ] && [ "$(echo "$hist" | cut -d' ' -f3)" = 0 ] \
        && grep -q "^VSM_BOITE : Aucun fondu posé" "$brouillon/rien.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "FONDU-A-LA-TETE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "FONDU-A-LA-TETE : le fondu finit ou commence à la tête, et s'entend à l'export"
