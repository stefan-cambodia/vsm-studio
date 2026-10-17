#!/usr/bin/env bash
# D561.3 — RETIRER LE DÉCALAGE CONTINU D'UN CLIP (clic droit sur un clip audio ▸ Retirer le décalage continu ; le
# *Remove DC Offset* de Cubase), MESURÉ PAR L'EXPORT.
#
# LES RÈGLES GARDÉES. Un la de deux secondes, posé sur un décalage : +0,25 à gauche, −0,10 à droite ; importé.
#   (1) le TÉMOIN : l'export porte le décalage — moyenne +0,25 et −0,10, à la loi de panoramique au centre près (×0,7071,
#       −3 dB, qui vaut pour tout l'export : la première écriture de ce banc l'oubliait), à 0,005, sur 0,1-1,9 s ;
#   (2) retiré, enregistré, rouvert, exporté : les deux moyennes sous 0,001, et le la au même niveau que celui du
#       témoin (à 0,05 dB, la composante alternative) ; le projet porte « dcOffset » et la version 3 ;
#   (3) un pas : Ctrl+Z rend le clip sans « dcOffset », après un geste qui a eu lieu ;
#   (4) l'entrée est COCHÉE sur le clip corrigé, et la décocher rend le décalage (le projet sans « dcOffset ») ;
#   (5) un la SANS décalage : rien à retirer, la boîte le dit, le projet ne change pas.
#
#   tools/decalage-continu.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-decalage.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
sr = 44100
la = [0.25 * math.sin(2 * math.pi * 440 * n / sr) for n in range(2 * sr)]
for nom, (dg, dd) in (("decale", (0.25, -0.10)), ("propre", (0.0, 0.0))):
    with wave.open(f"{b}/{nom}.wav", "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(b"".join(struct.pack("<hh", int((v + dg) * 32767), int((v + dd) * 32767)) for v in la))
    vide = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"
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
mesure() {   # « moyenne_g moyenne_d alternatif_dB » d'un export sur 0,1-1,9 s, ou « ABSENT »
    "$PY" - "$1" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    x, sr = sf.read(sys.argv[1], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
if len(x) < 2 * sr or x.shape[1] < 2:
    print("ABSENT"); sys.exit(0)
z = x[int(0.1 * sr):int(1.9 * sr)]
m = z.mean(axis=0)
ac = 20 * np.log10(max(float(np.sqrt(np.mean((z[:, 0] - m[0]) ** 2))), 1e-12))
print(f"{m[0]:.4f} {m[1]:.4f} {ac:.2f}")
PY2
}
decalage() {   # « dcOffset version » du clip audio d'un project.json, ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
c = [c for t in d['tracks'] if t.get('audio') for c in t.get('clips', [])]
if not c:
    print('ABSENT'); sys.exit(0)
dc = c[0].get('dcOffset')
print(('aucun' if dc is None else '/'.join(f'{v:.3f}' for v in dc)) + ' v' + str(d.get('version')))" "$1"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
MENU="clip-audio:Retirer le décalage continu"

echo "=== D561.3 : retirer le décalage continu d'un clip ==="
for nom in decale propre; do
    course "import-$nom" "$brouillon/$nom" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/$nom.wav" \
        VSM_GESTE_APRES="1500:enregistrer:$brouillon/$nom"
done
for x in r z; do cp -r "$brouillon/decale" "$brouillon/$x"; done
cp -r "$brouillon/propre" "$brouillon/p"
course retirer "$brouillon/r" VSM_DELAI=2500 VSM_MENU_CONTEXTE="$MENU" VSM_GESTE_APRES="1500:enregistrer:$brouillon/r"
course annuler "$brouillon/z" VSM_DELAI=2800 VSM_MENU_CONTEXTE="$MENU" \
    VSM_GESTE_APRES="1300:touche:ctrl + Z;1900:enregistrer:$brouillon/z"
course export-temoin "$brouillon/decale" VSM_DELAI=1500 VSM_EXPORT="$brouillon/temoin.wav"
course export-r "$brouillon/r" VSM_DELAI=1500 VSM_EXPORT="$brouillon/r.wav"
course cochee "$brouillon/r" VSM_DELAI=1800 VSM_MENU_CONTEXTE="clip-audio:?"
cp -r "$brouillon/r" "$brouillon/rendu"
course rendre "$brouillon/rendu" VSM_DELAI=2500 VSM_MENU_CONTEXTE="$MENU" VSM_GESTE_APRES="1500:enregistrer:$brouillon/rendu"
course propre "$brouillon/p" VSM_DELAI=2500 VSM_MENU_CONTEXTE="$MENU" VSM_GESTE_APRES="1500:enregistrer:$brouillon/p"

t="$(mesure "$brouillon/temoin.wav")"; r="$(mesure "$brouillon/r.wav")"
dr="$(decalage "$brouillon/r")"; dz="$(decalage "$brouillon/z")"; dd="$(decalage "$brouillon/rendu")"; dp="$(decalage "$brouillon/p")"
coche="$(grep -h "^VSM_MENU_CONTEXTE : clip-audio = " "$brouillon/cochee.txt" | head -1 | tr '|' '\n' | grep "décalage continu" | sed 's/^ *//; s/ *$//')"
for x in retirer annuler rendre propre; do grep -h "^VSM_DECALAGE\|^VSM_BOITE" "$brouillon/$x.txt" | head -2 | sed "s/^/       ($x) /"; done
echo "       témoin (moyennes g d, alternatif dB) : [$t] ; retiré, rouvert : [$r]"
echo "       projet retiré : [$dr] ; Ctrl+Z : [$dz] ; rendu : [$dd] ; sans décalage : [$dp] ; menu : [$coche]"

juge() { python3 -c "
import sys
try:
    tg, td, ta = map(float, sys.argv[1].split()); rg, rd, ra = map(float, sys.argv[2].split())
except ValueError:
    sys.exit(1)
k = 0.5 ** 0.5   # la loi de panoramique au centre
if sys.argv[3] == 'temoin': sys.exit(0 if abs(tg - 0.25 * k) < 0.005 and abs(td + 0.10 * k) < 0.005 else 1)
sys.exit(0 if abs(rg) < 0.001 and abs(rd) < 0.001 and abs(ra - ta) < 0.05 else 1)" "$1" "$2" "$3"; }
verdict "(1) le témoin porte le décalage : +0,25 et −0,10 (×0,7071, la loi de panoramique)" "$(juge "$t" "$t" temoin && echo 1 || echo 0)"
verdict "(2) retiré et rouvert : moyennes sous 0,001, le la au même niveau ; « dcOffset » et la version 3" \
    "$(juge "$t" "$r" retire && echo "$dr" | grep -qE "^0\.25[0-9]/-0\.(099|100|101) v3$" && echo 1 || echo 0)"
verdict "(3) un pas : Ctrl+Z rend le clip sans décalage retiré" \
    "$(echo "$dz" | grep -q "^aucun " && grep -q "^VSM_DECALAGE : retir.* sur 1 clip" "$brouillon/annuler.txt" && echo 1 || echo 0)"
verdict "(4) cochée sur le clip corrigé ; décochée, le décalage revient" \
    "$(echo "$coche" | grep -q "^Retirer le décalage continu \[cochee\]" && echo "$dd" | grep -q "^aucun " \
        && grep -q "^VSM_DECALAGE : rendu sur 1 clip" "$brouillon/rendre.txt" && echo 1 || echo 0)"
verdict "(5) sans décalage : rien, et la boîte le dit" \
    "$(echo "$dp" | grep -q "^aucun " && grep -q "^VSM_BOITE : Retirer le décalage continu : Aucun décalage continu" "$brouillon/propre.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "DECALAGE-CONTINU : $rates contrôle(s) raté(s)"; exit 1; fi
echo "DECALAGE-CONTINU : le zéro d'un clip se recale, à la lecture comme à l'export, et se rend d'un geste"
