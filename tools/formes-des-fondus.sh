#!/usr/bin/env bash
# D573.2 — LA FORME DE CHAQUE FONDU (clic droit d'un clip audio ▸ Forme du fondu d'entrée ▸ / Forme du fondu de
# sortie ▸ ; les deux fenêtres de fondu de Cubase), MESURÉE PAR L'EXPORT (D332) ET PAR LE PROJET RELU.
#
# LES RÈGLES GARDÉES. Un fichier de quatre secondes, un sinus de 1 kHz à niveau constant, importé ; le clip reçoit un
# fondu d'entrée et un de sortie d'UNE seconde, « lente au départ » tous deux (le projet est retouché avant d'être
# ouvert). Le gain se lit au MILIEU de chaque fondu (0,5 s et 3,5 s), efficace sur ± 5 ms, rapporté au même instant de
# l'export SANS fondu.
#   (1) le TÉMOIN à une seule forme (« lente » aux deux) : −12,04 dB au milieu de l'entrée ET de la sortie, à 0,1 dB
#       près — la forme dessinée S'ENTEND sur un clip non étiré (jusqu'à D573, elle ne s'entendait que sur un clip
#       étiré : `spansFromTrack` ne la recopiait pas ailleurs) ;
#   (2) L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D573) : « Sortie qui tient avant de lâcher » posée, le milieu de
#       la sortie remonte à −3,01 dB (√0,5), l'entrée reste à −12,04 dB, à 0,1 dB près ;
#   (3) le projet relu porte `fadeOutShape: fast` et `fadeShape: slow`, et la version 3 ;
#   (4) « Forme des fondus ▸ Droite » pose les DEUX bords : ni `fadeShape` ni `fadeOutShape` écrits ;
#   (5) un pas : Ctrl+Z rend la sortie « lente », après un geste qui a eu lieu — « exécutée » au journal : la première
#       version cherchait le libellé seul, et la ligne qui dit l'entrée INTROUVABLE (le binaire de D572) la satisfaisait.
#
#   tools/formes-des-fondus.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-formes-fondus.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
sr = 44100
la = [0.5 * math.sin(2 * math.pi * 1000 * n / sr) for n in range(4 * sr)]
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
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 120 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU_CONTEXTE|MENU|TOUCHE|EXPORT|IMPORT) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS|AUCUNE|GRISÉE)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
fondus() {   # $1 = dossier ; pose sur le clip audio un fondu d'entrée et de sortie d'une seconde, « lente » aux deux
    python3 -c "
import json, sys
p = sys.argv[1] + '/project.json'
d = json.load(open(p, encoding='utf-8'))
c = [c for t in d['tracks'] if t.get('audio') for c in t.get('clips', [])][0]
c['fadeIn'] = 1.0; c['fadeOut'] = 1.0; c['fadeShape'] = 'slow'
json.dump(d, open(p, 'w', encoding='utf-8'), indent=1)" "$1"
}
formes() {   # « entrée sortie version » du clip audio d'un project.json, ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
    c = [c for t in d['tracks'] if t.get('audio') for c in t.get('clips', [])][0]
except Exception:
    print('ABSENT'); sys.exit(0)
print(c.get('fadeShape', '-'), c.get('fadeOutShape', '-'), 'v' + str(d.get('version')))" "$1"
}
gains() {   # $1 = export avec fondus, $2 = export sans : « entrée sortie » en dB au milieu de chaque fondu, ou « ABSENT »
    "$PY" - "$1" "$2" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    a, sr = sf.read(sys.argv[1], dtype="float64", always_2d=True)
    b, sr2 = sf.read(sys.argv[2], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
if sr != sr2 or len(a) < 4 * sr or len(b) < 4 * sr:
    print("ABSENT"); sys.exit(0)
def g(t):
    i, w = int(round(t * sr)), int(round(0.005 * sr))
    ea = np.sqrt(np.mean(a[i - w:i + w, 0] ** 2)); eb = np.sqrt(np.mean(b[i - w:i + w, 0] ** 2))
    return 20 * np.log10(ea / eb) if ea > 0 and eb > 0 else float("nan")
print(f"{g(0.5):.3f} {g(3.5):.3f}")
PY2
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D573.2 : la forme de chaque fondu ==="
course import "$brouillon/vierge" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/sinus.wav" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/vierge"
cp -r "$brouillon/vierge" "$brouillon/plein"
fondus "$brouillon/vierge"
for k in sortie droite annule; do cp -r "$brouillon/vierge" "$brouillon/$k"; done
course poser "$brouillon/sortie" VSM_DELAI=2500 VSM_MENU_CONTEXTE="clip-audio:Sortie qui tient avant de lâcher" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/sortie"
course droite "$brouillon/droite" VSM_DELAI=2500 VSM_MENU_CONTEXTE="clip-audio:Droite" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/droite"
course annuler "$brouillon/annule" VSM_DELAI=2800 VSM_MENU_CONTEXTE="clip-audio:Sortie qui tient avant de lâcher" \
    VSM_GESTE_APRES="1300:touche:ctrl + Z;1900:enregistrer:$brouillon/annule"
course export-plein "$brouillon/plein" VSM_DELAI=1500 VSM_EXPORT="$brouillon/plein.wav"
course export-temoin "$brouillon/vierge" VSM_DELAI=1500 VSM_EXPORT="$brouillon/temoin.wav"
course export-sortie "$brouillon/sortie" VSM_DELAI=1500 VSM_EXPORT="$brouillon/sortie.wav"

gt="$(gains "$brouillon/temoin.wav" "$brouillon/plein.wav")"; gs="$(gains "$brouillon/sortie.wav" "$brouillon/plein.wav")"
ft="$(formes "$brouillon/vierge")"; fs="$(formes "$brouillon/sortie")"; fd="$(formes "$brouillon/droite")"; fa="$(formes "$brouillon/annule")"
grep -h "^VSM_MENU_CONTEXTE" "$brouillon/poser.txt" "$brouillon/droite.txt" "$brouillon/annuler.txt" | sed 's/^/       /'
echo "       formes (entrée sortie version) : témoin [$ft] ; sortie posée [$fs] ; droite [$fd] ; Ctrl+Z [$fa]"
echo "       gain au milieu (entrée sortie, dB) : témoin [$gt] ; sortie posée [$gs]"

proche() { python3 -c "
import sys
try:
    v = [float(x) for x in sys.argv[1].split()]
    a = [float(x) for x in sys.argv[2].split()]
except ValueError:
    sys.exit(1)
sys.exit(0 if len(v) == len(a) and all(abs(x - y) < 0.1 for x, y in zip(v, a)) else 1)" "$1" "$2"; }
verdict "(1) le témoin à une forme : −12,04 dB aux deux milieux — la forme s'entend sur un clip non étiré" \
    "$(proche "$gt" "-12.041 -12.041" && echo 1 || echo 0)"
verdict "(2) l'attendu écrit avant : sortie « qui tient » −3,01 dB, entrée −12,04 dB, à 0,1 dB" \
    "$(proche "$gs" "-12.041 -3.010" && echo 1 || echo 0)"
verdict "(3) le projet relu : entrée « slow », sortie « fast », version 3" "$([ "$fs" = "slow fast v3" ] && echo 1 || echo 0)"
verdict "(4) « Forme des fondus ▸ Droite » pose les deux bords : rien d'écrit" "$(echo "$fd" | grep -q "^- - " && echo 1 || echo 0)"
verdict "(5) un pas : Ctrl+Z rend la sortie « lente »" \
    "$(echo "$fa" | grep -q "^slow - " && grep -q "^VSM_MENU_CONTEXTE : « Sortie qui tient avant de lâcher » exécutée" "$brouillon/annuler.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "FORMES-DES-FONDUS : $rates contrôle(s) raté(s)"; exit 1; fi
echo "FORMES-DES-FONDUS : chaque bord d'un clip sonne sa forme, à l'export comme au projet"
