#!/usr/bin/env bash
# D543.3 — QUANTIFIER L'AUDIO : LES ATTAQUES D'UN CLIP CALÉES SUR LA GRILLE, MESURÉES DANS L'EXPORT.
#
# LA RÈGLE GARDÉE. Un fichier de quatre clics joués à +10, −10, +20 et +10 ms des croches de 120 BPM
# (0,25 · 0,50 · 0,75 · 1,00 s), importé sur une piste audio (un clip, qui ne suit pas le tempo).
#   (1) « Quantifier l'audio… » (grille 1/8) : le journal dit 4 attaques, 4 calées ; le projet relu
#       porte un clip « keepPitch » à six marqueurs (la paire neutre et un par attaque) ;
#   (2) dans l'EXPORT AUDIO, chaque attaque relevée tombe à 3 ms au plus de sa ligne — le témoin,
#       l'export d'avant le geste, les montre là où le fichier les a mises (+10, −10, +20, +10 ms, au
#       dixième de milliseconde près) : sans lui, « à moins de 3 ms » pourrait dire « jamais bougé » ;
#   (3) un pas : Ctrl+Z après le geste → l'export IDENTIQUE au bit près à celui d'avant, ET le projet
#       relu sans suivi de tempo ni marqueur (l'export ne suffit pas : un clip étiré au rapport un
#       sonne au bit près comme un clip qui ne l'est pas — c'est le court-circuit du moteur) ;
#   (4) quantifier À NOUVEAU le projet calé : rien ne bouge, c'est dit (« 0 calée(s), 4 déjà sur la
#       ligne »), et le projet garde ses six marqueurs — ceux du premier calage sont repris, pas
#       doublés ;
#   (5) la fenêtre du geste photographiée en français et en anglais (sa liste « Grille » lue au
#       journal, la photo regardée à la main) ;
#   (6) D543.4 : chaque crête de l'export calé à ±1 dB de celle de l'export d'avant — le vocodeur les
#       rendait de 4,8 à 13,7 dB trop bas (le « creux » d'une trame coupée, divisé par 1,5).
# L'attaque se relève dans l'export au premier échantillon qui dépasse le DIXIÈME de la crête de sa
# fenêtre (±125 ms autour de la ligne) : un clic part du silence à pleine amplitude, ce seuil le date à
# l'échantillon près, et un pré-écho du vocodeur de phase s'y verrait comme une attaque en avance.
# Chaque fichier se lit AVANT de juger (deux .wav présents et non vides avant le cmp).
#
#   tools/quantifier-audio.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-quantifier.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$brouillon"/*.wav "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
# Quatre clics (un sinus de 3 kHz qui s'éteint en ~5 ms, crête 0,8) dans un silence NUMÉRIQUE, 2 s.
sr = 44100
echantillons = [0.0] * (2 * sr)
for instant in (0.260, 0.490, 0.770, 1.010):
    debut = round(instant * sr)
    for n in range(int(0.010 * sr)):
        echantillons[debut + n] += 0.8 * math.sin(2 * math.pi * 3000 * n / sr) * math.exp(-n / (0.0012 * sr))
trames = b"".join(struct.pack("<hh", int(v * 32767), int(v * 32767)) for v in echantillons)
with wave.open(f"{b}/clics.wav", "wb") as w:
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
    local nom="$1" projet="$2" maison
    shift 2
    maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
    env HOME="$maison" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU_CONTEXTE|OPTIONS|EXPORT|IMPORT|TOUCHE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
