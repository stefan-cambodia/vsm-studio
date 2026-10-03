#!/usr/bin/env bash
# D542.3 — DÉCOUPER UN CLIP AUDIO À SES SILENCES INTÉRIEURS, MESURÉ PAR L'EXPORT AUDIO.
#
# LA RÈGLE GARDÉE. Un fichier de trois salves de bruit séparées de silences NUMÉRIQUES (des zéros),
# importé sur une piste audio (un clip).
#   (1) « Découper aux silences… » (seuil −60 dBFS, silence minimal 200 ms) : trois clips au projet
#       relu, et le journal dit « 3 passage(s) » ;
#   (2) l'export audio du projet découpé est, échantillon par échantillon, IDENTIQUE AU BIT PRÈS au
#       début de l'export d'avant le geste, et ce que celui-ci a DE PLUS ne dépasse pas le plancher
#       de l'export (un pas de 24 bits : le dither, présent dans TOUS ses silences, avant la première
#       salve compris) — le silence de fin retiré raccourcit l'export (D540 : il finit où finit ce
#       qu'on entend). Écrit d'abord « les deux fichiers identiques », puis « le reste n'est que des
#       zéros » : la première course a montré 4,0 s contre 4,3 s, la seconde un reste à ±1 pas ;
#   (3) le témoin : un seuil au-dessus des salves (−3 dBFS) → rien de découpé, c'est dit, un clip ;
#   (4) Ctrl+Z après le geste → un seul clip.
# Chaque fichier se lit AVANT de juger (deux .wav présents et non vides avant le cmp).
#
#   tools/decouper-aux-silences.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-silences.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$brouillon"/*.wav "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, random, struct, sys, wave
b = sys.argv[1]
# Trois salves de 0,3 s à −6 dBFS de crête, séparées de 0,4 s de ZÉROS, 0,3 s de zéros aux bords.
sr, r = 44100, random.Random(7)
trames = []
for duree, son in [(0.3, False), (0.3, True), (0.4, False), (0.3, True), (0.4, False), (0.3, True), (0.3, False)]:
    for _ in range(int(duree * sr)):
        v = int(r.uniform(-0.5, 0.5) * 32767) if son else 0
        trames.append(struct.pack("<hh", v, v))
with wave.open(f"{b}/salves.wav", "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(b"".join(trames))
# Un projet vide, qui a un dossier : l'import y pose sa piste audio.
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
    grep -E "VSM_(GESTE_APRES|MENU_CONTEXTE|OPTIONS|EXPORT|IMPORT) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
clips() {   # le nombre de clips de la piste audio d'un project.json, ou « ABSENT »
    python3 -c "
import json, sys
try:
    pistes = json.load(open(sys.argv[1]))['tracks']
except Exception:
    print('ABSENT'); sys.exit(0)
audio = [p for p in pistes if p.get('kind') == 'audio']
print(len(audio[0].get('clips', [])) if audio else 'PAS-DE-PISTE-AUDIO')
" "$1"
}

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D542.3 : découper un clip audio à ses silences ==="
# L'import, enregistré DANS le dossier du projet (le fichier importé y est) ; puis trois copies.
course import "$brouillon/base" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/salves.wav" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/base"
for x in decoupe temoin annule; do cp -r "$brouillon/base" "$brouillon/$x"; done
MENU="clip-audio-tous:Découper aux silences…"
course decouper "$brouillon/decoupe" VSM_DELAI=2500 VSM_MENU_CONTEXTE="$MENU" VSM_OPTIONS="seuil=-60;silence=200" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/decoupe"
course seuil-haut "$brouillon/temoin" VSM_DELAI=2500 VSM_MENU_CONTEXTE="$MENU" VSM_OPTIONS="seuil=-3;silence=200" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/temoin"
course annuler "$brouillon/annule" VSM_DELAI=3000 VSM_MENU_CONTEXTE="$MENU" VSM_OPTIONS="seuil=-60;silence=200" \
    VSM_GESTE_APRES="1500:touche:ctrl + Z;2000:enregistrer:$brouillon/annule"
course export-avant "$brouillon/base" VSM_DELAI=1500 VSM_EXPORT="$brouillon/avant.wav"
course export-apres "$brouillon/decoupe" VSM_DELAI=1500 VSM_EXPORT="$brouillon/apres.wav"

grep -h "^VSM_SILENCES" "$brouillon/decouper.txt" "$brouillon/seuil-haut.txt" | sed 's/^/       /'
cb="$(clips "$brouillon/base/project.json")"; cd_="$(clips "$brouillon/decoupe/project.json")"
ct="$(clips "$brouillon/temoin/project.json")"; ca="$(clips "$brouillon/annule/project.json")"
echo "       clips : importé $cb ; découpé $cd_ ; seuil haut $ct ; Ctrl+Z $ca"
echo "       exports (octets) : avant $(stat -c %s "$brouillon/avant.wav" 2>/dev/null || echo absent), après $(stat -c %s "$brouillon/apres.wav" 2>/dev/null || echo absent)"
verdict "(1) trois clips, et le journal dit trois passages" \
    "$([ "$cb" = 1 ] && [ "$cd_" = 3 ] && grep -q "^VSM_SILENCES : 3 passage(s)" "$brouillon/decouper.txt" && echo 1 || echo 0)"
compare="$("$racine/analyse/.venv/bin/python" - "$brouillon/avant.wav" "$brouillon/apres.wav" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    a, _ = sf.read(sys.argv[1], dtype="int32", always_2d=True)
    b, _ = sf.read(sys.argv[2], dtype="int32", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
if len(a) == 0 or len(b) == 0 or len(b) > len(a):
    print(f"LONGUEURS {len(a)} {len(b)}"); sys.exit(0)
debut = int(np.count_nonzero(a[:len(b)] != b))
plancher = int(np.abs(a[:4410] >> 8).max())          # les 0,1 s de silence d'avant la première salve
reste = int(np.abs(a[len(b):] >> 8).max()) if len(a) > len(b) else 0
print(f"{debut} échantillon(s) différent(s) sur {len(b)} trames ; reste au plus {reste} pas de 24 bits, plancher {plancher}")
PY2
)"
echo "       comparaison : $compare"
verdict "(2) l'export découpé : le début de l'export d'avant au bit près, le reste au plancher" \
    "$(echo "$compare" | "$racine/analyse/.venv/bin/python" -c "
import re, sys
m = re.match(r'^0 échantillon\(s\) différent\(s\) sur \d+ trames ; reste au plus (\d+) pas de 24 bits, plancher (\d+)', sys.stdin.read())
sys.exit(0 if m and int(m.group(1)) <= max(1, int(m.group(2))) else 1)" && echo 1 || echo 0)"
verdict "(3) le témoin : un seuil au-dessus des salves, rien de découpé, et c'est dit" \
    "$([ "$ct" = 1 ] && grep -q "^VSM_SILENCES : 0 passage(s) — rien de découpé" "$brouillon/seuil-haut.txt" && echo 1 || echo 0)"
verdict "(4) Ctrl+Z : un seul clip" "$([ "$ca" = 1 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "SILENCES : $rates contrôle(s) raté(s)"; exit 1; fi
echo "SILENCES : le clip se découpe à ses silences, et l'export n'en perd pas un échantillon"
