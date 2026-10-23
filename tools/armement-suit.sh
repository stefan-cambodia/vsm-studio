#!/usr/bin/env bash
# D579.2 — L'ARMEMENT SUIT LA PISTE CHOISIE (Piste ▸ L'armement suit la piste choisie ; l'*Enable Record on Selected
# Track* de Cubase), MESURÉ PAR LE RELEVÉ DES PISTES ARMÉES APRÈS DES CHOIX SUCCESSIFS, LE TÉMOIN SANS L'OPTION À CÔTÉ.
#
# LES RÈGLES GARDÉES. Trois pistes MIDI, aucune armée. Les choix passent par `choisir:` (le chemin de la liste des
# pistes, `onTrackSelected`) ; l'armement « à la main » par `armer:<n>`, le bouton R d'une piste qu'on ne choisit pas.
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D579) :
#   (1) l'option cochée, `choisir:0` : armées {0} ;
#   (2) `choisir:2` : {2} — la piste que la règle avait armée est désarmée ;
#   (3) la piste 1 armée par son bouton R, puis `choisir:0` : {0, 1} — ce que la main a armé reste armé ;
#   (4) le TÉMOIN sans l'option, `choisir:0` puis `choisir:2` : {} ;
#   (5) relancé dans le même HOME : l'entrée cochée ;
#   (6) en anglais : « Arming follows the selected track » ;
#   (7) la piste 1 armée par son bouton R, `choisir:0` : {0, 1} ; *Piste ▸ Descendre la piste* (la piste de la règle
#       passe au rang 1, celle de la main au rang 0) : {0, 1} ; `choisir:2` : {0, 2} — la règle suit SA piste, pas un
#       rang (le binaire d'avant la correction, retenant un rang, rendait {1, 2} : il désarmait celle de la main).
#
#   tools/armement-suit.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-armement-suit.XXXXXX")"
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
for i in range(3):
    corps = (b"\x00\xff\x51\x03\x07\xa1\x20" if i == 0 else b"") + b"\x00\xff\x03\x06Voie_" + bytes([0x41 + i])
    corps += vlq(0) + bytes([0x90 | i, 60, 100]) + vlq(480) + bytes([0x80 | i, 60, 0]) + b"\x00\xff\x2f\x00"
    pistes.append(b"MTrk" + struct.pack(">I", len(corps)) + corps)
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 3, 480) + b"".join(pistes))
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "armement", "midi": {"file": "midi/arrangement.mid"},
           "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                         "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
           "tracks": [{"channel": i, "color": "#FF6B9BFF", "effects": [], "name": f"Voie_{chr(65 + i)}", "mix": mix,
                       "instrument": {"preferredPlugin": "vsm.minimoog"}} for i in range(3)]},
          open(f"{b}/base/project.json", "w"), indent=1)
PY

course() {   # $1 = nom ; $2 = HOME (vide : un neuf) ; le reste : variables
    local nom="$1" maison="$2"
    shift 2
    [ -n "$maison" ] || maison="$(mktemp -d "$brouillon/home.XXXX")"
    env HOME="$maison" VSM_PROJET="$brouillon/base" VSM_TAILLE="1600x1000" VSM_VUE="sans-rapport" "$@" \
        VSM_CAPTURE="$brouillon/$nom.png" timeout 60 "$BIN" > "$brouillon/$nom.txt" 2>&1
    grep -E "VSM_(GESTE_APRES|VUE|MENU|TOUCHE|GESTE) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
armees() { grep -h "^VSM_ARMEMENT : {" "$brouillon/$1.txt" | sed -n "${2}p" | sed 's/^VSM_ARMEMENT : \({[^}]*}\).*/\1/'; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
REGLE="menu:Piste > L'armement suit la piste choisie"

echo "=== D579.2 : l'armement suit la piste choisie ==="
maison="$(mktemp -d "$brouillon/home.XXXX")"
course regle "$maison" VSM_DELAI=2600 \
    VSM_GESTE_APRES="700:$REGLE;900:choisir:0;1000:relever-armement;1200:choisir:2;1300:relever-armement;1500:armer:1;1700:choisir:0;1800:relever-armement"
course retenu "$maison" VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"
course temoin "" VSM_DELAI=2000 VSM_GESTE_APRES="900:choisir:0;1200:choisir:2;1300:relever-armement"
course anglais "" VSM_LANGUE=en VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"
course deplace "" VSM_DELAI=2600 \
    VSM_GESTE_APRES="700:$REGLE;900:armer:1;1100:choisir:0;1200:relever-armement;1400:menu:Piste > Descendre la piste;1700:relever-armement;1900:choisir:2;2000:relever-armement"

a1="$(armees regle 1)"; a2="$(armees regle 2)"; a3="$(armees regle 3)"; t1="$(armees temoin 1)"
d1="$(armees deplace 1)"; d2="$(armees deplace 2)"; d3="$(armees deplace 3)"
coche="$(grep -h "^VSM_MENU_LISTE : Piste > L'armement suit la piste choisie" "$brouillon/retenu.txt")"
en="$(grep -ch "^VSM_MENU_LISTE : Track > Arming follows the selected track" "$brouillon/anglais.txt")"
grep -h "^VSM_ARMEMENT\|^VSM_ENREGISTREMENT_REGLAGE\|^VSM_GESTE : armer" "$brouillon"/regle.txt "$brouillon"/temoin.txt | sed 's/^/       /'
echo "       après Descendre la piste :"
grep -h "^VSM_ARMEMENT\|^VSM_GESTE : armer\|^VSM_MENU : " "$brouillon"/deplace.txt | sed 's/^/       /'
echo "       relancé : [${coche#VSM_MENU_LISTE : }] ; en anglais : $en entrée(s)"

verdict "(1) l'option cochée, choisir:0 : {0}" "$([ "$a1" = "{0}" ] && echo 1 || echo 0)"
verdict "(2) choisir:2 : {2}" "$([ "$a2" = "{2}" ] && echo 1 || echo 0)"
verdict "(3) la piste 1 armée à la main, choisir:0 : {0, 1}" "$([ "$a3" = "{0, 1}" ] && echo 1 || echo 0)"
verdict "(4) le témoin sans l'option : {}" "$([ "$t1" = "{}" ] && echo 1 || echo 0)"
verdict "(5) retenu au poste : cochée au lancement suivant" "$(echo "$coche" | grep -q "\[cochée\]" && echo 1 || echo 0)"
verdict "(6) en anglais : « Arming follows the selected track »" "$([ "$en" = 1 ] && echo 1 || echo 0)"
verdict "(7) la règle suit SA piste : {0, 1}, Descendre {0, 1}, choisir:2 {0, 2}" \
    "$([ "$d1" = "{0, 1}" ] && [ "$d2" = "{0, 1}" ] && [ "$d3" = "{0, 2}" ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "ARMEMENT-SUIT : $rates contrôle(s) raté(s)"; exit 1; fi
echo "ARMEMENT-SUIT : la piste choisie s'arme, et ce que la main a armé le reste"
