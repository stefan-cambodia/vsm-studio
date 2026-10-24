#!/usr/bin/env bash
# D593 — LE COUPE-BAS ET LE COUPE-HAUT DE LA TRANCHE (la section *Pre* de Cubase), MESURÉS PAR L'EXPORT : le rendu
# hors-ligne (`vsm-render`) et l'export de l'application, chacun contre son témoin du MÊME code (le filtre éteint).
#
# LE JEU : un fichier de 4 s à 48 kHz, 50 Hz + 1 kHz + 8 kHz, chacun à −20 dBFS, importé sur une piste audio. Chaque
# composante se relit sur les deux secondes du milieu (un nombre entier de périodes pour les trois, à toute fréquence
# d'échantillonnage) et se rapporte au témoin.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D593 ; biquad de RBJ à 48 kHz, calculé avant le code) :
#   (2) vsm-render, la seule valeur du champ change : coupe-bas 200 Hz → 50 Hz à −24,1 dB (±0,3), 1 kHz et 8 kHz à
#       −0,0 (±0,1) ; coupe-haut 2 kHz → 8 kHz à −25,7 (±0,3), 1 kHz à −0,26 (±0,1), 50 Hz à −0,0 (±0,1) ;
#   (3) l'application : « mixeur.coupebas » réglé à 200 → `"lowCutHz": 200` dans project.json, et l'export de
#       l'application rend 50 Hz à −24,1 dB (±0,3) sous son témoin ; Ctrl+Z → le champ absent ; le double-clic → éteint ;
#       « Remettre la console à zéro » → éteint ; « Copier / Coller les réglages de la tranche » le porte à l'autre piste ;
#   (4) en anglais, la valeur posée se lit « Low cut 200 Hz » ;
#   (5) avec le binaire d'avant (le témoin), le curseur est introuvable.
#
#   tools/coupe-bas-haut.sh [binaire de l'application] [vsm-render]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
RENDU="${2:-./build/tools/vsm-render}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }
[ -x "$RENDU" ] || { echo "REFUS : $RENDU absent — compiler vsm-render d'abord"; exit 2; }
PY="$racine/analyse/.venv/bin/python"
[ -x "$PY" ] || { echo "REFUS : $PY absent (numpy, soundfile) — voir analyse/requirements.txt"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-coupe.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, math, os, struct, sys, wave
b = sys.argv[1]
sr = 48000
def v(n):
    t = n / sr
    return int(32767 * 0.1 * (math.sin(2 * math.pi * 50 * t) + math.sin(2 * math.pi * 1000 * t)
                              + math.sin(2 * math.pi * 8000 * t)))
with wave.open(f"{b}/sons.wav", "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes(b"".join(struct.pack("<hh", x, x) for x in (v(n) for n in range(4 * sr))))
os.makedirs(f"{b}/base/midi", exist_ok=True)
corps = b"\x00\xff\x51\x03\x07\xa1\x20\x00\xff\x2f\x00"
open(f"{b}/base/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                  + b"MTrk" + struct.pack(">I", len(corps)) + corps)
json.dump({"format": "vsm-project", "version": 1, "title": "coupe", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": []}, open(f"{b}/base/project.json", "w"), indent=1)
PY

avertis=0
course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2" n
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    # D147 : ce que l'application avertit se compte et se montre AVANT de conclure.
    local motif="VSM_(GESTE_APRES|MENU|TOUCHE|IMPORT|EXPORT|VALEUR_POSEE|DOUBLECLIC) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)"
    n=$(grep -cE "$motif" "$brouillon/$nom.txt")
    avertis=$((avertis + n))
    grep -E "$motif" "$brouillon/$nom.txt" | sed "s/^/        journal ($nom) : /" >&2
}
champs() {   # « coupe-bas/coupe-haut » de chaque piste d'un projet enregistré, ou ABSENT
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
print(' '.join('%g/%g' % (t['mix'].get('lowCutHz', 0), t['mix'].get('highCutHz', 0)) for t in d['tracks']) or 'VIDE')" "$1"
}
composantes() {   # l'amplitude de 50 Hz, 1 kHz et 8 kHz sur les deux secondes du milieu, ou ABSENT
    "$PY" - "$1" <<'PY2'
import sys
import numpy as np
import soundfile as sf
try:
    x, sr = sf.read(sys.argv[1], dtype="float64", always_2d=True)
except Exception:
    print("ABSENT"); sys.exit(0)
a = x[int(1.0 * sr):int(1.0 * sr) + int(2.0 * sr), 0]
if len(a) < int(2.0 * sr):
    print("COURT"); sys.exit(0)
n = np.arange(len(a))
print(" ".join("%.6e" % (abs(np.sum(a * np.exp(-2j * np.pi * f * n / sr))) * 2 / len(a)) for f in (50.0, 1000.0, 8000.0)))
PY2
}
ecarts() {   # $1 = composantes mesurées, $2 = celles du témoin → trois écarts en dB, ou ABSENT
    python3 -c "
import math, sys
try:
    m = [float(v) for v in sys.argv[1].split()]; t = [float(v) for v in sys.argv[2].split()]
    print(' '.join('%.3f' % (20 * math.log10(a / b)) for a, b in zip(m, t)) if len(m) == len(t) == 3 else 'ABSENT')
except Exception:
    print('ABSENT')" "$1" "$2"
}
pres() { python3 -c "import sys; sys.exit(0 if abs(float(sys.argv[1]) - float(sys.argv[2])) <= float(sys.argv[3]) else 1)" "$@" 2>/dev/null; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }

echo "=== D593 : le coupe-bas et le coupe-haut de la tranche ==="
course import "$brouillon/base" VSM_DELAI=2500 VSM_IMPORT_AUDIO="$brouillon/sons.wav" VSM_GESTE_APRES="1500:enregistrer:$brouillon/base"
if [ "$(champs "$brouillon/base")" != "0/0" ]; then
    echo "  RATÉ l'import : le projet de départ n'a pas UNE piste éteinte ($(champs "$brouillon/base")) — rien d'autre n'est mesuré"
    exit 1
fi

# (2) LE RENDU HORS-LIGNE : trois projets qui ne diffèrent QUE par le champ.
for x in r-temoin r-bas r-haut; do cp -r "$brouillon/base" "$brouillon/$x"; done
python3 - "$brouillon" <<'PY'
import json, sys
b = sys.argv[1]
for nom, cle, hz in (("r-bas", "lowCutHz", 200.0), ("r-haut", "highCutHz", 2000.0)):
    d = json.load(open(f"{b}/{nom}/project.json", encoding="utf-8"))
    d["tracks"][0]["mix"][cle] = hz
    json.dump(d, open(f"{b}/{nom}/project.json", "w", encoding="utf-8"), indent=1)
PY
for x in r-temoin r-bas r-haut; do "$RENDU" "$brouillon/$x" "$brouillon/$x.wav" --sample-rate 48000 --quiet > "$brouillon/$x.txt" 2>&1; done
ct="$(composantes "$brouillon/r-temoin.wav")"
read -r rb50 rb1k rb8k <<< "$(ecarts "$(composantes "$brouillon/r-bas.wav")" "$ct")"
read -r rh50 rh1k rh8k <<< "$(ecarts "$(composantes "$brouillon/r-haut.wav")" "$ct")"
echo "       vsm-render, témoin [$ct]"
echo "       coupe-bas 200 Hz : 50 Hz ${rb50:-?} dB, 1 kHz ${rb1k:-?} dB, 8 kHz ${rb8k:-?} dB"
echo "       coupe-haut 2 kHz : 50 Hz ${rh50:-?} dB, 1 kHz ${rh1k:-?} dB, 8 kHz ${rh8k:-?} dB"

# (3) L'APPLICATION. Deux pistes pour la copie : la même piste audio, recopiée sous un autre nom.
for x in a-bas a-temoin a-z a-dc a-zero a-en a-copie; do cp -r "$brouillon/base" "$brouillon/$x"; done
python3 - "$brouillon/a-copie/project.json" <<'PY'
import copy, json, sys
d = json.load(open(sys.argv[1], encoding="utf-8"))
autre = copy.deepcopy(d["tracks"][0]); autre["name"] = "Copie"
d["tracks"].append(autre)
json.dump(d, open(sys.argv[1], "w", encoding="utf-8"), indent=1)
PY
POSER="900:valeur:mixeur.coupebas=200"
course a-bas "$brouillon/a-bas" VSM_DELAI=3500 VSM_GESTE_APRES="$POSER;1300:enregistrer:$brouillon/a-bas;1700:exporter-audio:$brouillon/a-bas.wav"
course a-temoin "$brouillon/a-temoin" VSM_DELAI=3000 VSM_GESTE_APRES="1300:exporter-audio:$brouillon/a-temoin.wav"
course a-z "$brouillon/a-z" VSM_DELAI=2500 VSM_GESTE_APRES="$POSER;1300:touche:ctrl + Z;1800:enregistrer:$brouillon/a-z"
course a-dc "$brouillon/a-dc" VSM_DELAI=2500 VSM_GESTE_APRES="$POSER;1300:doubleclic:mixeur.coupebas;1800:enregistrer:$brouillon/a-dc"
course a-zero "$brouillon/a-zero" VSM_DELAI=2500 VSM_CONFIRMER=oui \
    VSM_GESTE_APRES="$POSER;1300:menu:Mixage > Remettre la console à zéro (1 tranche(s))…;1800:enregistrer:$brouillon/a-zero"
course a-copie "$brouillon/a-copie" VSM_DELAI=3200 \
    VSM_GESTE_APRES="600:choisir:0;$POSER;1300:menu:Piste > Copier les réglages de la tranche;1600:choisir:1;2000:menu:Piste > Coller les réglages sur les pistes choisies (1);2500:enregistrer:$brouillon/a-copie"
course a-en "$brouillon/a-en" VSM_LANGUE=en VSM_DELAI=2000 VSM_GESTE_APRES="$POSER"

read -r ab50 ab1k ab8k <<< "$(ecarts "$(composantes "$brouillon/a-bas.wav")" "$(composantes "$brouillon/a-temoin.wav")")"
pose="$(grep -h "^VSM_VALEUR_POSEE" "$brouillon/a-bas.txt" | head -1)"
en="$(grep -h "^VSM_VALEUR_POSEE" "$brouillon/a-en.txt" | head -1)"
echo "       l'application : $pose"
echo "       enregistré [$(champs "$brouillon/a-bas")] ; export : 50 Hz ${ab50:-?} dB, 1 kHz ${ab1k:-?} dB, 8 kHz ${ab8k:-?} dB"
echo "       Ctrl+Z [$(champs "$brouillon/a-z")] ; double-clic [$(champs "$brouillon/a-dc")] ; remise à zéro [$(champs "$brouillon/a-zero")] ; copie [$(champs "$brouillon/a-copie")]"
grep -h "^VSM_TRANCHE" "$brouillon/a-copie.txt" | head -1 | sed 's/^/       /'
echo "       en anglais : $en"

verdict "(2) vsm-render, coupe-bas : −24,1 / −0,0 / −0,0 dB" \
    "$(pres "${rb50:-x}" -24.1 0.3 && pres "${rb1k:-x}" 0 0.1 && pres "${rb8k:-x}" 0 0.1 && echo 1 || echo 0)"
verdict "(2) vsm-render, coupe-haut : −0,0 / −0,26 / −25,7 dB" \
    "$(pres "${rh50:-x}" 0 0.1 && pres "${rh1k:-x}" -0.26 0.1 && pres "${rh8k:-x}" -25.7 0.3 && echo 1 || echo 0)"
verdict "(3) le curseur réglé : 200/0 enregistré" "$([ "$(champs "$brouillon/a-bas")" = "200/0" ] && echo 1 || echo 0)"
verdict "(3) l'export de l'application : 50 Hz à −24,1 dB" "$(pres "${ab50:-x}" -24.1 0.3 && pres "${ab1k:-x}" 0 0.1 && echo 1 || echo 0)"
verdict "(3) Ctrl+Z : éteint" "$([ "$(champs "$brouillon/a-z")" = "0/0" ] && grep -q "coupebas=200" "$brouillon/a-z.txt" && echo 1 || echo 0)"
verdict "(3) le double-clic : éteint" "$([ "$(champs "$brouillon/a-dc")" = "0/0" ] && grep -q "coupebas=200" "$brouillon/a-dc.txt" && echo 1 || echo 0)"
verdict "(3) la remise à zéro de la console : éteint" "$([ "$(champs "$brouillon/a-zero")" = "0/0" ] && grep -q "coupebas=200" "$brouillon/a-zero.txt" && echo 1 || echo 0)"
verdict "(3) les réglages copiés : 200 sur les deux pistes" "$([ "$(champs "$brouillon/a-copie")" = "200/0 200/0" ] && echo 1 || echo 0)"
verdict "(4) en anglais : « Low cut 200 Hz »" "$(printf '%s' "$en" | grep -q "Low cut 200 Hz" && echo 1 || echo 0)"
[ "$avertis" -gt 0 ] && echo "       $avertis avertissement(s) de l'application (ci-dessus)"

echo
if [ "$rates" -gt 0 ]; then echo "COUPE-BAS-HAUT : $rates contrôle(s) raté(s)"; exit 1; fi
echo "COUPE-BAS-HAUT : la tranche coupe le grave et l'aigu avant ses inserts, dans le moteur comme dans l'export"
