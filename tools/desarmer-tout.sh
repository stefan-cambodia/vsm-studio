#!/usr/bin/env bash
# D589.2 — DÉSARMER TOUTES LES PISTES (le *Deactivate All Record-Enable* de Cubase), MESURÉ PAR LE RELEVÉ DE L'ARMEMENT,
# LE TÉMOIN À CÔTÉ.
#
# LES RÈGLES GARDÉES. Quatre pistes MIDI ; une piste s'arme par son bouton R (`armer:N`, D579.2), la règle de D579.2 se
# coche par son menu ; `relever-armement` dit les pistes armées et celles que la règle a armées (`VSM_ARMEMENT : {…} ;
# par la règle : {…}`) ; une prise se lance sans décompte, par F9, comme prise-hors-clips.sh.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D589) :
#   (1) `armer:0` et `armer:2`, l'entrée « (2) » : l'armement {}, « 2 piste(s) désarmée(s) » au journal ;
#   (2) Ctrl+Z : {0, 2} ;
#   (3) la règle cochée, la piste 1 choisie (armée par elle), `armer:3`, l'entrée « (2) » : {} et {} par la règle ;
#   (4) la piste 0 armée, une prise lancée : l'entrée « (prise en cours) » grisée, l'armement {0} après la prise ;
#   (5) en anglais, « Disarm all tracks (2) » ;
#   (6) le TÉMOIN (le binaire de D588, passé en argument) sur (1) : l'entrée absente, l'armement {0, 2}.
#
#   tools/desarmer-tout.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-desarmer.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
def vlq(n):
    o = [n & 0x7F]; n >>= 7
    while n: o.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(o)
pistes = []
for i in range(4):
    corps = (b"\x00\xff\x51\x03\x07\xa1\x20" if i == 0 else b"") + b"\x00\xff\x03\x06Voie_" + bytes([0x41 + i])
    corps += vlq(0) + bytes([0x90 | i, 60, 100]) + vlq(480) + bytes([0x80 | i, 60, 0]) + b"\x00\xff\x2f\x00"
    pistes.append(b"MTrk" + struct.pack(">I", len(corps)) + corps)
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 4, 480) + b"".join(pistes))
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "desarmer", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": i, "color": "#FF6B9BFF", "effects": [], "name": f"Voie_{chr(65 + i)}", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.minimoog"}} for i in range(4)]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; le reste : variables
    local nom="$1"
    shift
    env HOME="$(mktemp -d "$brouillon/home.XXXX")" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" \
        "$@" VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|GESTE|ARMEMENT) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS|en cours)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
# La n-ième ligne du relevé : « {…} ; par la règle : {…} ».
armement() { grep -h "^VSM_ARMEMENT : {" "$brouillon/$1.txt" | sed -n "${2}p" | sed 's/^VSM_ARMEMENT : //'; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
TOUT="menu:Enregistrement > Désarmer toutes les pistes (2)"
REGLE="menu:Piste > L'armement suit la piste choisie"
SANS="menu:Enregistrement > Sans décompte"

echo "=== D589.2 : désarmer toutes les pistes ==="
course desarme VSM_DELAI=1800 VSM_GESTE_APRES="300:armer:0;500:armer:2;700:relever-armement;1000:$TOUT;1300:relever-armement"
course annule VSM_DELAI=2200 \
    VSM_GESTE_APRES="300:armer:0;500:armer:2;700:relever-armement;1000:$TOUT;1300:relever-armement;1500:touche:ctrl + Z;1800:relever-armement"
course regle VSM_DELAI=2000 \
    VSM_GESTE_APRES="300:$REGLE;500:choisir:1;700:armer:3;900:relever-armement;1200:$TOUT;1500:relever-armement"
course prise VSM_DELAI=2400 \
    VSM_GESTE_APRES="300:armer:0;400:$SANS;800:touche:F9;1100:lister-menus;1500:touche:F9;1800:relever-armement"
course anglais VSM_LANGUE=en VSM_DELAI=1400 VSM_GESTE_APRES="300:armer:0;500:armer:2;800:lister-menus"

for n in desarme annule regle prise; do
    echo "       $n : $(grep -h "^VSM_ARMEMENT : {" "$brouillon/$n.txt" | sed 's/^VSM_ARMEMENT : //' | tr '\n' '|' | sed 's/|$//')"
done
grep -h "^VSM_ARMEMENT : [0-9a-z]" "$brouillon"/desarme.txt "$brouillon"/prise.txt | sed 's/^/       /'
enprise="$(grep -h "^VSM_MENU_LISTE : Enregistrement > Désarmer toutes les pistes" "$brouillon/prise.txt")"
en="$(grep -c "^VSM_MENU_LISTE : Recording > Disarm all tracks (2)" "$brouillon/anglais.txt")"
echo "       pendant la prise : « ${enprise:-ABSENTE} » ; en anglais : $en entrée"

verdict "(1) 0 et 2 armées, l'entrée « (2) » : l'armement {}, « 2 piste(s) désarmée(s) »" \
    "$(armement desarme 1 | grep -q "^{0, 2} ;" && armement desarme 2 | grep -q "^{} ;" \
        && grep -q "^VSM_ARMEMENT : 2 piste(s) désarmée(s)" "$brouillon/desarme.txt" && echo 1 || echo 0)"
verdict "(2) Ctrl+Z : {0, 2}" \
    "$(armement annule 2 | grep -q "^{} ;" && armement annule 3 | grep -q "^{0, 2} ;" && echo 1 || echo 0)"
verdict "(3) la règle de D579.2 : {} et {} par la règle" \
    "$(armement regle 1 | grep -q "^{1, 3} ; par la règle : {1}$" && [ "$(armement regle 2)" = "{} ; par la règle : {}" ] && echo 1 || echo 0)"
verdict "(4) pendant une prise : l'entrée grisée, l'armement {0} après la prise" \
    "$(echo "$enprise" | grep -q "(prise en cours) \[grisée\]" && armement prise 1 | grep -q "^{0} ;" && echo 1 || echo 0)"
verdict "(5) en anglais" "$([ "$en" = 1 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "DESARMER-TOUT : $rates contrôle(s) raté(s)"; exit 1; fi
echo "DESARMER-TOUT : toutes les pistes armées se désarment d'un geste, en un pas, et jamais pendant une prise"
