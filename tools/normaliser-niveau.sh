#!/usr/bin/env bash
# D545.3 — NORMALISER UN CLIP À UN NIVEAU CHOISI, MESURÉ PAR L'EXPORT AUDIO.
#
# LA RÈGLE GARDÉE. Un sinus de 1 kHz de 2 s, crête 0,5, importé sur une piste audio.
#   (1) « Normaliser à un niveau choisi… » à −1 dBFS : le gain du clip relu au projet vaut 1,7825 (±10⁻³), et
#       le journal dit la crête trouvée (0,5) ;
#   (2) l'EXPORT : sa crête vaut 1,7825 fois (±0,5 %) celle du témoin, l'export d'avant — et le geste en un
#       clic de D13.6, « Normaliser (gain = 1 / crête) », la multiplie par 2,0 ;
#   (3) un pas : Ctrl+Z → le gain d'avant (1, absent du projet relu) ;
#   (4) la fenêtre photographiée en français et en anglais (la photo regardée à la main).
#
#   tools/normaliser-niveau.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-normaliser.XXXXXX")"
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
    local nom="$1" projet="$2" maison
    shift 2
    maison="$(mktemp -d "$brouillon/home.XXXX")"   # D318 : un HOME NEUF par course
    env HOME="$maison" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU_CONTEXTE|OPTIONS|EXPORT|IMPORT|TOUCHE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
gain() {   # le gain du clip audio d'un project.json (1 s'il n'est pas écrit), ou « ABSENT »
    python3 -c "
import json, sys
try:
    p = json.load(open(sys.argv[1]))
except Exception:
    print('ABSENT'); sys.exit(0)
c = [c for t in p['tracks'] if t.get('kind') == 'audio' for c in t.get('clips', [])]
print(f\"{c[0].get('gain', 1.0):.4f}\" if c else 'PAS-DE-CLIP')
" "$1"
}
crete() {   # la crête d'un .wav, ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    x, _ = sf.read(sys.argv[1], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
print(f"{np.abs(x).max():.6f}" if len(x) else "VIDE")
PY2
}
rapport() { python3 -c "
import sys
try:
    a, b, attendu = (float(v) for v in sys.argv[1:4])
except ValueError:
    sys.exit(1)
sys.exit(0 if a > 0 and abs(b / a / attendu - 1.0) <= 0.005 else 1)" "$1" "$2" "$3"; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D545.3 : normaliser un clip à un niveau choisi ==="
course import "$brouillon/base" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/sinus.wav" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/base"
for x in niveau unclic annule; do cp -r "$brouillon/base" "$brouillon/$x"; done
course niveau "$brouillon/niveau" VSM_DELAI=2500 VSM_MENU_CONTEXTE="clip-audio-tous:Normaliser à un niveau choisi…" \
    VSM_OPTIONS="niveau=-1" VSM_GESTE_APRES="1500:enregistrer:$brouillon/niveau"
course unclic "$brouillon/unclic" VSM_DELAI=2500 VSM_MENU_CONTEXTE="clip-audio-tous:Normaliser (gain = 1 / crête)" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/unclic"
course annuler "$brouillon/annule" VSM_DELAI=3000 VSM_MENU_CONTEXTE="clip-audio-tous:Normaliser à un niveau choisi…" \
    VSM_OPTIONS="niveau=-1" VSM_GESTE_APRES="1300:touche:ctrl + Z;2000:enregistrer:$brouillon/annule"
for x in base niveau unclic; do course "export-$x" "$brouillon/$x" VSM_DELAI=1500 VSM_EXPORT="$brouillon/$x.wav"; done

grep -h "^VSM_NORMALISER" "$brouillon/niveau.txt" | sed 's/^/       /'
gn="$(gain "$brouillon/niveau/project.json")"; gu="$(gain "$brouillon/unclic/project.json")"; ga="$(gain "$brouillon/annule/project.json")"
echo "       gain du clip relu : à −1 dBFS $gn ; en un clic $gu ; puis Ctrl+Z $ga"
verdict "(1) à −1 dBFS : le gain relu à 1,7825, la crête trouvée dite" \
    "$(python3 -c "import sys; sys.exit(0 if abs(float(sys.argv[1]) - 1.7825) <= 1e-3 else 1)" "$gn" 2>/dev/null \
        && grep -q "^VSM_NORMALISER : crête 0.5000, niveau -1 dBFS" "$brouillon/niveau.txt" && echo 1 || echo 0)"
cb="$(crete "$brouillon/base.wav")"; cn="$(crete "$brouillon/niveau.wav")"; cu="$(crete "$brouillon/unclic.wav")"
echo "       crête de l'export : témoin $cb ; à −1 dBFS $cn ; en un clic $cu"
verdict "(2) l'export : × 1,7825 à −1 dBFS, × 2,0 en un clic (D13.6)" \
    "$(rapport "$cb" "$cn" 1.7825 && rapport "$cb" "$cu" 2.0 && echo 1 || echo 0)"
verdict "(3) un pas : Ctrl+Z rend le gain d'avant" \
    "$(python3 -c "import sys; sys.exit(0 if abs(float(sys.argv[1]) - 1.0) <= 1e-6 else 1)" "$ga" 2>/dev/null && echo 1 || echo 0)"

photos=0
for langue in fr en; do
    entree="Normaliser à un niveau choisi…"; [ "$langue" = en ] && entree="Normalise to a chosen level…"
    titre="Normaliser à un niveau choisi"; [ "$langue" = en ] && titre="Normalise to a chosen level"
    for essai in 1 2 3; do   # D72 : une boîte absente d'une photo ne prouve rien
        maison="$(mktemp -d "$brouillon/home.XXXX")"
        env HOME="$maison" VSM_LANGUE="$langue" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" \
            VSM_DELAI=3500 VSM_MENU_CONTEXTE="clip-audio-tous:$entree" VSM_CAPTURE_PANNEAUX=1 \
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
if [ "$rates" -gt 0 ]; then echo "NORMALISER-NIVEAU : $rates contrôle(s) raté(s)"; exit 1; fi
echo "NORMALISER-NIVEAU : la crête d'un clip va au niveau demandé, à l'export, en un pas"
