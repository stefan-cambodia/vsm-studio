#!/usr/bin/env bash
# D582.2 — LE SOLO CHOISIT LA PISTE (Piste ▸ Le solo choisit la piste ; le *Select Channel/Track on Solo* de Cubase),
# MESURÉ PAR LA PISTE CHOISIE RELEVÉE APRÈS CHAQUE SOLO, LE TÉMOIN SANS L'OPTION À CÔTÉ.
#
# LES RÈGLES GARDÉES. Trois pistes MIDI. Le S de la LIGNE choisit DÉJÀ une piste hors de la sélection (D38.4,
# `selectionPourUnGesteSur`) : l'option ne change que le S d'une TRANCHE de la console (`cliquer:mixeur.solo`, le premier
# homonyme — la tranche de la piste 0) et le solo EXCLUSIF (`modificateur:ctrl:solo:<n>`), et elle ne réduit pas une
# sélection de plusieurs dont on allume un membre. Le choix relevé par `relever-piste-choisie` (« n ; N choisie(s) »).
# L'ATTENDU ÉCRIT AVANT LA MESURE (ROADMAP-daw, D582.2, réécrit le 24/10 après le rouge du témoin) :
#   (1) l'option cochée, `choisir:2`, le S de la tranche 0 : la piste 0 ;
#   (2) `choisir:2`, le même S (éteint) : la piste 2 — éteindre un solo ne change pas le choix ;
#   (3) `choisir:0`, Ctrl+S de la ligne 1 : la piste 1 ;
#   (4) `choisir:0,1,2`, le S de la ligne 2 : la piste 0, 3 choisies — le lot reste ;
#   (5) le TÉMOIN sans l'option : le S de la tranche 0 après `choisir:2` → 2 ; Ctrl+S de la ligne 1 après `choisir:0` → 0 ;
#       le S de la ligne 2 après `choisir:0` → 2 (D38.4, que l'option ne change pas) ;
#   (6) relancé dans le même HOME : l'entrée cochée ; en anglais, « Solo selects the track ».
#
#   tools/solo-choisit.sh [binaire]
set -u
racine="$(cd "$(dirname "$0")/.." && pwd)"
cd "$racine"
BIN="${1:-./build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio}"
[ -x "$BIN" ] || { echo "REFUS : $BIN absent — compiler d'abord"; exit 2; }

brouillon="$(mktemp -d "${TMPDIR:-/tmp}/vsm-solo-choisit.XXXXXX")"
GARDER="${VSM_GARDER:-}"
trap '[ -n "$GARDER" ] && mkdir -p "$GARDER" && cp "$brouillon"/*.png "$brouillon"/*.txt "$GARDER"/ 2>/dev/null; rm -rf "$brouillon"' EXIT

python3 - "$brouillon" <<'PY'
import json, os, struct, sys
b = sys.argv[1]
pistes = []
for i in range(3):
    corps = (b"\x00\xff\x51\x03\x07\xa1\x20" if i == 0 else b"") + b"\x00\xff\x03\x06Voie_" + bytes([0x41 + i])
    corps += b"\x00" + bytes([0x90 | i, 60, 100]) + b"\x83\x60" + bytes([0x80 | i, 60, 0]) + b"\x00\xff\x2f\x00"
    pistes.append(b"MTrk" + struct.pack(">I", len(corps)) + corps)
