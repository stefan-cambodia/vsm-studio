#!/usr/bin/env bash
# D549.2 — RENDRE SILENCIEUSE UNE PLAGE D'UN CLIP AUDIO, MESURÉ PAR L'EXPORT.
#
# LA RÈGLE GARDÉE. Un sinus de 1 kHz de 4 s (crête 0,5) importé sur une piste audio, 120 BPM ; les locateurs de
# 1 · 3 à 2 · 3 (1,0 à 3,0 s).
#   (1) « Rendre silencieux entre les locateurs » : l'EXPORT est muet de 1,01 à 2,99 s (niveau efficace sous
#       10⁻⁴ de celui du témoin) ; hors de la plage — de 0 à 0,99 s et de 3,01 à 3,99 s — il égale le TÉMOIN,
#       l'export d'avant le geste, à 10⁻⁶ près, échantillon par échantillon ; le fichier du clip n'a pas changé
#       (même empreinte) ;
#   (2) un pas : le geste puis Ctrl+Z → plus de courbe de gain au projet relu ;
#   (3) sans locateurs : « Rendre silencieux » dit ce qui manque, aucun pas.
#
#   tools/silence-plage.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-silence.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
sr = 44100
trames = b"".join(struct.pack("<hh", v, v) for v in
                  (int(0.5 * 32767 * math.sin(2 * math.pi * 1000.0 * n / sr)) for n in range(4 * sr)))
with wave.open(f"{b}/sinus.wav", "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(trames)
for nom, boucle in (("base", {"enabled": False, "startTick": 960, "endTick": 2880}), ("sans", None)):
    os.makedirs(f"{b}/{nom}/midi", exist_ok=True)
    corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"
    open(f"{b}/{nom}/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                      + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    transport = {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                 "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]}
    if boucle:
        transport["loop"] = boucle
    json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
               "transport": transport, "tracks": []}, open(f"{b}/{nom}/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|MENU_CONTEXTE|EXPORT|IMPORT|TOUCHE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|GRISÉE|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
a_clip() { python3 -c "
import json, sys
p = json.load(open(sys.argv[1]))
sys.exit(0 if any(t.get('kind') == 'audio' and t.get('clips') for t in p['tracks']) else 1)" "$1" 2>/dev/null; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D549.2 : rendre silencieuse une plage ==="
course import "$brouillon/base" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/sinus.wav" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/base"
course import-sans "$brouillon/sans" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/sinus.wav" \
    VSM_GESTE_APRES="1500:enregistrer:$brouillon/sans"
grep -h "^Import audio" "$brouillon/import.txt" | sed 's/^/       /'
if ! a_clip "$brouillon/base/project.json" || ! a_clip "$brouillon/sans/project.json"; then
    echo "  RATÉ l'import : un projet de départ n'a pas de clip audio — rien d'autre n'est mesuré"; exit 1
fi
cp -r "$brouillon/base" "$brouillon/tu"; cp -r "$brouillon/base" "$brouillon/annule"
MENU="clip-audio:Rendre silencieux entre les locateurs"
course taire "$brouillon/tu" VSM_DELAI=2500 VSM_MENU_CONTEXTE="$MENU" VSM_GESTE_APRES="1500:enregistrer:$brouillon/tu"
course annuler "$brouillon/annule" VSM_DELAI=3000 VSM_MENU_CONTEXTE="$MENU" \
    VSM_GESTE_APRES="1300:touche:ctrl + Z;2000:enregistrer:$brouillon/annule"
course sans "$brouillon/sans" VSM_DELAI=2500 VSM_MENU_CONTEXTE="$MENU" VSM_GESTE_APRES="1300:relever-historique"
for x in base tu; do course "export-$x" "$brouillon/$x" VSM_DELAI=1500 VSM_EXPORT="$brouillon/$x.wav"; done

grep -h "^VSM_SILENCE" "$brouillon/taire.txt" | sed 's/^/       /'
mesure="$("$PY" - "$brouillon/base.wav" "$brouillon/tu.wav" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    t, sr = sf.read(sys.argv[1], dtype="float64", always_2d=True)
    x, _ = sf.read(sys.argv[2], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
n = min(len(t), len(x))
t, x = t[:n], x[:n]
z = lambda d, f: slice(int(d * sr), int(f * sr))
rms = lambda a, s: float(np.sqrt(np.mean(a[s, 0] ** 2)))
dedans = rms(x, z(1.01, 2.99)) / max(rms(t, z(1.01, 2.99)), 1e-12)
hors = max(float(np.max(np.abs(x[z(0.0, 0.99)] - t[z(0.0, 0.99)]))),
           float(np.max(np.abs(x[z(3.01, 3.99)] - t[z(3.01, 3.99)]))))
print(f"{dedans:.2e} {hors:.2e} {rms(t, z(0.0, 0.99)):.3f}")
PY2
)"
read -r dedans hors niveau <<< "$mesure"
empreinte="$(sha256sum "$brouillon/sinus.wav" | cut -c1-16)"
fichier_clip="$(ls "$brouillon/tu/audio/"*.wav 2>/dev/null | head -1)"
empreinte_clip="$([ -n "$fichier_clip" ] && sha256sum "$fichier_clip" | cut -c1-16)"
courbe_annulee="$(python3 -c "
import json, sys
p = json.load(open(sys.argv[1]))
print(sum(len(c.get('gainEnvelope', [])) for t in p['tracks'] for c in t.get('clips', [])))" "$brouillon/annule/project.json" 2>/dev/null)"
hist="$(grep -h "^VSM_HISTORIQUE_PAS : " "$brouillon/sans.txt" | tail -1)"
echo "       export : dans la plage ${dedans:-?} du témoin ; hors de la plage, écart max ${hors:-?} (témoin à ${niveau:-?} efficace)"
echo "       fichier : importé $empreinte, au projet ${empreinte_clip:-ABSENT} ; Ctrl+Z → ${courbe_annulee:-?} point(s) ; sans locateurs ${hist:-?}"
verdict "(1) l'export se tait dans la plage, égale le témoin hors d'elle, le fichier est intact" \
    "$(python3 -c "import sys; sys.exit(0 if float(sys.argv[1]) < 1e-4 and float(sys.argv[2]) < 1e-6 and float(sys.argv[3]) > 0.1 else 1)" \
        "${dedans:-1}" "${hors:-1}" "${niveau:-0}" 2>/dev/null && { [ -z "$fichier_clip" ] || [ "$empreinte_clip" = "$empreinte" ]; } && echo 1 || echo 0)"
verdict "(2) un pas : Ctrl+Z rend le clip sans courbe" "$([ "$courbe_annulee" = 0 ] && echo 1 || echo 0)"
verdict "(3) sans locateurs : le geste dit ce qui manque, aucun pas" \
    "$([ "$(echo "$hist" | cut -d' ' -f3)" = 0 ] && grep -q "^VSM_BOITE : Rendre silencieux : Aucune plage entre les locateurs" "$brouillon/sans.txt" \
        && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "SILENCE-PLAGE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "SILENCE-PLAGE : la plage se tait à l'export, et rien d'autre ne change"
