#!/usr/bin/env bash
# D561.2 — TAPER LE TEMPO (le bouton « Tap » de D3.2, Transport ▸ Taper le tempo, Maj+T ; le *Tap Tempo* de Cubase),
# MESURÉ PAR LE PROJET RELU (`tempoChanges`).
#
# LES RÈGLES GARDÉES — UNE règle pour le bouton « Tap » (D3.2), le menu et Maj+T. Un projet à 100 BPM.
#   (1) huit frappes de Maj+T à 500 ms : 120 BPM à ±1,5 ; la première ne change rien et le dit (« frappe 1 / 2 »), la
#       deuxième applique ;
#   (2) huit frappes par le MENU à 400 ms : 150 BPM à ±2 ;
#   (3) six frappes du BOUTON « Tap » à 500 ms, puis UN Ctrl+Z : 100 — la série est un seul pas, par le bouton aussi
#       (avant D561.2, chaque frappe du bouton ouvrait le sien) ; et huit frappes du bouton : 120 ;
#   (4) trois frappes à 500 ms, une pause de 2,5 s, trois à 400 ms : 150 — la pause a recommencé la série (sans elle,
#       l'intervalle de 2,9 s tirerait la moyenne vers 80) ; et un Ctrl+Z rend 120 : deux séries, deux pas ;
#   (5) huit frappes à 150 ms (400 BPM) : refusé et dit, 100 (le bouton de D3.2 le ramenait à 300 sans un mot).
#
#   tools/tempo-tape.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-tempo-tape.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
# 100 BPM : 600 000 µs par noire (0x0927C0).
corps = b"\x00\xff\x51\x03\x09\x27\xc0\x00\xff\x03\x06Partie\x00\x90\x3c\x64\x83\x60\x80\x3c\x00\x00\xff\x2f\x00"
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
                                                 + b"MTrk" + struct.pack(">I", len(corps)) + corps)
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "essai", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 100.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [], "name": "Partie", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.juno106"}}]}, open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = dossier du projet ; le reste : variables
    local nom="$1" projet="$2"
    shift 2
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$projet" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 90 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE) : .*(inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
tempo() {   # le tempo de départ d'un project.json, ou « ABSENT »
    python3 -c "
import json, sys
try:
    d = json.load(open(sys.argv[1] + '/project.json', encoding='utf-8'))
except Exception:
    print('ABSENT'); sys.exit(0)
t = [c for c in d['transport']['tempoChanges'] if c.get('tick', 0) == 0]
print(f\"{t[0]['bpm']:.2f}\" if t else 'ABSENT')" "$1"
}
serie() {   # $1 = départ (ms), $2 = pas (ms), $3 = nombre, $4 = geste : « ms:geste;… »
    local i out=""
    for ((i = 0; i < $3; i++)); do out+="$(( $1 + i * $2 )):$4;"; done
    echo "$out"
}
pres() { python3 -c "
import sys
try:
    sys.exit(0 if abs(float(sys.argv[1]) - float(sys.argv[2])) <= float(sys.argv[3]) else 1)
except ValueError:
    sys.exit(1)" "$1" "$2" "$3"; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
T="touche:shift + T"
M="menu:Transport > Taper le tempo"

echo "=== D561.2 : taper le tempo ==="
B="cliquer:Tap"
for x in t m b z p pz r; do cp -r "$brouillon/base" "$brouillon/$x"; done
course touche "$brouillon/t" VSM_DELAI=6000 VSM_GESTE_APRES="$(serie 1000 500 8 "$T")5000:enregistrer:$brouillon/t"
course menu "$brouillon/m" VSM_DELAI=5500 VSM_GESTE_APRES="$(serie 1000 400 8 "$M")4500:enregistrer:$brouillon/m"
course bouton "$brouillon/b" VSM_DELAI=6000 VSM_GESTE_APRES="$(serie 1000 500 8 "$B")5000:enregistrer:$brouillon/b"
course annuler "$brouillon/z" VSM_DELAI=5500 VSM_GESTE_APRES="$(serie 1000 500 6 "$B")4200:touche:ctrl + Z;4600:enregistrer:$brouillon/z"
course pause "$brouillon/p" VSM_DELAI=6500 VSM_GESTE_APRES="$(serie 1000 500 3 "$T")$(serie 4500 400 3 "$T")5800:enregistrer:$brouillon/p"
course pause-z "$brouillon/pz" VSM_DELAI=7000 \
    VSM_GESTE_APRES="$(serie 1000 500 3 "$T")$(serie 4500 400 3 "$T")5800:touche:ctrl + Z;6300:enregistrer:$brouillon/pz"
course rapide "$brouillon/r" VSM_DELAI=4000 VSM_GESTE_APRES="$(serie 1000 150 8 "$T")3000:enregistrer:$brouillon/r"

tt="$(tempo "$brouillon/t")"; tm="$(tempo "$brouillon/m")"; tb="$(tempo "$brouillon/b")"; tz="$(tempo "$brouillon/z")"
tp="$(tempo "$brouillon/p")"; tpz="$(tempo "$brouillon/pz")"; tr_="$(tempo "$brouillon/r")"
grep -h "^VSM_TEMPO_TAPE" "$brouillon/touche.txt" | head -3 | sed 's/^/       (touche) /'
grep -h "^VSM_TEMPO_TAPE" "$brouillon/pause.txt" | tr '\n' ' ' | sed 's/^/       (pause) /'; echo
grep -h "^VSM_TEMPO_TAPE" "$brouillon/rapide.txt" | tail -1 | sed 's/^/       (rapide) /'
echo "       Maj+T à 500 ms : [$tt] ; menu à 400 ms : [$tm] ; bouton à 500 ms : [$tb] ; six au bouton puis Ctrl+Z : [$tz]"
echo "       3 + pause + 3 : [$tp], puis Ctrl+Z : [$tpz] ; 150 ms : [$tr_]"

verdict "(1) Maj+T à 500 ms : 120 à ±1,5, la deuxième frappe applique" \
    "$(pres "$tt" 120 1.5 && grep -q "^VSM_TEMPO_TAPE : frappe 1 / 2" "$brouillon/touche.txt" \
        && grep -q "^VSM_TEMPO_TAPE : frappe 2, " "$brouillon/touche.txt" && echo 1 || echo 0)"
verdict "(2) par le menu à 400 ms : 150 à ±2" "$(pres "$tm" 150 2 && echo 1 || echo 0)"
verdict "(3) le bouton : 120, et une série, un pas — Ctrl+Z rend 100" \
    "$(pres "$tb" 120 1.5 && [ "$tz" = "100.00" ] && grep -q "^VSM_TEMPO_TAPE : frappe 6, " "$brouillon/annuler.txt" && echo 1 || echo 0)"
verdict "(4) une pause de 2,5 s recommence la série : 150, et Ctrl+Z rend 120" \
    "$(pres "$tp" 150 2 && pres "$tpz" 120 1.5 && [ "$(grep -c "^VSM_TEMPO_TAPE : frappe 1 / 2" "$brouillon/pause.txt")" = 2 ] && echo 1 || echo 0)"
verdict "(5) 400 BPM : refusé et dit, 100" \
    "$([ "$tr_" = "100.00" ] && grep -q "^VSM_TEMPO_TAPE : refus.*hors de 20" "$brouillon/rapide.txt" && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "TEMPO-TAPE : $rates contrôle(s) raté(s)"; exit 1; fi
echo "TEMPO-TAPE : le bouton, le menu et Maj+T frappent le même tempo, dès la deuxième frappe, une série par pas"
