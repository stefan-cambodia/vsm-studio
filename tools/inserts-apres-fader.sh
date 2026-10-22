#!/usr/bin/env bash
# D576.1 — LES INSERTS APRÈS LE FADER (Piste ▸ Inserts après le fader ▸ Après le fader : N. … ; les emplacements post-fader
# de la MixConsole de Cubase), MESURÉS PAR L'EXPORT (D332) ET PAR LE PROJET RELU.
#
# LES RÈGLES GARDÉES. Un fichier de deux secondes, un sinus de 1 kHz à −1 dBFS sur les deux canaux, importé sur une piste
# audio (rang 1 : le projet vierge s'ouvre avec une piste MIDI vide, la leçon de `largeur-stereo.sh`) ; le projet est
# retouché avant l'ouverture : un limiteur au plafond de −6 dBFS, le fader à −12 dB, au centre.
#   (1) le TÉMOIN, l'insert avant le fader : **−21,0 dBFS** à 0,3 dB près (−6 − 12 − 3,01), et le projet n'écrit pas de
#       `postFader` — sans quoi « l'insert après le fader rend plus fort » ne prouverait rien ;
#   (2) L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D576) : après le fader, le limiteur au repos sous son plafond —
#       **−16,0 dBFS** (−1 − 12 − 3,01) à 0,1 dB près ; le projet relu porte `postFader` sur l'insert ;
#   (3) le menu coche l'entrée après le geste, et ne la cochait pas avant ;
#   (4) un pas : Ctrl+Z rend l'insert avant le fader, après un geste qui a eu lieu.
#
#   tools/inserts-apres-fader.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-apres-fader.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
sr = 44100
a = 10 ** (-1 / 20)
la = [a * math.sin(2 * math.pi * 1000 * n / sr) for n in range(2 * sr)]
with wave.open(f"{b}/sinus.wav", "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(b"".join(struct.pack("<hh", int(v * 32767), int(v * 32767)) for v in la))
vide = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"
os.makedirs(f"{b}/vierge/midi", exist_ok=True)
open(f"{b}/vierge/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                    + b"MTrk" + struct.pack(">I", len(vide)) + vide)
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": []}, open(f"{b}/vierge/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 120 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|EXPORT|IMPORT) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
apres() {   # « oui » / « non » : le premier insert de la piste audio est-il après le fader ? ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
    t = [t for t in d['tracks'] if t.get('audio')][0]
    print('oui' if t['effects'][0].get('postFader') else 'non')
except Exception:
    print('ABSENT')" "$1"
}
regler() {   # $1 = dossier : le limiteur au plafond de −6 dBFS, le fader à −12 dB, sur la piste audio
    python3 -c "
import json, sys
p = sys.argv[1] + '/project.json'
d = json.load(open(p, encoding='utf-8'))
t = [t for t in d['tracks'] if t.get('audio')][0]
t['effects'] = [{'type': 'limiter', 'parameters': {'effect.limiter.ceiling': -6.0}}]
t['mix']['volume'] = 10 ** (-12 / 20)
json.dump(d, open(p, 'w', encoding='utf-8'), indent=1)" "$1"
}
crete() {   # la crête du canal gauche d'un export, en dBFS, ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    x, sr = sf.read(sys.argv[1], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
c = float(np.max(np.abs(x[:, 0]))) if x.size else 0.0
print("ABSENT" if c <= 1e-12 else f"{20 * np.log10(c):.3f}")
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
MENU="menu:Piste > Inserts après le fader > Après le fader : 1. Limiter"

echo "=== D576.1 : les inserts après le fader ==="
course import "$brouillon/vierge" VSM_DELAI=2500 VSM_VUE="sans-rapport" VSM_IMPORT_AUDIO="$brouillon/sinus.wav" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/vierge"
regler "$brouillon/vierge"
for k in apres annule; do cp -r "$brouillon/vierge" "$brouillon/$k"; done
course poser "$brouillon/apres" VSM_DELAI=2800 VSM_VUE="sans-rapport,piste:1" \
    VSM_GESTE_APRES="900:lister-menus;1200:$MENU;1500:lister-menus;1900:enregistrer:$brouillon/apres"
course annuler "$brouillon/annule" VSM_DELAI=2800 VSM_VUE="sans-rapport,piste:1" \
    VSM_GESTE_APRES="1200:$MENU;1600:touche:ctrl + Z;2100:enregistrer:$brouillon/annule"
course export-temoin "$brouillon/vierge" VSM_DELAI=1500 VSM_EXPORT="$brouillon/temoin.wav"
course export-apres "$brouillon/apres" VSM_DELAI=1500 VSM_EXPORT="$brouillon/apres.wav"

at="$(apres "$brouillon/vierge")"; ap="$(apres "$brouillon/apres")"; az="$(apres "$brouillon/annule")"
ct="$(crete "$brouillon/temoin.wav")"; ca="$(crete "$brouillon/apres.wav")"
mapfile -t coches < <(grep -h "^VSM_MENU_LISTE : Piste > Inserts après le fader > " "$brouillon/poser.txt" | sed 's/^VSM_MENU_LISTE : Piste > Inserts après le fader > //')
for x in "${coches[@]}"; do echo "       menu : $x"; done
grep -h "^VSM_CONSOLE : insert" "$brouillon/poser.txt" "$brouillon/annuler.txt" | sed 's/^/       /'
echo "       après le fader : témoin [$at] ; posé [$ap] ; Ctrl+Z [$az]"
echo "       crête gauche (dBFS) : témoin [$ct] ; après le fader [$ca]"
proche() { python3 -c "
import sys
try: sys.exit(0 if abs(float(sys.argv[1]) - float(sys.argv[2])) <= float(sys.argv[3]) else 1)
except ValueError: sys.exit(1)" "$1" "$2" "$3"; }

verdict "(1) le témoin avant le fader : −21,0 dBFS à 0,3 dB, rien d'écrit" "$([ "$at" = non ] && proche "$ct" -21.0103 0.3 && echo 1 || echo 0)"
verdict "(2) l'attendu écrit avant : après le fader −16,0 dBFS à 0,1 dB ; le projet le porte" \
    "$([ "$ap" = oui ] && proche "$ca" -16.0103 0.1 && echo 1 || echo 0)"
verdict "(3) l'entrée cochée après le geste, pas avant" \
    "$([ "${#coches[@]}" = 2 ] && ! echo "${coches[0]}" | grep -q "cochée" && echo "${coches[1]}" | grep -q "\[cochée\]" && echo 1 || echo 0)"
verdict "(4) un pas : Ctrl+Z rend l'insert avant le fader, après un geste qui a eu lieu" \
    "$([ "$az" = non ] && grep -q "^VSM_CONSOLE : insert 1 (limiter) après le fader" "$brouillon/annuler.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "INSERTS-APRES-FADER : $rates contrôle(s) raté(s)"; exit 1; fi
echo "INSERTS-APRES-FADER : un insert après le fader entend le fader, à l'export comme au projet"
