#!/usr/bin/env bash
# D559.3 — ÉCHANGER GAUCHE ET DROITE D'UN CLIP AUDIO (clic droit ▸ Gauche et droite échangées ; le *Stereo Flip* de
# Cubase, mode « échanger »), MESURÉ PAR L'EXPORT.
#
# LES RÈGLES GARDÉES. Un fichier de deux secondes : un la à 440 Hz à GAUCHE, le silence à droite ; importé.
#   (1) le TÉMOIN : l'export sonne à gauche, se tait à droite ;
#   (2) échangé, enregistré, puis rouvert et exporté : la gauche se tait, la droite sonne, au même niveau (à 0,1 dB)
#       que la gauche du témoin ; le projet porte « swapChannels » et la version 3 — un lecteur ancien refuse plutôt
#       que de jouer les canaux dans l'ordre du fichier ;
#   (3) un pas : Ctrl+Z rend le clip d'avant (pas de « swapChannels »), après un geste qui a eu lieu ;
#   (4) un fichier MONO : l'entrée est grisée — il n'y a rien à échanger.
#
#   tools/canaux-echanges.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-canaux.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
sr = 44100
la = [0.5 * math.sin(2 * math.pi * 440 * n / sr) for n in range(2 * sr)]
with wave.open(f"{b}/gauche.wav", "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(b"".join(struct.pack("<hh", int(v * 32767), 0) for v in la))
with wave.open(f"{b}/mono.wav", "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(b"".join(struct.pack("<h", int(v * 32767)) for v in la))
vide = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"
for nom in ("base", "basemono"):
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                       + b"MTrk" + struct.pack(">I", len(vide)) + vide)
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                             "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
               "tracks": []}, open(f"{b}/{nom}/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 120 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU_CONTEXTE|MENU|TOUCHE|EXPORT|IMPORT) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS|AUCUNE|GRISÉE)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
niveaux() {   # « gauche droite » en dBFS efficaces d'un export, ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    x, _ = sf.read(sys.argv[1], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
if len(x) == 0 or x.shape[1] < 2:
    print("ABSENT"); sys.exit(0)
db = lambda c: 20 * np.log10(max(float(np.sqrt(np.mean(c ** 2))), 1e-12))
print(f"{db(x[:, 0]):.2f} {db(x[:, 1]):.2f}")
PY2
}
echange() {   # « swapChannels version » du clip audio d'un project.json, ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1], encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
clips = [c for t in d['tracks'] for c in t.get('clips', []) if t.get('audio')]
print(('oui' if clips and clips[0].get('swapChannels') else 'non') + ' v' + str(d.get('version')))" "$1/project.json"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
MENU="clip-audio-tous:Gauche et droite échangées"

echo "=== D559.3 : échanger gauche et droite d'un clip audio ==="
course import "$brouillon/base" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/gauche.wav" VSM_GESTE_APRES="1500:enregistrer:$brouillon/base"
course import-mono "$brouillon/basemono" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/mono.wav" VSM_GESTE_APRES="1500:enregistrer:$brouillon/basemono"
for x in ech z; do cp -r "$brouillon/base" "$brouillon/$x"; done
course echanger "$brouillon/ech" VSM_DELAI=2500 VSM_MENU_CONTEXTE="$MENU" VSM_GESTE_APRES="1500:enregistrer:$brouillon/ech"
course annuler "$brouillon/z" VSM_DELAI=2800 VSM_MENU_CONTEXTE="$MENU" \
    VSM_GESTE_APRES="1300:touche:ctrl + Z;1900:enregistrer:$brouillon/z"
course export-temoin "$brouillon/base" VSM_DELAI=1500 VSM_EXPORT="$brouillon/temoin.wav"
course export-ech "$brouillon/ech" VSM_DELAI=1500 VSM_EXPORT="$brouillon/ech.wav"
course mono "$brouillon/basemono" VSM_DELAI=1800 VSM_MENU_LISTE=1

t="$(niveaux "$brouillon/temoin.wav")"; e="$(niveaux "$brouillon/ech.wav")"
pe="$(echange "$brouillon/ech")"; pz="$(echange "$brouillon/z")"
mono="$(grep -h "Gauche et droite échangées" "$brouillon/mono.txt" | grep "^VSM_MENU_LISTE" | head -1 | sed 's/^VSM_MENU_LISTE : //')"
grep -h "^VSM_CANAUX_ECHANGES" "$brouillon/echanger.txt" "$brouillon/annuler.txt" | sed 's/^/       /'
echo "       témoin (gauche droite, dBFS) : [$t] ; échangé, rouvert : [$e]"
echo "       projet échangé : [$pe] ; Ctrl+Z : [$pz] ; mono : [$mono]"

juge() { python3 -c "
import sys
try:
    tg, td = map(float, sys.argv[1].split()); eg, ed = map(float, sys.argv[2].split())
except ValueError:
    sys.exit(1)
mode = sys.argv[3]
if mode == 'temoin': sys.exit(0 if tg > -20 and td < -90 else 1)
sys.exit(0 if eg < -90 and abs(ed - tg) < 0.1 else 1)" "$1" "$2" "$3"; }
verdict "(1) le témoin : la gauche sonne, la droite se tait" "$(juge "$t" "$t" temoin && echo 1 || echo 0)"
verdict "(2) échangé et rouvert : la gauche se tait, la droite sonne au niveau de la gauche d'avant ; version 3" \
    "$(juge "$t" "$e" echange && [ "$pe" = "oui v3" ] && echo 1 || echo 0)"
verdict "(3) un pas : Ctrl+Z rend le clip d'avant" \
    "$(echo "$pz" | grep -q "^non " && grep -q "^VSM_CANAUX_ECHANGES : 1 clip" "$brouillon/annuler.txt" && echo 1 || echo 0)"
verdict "(4) un fichier mono : l'entrée grisée" "$(echo "$mono" | grep -q "Gauche et droite échangées .*\[grisée\]" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "CANAUX-ECHANGES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "CANAUX-ECHANGES : la gauche d'un clip joue à droite, à l'export comme à la lecture, et se rouvre ainsi"
