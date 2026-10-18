#!/usr/bin/env bash
# D564.3 — LES MODES DE CANAUX D'UN CLIP AUDIO (clic droit ▸ Canaux ▸ Gauche aux deux canaux, Droite aux deux canaux,
# Somme mono ; le *Stereo Flip* de Cubase : Left to Stereo, Right to Stereo, Merge), MESURÉS PAR L'EXPORT.
#
# LES RÈGLES GARDÉES. Le fichier de D559.3 : un la à GAUCHE, le silence à droite ; importé.
#   (1) gauche aux deux : le la des deux côtés, au niveau de la gauche du témoin (à 0,1 dB) ; « channelMode: left »,
#       version 3 ;
#   (2) droite aux deux : le silence des deux côtés ;
#   (3) somme mono : le la des deux côtés à −6,02 dB (à 0,1) du témoin ;
#   (4) un pas : Ctrl+Z rend les canaux du fichier, après un geste qui a eu lieu ; « Normaux » cochée au menu d'un clip
#       qui n'a rien ;
#   (5) un fichier mono : les quatre modes GRISÉS, « Normaux » non.
#
#   tools/canaux-modes.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-canaux-modes.XXXXXX")"
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
    grep -E "VSM_(GESTE_APRES|VUE|MENU_CONTEXTE|MENU|TOUCHE|EXPORT|IMPORT) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS|AUCUNE)" "$brouillon/$nom.txt" \
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
mode() {   # « channelMode/swapChannels version » du clip audio d'un project.json
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
c = [c for t in d['tracks'] if t.get('audio') for c in t.get('clips', [])]
if not c:
    print('ABSENT'); sys.exit(0)
print(c[0].get('channelMode', '-') + ('/swap' if c[0].get('swapChannels') else '') + ' v' + str(d.get('version')))" "$1"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D564.3 : les modes de canaux d'un clip ==="
course import "$brouillon/base" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/gauche.wav" VSM_GESTE_APRES="1500:enregistrer:$brouillon/base"
course import-mono "$brouillon/basemono" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/mono.wav" VSM_GESTE_APRES="1500:enregistrer:$brouillon/basemono"
for x in g d s z; do cp -r "$brouillon/base" "$brouillon/$x"; done
course gauche "$brouillon/g" VSM_DELAI=2500 VSM_MENU_CONTEXTE="clip-audio-tous:Gauche aux deux canaux" VSM_GESTE_APRES="1500:enregistrer:$brouillon/g"
course droite "$brouillon/d" VSM_DELAI=2500 VSM_MENU_CONTEXTE="clip-audio-tous:Droite aux deux canaux" VSM_GESTE_APRES="1500:enregistrer:$brouillon/d"
course somme "$brouillon/s" VSM_DELAI=2500 VSM_MENU_CONTEXTE="clip-audio-tous:Somme mono" VSM_GESTE_APRES="1500:enregistrer:$brouillon/s"
course annuler "$brouillon/z" VSM_DELAI=2800 VSM_MENU_CONTEXTE="clip-audio-tous:Somme mono" \
    VSM_GESTE_APRES="1300:touche:ctrl + Z;1900:enregistrer:$brouillon/z"
for x in base g d s; do course "export-$x" "$brouillon/$x" VSM_DELAI=1500 VSM_EXPORT="$brouillon/$x.wav"; done
course liste "$brouillon/base" VSM_DELAI=1800 VSM_MENU_CONTEXTE="clip-audio:?"
course mono "$brouillon/basemono" VSM_DELAI=1800 VSM_MENU_CONTEXTE="clip-audio:?"

t="$(niveaux "$brouillon/base.wav")"; ng="$(niveaux "$brouillon/g.wav")"; nd="$(niveaux "$brouillon/d.wav")"; ns="$(niveaux "$brouillon/s.wav")"
mg="$(mode "$brouillon/g")"; md="$(mode "$brouillon/d")"; ms="$(mode "$brouillon/s")"; mz="$(mode "$brouillon/z")"
canaux() { grep -h "^VSM_MENU_CONTEXTE : clip-audio = " "$1" | head -1 | tr '|' '\n' | grep -E "Normaux|échangées|aux deux canaux|Somme mono" | sed 's/^ *//; s/ *$//' | tr '\n' ';'; }
cl="$(canaux "$brouillon/liste.txt")"; cm="$(canaux "$brouillon/mono.txt")"
grep -h "^VSM_CANAUX : " "$brouillon/gauche.txt" "$brouillon/annuler.txt" | sed 's/^/       /'
echo "       export (g d, dBFS) — témoin [$t] ; gauche aux deux [$ng] ; droite aux deux [$nd] ; somme [$ns]"
echo "       projet — [$mg] [$md] [$ms] ; Ctrl+Z [$mz]"
echo "       menu stéréo : $cl"
echo "       menu mono   : $cm"

juge() { python3 -c "
import sys
try:
    tg, td = map(float, sys.argv[1].split()); g, d = map(float, sys.argv[2].split())
except ValueError:
    sys.exit(1)
m = sys.argv[3]
if m == 'gauche': sys.exit(0 if abs(g - tg) < 0.1 and abs(d - tg) < 0.1 else 1)
if m == 'droite': sys.exit(0 if g < -90 and d < -90 else 1)
sys.exit(0 if abs(g - (tg - 6.02)) < 0.1 and abs(d - (tg - 6.02)) < 0.1 else 1)" "$1" "$2" "$3"; }
verdict "(1) gauche aux deux : le la des deux côtés ; « left », version 3" "$(juge "$t" "$ng" gauche && [ "$mg" = "left v3" ] && echo 1 || echo 0)"
verdict "(2) droite aux deux : le silence des deux côtés" "$(juge "$t" "$nd" droite && [ "$md" = "right v3" ] && echo 1 || echo 0)"
verdict "(3) somme mono : −6,02 dB des deux côtés" "$(juge "$t" "$ns" somme && [ "$ms" = "mono v3" ] && echo 1 || echo 0)"
verdict "(4) un pas : Ctrl+Z rend les canaux du fichier ; « Normaux » cochée sans réglage" \
    "$(echo "$mz" | grep -q "^- " && grep -q "^VSM_CANAUX : somme mono sur 1 clip" "$brouillon/annuler.txt" \
        && echo "$cl" | grep -q "Normaux \[cochee\]" && echo 1 || echo 0)"
verdict "(5) un fichier mono : les quatre modes grisés, « Normaux » non" \
    "$([ "$(echo "$cm" | tr ';' '\n' | grep -c "\[grisee\]")" = 4 ] && echo "$cm" | grep -q "^Normaux \[cochee\];" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "CANAUX-MODES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "CANAUX-MODES : un clip joue sa gauche, sa droite ou leur somme des deux côtés, à l'export comme à la lecture"
