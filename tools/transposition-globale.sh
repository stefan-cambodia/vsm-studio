#!/usr/bin/env bash
# D544.3 — LA TRANSPOSITION GLOBALE, MESURÉE PAR L'EXPORT AUDIO.
#
# LA RÈGLE GARDÉE. Un fichier d'un la 440 Hz (1,5 s), importé sur une piste audio.
#   (1) Mixage ▸ « Transposition globale… » (+2) : le projet relu porte "globalTranspose": 2, et
#       l'export audio sonne à 493,9 Hz (±1 Hz) — le TÉMOIN, l'export d'avant, à 440 Hz ;
#   (2) la piste audio marquée Piste ▸ « Indépendante de la transposition globale », puis +2 : le
#       projet relu porte le drapeau, et l'export reste à 440 Hz ;
#   (3) un pas : +2 puis Ctrl+Z → l'export IDENTIQUE au bit près à celui d'avant, et le projet relu
#       sans "globalTranspose" (une valeur nulle ne s'écrit pas) ;
#   (4) la fenêtre photographiée en français et en anglais (la photo regardée à la main).
# Chaque fichier se lit AVANT de juger (deux .wav présents et non vides avant le cmp).
#
#   tools/transposition-globale.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-transposition.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$brouillon"/*.wav "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
sr = 44100
trames = b"".join(struct.pack("<hh", v, v) for v in
                  (int(0.5 * 32767 * math.sin(2 * math.pi * 440.0 * n / sr)) for n in range(int(1.5 * sr))))
with wave.open(f"{b}/la440.wav", "wb") as w:
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
    grep -E "VSM_(GESTE_APRES|GESTE|MENU|OPTIONS|EXPORT|IMPORT|TOUCHE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