clip_audio() {   # « mode marqueurs » du clip de la piste audio d'un project.json, ou « ABSENT »
    python3 -c "
import json, sys
try:
    pistes = json.load(open(sys.argv[1]))['tracks']
except Exception:
    print('ABSENT'); sys.exit(0)
audio = [p for p in pistes if p.get('kind') == 'audio']
if not audio or len(audio[0].get('clips', [])) != 1:
    print('PAS-UN-CLIP'); sys.exit(0)
c = audio[0]['clips'][0]
print(c.get('warp', 'off'), len(c.get('warpMarkers', [])))
" "$1"
}
cretes() {   # la crête de chaque clic de l'export (fenêtre de ±125 ms autour de sa ligne) — ou « ABSENT »
    "$PY" - "$1" <<'PY3'
import sys
import numpy as np
import soundfile as sf
try:
    x, sr = sf.read(sys.argv[1], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
m = np.abs(x).max(axis=1)
print(" ".join(f"{m[int((l - 0.125) * sr):int((l + 0.125) * sr)].max():.4f}" for l in (0.25, 0.50, 0.75, 1.00)))
PY3
}
attaques() {   # l'écart de chaque attaque de l'export à sa ligne, en ms — ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    x, sr = sf.read(sys.argv[1], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
if len(x) == 0:
    print("VIDE"); sys.exit(0)
m = np.abs(x).max(axis=1)
ecarts = []
for ligne in (0.25, 0.50, 0.75, 1.00):
    a, b = int((ligne - 0.125) * sr), int((ligne + 0.125) * sr)
    f = m[a:b]
    if len(f) == 0 or f.max() < 0.05:
        ecarts.append("RIEN"); continue
    i = int(np.argmax(f >= 0.1 * f.max()))
    ecarts.append(f"{1000.0 * ((a + i) / sr - ligne):+.1f}")
print(" ".join(ecarts))
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D543.3 : quantifier l'audio ==="
course import "$brouillon/base" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/clics.wav" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/base"
for x in cale annule; do cp -r "$brouillon/base" "$brouillon/$x"; done
MENU="clip-audio-tous:Quantifier l'audio…"
course quantifier "$brouillon/cale" VSM_DELAI=2500 VSM_MENU_CONTEXTE="$MENU" VSM_OPTIONS="grille=2" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/cale"
cp -r "$brouillon/cale" "$brouillon/encore"
course annuler "$brouillon/annule" VSM_DELAI=3000 VSM_MENU_CONTEXTE="$MENU" VSM_OPTIONS="grille=2" \
    VSM_GESTE_APRES="1500:touche:ctrl + Z;2000:enregistrer:$brouillon/annule"
course encore "$brouillon/encore" VSM_DELAI=2500 VSM_MENU_CONTEXTE="$MENU" VSM_OPTIONS="grille=2" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/encore"
course export-avant "$brouillon/base" VSM_DELAI=1500 VSM_EXPORT="$brouillon/avant.wav"
course export-apres "$brouillon/cale" VSM_DELAI=1500 VSM_EXPORT="$brouillon/apres.wav"
course export-annule "$brouillon/annule" VSM_DELAI=1500 VSM_EXPORT="$brouillon/annule.wav"

grep -h "^VSM_QUANTIFIER_AUDIO" "$brouillon/quantifier.txt" "$brouillon/encore.txt" | sed 's/^/       /'
cb="$(clip_audio "$brouillon/base/project.json")"; cc="$(clip_audio "$brouillon/cale/project.json")"
ca="$(clip_audio "$brouillon/annule/project.json")"; ce="$(clip_audio "$brouillon/encore/project.json")"
echo "       clip (mode, marqueurs) : importé « $cb » ; calé « $cc » ; Ctrl+Z « $ca » ; recalé « $ce »"
verdict "(1) quatre attaques calées, le clip « hauteur conservée » à six marqueurs" \
    "$([ "$cb" = "off 0" ] && [ "$cc" = "keepPitch 6" ] \
        && grep -q "^VSM_QUANTIFIER_AUDIO : grille 1/8, 4 attaque(s) à .* ; 4 calée(s), 0 déjà sur la ligne, 0 écartée(s)" "$brouillon/quantifier.txt" \
        && echo 1 || echo 0)"

ea="$(attaques "$brouillon/avant.wav")"; ep="$(attaques "$brouillon/apres.wav")"
echo "       attaques de l'export, en ms de leur ligne : avant [$ea] ; après [$ep]"
verdict "(2) dans l'export, chaque attaque à 3 ms au plus de sa ligne ; le témoin là où le fichier les met" \
    "$(python3 -c "
import sys
try:
    avant = [float(v) for v in sys.argv[1].split()]
    apres = [float(v) for v in sys.argv[2].split()]
except ValueError:
    sys.exit(1)
ok = len(avant) == len(apres) == 4 and all(abs(v) <= 3.0 for v in apres) \
     and all(abs(v - w) <= 0.5 for v, w in zip(avant, (10, -10, 20, 10)))
sys.exit(0 if ok else 1)" "$ea" "$ep" && echo 1 || echo 0)"

pa="$(stat -c %s "$brouillon/avant.wav" 2>/dev/null || echo 0)"; pz="$(stat -c %s "$brouillon/annule.wav" 2>/dev/null || echo 0)"
identiques=0
[ "$pa" -gt 44 ] && [ "$pz" -gt 44 ] && cmp -s "$brouillon/avant.wav" "$brouillon/annule.wav" && identiques=1
echo "       exports (octets) : avant $pa, après Ctrl+Z $pz ; identiques au bit : $identiques"
verdict "(3) un pas : Ctrl+Z rend l'export d'avant au bit près, et le clip sans suivi de tempo" \
    "$([ "$identiques" = 1 ] && [ "$ca" = "off 0" ] && echo 1 || echo 0)"

verdict "(4) quantifier à nouveau : rien ne bouge, c'est dit, les six marqueurs repris et non doublés" \
    "$([ "$ce" = "keepPitch 6" ] \
        && grep -q "^VSM_QUANTIFIER_AUDIO : grille 1/8, 4 attaque(s) à .* ; 0 calée(s), 4 déjà sur la ligne, 0 écartée(s)" "$brouillon/encore.txt" \
        && grep -q "^VSM_BOITE : Quantifier l'audio" "$brouillon/encore.txt" \
        && echo 1 || echo 0)"

ca_="$(cretes "$brouillon/avant.wav")"; cp_="$(cretes "$brouillon/apres.wav")"
echo "       crêtes des clics : avant [$ca_] ; après [$cp_]"
verdict "(6) chaque crête de l'export calé à ±1 dB de celle d'avant" \
    "$(python3 -c "
import math, sys
try:
    a = [float(v) for v in sys.argv[1].split()]
    b = [float(v) for v in sys.argv[2].split()]
except ValueError:
    sys.exit(1)
db = [20 * math.log10(y / x) for x, y in zip(a, b) if x > 0 and y > 0]
print('       écarts (dB) : ' + ' '.join(f'{d:+.1f}' for d in db), file=sys.stderr)
sys.exit(0 if len(a) == len(b) == len(db) == 4 and all(abs(d) <= 1.0 for d in db) else 1)" "$ca_" "$cp_" && echo 1 || echo 0)"

# (5) LA FENÊTRE, dans les deux langues : ouverte sans réponse de banc, photographiée.
photos=0
for langue in fr en; do
    entree="Quantifier l'audio…"; [ "$langue" = en ] && entree="Quantise audio…"
    for essai in 1 2 3; do   # D72 : une boîte absente d'une photo ne prouve rien
        maison="$(mktemp -d "$brouillon/home.XXXX")"
        env HOME="$maison" VSM_LANGUE="$langue" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" \
            VSM_DELAI=3500 VSM_MENU_CONTEXTE="clip-audio-tous:$entree" VSM_CAPTURE_PANNEAUX=1 \
            VSM_CAPTURE="$brouillon/fenetre-$langue.png" timeout 60 "$BIN" > "$brouillon/fenetre-$langue.txt" 2>&1
        grep -q "^VSM_CAPTURE_PANNEAUX : " "$brouillon/fenetre-$langue.txt" && break
        sleep 20
    done
    titre="Quantifier l'audio"; [ "$langue" = en ] && titre="Quantise audio"
    if grep -q "^VSM_BOITE : $titre — fenêtre modale ouverte" "$brouillon/fenetre-$langue.txt" \
        && grep -q "^VSM_CAPTURE_PANNEAUX : " "$brouillon/fenetre-$langue.txt"; then
        photos=$((photos + 1))
        sed -n 's/^VSM_CAPTURE_PANNEAUX : /       photo ('"$langue"') : /p' "$brouillon/fenetre-$langue.txt"
    fi
done
verdict "(5) la fenêtre ouverte et photographiée en français et en anglais" "$([ "$photos" = 2 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "QUANTIFIER-AUDIO : $rates contrôle(s) raté(s)"; exit 1; fi
echo "QUANTIFIER-AUDIO : les attaques tombent sur la grille dans l'export, en un pas"