os.makedirs(f"{b}/base/midi", exist_ok=True)
open(f"{b}/base/midi/arrangement.mid", "wb").write(b"MThd" + struct.pack(">IHHH", 6, 1, 3, 480) + b"".join(pistes))
mix = {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 0.8}
json.dump({"format": "vsm-project", "version": 1, "title": "solo", "midi": {"file": "midi/arrangement.mid"},
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
    grep -E "VSM_(GESTE_APRES|VUE|MENU|GESTE|CLIC) : .*(aucun|AUCUN|inconnu|introuvable|refus|REFUS|ATTENTION|ÉCHEC|grisée|AMBIGU|JAMAIS)" "$brouillon/$nom.txt" \
        | sed "s/^/        journal ($nom) : /" >&2
}
# le n-ième relevé d'une course : « piste ; N choisie(s) » → « piste » ($3 = 1) ou « N » ($3 = 2)
choisie() { grep -h "^VSM_PISTE_CHOISIE : " "$brouillon/$1.txt" | sed -n "${2}p" | sed 's/^VSM_PISTE_CHOISIE : //' \
                | awk -F' ; ' -v c="${3:-1}" '{ split($2, n, " "); print (c == 1 ? $1 : n[1]) }'; }

rates=0
verdict() { if [ "$2" = 1 ]; then printf '  OK   %s\n' "$1"; else printf '  RATÉ %s\n' "$1"; rates=$((rates + 1)); fi; }
REGLE="menu:Piste > Le solo choisit la piste"
TRANCHE="cliquer:mixeur.solo"
RELEVE="relever-piste-choisie"

echo "=== D582.2 : le solo choisit la piste ==="
maison="$(mktemp -d "$brouillon/home.XXXX")"
course regle "$maison" VSM_DELAI=3400 VSM_GESTE_APRES="500:$REGLE;\
700:choisir:2;900:$TRANCHE;1100:$RELEVE;\
1300:choisir:2;1500:$TRANCHE;1700:$RELEVE;\
1900:choisir:0;2100:modificateur:ctrl:solo:1;2300:$RELEVE;\
2500:choisir:0,1,2;2700:solo:2;2900:$RELEVE"
course temoin "" VSM_DELAI=2600 VSM_GESTE_APRES="\
700:choisir:2;900:$TRANCHE;1100:$RELEVE;\
1300:choisir:0;1500:modificateur:ctrl:solo:1;1700:$RELEVE;\
1900:choisir:0;2100:solo:2;2300:$RELEVE"
course retenu "$maison" VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"
course anglais "" VSM_LANGUE=en VSM_DELAI=1600 VSM_GESTE_APRES="900:lister-menus"

c1="$(choisie regle 1)"; c2="$(choisie regle 2)"; c3="$(choisie regle 3)"; c4="$(choisie regle 4)"; n4="$(choisie regle 4 2)"
t1="$(choisie temoin 1)"; t2="$(choisie temoin 2)"; t3="$(choisie temoin 3)"
grep -h "^VSM_SOLO_CHOISIT\|^VSM_GESTE : solo\|^VSM_CLIC\|^VSM_REGLAGE_POSTE" "$brouillon"/regle.txt "$brouillon"/temoin.txt | sed 's/^/       /'
coche="$(grep -h "^VSM_MENU_LISTE : Piste > Le solo choisit la piste" "$brouillon/retenu.txt")"
en="$(grep -ch "^VSM_MENU_LISTE : Track > Solo selects the track" "$brouillon/anglais.txt")"
echo "       option : tranche [$c1] ; tranche éteinte [$c2] ; Ctrl+S [$c3] ; le lot [$c4, $n4 choisie(s)]"
echo "       témoin : tranche [$t1] ; Ctrl+S [$t2] ; la ligne [$t3]"
echo "       relancé : [${coche#VSM_MENU_LISTE : }] ; en anglais : $en"

verdict "(1) le S de la tranche 0 : la piste 0" "$([ "$c1" = 0 ] && echo 1 || echo 0)"
verdict "(2) le même S éteint : la piste 2 encore" "$([ "$c2" = 2 ] && echo 1 || echo 0)"
verdict "(3) Ctrl+S de la ligne 1 : la piste 1" "$([ "$c3" = 1 ] && echo 1 || echo 0)"
verdict "(4) le S d'un membre du lot : la piste 0, 3 choisies" "$([ "$c4" = 0 ] && [ "$n4" = 3 ] && echo 1 || echo 0)"
verdict "(5) le témoin : 2, 0, puis 2 (D38.4)" "$([ "$t1" = 2 ] && [ "$t2" = 0 ] && [ "$t3" = 2 ] && echo 1 || echo 0)"
verdict "(6) retenu au poste ; en anglais" "$(echo "$coche" | grep -q "\[cochée\]" && [ "$en" = 1 ] && echo 1 || echo 0)"

echo
if [ "$rates" -gt 0 ]; then echo "SOLO-CHOISIT : $rates contrôle(s) raté(s)"; exit 1; fi
echo "SOLO-CHOISIT : allumer un solo à la console ou en exclusif choisit sa piste, l'éteindre ne change rien, le lot reste"