frequence() {   # la fréquence dominante d'un .wav, en Hz (pic de FFT interpolé), ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    x, sr = sf.read(sys.argv[1], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
m = x.mean(axis=1)[int(0.2 * sr):int(1.2 * sr)]
if len(m) < 1024 or np.abs(m).max() < 1e-3:
    print("SILENCE"); sys.exit(0)
n = 1 << 18
spectre = np.abs(np.fft.rfft(m * np.hanning(len(m)), n))
k = int(np.argmax(spectre[1:])) + 1
a, b, c = np.log(spectre[k - 1:k + 2] + 1e-30)
print(f"{(k + 0.5 * (a - c) / (a - 2 * b + c)) * sr / n:.1f}")
PY2
}
cle() {   # « globalTranspose » et « independentOfGlobalTranspose » d'un project.json, ou « ABSENT »
    python3 -c "
import json, sys
try:
    p = json.load(open(sys.argv[1]))
except Exception:
    print('ABSENT'); sys.exit(0)
ind = [t.get('name', '') for t in p.get('tracks', []) if t.get('independentOfGlobalTranspose')]
print(f\"global={p.get('globalTranspose', '-')} indépendantes={','.join(ind) or '-'}\")
" "$1"
}
pres() {   # $1 = Hz relevés ; $2 = Hz attendus : à ±1 Hz
    python3 -c "
import sys
try:
    sys.exit(0 if abs(float(sys.argv[1]) - float(sys.argv[2])) <= 1.0 else 1)
except ValueError:
    sys.exit(1)" "$1" "$2"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D544.3 : la transposition globale ==="
course import "$brouillon/base" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/la440.wav" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/base"
for x in plus2 indep annule; do cp -r "$brouillon/base" "$brouillon/$x"; done
# La piste audio est la DERNIÈRE du projet importé (une piste MIDI vide la précède) : on la choisit par
# son index, relevé dans le projet plutôt que supposé.
audio="$(python3 -c "
import json, sys
p = json.load(open(sys.argv[1]))['tracks']
print(next(i for i, t in enumerate(p) if t.get('kind') == 'audio'))" "$brouillon/base/project.json" 2>/dev/null || echo 0)"
course plus2 "$brouillon/plus2" VSM_DELAI=2500 VSM_OPTIONS="demitons=2" \
    VSM_GESTE_APRES="800:menu:Transposition globale…;1500:enregistrer:$brouillon/plus2"
course indep "$brouillon/indep" VSM_DELAI=3000 VSM_OPTIONS="demitons=2" \
    VSM_GESTE_APRES="800:choisir:$audio;1100:menu:Indépendante de la transposition globale;1400:menu:Transposition globale…;2000:enregistrer:$brouillon/indep"
course annuler "$brouillon/annule" VSM_DELAI=3000 VSM_OPTIONS="demitons=2" \
    VSM_GESTE_APRES="800:menu:Transposition globale…;1300:touche:ctrl + Z;2000:enregistrer:$brouillon/annule"
for x in base plus2 indep annule; do
    course "export-$x" "$brouillon/$x" VSM_DELAI=1500 VSM_EXPORT="$brouillon/$x.wav"
done

grep -h "^VSM_TRANSPOSITION_GLOBALE" "$brouillon/plus2.txt" "$brouillon/indep.txt" | sed 's/^/       /'
fb="$(frequence "$brouillon/base.wav")"; fp="$(frequence "$brouillon/plus2.wav")"; fi_="$(frequence "$brouillon/indep.wav")"
echo "       fréquence de l'export : avant $fb Hz ; +2 $fp Hz ; piste indépendante, +2 $fi_ Hz"
cb="$(cle "$brouillon/base/project.json")"; cp_="$(cle "$brouillon/plus2/project.json")"
ci="$(cle "$brouillon/indep/project.json")"; ca="$(cle "$brouillon/annule/project.json")"
echo "       projet relu : avant [$cb] ; +2 [$cp_] ; indépendante [$ci] ; Ctrl+Z [$ca]"
verdict "(1) +2 : le projet le porte, l'export sonne à 493,9 Hz ; le témoin à 440" \
    "$([ "${cp_%% *}" = "global=2" ] && pres "$fp" 493.9 && pres "$fb" 440 && echo 1 || echo 0)"
verdict "(2) la piste indépendante reste à 440 Hz, et le projet porte son drapeau" \
    "$([ "${ci%% *}" = "global=2" ] && [ "${ci#* }" != "indépendantes=-" ] && pres "$fi_" 440 && echo 1 || echo 0)"
pa="$(stat -c %s "$brouillon/base.wav" 2>/dev/null || echo 0)"; pz="$(stat -c %s "$brouillon/annule.wav" 2>/dev/null || echo 0)"
identiques=0
[ "$pa" -gt 44 ] && [ "$pz" -gt 44 ] && cmp -s "$brouillon/base.wav" "$brouillon/annule.wav" && identiques=1
echo "       exports (octets) : avant $pa, après +2 et Ctrl+Z $pz ; identiques au bit : $identiques"
verdict "(3) un pas : Ctrl+Z rend l'export d'avant au bit près, et le projet sans transposition globale" \
    "$([ "$identiques" = 1 ] && [ "${ca%% *}" = "global=-" ] && echo 1 || echo 0)"

photos=0
for langue in fr en; do
    entree="Transposition globale…"; [ "$langue" = en ] && entree="Global transpose…"
    titre="Transposition globale"; [ "$langue" = en ] && titre="Global transpose"
    for essai in 1 2 3; do   # D72 : une boîte absente d'une photo ne prouve rien
        maison="$(mktemp -d "$brouillon/home.XXXX")"
        env HOME="$maison" VSM_LANGUE="$langue" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" \
            VSM_DELAI=3500 VSM_GESTE_APRES="800:menu:$entree" VSM_CAPTURE_PANNEAUX=1 \
            VSM_CAPTURE="$brouillon/fenetre-$langue.png" timeout 60 "$BIN" > "$brouillon/fenetre-$langue.txt" 2>&1
        grep -q "^VSM_CAPTURE_PANNEAUX : " "$brouillon/fenetre-$langue.txt" && break
        sleep 20
    done
    if grep -q "^VSM_BOITE : $titre — fenêtre modale ouverte" "$brouillon/fenetre-$langue.txt" \
        && grep -q "^VSM_CAPTURE_PANNEAUX : " "$brouillon/fenetre-$langue.txt"; then
        photos=$((photos + 1))
        sed -n 's/^VSM_CAPTURE_PANNEAUX : /       photo ('"$langue"') : /p' "$brouillon/fenetre-$langue.txt"
    fi
done
verdict "(4) la fenêtre ouverte et photographiée en français et en anglais" "$([ "$photos" = 2 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "TRANSPOSITION-GLOBALE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "TRANSPOSITION-GLOBALE : le morceau se transpose d'un réglage, à l'export, en un pas"
